<?php
// GET -> {"ok", "network_updated", "network_fresh", "controllers_updated",
//         "controllers_fresh", "controllers_online", "api_key", "active",
//         "user_names"}
//
// No key needed and nothing private in it: a quick look, from a browser, that
// the database is reachable and the cron job is keeping the network current.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/codes.php';

$active      = (int)db()->query('SELECT COUNT(*) FROM assignments WHERE released_at IS NULL')->fetchColumn();
$controllers = (int)db()->query('SELECT COUNT(*) FROM network_controllers')->fetchColumn();
$userNames   = (int)db()->query('SELECT COUNT(*) FROM user_names')->fetchColumn();
// null while the table has not been created - schema.sql not imported again.
try {
    $observers = (int)db()->query('SELECT COUNT(*) FROM network_observers')->fetchColumn();
} catch (PDOException $e) {
    $observers = null;
}

json_out(200, [
    'ok'                  => true,
    'network_updated'     => sync_state_get('network_updated'),
    'network_fresh'       => network_is_fresh(),
    // Without a fresh controller list nobody can be let in at all, so this is
    // the first thing to look at when every position is getting "not_online".
    'controllers_updated' => sync_state_get('controllers_updated'),
    'controllers_fresh'   => controllers_are_fresh(),
    'controllers_online'  => $controllers,
    'observers_online'    => $observers,
    // Whether the optional second lock is on; never the key itself.
    'api_key'             => api_key_required(),
    'active'              => $active,
    // How many controllers have a name entered - a count, not the names.
    'user_names'          => $userNames,
]);
