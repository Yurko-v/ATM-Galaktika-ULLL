<?php
// GET ?position=ULLL_APP -> {"cid": "1234567", "name": "Велбовец Юрий Владимирович"}
//
// The name the Пользователь block shows for the controller on that position,
// from the user_names table - "name": null when nobody has entered one, and the
// plug-in falls back to its own reading of the VATSIM name.
//
// Only ever the caller's own: the CID is the one the network lists for the
// position the caller is checked against (see require_caller()), never one the
// request names, so the table cannot be read through from outside.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
$cid = require_caller($position);

// A test position has no CID behind it, and so no name.
$name = null;
if ($cid !== null) {
    $st = db()->prepare('SELECT name FROM user_names WHERE cid = ?');
    $st->execute([$cid]);
    $found = trim((string)$st->fetchColumn());
    $name = $found === '' ? null : $found;
}

json_out(200, ['cid' => $cid, 'name' => $name]);
