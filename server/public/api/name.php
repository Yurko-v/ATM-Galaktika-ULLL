<?php
// GET  ?position=ULLL_APP -> {"cid": "1234567", "name": "Велбовец Юрий Владимирович"}
// POST {"position": "ULLL_APP", "name": "Велбовец Юрий Владимирович"}
//      -> {"cid": "1234567", "name": "Велбовец Юрий Владимирович"}
//
// GET: the name the Пользователь block shows for the controller on that
// position, from the user_names table - "name": null when nobody has entered
// one, and the plug-in falls back to its own reading of the VATSIM name.
//
// POST: a controller the table has no name for enters their own, in the window
// the plug-in's LOGIN opens for them. Only where the table has nothing yet: a
// name already there - the admin's, or one sent before - is kept and handed
// back with 409 "exists", so a slip in the window is put right in phpMyAdmin
// rather than by sending again.
//
// Either way only ever the caller's own: the CID is the one the network lists
// for the position the caller is checked against (see require_caller()), never
// one the request names, so the table can neither be read through nor written
// over from outside.
//
// An observer is let in here as well as a controller: the panel is theirs to
// open too, and this is what opens it. The code endpoints still turn one away.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

function stored_name(string $cid): ?string
{
    $st = db()->prepare('SELECT name FROM user_names WHERE cid = ?');
    $st->execute([$cid]);
    $found = trim((string)$st->fetchColumn());
    return $found === '' ? null : $found;
}

// What the window sends, the way NormalizeEnteredName in the plug-in's
// UserName.cpp puts it: "Фамилия Имя Отчество" in full, or shortened to
// "Фамилия И.О." / "Фамилия И." - Cyrillic only, with a hyphen allowed inside a
// double surname. Anything else is not a name the plug-in can show, and is
// turned down rather than stored.
function clean_user_name($value): ?string
{
    $s = preg_replace('/\s+/u', ' ', trim((string)$value));
    if ($s === null || preg_match_all('/./u', $s) > 100) {
        return null;
    }
    $word = '\p{Cyrillic}+(?:-\p{Cyrillic}+)*';
    $full = ' \p{Cyrillic}{2,} \p{Cyrillic}{2,}';
    $short = ' \p{Cyrillic}\.(?:\p{Cyrillic}\.)?';
    return preg_match("/^$word(?:$full|$short)$/u", $s) ? $s : null;
}

$method = $_SERVER['REQUEST_METHOD'] ?? '';

if ($method === 'GET') {
    $position = clean_position($_GET['position'] ?? '');
    if ($position === null) {
        json_out(400, ['error' => 'bad_request']);
    }
    $cid = require_caller($position, true);

    // A test position has no CID behind it, and so no name.
    json_out(200, ['cid' => $cid, 'name' => $cid === null ? null : stored_name($cid)]);
}

if ($method !== 'POST') {
    json_out(405, ['error' => 'method_not_allowed']);
}

$body = read_json_body();
$position = clean_position($body['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
$cid = require_caller($position, true);

// A test position has no CID to file a name under.
if ($cid === null) {
    json_out(400, ['error' => 'no_cid']);
}
$name = clean_user_name($body['name'] ?? '');
if ($name === null) {
    json_out(400, ['error' => 'bad_name']);
}

// A row that is there but blank counts as no name, as it does for GET. MySQL
// reports 1 for an insert, 2 for that blank being filled, and 0 when a real
// name was already there and is left as it was.
$st = db()->prepare(
    "INSERT INTO user_names (cid, name) VALUES (?, ?)
     ON DUPLICATE KEY UPDATE name = IF(TRIM(name) = '', VALUES(name), name)"
);
$st->execute([$cid, $name]);
if ($st->rowCount() === 0) {
    json_out(409, ['error' => 'exists', 'cid' => $cid, 'name' => stored_name($cid)]);
}

json_out(200, ['cid' => $cid, 'name' => $name]);
