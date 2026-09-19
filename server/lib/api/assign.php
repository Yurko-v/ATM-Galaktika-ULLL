<?php
// POST {"callsign": "AFL123", "position": "ULLI_DEL", "new": false}
//
// Gives the aircraft a code. If it already holds one, that code comes back -
// which is what keeps every position on the same code for the same aircraft.
// "new": true gives back the code it holds and hands out a different one, for
// a code found to clash on the radar.
//
// 200 {"callsign", "code", "existing"} | 409 {"error": "pool_empty"}
//     401 {"error": "not_online"} if that position is not controlling on VATSIM

declare(strict_types=1);

require __DIR__ . '/../../bootstrap.php';
require __DIR__ . '/../../codes.php';

require_method('POST');

$in = read_json_body();
$callsign = clean_callsign($in['callsign'] ?? '');
$position = clean_position($in['position'] ?? '');
$new = !empty($in['new']);
if ($callsign === null || $position === null) {
    json_out(400, ['error' => 'bad_request']);
}

// Read first, checked second: the position in the body is what the caller is
// checked against - they must be controlling it on the network right now.
$cid = require_ksa_caller($position);

$exclude = [];
for ($attempt = 0; $attempt < 3; $attempt++) {
    $current = active_assignment($callsign);
    if ($current !== null) {
        if (!$new) {
            json_out(200, ['callsign' => $callsign, 'code' => $current['code'], 'existing' => true]);
        }
        release_assignment((int)$current['id']);
        $exclude[] = $current['code'];
        // Given back. If another position assigns this aircraft before we do,
        // the next pass finds that code and returns it rather than a third one.
        $new = false;
    }

    foreach (free_codes_in_order($position, $callsign, $exclude) as $code) {
        $result = try_insert($callsign, $code, $position, $cid);
        if ($result === 'ok') {
            json_out(200, ['callsign' => $callsign, 'code' => $code, 'existing' => false]);
        }
        if ($result === 'callsign_taken') {
            continue 2;
        }
        // 'code_taken': another aircraft got this code a moment ago - try the next.
    }

    json_out(409, ['error' => 'pool_empty']);
}

json_out(503, ['error' => 'busy']);
