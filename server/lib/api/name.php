<?php

declare(strict_types=1);

require __DIR__ . '/../../bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');

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

$st->execute([$cid]);
$name = trim((string)$st->fetchColumn());

json_out(200, [
    'cid' => $cid,
    'name' => $name === '' ? null : $name,
]);

$name = trim((string)$row['name']);

json_out(200, [
    'cid' => $cid,
    'name' => $name === '' ? null : $name,
]);