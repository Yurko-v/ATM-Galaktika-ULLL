<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/codes.php';

require_method('POST');

$in = read_json_body();
$callsign = clean_callsign($in['callsign'] ?? '');
$position = clean_position($in['position'] ?? '');
$code = clean_code($in['code'] ?? '');
if ($callsign === null || $position === null || $code === null) {
    json_out(400, ['error' => 'bad_request']);
}

$cid = require_caller($position);

if (in_array($code, app_config()['reserved'], true)) {
    json_out(200, ['status' => 'ignored']);
}

$current = active_assignment($callsign);
if ($current !== null && $current['code'] === $code) {
    json_out(200, ['status' => 'ok']);
}

$holder = code_holder($code);
if ($holder !== null && $holder !== $callsign) {
    json_out(409, ['error' => 'conflict', 'holder' => $holder]);
}

if ($current !== null) {
    release_assignment((int)$current['id']);
}

$result = try_insert($callsign, $code, $position, $cid);
if ($result === 'ok') {
    json_out(200, ['status' => 'ok']);
}
json_out(409, ['error' => 'conflict', 'holder' => code_holder($code)]);
