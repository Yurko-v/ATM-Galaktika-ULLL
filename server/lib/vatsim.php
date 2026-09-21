<?php

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

function vatsim_write_controllers(array $map, int $feedTime, array $observers = []): void
{
    $pdo = db();

    $own = !$pdo->inTransaction();
    if ($own) {
        $pdo->beginTransaction();
    }

    vatsim_write_callsigns($pdo, 'network_controllers', $map);

    try {
        vatsim_write_callsigns($pdo, 'network_observers', $observers);
    } catch (PDOException $e) {
    }

    sync_state_set('controllers_updated', gmdate('Y-m-d H:i:s', $feedTime));

    if ($own) {
        $pdo->commit();
    }
}
