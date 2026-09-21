<?php

declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    http_response_code(404);
    exit;
}

require __DIR__ . '/../lib/bootstrap.php';

function fail(string $why): void
{
    fwrite(STDERR, gmdate('Y-m-d H:i:s') . " vatsim_sync: $why" . PHP_EOL);
    exit(1);
}

$config = app_config();
$source = $argv[1] ?? $config['vatsim_data_url'];

$raw = vatsim_fetch($source);
if ($raw === null) {
    fail("could not read $source");
}

$feed = vatsim_parse($raw);
if ($feed === null || !isset($feed['pilots']) || !is_array($feed['pilots'])) {
    fail('not a usable VATSIM data feed - missing, malformed or stale');
}
$feedTime = (int)$feed['_time'];
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

$controllers = vatsim_controllers($feed);
if ($controllers === null) {
    fail('feed has no controllers list');
}

$pdo = db();
$pdo->beginTransaction();

vatsim_write_controllers($controllers, $feedTime, vatsim_observers($feed));

$pdo->exec('DELETE FROM network_pilots');
foreach (array_chunk($rows, 300) as $chunk) {
    $sql = 'INSERT IGNORE INTO network_pilots (callsign, transponder, assigned_transponder, latitude, longitude) VALUES '
        . implode(',', array_fill(0, count($chunk), '(?, ?, ?, ?, ?)'));
    $pdo->prepare($sql)->execute(array_merge(...$chunk));
}

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

$release = $pdo->prepare(
    'UPDATE assignments SET released_at = NOW()
     WHERE released_at IS NULL AND COALESCE(last_seen_online, assigned_at) < NOW() - INTERVAL ? MINUTE'
);
$release->execute([(int)$config['release_after_min']]);
$released = $release->rowCount();

$pdo->exec('DELETE FROM assignments WHERE released_at IS NOT NULL AND released_at < NOW() - INTERVAL 30 DAY');

sync_state_set('network_updated', gmdate('Y-m-d H:i:s', $feedTime));

$pdo->exec('DELETE FROM rate_limit WHERE window_start < NOW() - INTERVAL 1 HOUR');

$pdo->commit();

echo gmdate('Y-m-d H:i:s') . ' vatsim_sync: pilots=' . count($rows)
    . ' controllers=' . count($controllers) . ' held=' . count($seen) . " released=$released" . PHP_EOL;
