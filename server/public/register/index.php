<?php
// Регистрация в системе КСА: a controller enters their CID on VATSIM, surname,
// first name, patronymic and a password of their own. From then on LOGIN in the
// plug-in lets them in with the same name and password (api/login.php).
//
// Nothing here proves the CID is the person's own - VATSIM offers no way to
// check that short of its own sign-in. What limits a stranger is that a CID
// registers once: a second registration is turned away, and only the admin
// page's "Сбросить пароль" opens it again. And a registration opens the panel to
// nobody but the CID's owner, since LOGIN is only let in from a position the
// network lists under that CID.
//
// The same page, under "?reset", sets a new password on a registration that
// already exists: CID, the surname, first name and patronymic it was made
// under, and the new password. Matching the name is the whole of the check -
// so anyone who knows a controller's CID and name can take their password
// away and shut them out of the panel. What they cannot do is get in: LOGIN
// still only accepts the position the network lists under that CID, so a
// stolen registration opens nothing. That trade was made deliberately - the
// alternative was a person answering every request by hand.
//
// The service has no SSL, so the password goes over plain http - the page
// says as much.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';

const REGISTER_TRIES_PER_MIN = 5;   // per address
const RESET_TRIES_PER_MIN = 5;      // per address
const RESET_TRIES_PER_CID_PER_MIN = 5;  // per CID, on top of the per-address one
const PASSWORD_MIN_CHARS = 8;
const PASSWORD_MAX_BYTES = 72;      // all bcrypt reads of a password - the rest would be ignored

html_page_setup('squawk register');
start_page_session('galaxy_register');

// The three name fields both forms ask for, cleaned: surname, first name and
// an optional patronymic, or a message saying which of them is not a name.
// $refuse never returns.
function posted_name_parts(callable $refuse): array
{
    $surname = clean_name_part($_POST['surname'] ?? '');
    if ($surname === null) {
        $refuse('Фамилия — русскими буквами, двойная — через дефис.');
    }
    $firstName = clean_name_part($_POST['first_name'] ?? '');
    if ($firstName === null) {
        $refuse('Имя — русскими буквами.');
    }
    $patronymic = '';
    if (trim((string)($_POST['patronymic'] ?? '')) !== '') {
        $patronymic = clean_name_part($_POST['patronymic']);
        if ($patronymic === null) {
            $refuse('Отчество — русскими буквами. Если отчества нет, оставьте поле пустым.');
        }
    }
    return [$surname, $firstName, $patronymic];
}

// The new password, twice, as both forms ask for it. $refuse never returns.
function posted_password(callable $refuse): string
{
    $password = (string)($_POST['password'] ?? '');
    if (mb_strlen($password, 'UTF-8') < PASSWORD_MIN_CHARS) {
        $refuse('Пароль — не короче ' . PASSWORD_MIN_CHARS . ' символов.');
    }
    if (strlen($password) > PASSWORD_MAX_BYTES) {
        $refuse('Пароль слишком длинный: не больше 72 латинских символов (русских — вдвое меньше).');
    }
    if (!hash_equals($password, (string)($_POST['password2'] ?? ''))) {
        $refuse('Пароли не совпадают.');
    }
    return $password;
}

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $action = (string)($_POST['action'] ?? 'register');
    $query = $action === 'reset' ? '?reset' : '';

    // Everything typed but the passwords comes back into the form after an error.
    $form = [];
    foreach (['cid', 'surname', 'first_name', 'patronymic'] as $field) {
        $form[$field] = substr(trim((string)($_POST[$field] ?? '')), 0, 200);
    }
    $refuse = function (string $text) use ($form, $query): void {
        $_SESSION['form'] = $form;
        flash('error', $text);
        back_to_page($query);
    };

    if (!csrf_ok()) {
        $refuse('Страница устарела — отправьте форму ещё раз.');
    }
    $tries = $action === 'reset' ? RESET_TRIES_PER_MIN : REGISTER_TRIES_PER_MIN;
    if (!within_rate_limit(client_bucket($action === 'reset' ? 'rst:' : 'reg:'), $tries)) {
        $refuse('Слишком много попыток — подождите минуту.');
    }

    $cid = $form['cid'];
    if (!preg_match('/^\d{6,8}$/', $cid)) {
        $refuse('CID — ваш номер участника VATSIM, 6–8 цифр.');
    }
    [$surname, $firstName, $patronymic] = posted_name_parts($refuse);

    // ---- Сброс пароля -------------------------------------------------------
    // A new password on a registration that exists already. The name is what
    // is checked, exactly as LOGIN checks it - case, ё or е and the spaces
    // around it make no difference - and the name is not a secret: see the top
    // of the file for what this does and does not protect.
    if ($action === 'reset') {
        if (!within_rate_limit('reset:' . $cid, RESET_TRIES_PER_CID_PER_MIN)) {
            $refuse('Слишком много попыток для этого CID — подождите минуту.');
        }
        $password = posted_password($refuse);

        $st = db()->prepare('SELECT name, surname, first_name, patronymic, password_hash FROM user_names WHERE cid = ?');
        $st->execute([$cid]);
        $row = $st->fetch();
        if (!$row || (string)$row['password_hash'] === '') {
            $refuse('CID ' . $cid . ' не зарегистрирован — менять нечего, зарегистрируйтесь.');
        }

        $sameName = name_key($surname) === name_key($row['surname'])
            && name_key($firstName) === name_key($row['first_name'])
            && name_key($patronymic) === name_key($row['patronymic']);
        if (!$sameName) {
            error_log('squawk register: wrong name for CID ' . $cid . ' on reset from ' . client_ip());
            $refuse('ФИО не совпадает с регистрацией CID ' . $cid . '. Отчество — так же, как тогда: '
                . 'было пусто — оставьте пусто.');
        }

        // Only the password. The name stays as it was registered: this form
        // proves nothing about who is typing, so it changes nothing else.
        db()->prepare('UPDATE user_names SET password_hash = ? WHERE cid = ?')
            ->execute([password_hash($password, PASSWORD_DEFAULT), $cid]);

        error_log('squawk register: CID ' . $cid . ' changed its password from ' . client_ip());
        $_SESSION['reset'] = ['cid' => $cid, 'name' => (string)$row['name']];
        back_to_page('?reset');
    }

    // ---- Регистрация --------------------------------------------------------
    $password = posted_password($refuse);

    // A row the admin entered by name alone is taken over by the registration;
    // a row that already has a password is left exactly as it is. password_hash
    // is assigned last, so every IF before it still sees the old value.
    $name = user_display_name($surname, $firstName, $patronymic);
    $st = db()->prepare(
        'INSERT INTO user_names (cid, name, surname, first_name, patronymic, registered_at, password_hash)
         VALUES (?, ?, ?, ?, ?, NOW(), ?)
         ON DUPLICATE KEY UPDATE
             name          = IF(password_hash IS NULL, VALUES(name), name),
             surname       = IF(password_hash IS NULL, VALUES(surname), surname),
             first_name    = IF(password_hash IS NULL, VALUES(first_name), first_name),
             patronymic    = IF(password_hash IS NULL, VALUES(patronymic), patronymic),
             registered_at = IF(password_hash IS NULL, VALUES(registered_at), registered_at),
             password_hash = IF(password_hash IS NULL, VALUES(password_hash), password_hash)'
    );
    $st->execute([$cid, $name, $surname, $firstName, $patronymic, password_hash($password, PASSWORD_DEFAULT)]);

    // MySQL counts 1 for a new row, 2 for a changed one, 0 for one left alone.
    if ($st->rowCount() === 0) {
        error_log('squawk register: CID ' . $cid . ' is already registered - refused from ' . client_ip());
        $refuse('CID ' . $cid . ' уже зарегистрирован. Забыли пароль — смените его: '
            . 'ссылка под формой.');
    }

    error_log('squawk register: CID ' . $cid . ' registered as "' . $name . '" from ' . client_ip());
    $_SESSION['registered'] = ['cid' => $cid, 'surname' => $surname, 'first_name' => $firstName, 'patronymic' => $patronymic];
    back_to_page();
}

$reset = isset($_GET['reset']);
$flash = take_flash();
$form = ($_SESSION['form'] ?? []) + ['cid' => '', 'surname' => '', 'first_name' => '', 'patronymic' => ''];
unset($_SESSION['form']);
$registered = $_SESSION['registered'] ?? null;
unset($_SESSION['registered']);
$changed = $_SESSION['reset'] ?? null;
unset($_SESSION['reset']);
$done = $registered !== null || $changed !== null;
$csrf = (string)$_SESSION['csrf'];
?>
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title><?= $reset ? 'Сброс пароля' : 'Регистрация в системе КСА' ?> — Galaxy ATM System</title>
<?= page_fonts() ?>
<style>
<?= page_css() ?>
    .shell { max-width: 560px; }
    .foot { max-width: 560px; }
    .pair { display: flex; gap: 16px; flex-wrap: wrap; }
    .pair .field { flex: 1 1 180px; margin-bottom: 16px; }
    .optional { font-weight: 400; color: var(--text-tertiary); }
    form button[type=submit] { width: 100%; height: 56px; margin-top: 8px; }
    .swap { text-align: center; margin: 0; padding: 4px 0 0; color: var(--text-secondary); font-size: 15px; }
    .done { text-align: center; }
    .done .mark {
        width: 64px; height: 64px; margin: 0 auto 20px; border-radius: 50%;
        background: var(--background-surface-accent); color: var(--text-accent);
        display: flex; align-items: center; justify-content: center; font-size: 30px; line-height: 1;
    }
    .done h1 { font-size: 28px; }
    .done dl {
        display: grid; grid-template-columns: auto 1fr; gap: 8px 20px; text-align: left;
        margin: 24px 0; padding: 20px 24px; border-radius: var(--radius-s);
        background: var(--background-section-light);
    }
    .done dt { color: var(--text-secondary); font-size: 14px; align-self: center; }
    .done dd { margin: 0; color: var(--text-primary); }
    .steps { text-align: left; color: var(--text-secondary); margin: 0; padding-left: 22px; }
    .steps li { margin-bottom: 6px; }
</style>
</head>
<body>
<div class="shell">
<?= page_header($done ? '' : ($reset
        ? '<a class="nav-link" href="./">Регистрация</a>'
        : '<a class="nav-link" href="?reset">Сменить пароль</a>')) ?>
<main>
<?php if ($registered): ?>
    <div class="card done" role="status">
        <div class="mark"><svg viewBox="0 0 24 24" width="30" height="30" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 12.5 L9.5 18 L20 6.5"/></svg></div>
        <h1>Вы зарегистрированы в системе КСА</h1>
        <dl>
            <dt>CID</dt><dd><?= h($registered['cid']) ?></dd>
            <dt>Фамилия</dt><dd><?= h($registered['surname']) ?></dd>
            <dt>Имя</dt><dd><?= h($registered['first_name']) ?></dd>
            <dt>Отчество</dt><dd><?= $registered['patronymic'] !== '' ? h($registered['patronymic']) : '—' ?></dd>
        </dl>
        <ol class="steps">
            <li>Подключитесь к VATSIM в EuroScope.</li>
            <li>Нажмите LOGIN на панели.</li>
            <li>Введите фамилию, имя, отчество и пароль.</li>
        </ol>
    </div>
<?php elseif ($changed): ?>
    <div class="card done" role="status">
        <div class="mark"><svg viewBox="0 0 24 24" width="30" height="30" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 12.5 L9.5 18 L20 6.5"/></svg></div>
        <h1>Пароль изменён</h1>
        <dl>
            <dt>CID</dt><dd><?= h($changed['cid']) ?></dd>
            <dt>Диспетчер</dt><dd><?= h($changed['name']) ?></dd>
        </dl>
        <ol class="steps">
            <li>Старый пароль при входе больше не подойдёт.</li>
            <li>В EuroScope нажмите LOGIN и введите те же ФИО и новый пароль.</li>
        </ol>
        <p class="hint">Панель, открытая со старым паролем, доработает до отключения от сети —
            новый пароль понадобится при следующем входе.</p>
    </div>
<?php elseif ($reset): ?>
    <h1>Сброс пароля</h1>
    <p class="sub">Введите CID и ФИО, указанные при регистрации, и придумайте новый пароль.</p>

    <?php if ($flash): ?>
        <div class="flash <?= h($flash[0]) ?>" role="alert"><?= h($flash[1]) ?></div>
    <?php endif; ?>

    <form method="post" class="card" autocomplete="off">
        <input type="hidden" name="action" value="reset">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <div class="field">
            <label for="cid">CID на VATSIM</label>
            <input id="cid" name="cid" inputmode="numeric" pattern="\d{6,8}" maxlength="8" required autofocus
                   value="<?= h($form['cid']) ?>" placeholder="1234567">
        </div>
        <div class="field">
            <label for="surname">Фамилия</label>
            <input id="surname" name="surname" maxlength="40" required
                   value="<?= h($form['surname']) ?>" placeholder="Иванов">
        </div>
        <div class="pair">
            <div class="field">
                <label for="first_name">Имя</label>
                <input id="first_name" name="first_name" maxlength="40" required
                       value="<?= h($form['first_name']) ?>" placeholder="Иван">
            </div>
            <div class="field">
                <label for="patronymic">Отчество <span class="optional">— если есть</span></label>
                <input id="patronymic" name="patronymic" maxlength="40"
                       value="<?= h($form['patronymic']) ?>" placeholder="Иванович">
            </div>
        </div>
        <div class="pair">
            <div class="field">
                <label for="password">Новый пароль</label>
                <input id="password" name="password" type="password" minlength="<?= PASSWORD_MIN_CHARS ?>"
                       autocomplete="new-password" required>
            </div>
            <div class="field">
                <label for="password2">Новый пароль ещё раз</label>
                <input id="password2" name="password2" type="password" minlength="<?= PASSWORD_MIN_CHARS ?>"
                       autocomplete="new-password" required>
            </div>
        </div>
        <button type="submit">Сменить пароль</button>
        <p class="hint">
            ФИО — те же, что при регистрации: регистр букв и «ё» вместо «е» значения не имеют, а вот
            отчество должно быть так же, как тогда. Старый пароль перестанет работать сразу.
        </p>
    </form>
    <p class="swap">Ещё не регистрировались? <a href="./">Зарегистрируйтесь</a></p>
<?php else: ?>
    <h1>Регистрация в системе КСА</h1>
    <p class="sub">Galaxy ATM System · ULLL FIR</p>

    <?php if ($flash): ?>
        <div class="flash <?= h($flash[0]) ?>" role="alert"><?= h($flash[1]) ?></div>
    <?php endif; ?>

    <form method="post" class="card" autocomplete="off">
        <input type="hidden" name="action" value="register">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <div class="field">
            <label for="cid">CID на VATSIM</label>
            <input id="cid" name="cid" inputmode="numeric" pattern="\d{6,8}" maxlength="8" required autofocus
                   value="<?= h($form['cid']) ?>" placeholder="1234567">
        </div>
        <div class="field">
            <label for="surname">Фамилия</label>
            <input id="surname" name="surname" maxlength="40" required
                   value="<?= h($form['surname']) ?>" placeholder="Иванов">
        </div>
        <div class="pair">
            <div class="field">
                <label for="first_name">Имя</label>
                <input id="first_name" name="first_name" maxlength="40" required
                       value="<?= h($form['first_name']) ?>" placeholder="Иван">
            </div>
            <div class="field">
                <label for="patronymic">Отчество <span class="optional">— если есть</span></label>
                <input id="patronymic" name="patronymic" maxlength="40"
                       value="<?= h($form['patronymic']) ?>" placeholder="Иванович">
            </div>
        </div>
        <div class="pair">
            <div class="field">
                <label for="password">Пароль</label>
                <input id="password" name="password" type="password" minlength="<?= PASSWORD_MIN_CHARS ?>"
                       autocomplete="new-password" required>
            </div>
            <div class="field">
                <label for="password2">Пароль ещё раз</label>
                <input id="password2" name="password2" type="password" minlength="<?= PASSWORD_MIN_CHARS ?>"
                       autocomplete="new-password" required>
            </div>
        </div>
        <button type="submit">Зарегистрироваться</button>
        <p class="hint">
            Фамилию, имя, отчество и пароль вы будете вводить в EuroScope при нажатии LOGIN — запишите их так же.
            Сайт работает без шифрования: не используйте пароль от VATSIM или почты.
        </p>
    </form>
    <p class="swap">Забыли пароль? <a href="?reset">Задайте новый</a></p>
<?php endif; ?>
</main>
</div>
<?= page_footer() ?>
</body>
</html>
