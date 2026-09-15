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
// The service has no SSL, so the password goes over plain http - the page
// says as much.

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';

const REGISTER_TRIES_PER_MIN = 5;   // per address
const PASSWORD_MIN_CHARS = 8;
const PASSWORD_MAX_BYTES = 72;      // all bcrypt reads of a password - the rest would be ignored

html_page_setup('squawk register');
start_page_session('galaxy_register');

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    // Everything typed but the passwords comes back into the form after an error.
    $form = [];
    foreach (['cid', 'surname', 'first_name', 'patronymic'] as $field) {
        $form[$field] = substr(trim((string)($_POST[$field] ?? '')), 0, 200);
    }
    $refuse = function (string $text) use ($form): void {
        $_SESSION['form'] = $form;
        flash('error', $text);
        back_to_page();
    };

    if (!csrf_ok()) {
        $refuse('Страница устарела — отправьте форму ещё раз.');
    }
    if (!within_rate_limit(client_bucket('reg:'), REGISTER_TRIES_PER_MIN)) {
        $refuse('Слишком много попыток — подождите минуту.');
    }

    $cid = $form['cid'];
    if (!preg_match('/^\d{6,8}$/', $cid)) {
        $refuse('CID — ваш номер участника VATSIM, 6–8 цифр.');
    }
    $surname = clean_name_part($form['surname']);
    if ($surname === null) {
        $refuse('Фамилия — русскими буквами, двойная — через дефис.');
    }
    $firstName = clean_name_part($form['first_name']);
    if ($firstName === null) {
        $refuse('Имя — русскими буквами.');
    }
    $patronymic = '';
    if ($form['patronymic'] !== '') {
        $patronymic = clean_name_part($form['patronymic']);
        if ($patronymic === null) {
            $refuse('Отчество — русскими буквами. Если отчества нет, оставьте поле пустым.');
        }
    }

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
        $refuse('CID ' . $cid . ' уже зарегистрирован. Если вы забыли пароль или регистрировались не вы, '
            . 'обратитесь к инженеру системы.');
    }

    error_log('squawk register: CID ' . $cid . ' registered as "' . $name . '" from ' . client_ip());
    $_SESSION['registered'] = ['cid' => $cid, 'surname' => $surname, 'first_name' => $firstName, 'patronymic' => $patronymic];
    back_to_page();
}

$flash = take_flash();
$form = $_SESSION['form'] ?? ['cid' => '', 'surname' => '', 'first_name' => '', 'patronymic' => ''];
unset($_SESSION['form']);
$registered = $_SESSION['registered'] ?? null;
unset($_SESSION['registered']);
$csrf = (string)$_SESSION['csrf'];
?>
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>Регистрация в системе КСА — Galaxy ATM System</title>
<style>
<?= page_css() ?>
    main { max-width: 440px; margin-top: 6vh; }
    .field { margin-bottom: 12px; }
    .pair { display: flex; gap: 12px; flex-wrap: wrap; }
    .pair .field { flex: 1 1 160px; }
    .optional { color: var(--dim); font-weight: 400; }
    form button[type=submit] { width: 100%; margin-top: 4px; }
    .done { text-align: center; padding: 24px 16px; }
    .done .mark {
        width: 44px; height: 44px; margin: 0 auto 12px; border-radius: 50%;
        border: 2px solid var(--accent); color: var(--accent);
        display: flex; align-items: center; justify-content: center; font-size: 24px; line-height: 1;
    }
    .done h1 { margin-bottom: 12px; }
    .done dl { display: grid; grid-template-columns: auto 1fr; gap: 4px 12px; text-align: left; margin: 16px 0; }
    .done dt { color: var(--dim); }
    .done dd { margin: 0; }
    .steps { text-align: left; color: var(--dim); margin: 0; padding-left: 20px; }
</style>
</head>
<body>
<main>
    <?= brand_logo() ?>
<?php if ($registered): ?>
    <div class="card done" role="status">
        <div class="mark" aria-hidden="true">&#10003;</div>
        <h1>Вы успешно зарегистрированы в системе КСА</h1>
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
<?php else: ?>
    <h1>Регистрация в системе КСА</h1>
    <p class="sub">Galaxy ATM System · ULLL FIR</p>

    <?php if ($flash): ?>
        <div class="flash <?= h($flash[0]) ?>" role="alert"><?= h($flash[1]) ?></div>
    <?php endif; ?>

    <form method="post" class="card" autocomplete="off">
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
<?php endif; ?>
</main>
</body>
</html>
