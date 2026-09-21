<?php

declare(strict_types=1);

date_default_timezone_set('UTC');

require_once __DIR__ . '/vatsim.php';

set_exception_handler(function (Throwable $e): void {
    error_log('squawk: ' . $e->getMessage());
    if (PHP_SAPI === 'cli') {
        fwrite(STDERR, $e->getMessage() . PHP_EOL);
        exit(1);
    }
    json_out(500, ['error' => 'server_error']);
});

function app_config(): array
{
    static $config = null;
    if ($config === null) {
        $path = __DIR__ . '/../config.php';
        if (!is_file($path)) {
            throw new RuntimeException('config.php is missing - copy config.sample.php');
        }
        $config = require $path;
    }
    return $config;
}

function db(): PDO
{
    static $pdo = null;
    if ($pdo === null) {
        $c = app_config()['db'];
        $pdo = new PDO($c['dsn'], $c['user'], $c['pass'], [
            PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES   => false,
        ]);
        $pdo->exec("SET time_zone = '+00:00'");
    }
    return $pdo;
}

function json_out(int $status, array $body): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($body, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function require_method(string $method): void
{
    if (($_SERVER['REQUEST_METHOD'] ?? '') !== $method) {
        json_out(405, ['error' => 'method_not_allowed']);
    }
}

function sync_state_get(string $name): ?string
{
    $st = db()->prepare('SELECT value FROM sync_state WHERE name = ?');
    $st->execute([$name]);
    $value = $st->fetchColumn();
    return $value === false ? null : (string)$value;
}

function sync_state_set(string $name, string $value): void
{
    db()->prepare(
        'INSERT INTO sync_state (name, value) VALUES (?, ?)
         ON DUPLICATE KEY UPDATE value = VALUES(value)'
    )->execute([$name, $value]);
}

function api_key_required(): bool
{
    $key = (string)(app_config()['api_key'] ?? '');
    return $key !== '' && $key !== 'CHANGE_ME';
}

function check_api_key(): void
{
    if (!api_key_required()) {
        return;
    }
    $key   = (string)app_config()['api_key'];
    $given = (string)($_SERVER['HTTP_X_API_KEY'] ?? '');
    if (!hash_equals($key, $given)) {
        json_out(401, ['error' => 'unauthorized']);
    }
}

function controllers_are_fresh(): bool
{
    $updated = sync_state_get('controllers_updated');
    if ($updated === null) {
        return false;
    }
    $max = max(60, (int)(app_config()['controller_max_age_sec'] ?? 180));
    return strtotime($updated . ' UTC') >= time() - $max;
}

function controller_cid(string $position): ?string
{
    $st = db()->prepare('SELECT cid FROM network_controllers WHERE callsign = ?');
    $st->execute([$position]);
    $cid = $st->fetchColumn();
    return $cid === false ? null : (string)$cid;
}

function observer_cid(string $position): ?string
{
    try {
        $st = db()->prepare('SELECT cid FROM network_observers WHERE callsign = ?');
        $st->execute([$position]);
        $cid = $st->fetchColumn();
    } catch (PDOException $e) {
        return null;
    }
    return $cid === false ? null : (string)$cid;
}

function caller_cid(string $position, bool $observerToo): ?string
{
    $cid = controller_cid($position);
    return ($cid === null && $observerToo) ? observer_cid($position) : $cid;
}

function refresh_controllers_if_due(): void
{
    $config = app_config();
    $every  = max(5, (int)($config['controller_refresh_sec'] ?? 20));

    $last = sync_state_get('controllers_polled');
    if ($last !== null && strtotime($last . ' UTC') > time() - $every) {
        return;
    }
    sync_state_set('controllers_polled', gmdate('Y-m-d H:i:s'));

    $feed = vatsim_parse(vatsim_fetch((string)$config['vatsim_data_url'], 5, 10));
    if ($feed === null) {
        return;
    }
    $controllers = vatsim_controllers($feed);
    if ($controllers !== null) {
        vatsim_write_controllers($controllers, (int)$feed['_time'], vatsim_observers($feed));
    }
}

function within_rate_limit(string $bucket, int $limit): bool
{
    $pdo = db();
    $pdo->prepare(
        'INSERT INTO rate_limit (bucket, window_start, hits) VALUES (?, NOW(), 1)
         ON DUPLICATE KEY UPDATE
             hits         = IF(window_start < NOW() - INTERVAL 60 SECOND, 1, hits + 1),
             window_start = IF(window_start < NOW() - INTERVAL 60 SECOND, NOW(), window_start)'
    )->execute([$bucket]);

    $st = $pdo->prepare('SELECT hits FROM rate_limit WHERE bucket = ?');
    $st->execute([$bucket]);
    return (int)$st->fetchColumn() <= $limit;
}

function check_rate_limit(string $position): void
{
    $limit = (int)(app_config()['rate_limit_per_min'] ?? 120);
    if ($limit > 0 && !within_rate_limit($position, $limit)) {
        json_out(429, ['error' => 'rate_limited']);
    }
}

function require_caller(string $position, bool $observerToo = false): ?string
{
    check_rate_limit($position);
    check_api_key();

    if (in_array($position, (array)(app_config()['test_positions'] ?? []), true)) {
        return null;
    }

    $cid = caller_cid($position, $observerToo);
    if ($cid === null || !controllers_are_fresh()) {
        refresh_controllers_if_due();
        $cid = caller_cid($position, $observerToo);
    }

    if (!controllers_are_fresh()) {
        json_out(503, ['error' => 'network_stale']);
    }
    if ($cid === null) {
        json_out(401, ['error' => 'not_online']);
    }
    return $cid;
}

function read_json_body(): array
{
    $raw  = file_get_contents('php://input', false, null, 0, 16384);
    $data = json_decode((string)$raw, true);
    if (!is_array($data)) {
        json_out(400, ['error' => 'bad_json']);
    }
    return $data;
}

function clean_callsign($value): ?string
{
    $s = strtoupper(trim((string)$value));
    return preg_match('/^[A-Z0-9]{2,12}$/', $s) ? $s : null;
}

function clean_position($value): ?string
{
    $s = strtoupper(trim((string)$value));
    return preg_match('/^[A-Z0-9_-]{2,20}$/', $s) ? $s : null;
}

function clean_code($value): ?string
{
    $s = trim((string)$value);
    return preg_match('/^[0-7]{4}$/', $s) ? $s : null;
}

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

function clean_name_part($value): ?string
{
    $s = trim((string)$value);
    if (!preg_match('/^\p{Cyrillic}+(?:-\p{Cyrillic}+)*$/u', $s)) {
        return null;
    }
    $length = mb_strlen($s, 'UTF-8');
    if ($length < 2 || $length > 40) {
        return null;
    }
    $words = [];
    foreach (explode('-', $s) as $word) {
        $words[] = mb_strtoupper(mb_substr($word, 0, 1, 'UTF-8'), 'UTF-8')
            . mb_strtolower(mb_substr($word, 1, null, 'UTF-8'), 'UTF-8');
    }
    return implode('-', $words);
}

function name_key($value): string
{
    return str_replace('ё', 'е', mb_strtolower(trim((string)$value), 'UTF-8'));
}

function user_display_name(string $surname, string $firstName, string $patronymic): string
{
    $initial = function (string $word): string {
        return mb_substr($word, 0, 1, 'UTF-8') . '.';
    };
    $isPatronymic = preg_match('/\p{Cyrillic}{2}(?:вич|вна|чна|ич)$/u', mb_strtolower($patronymic, 'UTF-8')) === 1;
    if ($isPatronymic && strpos($surname, '-') === false) {
        return $surname . ' ' . $firstName . ' ' . $patronymic;
    }
    return $surname . ' ' . $initial($firstName) . ($patronymic !== '' ? $initial($patronymic) : '');
}
