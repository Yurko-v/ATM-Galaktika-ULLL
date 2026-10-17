<?php
// GET -> {"ok", "generated", "components": {"plugin": {"version", "url", "sha256",
//         "size", "date", "changelog"}, "fonts": {...}, "launcher": {...}},
//         "history": [{"component", "version", "date", "changelog"}, ...],
//         "sector": {"source", "page"}, "manual"}
//
// What the launcher should have installed: the current release of each
// component, put up on the admin page (lib/launcher.php). A component with no
// current release is left out of "components". "history" is the last releases
// of every component, newest first, for the changelog screen. No key and no
// caller check - the same answer for everyone, and nothing private in it - so
// it may be cached for a minute.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/launcher.php';

require_method('GET');

$components = [];
$current = db()->query(
    'SELECT component, version, file, sha256, size, changelog, created_at
     FROM launcher_releases
     WHERE is_current = 1
     ORDER BY created_at, id'
);
foreach ($current as $row) {
    $components[$row['component']] = [
        'version'   => $row['version'],
        'url'       => launcher_file_url($row['file']),
        'sha256'    => $row['sha256'],
        'size'      => (int)$row['size'],
        'date'      => substr((string)$row['created_at'], 0, 10),
        'changelog' => $row['changelog'],
    ];
}

$history = [];
$past = db()->query(
    'SELECT component, version, changelog, created_at
     FROM launcher_releases
     ORDER BY created_at DESC, id DESC
     LIMIT ' . LAUNCHER_HISTORY_LIMIT
);
foreach ($past as $row) {
    $history[] = [
        'component' => $row['component'],
        'version'   => $row['version'],
        'date'      => substr((string)$row['created_at'], 0, 10),
        'changelog' => $row['changelog'],
    ];
}

$config = app_config();
$manual = trim((string)($config['launcher_manual_url'] ?? ''));

json_out(200, [
    'ok'         => true,
    'generated'  => gmdate('Y-m-d\TH:i:s\Z'),
    // An object even when empty: the launcher looks components up by name.
    'components' => (object)$components,
    'history'    => $history,
    'sector'     => [
        'source' => 'aero-nav',
        'page'   => (string)($config['launcher_sector_page'] ?? 'https://files.aero-nav.com/ULLL'),
    ],
    'manual'     => $manual === '' ? null : $manual,
], 'public, max-age=60');
