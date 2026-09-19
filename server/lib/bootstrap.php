<?php
// Shared by every endpoint and by the cron job: settings, the database, JSON
// replies and input checks.

declare(strict_types=1);

date_default_timezone_set('UTC');

// Reading the VATSIM feed: needed by the cron job, and by the check on who is
// calling further down.
require_once __DIR__ . '/vatsim.php';
require_once __DIR__ . '/rating.php';

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

// The small key/value store the sync times live in.
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

// ---- Who is calling ---------------------------------------------------------
// There is no secret on the controller's side to check: the plug-in is handed
// out as it stands, and a file with a shared key in it is a file that leaks.
// What a real caller has instead is something no config file can carry - they
// are sitting on that position on the VATSIM network at this moment. That is
// what is checked here, against the snapshot the cron job keeps (see
// lib/vatsim.php), so every secret the service has stays in config.php on the
// server.
//
// An api_key in config.php is still honoured if it is set, as a second lock on
// top - but it is optional, and empty by default.

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

// True while the snapshot of who is controlling is recent enough to judge a
// caller by. Its own clock, separate from the pilots' - the controller list is
// what stands between the service and anyone at all, so it is held to a tighter
// age than the code pool is.
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

// The CID of an observer on that callsign - null as well on a server that has
// no network_observers table yet.
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

// The caller's CID on that callsign: a controller's, and with $observerToo an
// observer's as well.
function caller_cid(string $position, bool $observerToo): ?string
{
    $cid = controller_cid($position);
    return ($cid === null && $observerToo) ? observer_cid($position) : $cid;
}

// Reads the feed here and now, rather than waiting for the next cron run -
// but at most once every controller_refresh_sec, whatever the answer, so a
// caller nobody knows cannot turn every request into a fetch from VATSIM.
function refresh_controllers_if_due(): void
{
    $config = app_config();
    $every  = max(5, (int)($config['controller_refresh_sec'] ?? 20));

    $last = sync_state_get('controllers_polled');
    if ($last !== null && strtotime($last . ' UTC') > time() - $every) {
        return;
    }
    // Stamped before the fetch, not after: a feed that times out must not leave
    // the door open for another fetch on the very next request.
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

// Counts one more hit on a bucket over a rolling minute, and says whether it is
// still within $limit. Buckets are up to 40 characters: a position, or a
// prefix and a hash for anything else.
function within_rate_limit(string $bucket, int $limit): bool
{
    $pdo = db();
    // The window starts over once it is a minute old; MySQL takes the
    // assignments left to right, so hits still sees the old window_start.
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

// No more than this many requests a minute from one position. Not security -
// the online check is that - just a bound on what a plug-in stuck in a loop, or
// someone poking at the service, can cost the database.
function check_rate_limit(string $position): void
{
    $limit = (int)(app_config()['rate_limit_per_min'] ?? 120);
    if ($limit > 0 && !within_rate_limit($position, $limit)) {
        json_out(429, ['error' => 'rate_limited']);
    }
}

// Every endpoint that changes or reads the pool goes through here. Returns the
// caller's CID, for the record kept against the codes they hand out.
// $observerToo lets an observer in as well - only api/name.php does: an OBS may
// open the plug-in's panel, but never hands out a code.
function require_caller(string $position, bool $observerToo = false): ?string
{
    check_rate_limit($position);
    check_api_key();

    // A position let in without the online check - for testing the server from
    // the command line. Empty in normal use; see config.sample.php.
    if (in_array($position, (array)(app_config()['test_positions'] ?? []), true)) {
        return null;
    }

    $cid = caller_cid($position, $observerToo);
    if ($cid === null || !controllers_are_fresh()) {
        refresh_controllers_if_due();
        $cid = caller_cid($position, $observerToo);
    }

    if (!controllers_are_fresh()) {
        // Nobody can be checked, so nobody is let in: the alternative is a
        // service that quietly stops checking the moment VATSIM is unreachable.
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

// A controller's name for user_names: "Фамилия Имя Отчество" in full, or
// shortened to "Фамилия И.О." / "Фамилия И." - Cyrillic only, with a hyphen
// allowed inside a double surname. Anything else is not a name the plug-in's
// RussianShortName can show, and is turned down rather than stored. For the
// admin page; registration builds the name from its parts (user_display_name).
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

// ---- Registration and LOGIN -------------------------------------------------

// One part of a name as typed on the registration page - surname, first name
// or patronymic: Cyrillic letters, at least two, with a hyphen inside a double
// word - put in capitals the usual way: "римская-КОРСАКОВА" -> "Римская-Корсакова".
// Null when it is not that.
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

// How a name part typed at LOGIN is matched against the registered one: case,
// ё or е, and spaces around it make no difference.
function name_key($value): string
{
    return str_replace('ё', 'е', mb_strtolower(trim((string)$value), 'UTF-8'));
}

// The name the Пользователь block is to show, from the registered parts. In
// full only where the plug-in's RussianShortName reads it back the same way:
// a patronymic with a patronymic's ending tells it which word is the surname,
// and a double surname would lose its second capital there. Otherwise
// shortened here, while the order is still known: "Римская-Корсакова И.П.".
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
