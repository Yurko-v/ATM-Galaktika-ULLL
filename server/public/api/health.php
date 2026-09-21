<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/codes.php';

$active      = (int)db()->query('SELECT COUNT(*) FROM assignments WHERE released_at IS NULL')->fetchColumn();
$controllers = (int)db()->query('SELECT COUNT(*) FROM network_controllers')->fetchColumn();
$userNames   = (int)db()->query('SELECT COUNT(*) FROM user_names')->fetchColumn();
try {
    $observers = (int)db()->query('SELECT COUNT(*) FROM network_observers')->fetchColumn();
} catch (PDOException $e) {
    $observers = null;
}
try {
    $registered = (int)db()->query('SELECT COUNT(*) FROM user_names WHERE password_hash IS NOT NULL')->fetchColumn();
} catch (PDOException $e) {
    $registered = null;
}

json_out(200, [
    'ok'                  => true,
    'network_updated'     => sync_state_get('network_updated'),
    'network_fresh'       => network_is_fresh(),
    'controllers_updated' => sync_state_get('controllers_updated'),
    'controllers_fresh'   => controllers_are_fresh(),
    'controllers_online'  => $controllers,
    'observers_online'    => $observers,
    'api_key'             => api_key_required(),
    'active'              => $active,
    'user_names'          => $userNames,
    'registered'          => $registered,
]);
