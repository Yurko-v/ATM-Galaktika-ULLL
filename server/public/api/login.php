<?php
// POST {"position": "ULLL_APP", "surname": "Велбовец", "first_name": "Юрий",
//       "patronymic": "Владимирович", "password": "..."}
//   -> 200 {"cid": "1234567", "name": "Велбовец Юрий Владимирович"}
//
// The plug-in's LOGIN: the Вход в систему КСА window sends what the controller
// typed, and the panel opens only on a 200. Checked against the user_names row
// for the CID the network lists for the position (see require_caller()) - never
// a CID the request names - so a registration is of no use to anyone but the
// CID's owner, sitting on the network under it.
//
// 403 "not_registered": no row for the CID, or one with no password - the
//     admin entered the name only, and the controller has yet to register on
//     the site (public/register/).
// 401 "wrong_credentials": the name or the password is not the registered one.
//     Which of them is not said.
// 429 "rate_limited": more than LOGIN_TRIES_PER_MIN tries for the CID in a
//     minute, right or wrong.
// Case, ё or е and spaces around a name part do not matter; an empty patronymic
// matches a registration without one.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

const LOGIN_TRIES_PER_MIN = 5;

require_method('POST');

$body = read_json_body();
$position = clean_position($body['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
$cid = require_caller($position, true);

// A test position has no CID, and so no registration.
if ($cid === null) {
    json_out(400, ['error' => 'no_cid']);
}
if (!within_rate_limit('login:' . $cid, LOGIN_TRIES_PER_MIN)) {
    json_out(429, ['error' => 'rate_limited']);
}

$st = db()->prepare('SELECT name, surname, first_name, patronymic, password_hash FROM user_names WHERE cid = ?');
$st->execute([$cid]);
$row = $st->fetch();
if (!$row || (string)$row['password_hash'] === '') {
    error_log('squawk login: ' . $position . ' (CID ' . $cid . ') is not registered');
    json_out(403, ['error' => 'not_registered']);
}

$password = substr((string)($body['password'] ?? ''), 0, 1024);
$sameName = name_key($body['surname'] ?? '') === name_key($row['surname'])
    && name_key($body['first_name'] ?? '') === name_key($row['first_name'])
    && name_key($body['patronymic'] ?? '') === name_key($row['patronymic']);

// The password is checked whatever the name, so the time the answer takes does
// not say which of the two was wrong.
if (!password_verify($password, (string)$row['password_hash']) || !$sameName) {
    error_log('squawk login: wrong name or password for ' . $position . ' (CID ' . $cid . ')');
    json_out(401, ['error' => 'wrong_credentials']);
}

if (password_needs_rehash((string)$row['password_hash'], PASSWORD_DEFAULT)) {
    db()->prepare('UPDATE user_names SET password_hash = ? WHERE cid = ?')
        ->execute([password_hash($password, PASSWORD_DEFAULT), $cid]);
}

json_out(200, ['cid' => $cid, 'name' => (string)$row['name']]);
