<?php
// GET ?position=ULLI_GND
//   -> 200 {"cid": "1234567", "name": "Велбовец Ю.В.",
//           "rating_id": 5, "rating": "C1"}
//
// The name and rating the plug-in's Пользователь block shows, for the CID the
// network lists on that position (see require_ksa_caller()) - never one the
// request names. "name" is null for a profile with no name entered yet.
//
// rating_id is VATSIM's own number; "rating" is its short name, "C1".

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');

if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}

$cid = require_ksa_caller($position);

$profile = ksa_profile($cid);

if ($profile === null) {
    json_out(403, ['error' => 'not_registered']);
}

$name     = trim((string)$profile['name']);
$ratingId = profile_rating_id($profile);

json_out(200, [
    'cid'       => $cid,
    'name'      => $name === '' ? null : $name,
    'rating_id' => $ratingId,
    'rating'    => controller_rating_short($ratingId),
]);
