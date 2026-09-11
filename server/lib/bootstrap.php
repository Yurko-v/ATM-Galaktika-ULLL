<?php
// Shared by every endpoint and by the cron job: settings, the database, JSON
// replies and input checks.

declare(strict_types=1);

date_default_timezone_set('UTC');

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

// Everything in UTC, on the PHP side and the database side alike, so NOW() in
// a query and time() in PHP agree.
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

function require_api_key(): void
{
    $key   = (string)(app_config()['api_key'] ?? '');
    $given = (string)($_SERVER['HTTP_X_API_KEY'] ?? '');
    if ($key === '' || $key === 'CHANGE_ME' || !hash_equals($key, $given)) {
        json_out(401, ['error' => 'unauthorized']);
    }
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
