<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';

const REGISTER_TRIES_PER_MIN = 5;

html_page_setup('squawk register');
start_page_session('galaxy_register');

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

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $form = [];

    foreach (['cid', 'rating_id', 'surname', 'first_name', 'patronymic'] as $field) {
        $form[$field] = substr(
            trim((string)($_POST[$field] ?? '')),
            0,
            200
        );
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

    // The id VATSIM itself gives the rating, not its name - see lib/ratings.php.
    $ratingId = clean_rating_id($form['rating_id']);

    if ($ratingId === null) {
        $refuse('Выберите диспетчерский рейтинг.');
    }

    if (!rating_allows_ksa($ratingId)) {
        $refuse('С данным диспетчерским рейтингом доступ к системе КСА не предоставляется.');
    }

    [$surname, $firstName, $patronymic] = posted_name_parts($refuse);

    $st = db()->prepare('SELECT cid FROM user_names WHERE cid = ?');
    $st->execute([$cid]);

    if ($st->fetchColumn() !== false) {
        error_log(
            'squawk register: CID ' . $cid
            . ' is already registered - refused from ' . client_ip()
        );

        $refuse(
            'CID ' . $cid . ' уже зарегистрирован. '
            . 'Для изменения данных обратитесь к инженеру системы.'
        );
    }

    $name = user_display_name(
        $surname,
        $firstName,
        $patronymic
    );

    $st = db()->prepare(
        'INSERT INTO user_names
            (cid, rating_id, name, surname, first_name, patronymic, registered_at)
         VALUES (?, ?, ?, ?, ?, ?, NOW())'
    );

    $st->execute([
        $cid,
        $ratingId,
        $name,
        $surname,
        $firstName,
        $patronymic,
    ]);

    error_log(
        'squawk register: CID ' . $cid
        . ' (' . controller_rating_short($ratingId) . ') '
        . 'registered as "' . $name . '" from ' . client_ip()
    );

    $_SESSION['registered'] = [
        'cid' => $cid,
        'rating_id' => $ratingId,
        'surname' => $surname,
        'first_name' => $firstName,
        'patronymic' => $patronymic,
    ];

    back_to_page();
}

$flash = take_flash();

$form = ($_SESSION['form'] ?? []) + [
    'cid' => '',
    'rating_id' => '',
    'surname' => '',
    'first_name' => '',
    'patronymic' => '',
];

unset($_SESSION['form']);

$registered = $_SESSION['registered'] ?? null;
unset($_SESSION['registered']);

$done = $registered !== null;

$csrf = (string)$_SESSION['csrf'];
$ratingOptions = ksa_rating_options();
?>
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>Регистрация в системе КСА — Galaxy ATM System</title>
<?= page_fonts() ?>
<style>
<?= page_css() ?>
    .shell { max-width: 560px; }
    .foot { max-width: 560px; }
    .pair { display: flex; gap: 16px; flex-wrap: wrap; }
    .pair .field { flex: 1 1 180px; margin-bottom: 16px; }
    form button[type=submit] { width: 100%; height: 56px; margin-top: 8px; }
    .done { text-align: center; }
    .done .mark {
        width: 64px;
        height: 64px;
        margin: 0 auto 20px;
        border-radius: 50%;
        background: var(--background-surface-accent);
        color: var(--text-accent);
        display: flex;
        align-items: center;
        justify-content: center;
        font-size: 30px;
        line-height: 1;
    }
    .done h1 { font-size: 28px; }
    .done dl {
        display: grid;
        grid-template-columns: auto 1fr;
        gap: 8px 20px;
        text-align: left;
        margin: 24px 0;
        padding: 20px 24px;
        border-radius: var(--radius-s);
        background: var(--background-section-light);
    }
    .done dt {
        color: var(--text-secondary);
        font-size: 14px;
        align-self: center;
    }
    .done dd {
        margin: 0;
        color: var(--text-primary);
    }
</style>
</head>
<body>
<div class="shell">
<?= page_header('') ?>

<main>
<?php if ($registered): ?>
    <div class="card done" role="status">
        <div class="mark">
            <svg viewBox="0 0 24 24" width="30" height="30" fill="none"
                 stroke="currentColor" stroke-width="2.2"
                 stroke-linecap="round" stroke-linejoin="round"
                 aria-hidden="true">
                <path d="M4 12.5 L9.5 18 L20 6.5"/>
            </svg>
        </div>

        <h1>Вы зарегистрированы в системе КСА</h1>

        <dl>
            <dt>CID</dt>
            <dd><?= h($registered['cid']) ?></dd>

            <dt>Рейтинг</dt>
            <dd><?= h(controller_rating_short((int)$registered['rating_id']) ?? '—') ?></dd>

            <dt>Фамилия</dt>
            <dd><?= h($registered['surname']) ?></dd>

            <dt>Имя</dt>
            <dd><?= h($registered['first_name']) ?></dd>

            <dt>Отчество</dt>
            <dd>
                <?= $registered['patronymic'] !== ''
                    ? h($registered['patronymic'])
                    : '—' ?>
            </dd>
        </dl>

        <p class="hint">
            После подключения к VATSIM и занятия диспетчерской позиции система
            автоматически определит ваш CID и предоставит доступ к КСА.
        </p>
    </div>
<?php else: ?>

    <h1>Регистрация в системе КСА</h1>
    <p class="sub">Galaxy ATM System · ULLL FIR</p>

    <?php if ($flash): ?>
        <div class="flash <?= h($flash[0]) ?>" role="alert">
            <?= h($flash[1]) ?>
        </div>
    <?php endif; ?>

    <form method="post" class="card" autocomplete="off">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">

        <div class="field">
            <label for="cid">CID на VATSIM</label>
            <input
                id="cid"
                name="cid"
                inputmode="numeric"
                pattern="\d{6,8}"
                maxlength="8"
                required
                autofocus
                value="<?= h($form['cid']) ?>"
                placeholder="1234567"
            >
        </div>

        <div class="field">
            <label for="rating_id">Диспетчерский рейтинг</label>
            <select id="rating_id" name="rating_id" required>
                <option value="">Выберите рейтинг</option>

                <?php foreach ($ratingOptions as $ratingId => $rating): ?>
                    <option
                        value="<?= h((string)$ratingId) ?>"
                        <?= $form['rating_id'] === (string)$ratingId ? 'selected' : '' ?>
                    >
                        <?= h($rating['short']) ?> — <?= h($rating['long']) ?>
                    </option>
                <?php endforeach; ?>
            </select>
        </div>

        <div class="field">
            <label for="surname">Фамилия</label>
            <input
                id="surname"
                name="surname"
                maxlength="40"
                required
                value="<?= h($form['surname']) ?>"
                placeholder="Иванов"
            >
        </div>

        <div class="pair">
            <div class="field">
                <label for="first_name">Имя</label>
                <input
                    id="first_name"
                    name="first_name"
                    maxlength="40"
                    required
                    value="<?= h($form['first_name']) ?>"
                    placeholder="Иван"
                >
            </div>

            <div class="field">
                <label for="patronymic">Отчество <span class="optional">— если есть</span></label>
                <input
                    id="patronymic"
                    name="patronymic"
                    maxlength="40"
                    value="<?= h($form['patronymic']) ?>"
                    placeholder="Иванович"
                >
            </div>
        </div>

        <button type="submit">Зарегистрироваться</button>

        <p class="hint">
            Сейчас CID и рейтинг вводятся вручную. После подключения VATSIM OAuth
            эти данные будут автоматически получаться из вашего аккаунта VATSIM.
        </p>
    </form>

<?php endif; ?>
</main>
</div>

<?= page_footer() ?>
</body>
</html>