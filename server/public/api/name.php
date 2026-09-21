<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
$cid = require_caller($position, true);

$name = null;
if ($cid !== null) {
    $st = db()->prepare('SELECT name FROM user_names WHERE cid = ?');
    $st->execute([$cid]);
    $found = trim((string)$st->fetchColumn());
    $name = $found === '' ? null : $found;
}

json_out(200, ['cid' => $cid, 'name' => $name]);
