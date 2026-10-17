<?php
// The launcher's side of the server: the releases it downloads and the news it
// shows. Read by api/launcher_manifest.php and api/launcher_news.php, written
// from the admin page. Needs bootstrap.php first.
//
// The files themselves are plain static files in public_html/launcher/files,
// served by the web server like any other; the database only says which of
// them is current, with the checksum the launcher verifies a download against.
// The sector is not among them - the launcher takes it from aero-nav.

declare(strict_types=1);

const LAUNCHER_COMPONENTS = [
    'plugin'   => 'Плагин',
    'fonts'    => 'Шрифты',
    'launcher' => 'Лаунчер',
];

const LAUNCHER_EXTENSIONS = ['zip', 'exe'];

const LAUNCHER_NEWS_LEVELS = [
    'info'  => 'Новость',
    'warn'  => 'Важно',
    'maint' => 'Работы',
];

// Releases of every component in the manifest's history, newest first.
const LAUNCHER_HISTORY_LIMIT = 30;

const LAUNCHER_NEWS_LIMIT = 50;

// The folder the admin page writes uploads into. Without a setting, the one
// deploy.sh makes: public_html/launcher/files beside lib/.
function launcher_files_dir(): string
{
    $dir = trim((string)(app_config()['launcher_files_dir'] ?? ''));
    if ($dir === '') {
        $dir = dirname(__DIR__) . '/public_html/launcher/files';
    }
    return rtrim($dir, '/\\');
}

// The scheme the request came in on. Beget puts nginx in front of Apache, so a
// request that reached nginx over https may arrive here as plain http with the
// original scheme in X-Forwarded-Proto.
function request_scheme(): string
{
    $https = strtolower((string)($_SERVER['HTTPS'] ?? ''));
    if ($https !== '' && $https !== 'off') {
        return 'https';
    }
    if (strtolower((string)($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https') {
        return 'https';
    }
    return (string)($_SERVER['SERVER_PORT'] ?? '') === '443' ? 'https' : 'http';
}

// The host the request named - only if it looks like one, so a forged Host
// header cannot put anything but a host name into the URLs handed out.
function request_host(): string
{
    $host = (string)($_SERVER['HTTP_HOST'] ?? '');
    if (preg_match('/^[A-Za-z0-9.-]+(?::\d{1,5})?$/', $host)) {
        return $host;
    }
    return (string)($_SERVER['SERVER_NAME'] ?? 'localhost');
}

// Where the launcher downloads a release file from: launcher_files_url when it
// is set, otherwise /launcher/files/ on the host the request came to.
function launcher_file_url(string $file): string
{
    $base = trim((string)(app_config()['launcher_files_url'] ?? ''));
    if ($base === '') {
        $base = request_scheme() . '://' . request_host() . '/launcher/files';
    }
    return rtrim($base, '/') . '/' . rawurlencode($file);
}

function clean_launcher_component($value): ?string
{
    $s = trim((string)$value);
    return array_key_exists($s, LAUNCHER_COMPONENTS) ? $s : null;
}

// A version as it goes into the file name and the manifest: "2026.10.17",
// "1.4.0-beta2". Letters, digits, dot, hyphen and underscore, starting with a
// letter or a digit - nothing that could step out of the files folder.
function clean_launcher_version($value): ?string
{
    $s = trim((string)$value);
    return preg_match('/^[0-9A-Za-z][0-9A-Za-z._-]{0,31}$/', $s) ? $s : null;
}

function clean_news_level($value): ?string
{
    $s = trim((string)$value);
    return array_key_exists($s, LAUNCHER_NEWS_LEVELS) ? $s : null;
}

// Text from a form as stored: valid UTF-8 (a broken byte would make the JSON
// the launcher reads fail to encode), Unix line ends, no space around it.
function clean_text($value): string
{
    return trim(str_replace("\r\n", "\n", mb_scrub((string)$value, 'UTF-8')));
}

// A DATETIME from the database (UTC) as the API gives times out.
function launcher_iso_time(string $datetime): string
{
    return gmdate('Y-m-d\TH:i:s\Z', (int)strtotime($datetime . ' UTC'));
}

// A file size for a person to read: "12,4 МБ".
function launcher_size_label(int $bytes): string
{
    if ($bytes < 1024) {
        return $bytes . ' Б';
    }
    if ($bytes < 1024 * 1024) {
        return number_format($bytes / 1024, 1, ',', ' ') . ' КБ';
    }
    return number_format($bytes / (1024 * 1024), 1, ',', ' ') . ' МБ';
}
