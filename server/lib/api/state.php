<?php
// GET ?position=ULLI_GND -> {"assignments": {"AFL123": "0761", ...}, "time": "..."}
//
// Every code held right now, polled by each plugin so the column shows the
// same code at every position. The position goes in the query string rather
// than a body because this is a GET; it is checked exactly as on the other two
// endpoints - see require_ksa_caller().

declare(strict_types=1);

require __DIR__ . '/../bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
require_ksa_caller($position);

$map = [];
foreach (db()->query('SELECT callsign, code FROM assignments WHERE released_at IS NULL ORDER BY callsign') as $row) {
    $map[$row['callsign']] = $row['code'];
}

json_out(200, ['assignments' => (object)$map, 'time' => gmdate('Y-m-d\TH:i:s\Z')]);
