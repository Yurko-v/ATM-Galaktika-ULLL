<?php
// GET -> {"ok": true, "network_updated": "...", "network_fresh": true, "active": 12}
//
// No key needed and nothing private in it: a quick look, from a browser, that
// the database is reachable and the cron job is keeping the network current.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/codes.php';

$st = db()->prepare("SELECT value FROM sync_state WHERE name = 'network_updated'");
$st->execute();
$updated = $st->fetchColumn();

$active = (int)db()->query('SELECT COUNT(*) FROM assignments WHERE released_at IS NULL')->fetchColumn();

json_out(200, [
    'ok'              => true,
    'network_updated' => $updated === false ? null : $updated,
    'network_fresh'   => network_is_fresh(),
    'active'          => $active,
]);
