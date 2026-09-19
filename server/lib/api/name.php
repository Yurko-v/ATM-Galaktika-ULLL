<?php
// GET ?position=ULLL_APP -> {"cid": "1234567", "name": "Велбовец Юрий Владимирович"}
//
// The name the Пользователь block shows for the controller on that position,
// from the user_names table - "name": null when there is none, and the plug-in
// falls back to its own reading of the VATSIM name. Asked every minute: a name
// that was there and is gone is the admin taking the controller out, and the
// plug-in shuts the panel.
//
// Only ever the caller's own: the CID is the one the network lists for the
// position the caller is checked against (see require_caller()), never one the
// request names, so the table cannot be read through from outside.
//
// Names are written by registration (/register/) and the admin page, not
// from here: letting the plug-in in is api/login.php's job, and it wants a
// password.
//
// An observer is let in here as well as a controller: the panel is theirs to
// open too. The code endpoints still turn one away.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
$cid = require_caller($position, true);

// A test position has no CID behind it, and so no name.
$name = null;
if ($cid !== null) {
    $st = db()->prepare('SELECT name FROM user_names WHERE cid = ?');
    $st->execute([$cid]);
    $found = trim((string)$st->fetchColumn());
    $name = $found === '' ? null : $found;
}

json_out(200, ['cid' => $cid, 'name' => $name]);
