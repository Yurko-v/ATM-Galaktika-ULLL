<?php
// GET -> {"assignments": {"AFL123": "0761", ...}, "time": "..."}
//
// Every code held right now, polled by each plugin so the column shows the
// same code at every position.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

require_method('GET');
require_api_key();

$map = [];
foreach (db()->query('SELECT callsign, code FROM assignments WHERE released_at IS NULL ORDER BY callsign') as $row) {
    $map[$row['callsign']] = $row['code'];
}

json_out(200, ['assignments' => (object)$map, 'time' => gmdate('Y-m-d\TH:i:s\Z')]);
