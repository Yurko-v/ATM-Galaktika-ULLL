<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');

$position = clean_position($_GET['position'] ?? '');
if ($position === null) {
    json_out(400, ['error' => 'bad_request']);
}
require_caller($position);

$map = [];
foreach (db()->query('SELECT callsign, code FROM assignments WHERE released_at IS NULL ORDER BY callsign') as $row) {
    $map[$row['callsign']] = $row['code'];
}

json_out(200, ['assignments' => (object)$map, 'time' => gmdate('Y-m-d\TH:i:s\Z')]);
