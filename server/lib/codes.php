<?php

declare(strict_types=1);

function codes_in_range(string $from, string $to): array
{
    $out = [];
    for ($n = octdec($from), $end = octdec($to); $n <= $end; $n++) {
        $out[] = sprintf('%04o', $n);
    }
    return $out;
}

function candidate_groups(string $position): array
{
    $config = app_config();

    $groups = [];
    foreach ($config['ranges'] as $rule) {
        if (preg_match($rule['pattern'], $position)) {
            $groups[] = $rule['ranges'];
            break;
        }
    }
    $groups[] = $config['fallback'];

    $reserved = array_flip($config['reserved']);
    $seen = [];
    $out = [];
    foreach ($groups as $ranges) {
        $codes = [];
        foreach ($ranges as [$from, $to]) {
            foreach (codes_in_range($from, $to) as $code) {
                if (isset($reserved[$code]) || isset($seen[$code])) {
                    continue;
                }
                $seen[$code] = true;
                $codes[] = $code;
            }
        }
        $out[] = $codes;
    }
    return $out;
}

function distance_km(float $lat1, float $lon1, float $lat2, float $lon2): float
{
    $r = 6371.0;
    $dLat = deg2rad($lat2 - $lat1);
    $dLon = deg2rad($lon2 - $lon1);
    $a = sin($dLat / 2) ** 2 + cos(deg2rad($lat1)) * cos(deg2rad($lat2)) * sin($dLon / 2) ** 2;
    return 2 * $r * asin(min(1.0, sqrt($a)));
}

function network_is_fresh(): bool
{
    $st = db()->prepare("SELECT value FROM sync_state WHERE name = 'network_updated'");
    $st->execute();
    $updated = $st->fetchColumn();
    if ($updated === false) {
        return false;
    }
    $max = 60 * (int)app_config()['network_max_age_min'];
    return strtotime($updated . ' UTC') >= time() - $max;
}

function taken_codes(string $callsign): array
{
    $pdo = db();
    $taken = [];

    foreach ($pdo->query('SELECT code FROM assignments WHERE released_at IS NULL') as $row) {
        $taken[$row['code']] = true;
    }

    if (network_is_fresh()) {
        $config = app_config();
        [$lat0, $lon0] = $config['network_center'];
        $radius = (float)$config['network_radius_km'];

        $band = $radius / 111.0;
        $st = $pdo->prepare(
            'SELECT transponder, assigned_transponder, latitude, longitude FROM network_pilots
             WHERE callsign <> ? AND latitude BETWEEN ? AND ?'
        );
        $st->execute([$callsign, $lat0 - $band, $lat0 + $band]);
        foreach ($st as $p) {
            if (distance_km($lat0, $lon0, (float)$p['latitude'], (float)$p['longitude']) > $radius) {
                continue;
            }
            foreach (['transponder', 'assigned_transponder'] as $field) {
                if ($p[$field] !== null) {
                    $taken[$p[$field]] = true;
                }
            }
        }
    }

    return $taken;
}

function last_released(array $codes): array
{
    if (!$codes) {
        return [];
    }
    $in = implode(',', array_fill(0, count($codes), '?'));
    $st = db()->prepare(
        "SELECT code, MAX(released_at) AS t FROM assignments
         WHERE code IN ($in) AND released_at IS NOT NULL GROUP BY code"
    );
    $st->execute($codes);
    $out = [];
    foreach ($st as $row) {
        $out[$row['code']] = $row['t'];
    }
    return $out;
}

function free_codes_in_order(string $position, string $callsign, array $exclude): array
{
    $taken = taken_codes($callsign);
    foreach ($exclude as $code) {
        $taken[$code] = true;
    }

    $out = [];
    foreach (candidate_groups($position) as $group) {
        $free = array_values(array_filter($group, fn($c) => !isset($taken[$c])));
        $last = last_released($free);
        usort($free, fn($a, $b) => strcmp($last[$a] ?? '', $last[$b] ?? ''));
        array_push($out, ...$free);
    }
    return $out;
}

function active_assignment(string $callsign): ?array
{
    $st = db()->prepare('SELECT id, code FROM assignments WHERE callsign = ? AND released_at IS NULL');
    $st->execute([$callsign]);
    $row = $st->fetch();
    return $row === false ? null : $row;
}

function code_holder(string $code): ?string
{
    $st = db()->prepare('SELECT callsign FROM assignments WHERE code = ? AND released_at IS NULL');
    $st->execute([$code]);
    $holder = $st->fetchColumn();
    return $holder === false ? null : $holder;
}

function release_assignment(int $id): void
{
    db()->prepare('UPDATE assignments SET released_at = NOW() WHERE id = ? AND released_at IS NULL')
        ->execute([$id]);
}

function try_insert(string $callsign, string $code, string $position, ?string $cid = null): string
{
    try {
        db()->prepare(
            'INSERT INTO assignments (callsign, code, assigned_by, assigned_cid, assigned_at, last_seen_online)
             VALUES (?, ?, ?, ?, NOW(), NOW())'
        )->execute([$callsign, $code, $position, $cid]);
        return 'ok';
    } catch (PDOException $e) {
        if ((string)$e->getCode() !== '23000') {
            throw $e;
        }
        return str_contains($e->getMessage(), 'uq_active_callsign') ? 'callsign_taken' : 'code_taken';
    }
}
