<?php
// POST {"position": "ULLL_APP"}
//   -> 200 {"cid": "1234567", "name": "Велбовец Ю.В.",
//           "rating_id": 5, "rating": "C1"}
//
// The plug-in's LOGIN. The panel opens for the CID the network lists on that
// position (see require_ksa_caller()), never for a CID the request names.
//
// rating_id is VATSIM's own number for the rating and is what anything on the
// server decides by; "rating" is its short name, for showing to the
// controller. Until OAuth is in place the number is the one entered on the
// registration page.
//
// 403 "not_registered": no profile for the CID, or one whose rating does not
//     open the КСА - an observer, a suspended account.
// 401 "not_online": the position is not controlling on the network.
// 429 "rate_limited": too many requests from the position in a minute.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('POST');

$body = read_json_body();

$position = clean_position($body['position'] ?? '');

if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}

$cid = require_ksa_caller($position);

// require_ksa_caller() let the caller in, so there is a profile behind this.
$profile = ksa_profile($cid);

if ($profile === null) {
    json_out(403, ['error' => 'not_registered']);
}

$ratingId = profile_rating_id($profile);

json_out(200, [
    'cid'       => $cid,
    'name'      => (string)$profile['name'],
    'rating_id' => $ratingId,
    'rating'    => controller_rating_short($ratingId),
]);
