<?php
// Run once a minute from cron:
//   php /path/to/cron/vatsim_sync.php
//
// Reads the VATSIM network, keeps the snapshot of who squawks what current,
// marks the aircraft holding a code that are still online, and gives back the
// code of every aircraft that has been gone longer than release_after_min.
//
// A path instead of a URL can be passed for testing:
//   php vatsim_sync.php test-feed.json

declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    http_response_code(404);
    exit;
}

require __DIR__ . '/../lib/bootstrap.php';

function fetch_feed(string $source): ?string
{
    if (!preg_match('~^https?://~i', $source)) {
        $data = @file_get_contents($source);
        return $data === false ? null : $data;
    }

    if (function_exists('curl_init')) {
        $ch = curl_init($source);
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_FOLLOWLOCATION => true,
            CURLOPT_CONNECTTIMEOUT => 10,
            CURLOPT_TIMEOUT        => 30,
            CURLOPT_ENCODING       => '',
            CURLOPT_USERAGENT      => 'GalaxyATMSystem-squawk/1.0',
        ]);
        $data = curl_exec($ch);
        $ok = $data !== false && curl_getinfo($ch, CURLINFO_HTTP_CODE) === 200;
        curl_close($ch);
        return $ok ? $data : null;
    }

    $ctx = stream_context_create(['http' => ['timeout' => 30, 'user_agent' => 'GalaxyATMSystem-squawk/1.0']]);
    $data = @file_get_contents($source, false, $ctx);
    return $data === false ? null : $data;
}

function fail(string $why): void
{
    fwrite(STDERR, gmdate('Y-m-d H:i:s') . " vatsim_sync: $why" . PHP_EOL);
    exit(1);
}

$config = app_config();
$source = $argv[1] ?? $config['vatsim_data_url'];

$raw = fetch_feed($source);
if ($raw === null) {
    fail("could not read $source");
}

$feed = json_decode($raw, true);
if (!is_array($feed) || !isset($feed['pilots']) || !is_array($feed['pilots'])) {
    fail('not a VATSIM data feed');
}

// Nothing is released on a feed that is old or empty: a network outage must
// not look like every pilot disconnecting at once.
$feedTime = strtotime((string)($feed['general']['update_timestamp'] ?? ''));
if ($feedTime === false) {
    fail('feed has no update_timestamp');
}
if ($feedTime < time() - 60 * (int)$config['network_max_age_min']) {
    fail('feed is stale: ' . gmdate('Y-m-d H:i:s', $feedTime));
}
if (count($feed['pilots']) === 0) {
    fail('feed has no pilots');
}

$online = [];
$rows = [];
foreach ($feed['pilots'] as $p) {
    $callsign = clean_callsign($p['callsign'] ?? '');
    if ($callsign === null || isset($online[$callsign])) {
        continue;
    }
    $online[$callsign] = true;

    $squawk = clean_code($p['transponder'] ?? '');
    $assigned = clean_code($p['flight_plan']['assigned_transponder'] ?? '');
    $rows[] = [
        $callsign,
        $squawk,
        $assigned === '0000' ? null : $assigned,
        (float)($p['latitude'] ?? 0),
        (float)($p['longitude'] ?? 0),
    ];
}

$pdo = db();
$pdo->beginTransaction();

$pdo->exec('DELETE FROM network_pilots');
foreach (array_chunk($rows, 300) as $chunk) {
    $sql = 'INSERT IGNORE INTO network_pilots (callsign, transponder, assigned_transponder, latitude, longitude) VALUES '
        . implode(',', array_fill(0, count($chunk), '(?, ?, ?, ?, ?)'));
    $pdo->prepare($sql)->execute(array_merge(...$chunk));
}

// Still online: the clock on their code starts again.
$seen = [];
foreach ($pdo->query('SELECT id, callsign FROM assignments WHERE released_at IS NULL') as $a) {
    if (isset($online[$a['callsign']])) {
        $seen[] = (int)$a['id'];
    }
}
foreach (array_chunk($seen, 500) as $chunk) {
    $in = implode(',', array_fill(0, count($chunk), '?'));
    $pdo->prepare("UPDATE assignments SET last_seen_online = NOW() WHERE id IN ($in)")->execute($chunk);
}

// Gone for longer than the grace period: the code goes back into the pool.
$release = $pdo->prepare(
    'UPDATE assignments SET released_at = NOW()
     WHERE released_at IS NULL AND COALESCE(last_seen_online, assigned_at) < NOW() - INTERVAL ? MINUTE'
);
$release->execute([(int)$config['release_after_min']]);
$released = $release->rowCount();

// A month of history is plenty to see what happened; the rest goes.
$pdo->exec('DELETE FROM assignments WHERE released_at IS NOT NULL AND released_at < NOW() - INTERVAL 30 DAY');

$pdo->prepare(
    "INSERT INTO sync_state (name, value) VALUES ('network_updated', ?)
     ON DUPLICATE KEY UPDATE value = VALUES(value)"
)->execute([gmdate('Y-m-d H:i:s', $feedTime)]);

$pdo->commit();

echo gmdate('Y-m-d H:i:s') . ' vatsim_sync: pilots=' . count($rows) . ' held=' . count($seen) . " released=$released" . PHP_EOL;
