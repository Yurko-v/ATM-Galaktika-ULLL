<?php
// The VATSIM data feed: reading it, and the part of it every request needs -
// who is online as a controller right now.
//
// The cron job reads the whole feed once a minute, pilots included. An endpoint
// that cannot find its caller in that snapshot reads the feed again from here,
// so a controller who has just logged in is not turned away for the minute it
// takes the cron to notice - and so the service keeps working for a while if
// the cron stops altogether.

declare(strict_types=1);

function vatsim_fetch(string $source, int $connectTimeout = 10, int $timeout = 30): ?string
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
            CURLOPT_CONNECTTIMEOUT => $connectTimeout,
            CURLOPT_TIMEOUT        => $timeout,
            CURLOPT_ENCODING       => '',
            CURLOPT_USERAGENT      => 'GalaxyATMSystem-squawk/1.0',
        ]);
        $data = curl_exec($ch);
        $ok = $data !== false && curl_getinfo($ch, CURLINFO_HTTP_CODE) === 200;
        curl_close($ch);
        return $ok ? $data : null;
    }

    $ctx = stream_context_create(['http' => ['timeout' => $timeout, 'user_agent' => 'GalaxyATMSystem-squawk/1.0']]);
    $data = @file_get_contents($source, false, $ctx);
    return $data === false ? null : $data;
}

// The feed as an array, or null if it is not one, or is too old to act on.
function vatsim_parse(?string $raw): ?array
{
    $feed = json_decode((string)$raw, true);
    if (!is_array($feed)) {
        return null;
    }
    $time = strtotime((string)($feed['general']['update_timestamp'] ?? ''));
    if ($time === false || $time < time() - 60 * (int)app_config()['network_max_age_min']) {
        return null;
    }
    $feed['_time'] = $time;
    return $feed;
}

// callsign -> CID, for everyone online on a real position. Observers are left
// out on purpose: an OBS hands out no codes, so an OBS is not a caller.
//
// null, not an empty list, if the feed has no controllers section at all: an
// empty list means nobody is controlling, and locks everyone out - which is
// right when it is true and wrong when the feed has simply changed shape.
function vatsim_controllers(array $feed): ?array
{
    if (!isset($feed['controllers']) || !is_array($feed['controllers'])) {
        return null;
    }

    $out = [];
    foreach ($feed['controllers'] as $c) {
        $callsign = clean_position($c['callsign'] ?? '');
        $cid      = (int)($c['cid'] ?? 0);
        if ($callsign === null || $cid <= 0 || (int)($c['facility'] ?? 0) <= 0) {
            continue;
        }
        $out[$callsign] = (string)$cid;
    }
    return $out;
}

// callsign -> CID for the observers the feed lists (facility 0). Apart from the
// controllers: an OBS may open the plug-in's panel, so api/name.php lets one
// in, but hands out no codes, so nothing else looks at this list.
function vatsim_observers(array $feed): array
{
    $out = [];
    foreach ((array)($feed['controllers'] ?? []) as $c) {
        $callsign = clean_position($c['callsign'] ?? '');
        $cid      = (int)($c['cid'] ?? 0);
        if ($callsign === null || $cid <= 0 || (int)($c['facility'] ?? 0) > 0) {
            continue;
        }
        $out[$callsign] = (string)$cid;
    }
    return $out;
}

// Empties the table and fills it with callsign -> CID.
function vatsim_write_callsigns(PDO $pdo, string $table, array $map): void
{
    $pdo->exec("DELETE FROM $table");

    $rows = [];
    foreach ($map as $callsign => $cid) {
        $rows[] = [$callsign, $cid];
    }
    foreach (array_chunk($rows, 300) as $chunk) {
        $sql = "INSERT IGNORE INTO $table (callsign, cid) VALUES "
            . implode(',', array_fill(0, count($chunk), '(?, ?)'));
        $pdo->prepare($sql)->execute(array_merge(...$chunk));
    }
}

// Replaces the snapshot of who is controlling, and of who is observing, and
// stamps it with the feed's own time - not ours, so a feed that has stopped
// moving looks stale even if we keep fetching it.
function vatsim_write_controllers(array $map, int $feedTime, array $observers = []): void
{
    $pdo = db();

    // Emptied and refilled in one go, so a request arriving mid-refresh cannot
    // find the table half written. The cron job already has one open.
    $own = !$pdo->inTransaction();
    if ($own) {
        $pdo->beginTransaction();
    }

    vatsim_write_callsigns($pdo, 'network_controllers', $map);

    // The observers' table came later: a server whose schema.sql has not been
    // imported again since has none, and that must not stop the controllers -
    // and with them every code - from being written. Observers just cannot
    // log in there until it is.
    try {
        vatsim_write_callsigns($pdo, 'network_observers', $observers);
    } catch (PDOException $e) {
    }

    sync_state_set('controllers_updated', gmdate('Y-m-d H:i:s', $feedTime));

    if ($own) {
        $pdo->commit();
    }
}
