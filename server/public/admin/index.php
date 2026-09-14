<?php
// Админ-страница: the user_names table - who may open the plug-in's panel, and
// under what name - behind a login of its own, so a person can be handed the
// list of controllers without the database password, the codes or anything
// else in the database.
//
// Logins are config.php's 'admins', login => password_hash(). Taking a login
// out, or changing its hash, ends every session it has open on the next click.
//
// The service has no SSL, so the password goes over plain http like everything
// else does: hand out passwords that are used nowhere else.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';

const ADMIN_IDLE_SEC = 2 * 3600;        // a session nobody has clicked in for this long is over
const ADMIN_LOGIN_TRIES_PER_MIN = 5;    // per address, right or wrong

// An HTML page, not the API's JSON: a failure here is a page too.
set_exception_handler(function (Throwable $e): void {
    error_log('squawk admin: ' . $e->getMessage());
    if (!headers_sent()) {
        http_response_code(500);
        header('Content-Type: text/html; charset=utf-8');
    }
    echo '<!doctype html><meta charset="utf-8"><p>Ошибка сервера. Подробности — в журнале ошибок сайта.</p>';
    exit;
});

header('Content-Type: text/html; charset=utf-8');
header('Cache-Control: no-store');
header('X-Frame-Options: DENY');
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: no-referrer');

function h($value): string
{
    return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

// The AZIMUT logo, drawn here in white rather than served as a file: the mark
// - a ring over a half-disc, the horizon between them - and the word beside it.
function brand_logo(): string
{
    return '<div class="brand" aria-label="AZIMUT">'
        . '<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">'
        . '<path d="M3 16 A13 13 0 0 1 29 16" fill="none" stroke="#fff" stroke-width="5"/>'
        . '<path d="M0.8 19 H31.2 A15.5 15.5 0 0 1 0.8 19 Z" fill="#fff"/>'
        . '</svg><span>AZIMUT</span></div>';
}

// login => hash, keeping only entries that really are password_hash() output -
// a plain password pasted in by mistake lets nobody in.
function admin_accounts(): array
{
    $accounts = [];
    foreach ((array)(app_config()['admins'] ?? []) as $login => $hash) {
        if (is_string($login) && $login !== '' && is_string($hash)
            && password_get_info($hash)['algoName'] !== 'unknown') {
            $accounts[$login] = $hash;
        }
    }
    return $accounts;
}

function client_ip(): string
{
    return (string)($_SERVER['REMOTE_ADDR'] ?? '');
}

// The API's rolling-minute counter, on a bucket of its own per address.
function admin_login_allowed(): bool
{
    $bucket = 'adm:' . substr(hash('sha256', client_ip()), 0, 32);
    $pdo = db();
    $pdo->prepare(
        'INSERT INTO rate_limit (bucket, window_start, hits) VALUES (?, NOW(), 1)
         ON DUPLICATE KEY UPDATE
             hits         = IF(window_start < NOW() - INTERVAL 60 SECOND, 1, hits + 1),
             window_start = IF(window_start < NOW() - INTERVAL 60 SECOND, NOW(), window_start)'
    )->execute([$bucket]);

    $st = $pdo->prepare('SELECT hits FROM rate_limit WHERE bucket = ?');
    $st->execute([$bucket]);
    return (int)$st->fetchColumn() <= ADMIN_LOGIN_TRIES_PER_MIN;
}

// Who is logged in: a login config.php still has, with the same hash it had at
// login, clicked in recently enough. Null otherwise.
function current_admin(array $accounts): ?string
{
    $login = $_SESSION['login'] ?? null;
    if (!is_string($login) || !isset($accounts[$login])
        || !hash_equals((string)($_SESSION['fingerprint'] ?? ''), hash('sha256', $accounts[$login]))
        || (int)($_SESSION['seen'] ?? 0) < time() - ADMIN_IDLE_SEC) {
        return null;
    }
    $_SESSION['seen'] = time();
    return $login;
}

function flash(string $kind, string $text): void
{
    $_SESSION['flash'] = [$kind, $text];
}

// After every POST, back to a plain GET of the page, so a reload never sends
// the form again.
function back_to_page(): void
{
    header('Location: ' . $_SERVER['SCRIPT_NAME'], true, 303);
    exit;
}

session_name('galaxy_admin');
// Lax rather than Strict, so the page opened from a link in a messenger still
// finds the session; the forms carry their own CSRF token either way.
session_set_cookie_params(['lifetime' => 0, 'path' => '/', 'httponly' => true, 'samesite' => 'Lax']);
session_start();
if (!isset($_SESSION['csrf'])) {
    $_SESSION['csrf'] = bin2hex(random_bytes(16));
}

$accounts = admin_accounts();

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $action = (string)($_POST['action'] ?? '');

    if (!hash_equals((string)$_SESSION['csrf'], (string)($_POST['csrf'] ?? ''))) {
        flash('error', 'Страница устарела — попробуйте ещё раз.');
        back_to_page();
    }

    if ($action === 'login') {
        $login = trim((string)($_POST['login'] ?? ''));
        $password = (string)($_POST['password'] ?? '');
        if (!admin_login_allowed()) {
            flash('error', 'Слишком много попыток входа — подождите минуту.');
        } elseif (isset($accounts[$login]) && password_verify($password, $accounts[$login])) {
            session_regenerate_id(true);
            $_SESSION['login'] = $login;
            $_SESSION['fingerprint'] = hash('sha256', $accounts[$login]);
            $_SESSION['seen'] = time();
            $_SESSION['csrf'] = bin2hex(random_bytes(16));
            error_log('squawk admin: ' . $login . ' logged in from ' . client_ip());
        } else {
            flash('error', 'Неверный логин или пароль.');
            error_log('squawk admin: failed login as "' . substr($login, 0, 40) . '" from ' . client_ip());
        }
        back_to_page();
    }

    if ($action === 'logout') {
        $_SESSION = [];
        session_regenerate_id(true);
        back_to_page();
    }

    $admin = current_admin($accounts);
    if ($admin === null) {
        flash('error', 'Сеанс закончился — войдите снова.');
        back_to_page();
    }

    $cid = trim((string)($_POST['cid'] ?? ''));
    if (!preg_match('/^\d{1,12}$/', $cid)) {
        $_SESSION['form'] = ['cid' => $cid, 'name' => (string)($_POST['name'] ?? '')];
        flash('error', 'CID — это номер на VATSIM, только цифры.');
        back_to_page();
    }

    if ($action === 'save') {
        $name = clean_user_name($_POST['name'] ?? '');
        if ($name === null) {
            $_SESSION['form'] = ['cid' => $cid, 'name' => (string)($_POST['name'] ?? '')];
            flash('error', 'Имя кириллицей: «Фамилия Имя Отчество» полностью или «Фамилия И.О.» / «Фамилия И.».');
            back_to_page();
        }
        // MySQL counts 1 for a new row, 2 for a changed one, 0 for the same name again.
        $st = db()->prepare(
            'INSERT INTO user_names (cid, name) VALUES (?, ?)
             ON DUPLICATE KEY UPDATE name = VALUES(name)'
        );
        $st->execute([$cid, $name]);
        $rows = $st->rowCount();
        flash('ok', ($rows === 1 ? 'Добавлен: ' : ($rows === 2 ? 'Имя изменено: ' : 'Без изменений: '))
            . $cid . ' — ' . $name);
        error_log('squawk admin: ' . $admin . ' saved ' . $cid . ' as "' . $name . '"');
        back_to_page();
    }

    if ($action === 'delete') {
        $st = db()->prepare('DELETE FROM user_names WHERE cid = ?');
        $st->execute([$cid]);
        flash('ok', $st->rowCount() > 0
            ? 'Удалён ' . $cid . ' — доступ к панели у него закроется в течение минуты.'
            : 'Записи ' . $cid . ' уже нет.');
        error_log('squawk admin: ' . $admin . ' deleted ' . $cid);
        back_to_page();
    }

    back_to_page();
}

$admin = current_admin($accounts);
$flash = $_SESSION['flash'] ?? null;
unset($_SESSION['flash']);
$form = $_SESSION['form'] ?? ['cid' => '', 'name' => ''];
unset($_SESSION['form']);

$rows = [];
if ($admin !== null) {
    $rows = db()->query('SELECT cid, name, updated_at FROM user_names ORDER BY name')->fetchAll();
}
$csrf = (string)$_SESSION['csrf'];
?>
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>Galaxy ATM System — пользователи</title>
<style>
    :root {
        --ground: #1b2017; --card: #262d20; --line: #3f4836; --text: #eef0ea;
        --dim: #a3aa9a; --accent: #9eff3d; --danger: #ff6b5e; --field: #11140e;
    }
    * { box-sizing: border-box; }
    body {
        margin: 0; padding: 24px 16px; background: var(--ground); color: var(--text);
        font: 15px/1.45 "Segoe UI", Roboto, Arial, sans-serif;
    }
    main { max-width: 820px; margin: 0 auto; }
    h1 { font-size: 20px; margin: 0 0 4px; }
    .sub { color: var(--dim); margin: 0 0 20px; }
    .card { background: var(--card); border: 1px solid var(--line); border-radius: 8px; padding: 16px; margin-bottom: 16px; }
    .card h2 { font-size: 15px; margin: 0 0 12px; }
    label { display: block; color: var(--dim); font-size: 13px; margin-bottom: 4px; }
    input {
        width: 100%; padding: 8px 10px; background: var(--field); color: var(--text);
        border: 1px solid var(--line); border-radius: 6px; font: inherit;
    }
    input:focus { outline: 2px solid var(--accent); outline-offset: -1px; }
    button {
        padding: 8px 14px; border-radius: 6px; border: 1px solid var(--text); background: transparent;
        color: var(--text); font: inherit; cursor: pointer; white-space: nowrap;
    }
    button:hover { background: rgba(255,255,255,.08); }
    button.danger { border-color: var(--danger); color: var(--danger); }
    button.link { border: 0; padding: 0; color: var(--dim); text-decoration: underline; }
    .row { display: flex; gap: 12px; flex-wrap: wrap; align-items: flex-end; }
    .row .cid { flex: 0 1 160px; }
    .row .name { flex: 1 1 260px; }
    .hint { color: var(--dim); font-size: 13px; margin: 8px 0 0; }
    .flash { padding: 10px 14px; border-radius: 6px; margin-bottom: 16px; }
    .flash.ok { background: rgba(158,255,61,.12); border: 1px solid rgba(158,255,61,.4); }
    .flash.error { background: rgba(255,107,94,.12); border: 1px solid rgba(255,107,94,.45); }
    .top { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; flex-wrap: wrap; }
    .table-wrap { overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; }
    th, td { text-align: left; padding: 8px 6px; border-bottom: 1px solid var(--line); vertical-align: middle; }
    th { color: var(--dim); font-weight: 600; font-size: 13px; }
    td.num { font-variant-numeric: tabular-nums; }
    td.when { color: var(--dim); font-size: 13px; white-space: nowrap; }
    td.actions { text-align: right; white-space: nowrap; }
    td.actions form { display: inline; }
    .empty { color: var(--dim); padding: 12px 0 0; }
    .brand { display: flex; align-items: center; gap: 10px; margin: 0 0 18px; color: #fff; }
    .brand span {
        font: 800 24px/1 "Montserrat", "Segoe UI", Arial, sans-serif; letter-spacing: .06em;
    }
    .login { max-width: 360px; margin: 10vh auto 0; }
    .login input { margin-bottom: 12px; }
</style>
</head>
<body>
<main>
<?php if ($admin === null): ?>
    <div class="login">
        <?= brand_logo() ?>
        <h1>Galaxy ATM System</h1>
        <p class="sub">База пользователей КСА</p>
        <?php if ($flash): ?>
            <div class="flash <?= h($flash[0]) ?>"><?= h($flash[1]) ?></div>
        <?php endif; ?>
        <?php if (!$accounts): ?>
            <div class="flash error">Логины не заданы: добавьте их в <code>'admins'</code> в config.php на сервере.</div>
        <?php endif; ?>
        <form method="post" class="card">
            <input type="hidden" name="action" value="login">
            <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
            <label for="login">Логин</label>
            <input id="login" name="login" autocomplete="username" required autofocus>
            <label for="password">Пароль</label>
            <input id="password" name="password" type="password" autocomplete="current-password" required>
            <button type="submit">Войти</button>
        </form>
    </div>
<?php else: ?>
    <?= brand_logo() ?>
    <div class="top">
        <div>
            <h1>База пользователей КСА</h1>
            <p class="sub">Кто может открыть панель плагина. Вошли как <?= h($admin) ?>.</p>
        </div>
        <form method="post">
            <input type="hidden" name="action" value="logout">
            <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
            <button type="submit" class="link">Выйти</button>
        </form>
    </div>

    <?php if ($flash): ?>
        <div class="flash <?= h($flash[0]) ?>"><?= h($flash[1]) ?></div>
    <?php endif; ?>

    <form method="post" class="card" id="edit">
        <h2>Добавить или исправить</h2>
        <input type="hidden" name="action" value="save">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <div class="row">
            <div class="cid">
                <label for="cid">CID на VATSIM</label>
                <input id="cid" name="cid" inputmode="numeric" pattern="\d{1,12}" maxlength="12" required
                       value="<?= h($form['cid']) ?>" placeholder="1234567">
            </div>
            <div class="name">
                <label for="name">Фамилия Имя Отчество</label>
                <input id="name" name="name" maxlength="100" required
                       value="<?= h($form['name']) ?>" placeholder="Иванов Иван Иванович">
            </div>
            <button type="submit">Сохранить</button>
        </div>
        <p class="hint">Без отчества — сокращённо: «Иванов И.». Для CID, который уже есть, имя заменится.</p>
    </form>

    <div class="card">
        <div class="top">
            <h2>Диспетчеры: <?= count($rows) ?></h2>
            <div style="flex: 0 1 240px"><input id="filter" type="search" placeholder="Поиск по имени или CID" aria-label="Поиск"></div>
        </div>
        <?php if (!$rows): ?>
            <p class="empty">Пока никого нет.</p>
        <?php else: ?>
        <div class="table-wrap">
            <table>
                <thead><tr><th>CID</th><th>Имя</th><th>Изменено, UTC</th><th></th></tr></thead>
                <tbody>
                <?php foreach ($rows as $row): ?>
                    <tr data-search="<?= h($row['cid'] . ' ' . $row['name']) ?>">
                        <td class="num"><?= h($row['cid']) ?></td>
                        <td><?= h($row['name']) ?></td>
                        <td class="when"><?= h(substr((string)$row['updated_at'], 0, 16)) ?></td>
                        <td class="actions">
                            <button type="button" class="link js-edit"
                                    data-cid="<?= h($row['cid']) ?>" data-name="<?= h($row['name']) ?>">Изменить</button>
                            &nbsp;
                            <form method="post" class="js-delete" data-name="<?= h($row['name']) ?>">
                                <input type="hidden" name="action" value="delete">
                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                <input type="hidden" name="cid" value="<?= h($row['cid']) ?>">
                                <button type="submit" class="danger">Удалить</button>
                            </form>
                        </td>
                    </tr>
                <?php endforeach; ?>
                </tbody>
            </table>
        </div>
        <?php endif; ?>
    </div>

    <script>
        document.querySelectorAll('.js-edit').forEach(function (b) {
            b.addEventListener('click', function () {
                document.getElementById('cid').value = b.dataset.cid;
                document.getElementById('name').value = b.dataset.name;
                document.getElementById('edit').scrollIntoView({ behavior: 'smooth' });
                document.getElementById('name').focus();
            });
        });
        document.querySelectorAll('.js-delete').forEach(function (f) {
            f.addEventListener('submit', function (e) {
                if (!confirm('Удалить «' + f.dataset.name + '»? Доступ к панели у него закроется.')) e.preventDefault();
            });
        });
        var filter = document.getElementById('filter');
        if (filter) filter.addEventListener('input', function () {
            var q = filter.value.trim().toLowerCase();
            document.querySelectorAll('tbody tr').forEach(function (tr) {
                tr.hidden = q !== '' && tr.dataset.search.toLowerCase().indexOf(q) === -1;
            });
        });
    </script>
<?php endif; ?>
</main>
</body>
</html>
