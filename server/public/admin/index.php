<?php
// Админ-страница: the user_names table - who may log in to the plug-in's panel,
// and under what name - behind a login of its own, so a person can be handed
// the list of controllers without the database password, the codes or anything
// else in the database.
//
// Logins are config.php's 'admins', login => password_hash(). Taking a login
// out, or changing its hash, ends every session it has open on the next click.
//
// Controllers register themselves on public/register/. Here a name can be
// corrected (the one shown - LOGIN still checks the registered parts), a
// password reset so the CID can register again, or a controller taken out.
//
// The service has no SSL, so the password goes over plain http like everything
// else does: hand out passwords that are used nowhere else.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';

const ADMIN_IDLE_SEC = 2 * 3600;        // a session nobody has clicked in for this long is over
const ADMIN_LOGIN_TRIES_PER_MIN = 5;    // per address, right or wrong

html_page_setup('squawk admin');

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

start_page_session('galaxy_admin');

$accounts = admin_accounts();

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $action = (string)($_POST['action'] ?? '');

    if (!csrf_ok()) {
        flash('error', 'Страница устарела — попробуйте ещё раз.');
        back_to_page();
    }

    if ($action === 'login') {
        $login = trim((string)($_POST['login'] ?? ''));
        $password = (string)($_POST['password'] ?? '');
        if (!within_rate_limit(client_bucket('adm:'), ADMIN_LOGIN_TRIES_PER_MIN)) {
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
        flash('ok', ($rows === 1 ? 'Добавлен: ' . $cid . ' — ' . $name . '. Войти он сможет после регистрации на сайте.'
            : ($rows === 2 ? 'Имя изменено: ' : 'Без изменений: ') . $cid . ' — ' . $name));
        error_log('squawk admin: ' . $admin . ' saved ' . $cid . ' as "' . $name . '"');
        back_to_page();
    }

    // The password goes, the row and its name stay: the CID can register on
    // the site again, and until then LOGIN turns it away.
    if ($action === 'reset') {
        $st = db()->prepare('UPDATE user_names SET password_hash = NULL WHERE cid = ? AND password_hash IS NOT NULL');
        $st->execute([$cid]);
        flash('ok', $st->rowCount() > 0
            ? 'Пароль ' . $cid . ' сброшен — диспетчер может зарегистрироваться на сайте заново.'
            : 'У ' . $cid . ' пароля и так нет.');
        error_log('squawk admin: ' . $admin . ' reset the password of ' . $cid);
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
$flash = take_flash();
$form = $_SESSION['form'] ?? ['cid' => '', 'name' => ''];
unset($_SESSION['form']);

$rows = [];
if ($admin !== null) {
    $rows = db()->query(
        'SELECT cid, name, password_hash IS NOT NULL AS has_password, updated_at FROM user_names ORDER BY name'
    )->fetchAll();
}
$csrf = (string)$_SESSION['csrf'];
?>
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>АРМ инженера системы — Galaxy ATM System</title>
<style>
<?= page_css() ?>
    .row { display: flex; gap: 12px; flex-wrap: wrap; align-items: flex-end; }
    .row .cid { flex: 0 1 160px; }
    .row .name { flex: 1 1 260px; }
    .top { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; flex-wrap: wrap; }
    .table-wrap { overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; }
    th, td { text-align: left; padding: 8px 6px; border-bottom: 1px solid var(--line); vertical-align: middle; }
    th { color: var(--dim); font-weight: 600; font-size: 13px; }
    td.num { font-variant-numeric: tabular-nums; }
    td.when { color: var(--dim); font-size: 13px; white-space: nowrap; }
    td.pass { font-size: 13px; white-space: nowrap; }
    td.pass .no { color: var(--dim); }
    td.actions { text-align: right; white-space: nowrap; }
    td.actions form { display: inline; }
    .empty { color: var(--dim); padding: 12px 0 0; }
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
        <p class="sub">АРМ инженера системы</p>
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
            <h1>АРМ инженера системы</h1>
            <p class="sub">Кто может войти в панель плагина. Вошли как <?= h($admin) ?>.</p>
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
        <h2>Добавить или исправить имя</h2>
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
        <p class="hint">
            Меняет только имя в блоке «Пользователь»; при входе диспетчер вводит ФИО и пароль, указанные при
            регистрации. Без отчества — сокращённо: «Иванов И.». Диспетчеры регистрируются сами на
            <a href="../register/" style="color: inherit">странице регистрации</a>.
        </p>
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
                <thead><tr><th>CID</th><th>Имя</th><th>Пароль</th><th>Изменено, UTC</th><th></th></tr></thead>
                <tbody>
                <?php foreach ($rows as $row): ?>
                    <tr data-search="<?= h($row['cid'] . ' ' . $row['name']) ?>">
                        <td class="num"><?= h($row['cid']) ?></td>
                        <td><?= h($row['name']) ?></td>
                        <td class="pass"><?= $row['has_password'] ? 'задан' : '<span class="no">не зарегистрирован</span>' ?></td>
                        <td class="when"><?= h(substr((string)$row['updated_at'], 0, 16)) ?></td>
                        <td class="actions">
                            <button type="button" class="link js-edit"
                                    data-cid="<?= h($row['cid']) ?>" data-name="<?= h($row['name']) ?>">Изменить</button>
                            <?php if ($row['has_password']): ?>
                            &nbsp;
                            <form method="post" class="js-confirm"
                                  data-question="Сбросить пароль «<?= h($row['name']) ?>»? Войти можно будет только после новой регистрации на сайте.">
                                <input type="hidden" name="action" value="reset">
                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                <input type="hidden" name="cid" value="<?= h($row['cid']) ?>">
                                <button type="submit">Сбросить пароль</button>
                            </form>
                            <?php endif; ?>
                            &nbsp;
                            <form method="post" class="js-confirm"
                                  data-question="Удалить «<?= h($row['name']) ?>»? Доступ к панели у него закроется.">
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
        document.querySelectorAll('.js-confirm').forEach(function (f) {
            f.addEventListener('submit', function (e) {
                if (!confirm(f.dataset.question)) e.preventDefault();
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
