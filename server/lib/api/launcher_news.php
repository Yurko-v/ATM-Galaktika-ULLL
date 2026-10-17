<?php
// GET -> {"ok", "news": [{"id", "title", "body", "level", "pinned", "date"}, ...]}
//
// The launcher's news feed, written on the admin page: published items only,
// pinned ones first, then the newest, at most LAUNCHER_NEWS_LIMIT. body is
// Markdown; level is "info", "warn" or "maint". No key, the same for everyone,
// so it may be cached for a minute.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/launcher.php';

require_method('GET');

$news = [];
$rows = db()->query(
    'SELECT id, title, body, level, pinned, created_at
     FROM launcher_news
     WHERE published = 1
     ORDER BY pinned DESC, created_at DESC, id DESC
     LIMIT ' . LAUNCHER_NEWS_LIMIT
);
foreach ($rows as $row) {
    $news[] = [
        'id'     => (int)$row['id'],
        'title'  => $row['title'],
        'body'   => $row['body'],
        'level'  => $row['level'],
        'pinned' => (int)$row['pinned'] === 1,
        'date'   => launcher_iso_time((string)$row['created_at']),
    ];
}

json_out(200, ['ok' => true, 'news' => $news], 'public, max-age=60');
