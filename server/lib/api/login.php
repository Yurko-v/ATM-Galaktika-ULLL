<?php

declare(strict_types=1);

require __DIR__ . '/../bootstrap.php';

const LOGIN_TRIES_PER_MIN = 5;

require_method('POST');

$body = read_json_body();

$position = clean_position($body['position'] ?? '');

if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}

$cid = require_ksa_caller($position);

$st = db()->prepare(
    'SELECT name
     FROM user_names
     WHERE cid = ?'
);

$st->execute([$cid]);
$row = $st->fetch();

if (!$row) {
    json_out(403, ['error' => 'not_registered']);
}

json_out(200, [
    'cid' => $cid,
    'name' => (string)$row['name'],
]);