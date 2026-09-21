<?php

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

if ($cid === null) {
    json_out(400, ['error' => 'no_cid']);
}
if (!within_rate_limit('login:' . $cid, LOGIN_TRIES_PER_MIN)) {
    json_out(429, ['error' => 'rate_limited']);
}

$typedCid = trim((string)($body['cid'] ?? ''));
if ($typedCid === '' || !preg_match('/^\d{1,12}$/', $typedCid)) {
    json_out(400, ['error' => 'bad_request']);
}
if ($typedCid !== (string)$cid) {
    error_log('squawk login: ' . $position . ' typed CID ' . $typedCid . ', the network lists ' . $cid);
    json_out(401, ['error' => 'wrong_cid']);
}

$st = db()->prepare('SELECT name, surname, first_name, patronymic, password_hash FROM user_names WHERE cid = ?');
$st->execute([$cid]);
$row = $st->fetch();
if (!$row || (string)$row['password_hash'] === '') {
    error_log('squawk login: ' . $position . ' (CID ' . $cid . ') is not registered');
    json_out(403, ['error' => 'not_registered']);
}

$sameName = name_key($body['surname'] ?? '') === name_key($row['surname'])
    && name_key($body['first_name'] ?? '') === name_key($row['first_name'])
    && name_key($body['patronymic'] ?? '') === name_key($row['patronymic']);

if (!$sameName) {
    error_log('squawk login: wrong name for ' . $position . ' (CID ' . $cid . ')');
    json_out(401, ['error' => 'wrong_credentials']);
}

json_out(200, ['cid' => $cid, 'name' => (string)$row['name']]);
