<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';

const ADMIN_IDLE_SEC = 2 * 3600;
const ADMIN_LOGIN_TRIES_PER_MIN = 5;

html_page_setup('squawk admin');

function current_engineer(): ?array
{
    $cid = $_SESSION['engineer_cid'] ?? null;

    if (!is_string($cid) || !preg_match('/^\d{6,8}$/', $cid)) {
        return null;
    }

    $st = db()->prepare(
        'SELECT cid, rating_id, name, surname, first_name, patronymic, is_engineer
         FROM user_names
         WHERE cid = ? AND is_engineer = 1'
    );

    $st->execute([$cid]);
    $engineer = $st->fetch();

    if (!$engineer) {
        unset($_SESSION['engineer_cid']);
        return null;
    }

    $seen = (int)($_SESSION['engineer_seen'] ?? 0);

    if ($seen < time() - ADMIN_IDLE_SEC) {
        unset($_SESSION['engineer_cid'], $_SESSION['engineer_seen']);
        return null;
    }

    $_SESSION['engineer_seen'] = time();

    return $engineer;
}

start_page_session('galaxy_admin');

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $action = (string)($_POST['action'] ?? '');

    if (!csrf_ok()) {
        flash('error', 'Страница устарела — попробуйте ещё раз.');
        back_to_page();
    }

    if ($action === 'login') {
        $cid = trim((string)($_POST['cid'] ?? ''));

        if (!preg_match('/^\d{6,8}$/', $cid)) {
            flash('error', 'CID — это номер участника VATSIM, 6–8 цифр.');
            back_to_page();
        }

        if (!within_rate_limit(client_bucket('adm:'), ADMIN_LOGIN_TRIES_PER_MIN)) {
            flash('error', 'Слишком много попыток входа — подождите минуту.');
            back_to_page();
        }

        $st = db()->prepare(
            'SELECT cid
             FROM user_names
             WHERE cid = ? AND is_engineer = 1'
        );

        $st->execute([$cid]);

        if ($st->fetchColumn() !== false) {
            session_regenerate_id(true);

            $_SESSION['engineer_cid'] = $cid;
            $_SESSION['engineer_seen'] = time();
            $_SESSION['csrf'] = bin2hex(random_bytes(16));

            error_log(
                'squawk admin: CID ' . $cid
                . ' entered engineer panel from ' . client_ip()
            );
        } else {
            flash('error', 'Этот CID не имеет доступа к инженерной панели.');

            error_log(
                'squawk admin: failed engineer login for CID '
                . substr($cid, 0, 20)
                . ' from ' . client_ip()
            );
        }

        back_to_page();
    }

    if ($action === 'logout') {
        $_SESSION = [];
        session_regenerate_id(true);
        back_to_page();
    }

    $engineer = current_engineer();

    if ($engineer === null) {
        flash('error', 'Сеанс закончился — войдите снова.');
        back_to_page();
    }

    $cid = trim((string)($_POST['cid'] ?? ''));

    if (!preg_match('/^\d{6,8}$/', $cid)) {
        $_SESSION['form'] = $_POST;
        flash('error', 'CID — это номер на VATSIM, только 6–8 цифр.');
        back_to_page();
    }

    if ($action === 'create') {
        $ratingId = clean_rating_id($_POST['rating_id'] ?? '');

        if ($ratingId === null) {
            $_SESSION['form'] = $_POST;
            flash('error', 'Выберите диспетчерский рейтинг.');
            back_to_page();
        }

        if (!rating_allows_ksa($ratingId)) {
            $_SESSION['form'] = $_POST;
            flash('error', 'Этот рейтинг не даёт доступа к КСА.');
            back_to_page();
        }

        $surname = clean_name_part($_POST['surname'] ?? '');
        $firstName = clean_name_part($_POST['first_name'] ?? '');

        $patronymic = '';

        if (trim((string)($_POST['patronymic'] ?? '')) !== '') {
            $patronymic = clean_name_part($_POST['patronymic']);

            if ($patronymic === null) {
                $_SESSION['form'] = $_POST;
                flash('error', 'Отчество должно быть указано русскими буквами.');
                back_to_page();
            }
        }

        if ($surname === null || $firstName === null) {
            $_SESSION['form'] = $_POST;
            flash('error', 'Фамилия и имя должны быть указаны русскими буквами.');
            back_to_page();
        }

        $name = user_display_name(
            $surname,
            $firstName,
            $patronymic
        );

        $isEngineer = isset($_POST['is_engineer']) ? 1 : 0;

        $st = db()->prepare(
            'SELECT cid
             FROM user_names
             WHERE cid = ?'
        );
        $st->execute([$cid]);

        if ($st->fetchColumn() !== false) {
            $_SESSION['form'] = $_POST;
            flash('error', 'Пользователь с этим CID уже существует.');
            back_to_page();
        }

        $st = db()->prepare(
            'INSERT INTO user_names
                (
                    cid,
                    rating_id,
                    name,
                    surname,
                    first_name,
                    patronymic,
                    is_engineer,
                    registered_at
                )
             VALUES (?, ?, ?, ?, ?, ?, ?, NOW())'
        );

        $st->execute([
            $cid,
            $ratingId,
            $name,
            $surname,
            $firstName,
            $patronymic,
            $isEngineer,
        ]);

        flash(
            'ok',
            'Добавлен: ' . $cid
            . ' — ' . $name
            . ' (' . controller_rating_short($ratingId) . ').'
        );

        error_log(
            'squawk admin: '
            . $engineer['cid']
            . ' added CID '
            . $cid
            . ' as "'
            . $name
            . '"'
        );

        back_to_page();
    }

    if ($action === 'update') {
        $ratingId = clean_rating_id($_POST['rating_id'] ?? '');

        if ($ratingId === null) {
            flash('error', 'Выберите диспетчерский рейтинг.');
            back_to_page();
        }

        if (!rating_allows_ksa($ratingId)) {
            flash('error', 'Этот рейтинг не даёт доступа к КСА.');
            back_to_page();
        }

        $surname = clean_name_part($_POST['surname'] ?? '');
        $firstName = clean_name_part($_POST['first_name'] ?? '');

        $patronymic = '';

        if (trim((string)($_POST['patronymic'] ?? '')) !== '') {
            $patronymic = clean_name_part($_POST['patronymic']);

            if ($patronymic === null) {
                flash('error', 'Отчество должно быть указано русскими буквами.');
                back_to_page();
            }
        }

        if ($surname === null || $firstName === null) {
            flash('error', 'Фамилия и имя должны быть указаны русскими буквами.');
            back_to_page();
        }

        $name = user_display_name(
            $surname,
            $firstName,
            $patronymic
        );

        $isEngineer = isset($_POST['is_engineer']) ? 1 : 0;

        if ($cid === $engineer['cid'] && $isEngineer === 0) {
            flash('error', 'Нельзя снять доступ к инженерной панели у текущего инженера.');
            back_to_page();
        }

        $st = db()->prepare(
            'UPDATE user_names
            SET rating_id = ?,
                name = ?,
                surname = ?,
                first_name = ?,
                patronymic = ?,
                is_engineer = ?
            WHERE cid = ?'
        );

        $st->execute([
            $ratingId,
            $name,
            $surname,
            $firstName,
            $patronymic,
            $isEngineer,
            $cid,
        ]);

        flash(
            'ok',
            'Сохранено: ' . $cid
            . ' — ' . $name
            . ' (' . controller_rating_short($ratingId) . ').'
        );

        back_to_page();
    }

    if ($action === 'delete') {
        if ($cid === $engineer['cid']) {
            flash('error', 'Нельзя удалить учётную запись текущего инженера.');
            back_to_page();
        }

        $st = db()->prepare(
            'DELETE FROM user_names WHERE cid = ?'
        );

        $st->execute([$cid]);

        flash(
            'ok',
            $st->rowCount() > 0
                ? 'Удалён ' . $cid . ' — доступ к панели будет закрыт.'
                : 'Записи ' . $cid . ' уже нет.'
        );

        error_log(
            'squawk admin: '
            . $engineer['cid']
            . ' deleted '
            . $cid
        );

        back_to_page();
    }

    back_to_page();
}

$engineer = current_engineer();

$flash = take_flash();

$form = $_SESSION['form'] ?? [
    'cid' => '',
    'rating_id' => '',
    'surname' => '',
    'first_name' => '',
    'patronymic' => '',
    'is_engineer' => '',
];

unset($_SESSION['form']);

$rows = [];

if ($engineer !== null) {
    $rows = db()->query(
        'SELECT
            cid,
            rating_id,
            name,
            surname,
            first_name,
            patronymic,
            is_engineer,
            updated_at
        FROM user_names
        ORDER BY name'
    )->fetchAll();
}

$csrf = (string)$_SESSION['csrf'];
$ratingOptions = ksa_rating_options();
?>
<!DOCTYPE html>
<html lang="ru">
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <meta name="robots" content="noindex, nofollow">
        <title>АРМ инженера системы — Galaxy ATM System</title>
        <?= page_fonts() ?>
        <style>
        <?= page_css() ?>

            .shell,
            .foot {
                max-width: 1040px;
            }

            .row {
                display: flex;
                gap: 16px;
                flex-wrap: wrap;
                align-items: flex-end;
            }

            .row .cid {
                flex: 0 1 180px;
            }

            .row .rating {
                flex: 0 1 180px;
            }

            .row .name {
                flex: 1 1 280px;
            }

            .row .field {
                margin-bottom: 0;
            }

            .row button {
                height: 52px;
            }

            .top {
                display: flex;
                justify-content: space-between;
                align-items: center;
                gap: 12px 20px;
                flex-wrap: wrap;
            }

            .top h2 {
                display: flex;
                align-items: center;
                gap: 10px;
                margin-bottom: 0;
            }

            .card .top {
                margin-bottom: 20px;
            }

            .search {
                flex: 0 1 280px;
            }

            .head {
                margin-bottom: 28px;
            }

            .head h1 {
                margin-bottom: 4px;
            }

            .head .sub {
                margin-bottom: 0;
            }

            .count {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                min-width: 28px;
                height: 28px;
                padding: 0 10px;
                border-radius: var(--radius-pill);
                background: var(--background-primary);
                color: var(--text-main-alt);
                font-size: 14px;
                font-weight: 500;
            }

            .login {
                max-width: 420px;
                margin: 4vh auto 0;
            }

            .login .field {
                margin-bottom: 16px;
            }

            .login button {
                width: 100%;
                height: 56px;
                margin-top: 8px;
            }

            .engineer-check {
                display: flex;
                gap: 10px;
                align-items: center;
                margin: 18px 0 0;
            }

            .engineer-check input {
                width: auto;
            }

            .user-row .edit-field {
                display: none;
            }

            .user-row.is-editing .view-value {
                display: none;
            }

            .user-row.is-editing .edit-field {
                display: inline-flex;
            }

            .user-row.is-editing .actions .js-edit {
                display: none;
            }

            .user-row .inline-edit-form {
                display: contents;
            }

            .user-row input,
            .user-row select {
                width: 100%;
                min-width: 100px;
                height: 40px;
                padding: 0 14px;
            }

            .user-row .engineer-cell {
                display: none;
                align-items: center;
                gap: 8px;
                margin: 0;
            }

            .user-row.is-editing .engineer-cell {
                display: inline-flex;
            }

            .user-row .engineer-cell input {
                width: auto;
                min-width: 0;
                height: auto;
            }

            .user-row .actions .acts {
                align-items: center;
            }
        </style>
    </head>
    <body>
        <?php if ($engineer === null): ?>
        <div class="shell">
            <?= page_header('<a class="nav-link" href="../register/">Регистрация</a>') ?>
            <main class="login">
                <h1>АРМ инженера системы</h1>
                <p class="sub">Galaxy ATM System · ULLL FIR</p>
                <?php if ($flash): ?>
                    <div class="flash <?= h($flash[0]) ?>" role="alert">
                        <?= h($flash[1]) ?>
                    </div>
                <?php endif; ?>
                <form method="post" class="card">
                    <input type="hidden" name="action" value="login">
                    <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                    <div class="field">
                        <label for="cid">CID на VATSIM</label>
                        <input id="cid" name="cid" inputmode="numeric" pattern="\d{6,8}" maxlength="8" autocomplete="off" required autofocus placeholder="1234567">
                    </div>
                    <button type="submit">Войти</button>
                    <p class="hint">Временный режим авторизации до подключения VATSIM OAuth 2.0.</p>
                </form>
            </main>
        </div>
        <?php else: ?>
            <div class="shell">
                <?= page_header(
                    '<a class="nav-link" href="../register/">Страница регистрации</a>'
                    . '<form method="post">'
                    . '<input type="hidden" name="action" value="logout">'
                    . '<input type="hidden" name="csrf" value="' . h($csrf) . '">'
                    . '<button type="submit" class="secondary">Выйти</button>'
                    . '</form>'
                ) ?>
                <main>
                    <div class="head">
                        <h1>АРМ инженера системы</h1>
                        <p class="sub">Инженер: <?= h($engineer['name']) ?> · CID <?= h($engineer['cid']) ?></p>
                    </div>
                    <?php if ($flash): ?>
                        <div class="flash <?= h($flash[0]) ?>" role="alert">
                            <?= h($flash[1]) ?>
                        </div>
                    <?php endif; ?>
                    <form method="post" class="card" id="edit">
                        <h2>Добавить пользователя</h2>
                        <input type="hidden" name="action" value="create">
                        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                        <div class="row">
                            <div class="field cid">
                                <label for="cid">CID на VATSIM</label>
                                <input id="cid" name="cid" inputmode="numeric" pattern="\d{6,8}" maxlength="8" required value="<?= h($form['cid']) ?>" placeholder="1234567">
                            </div>
                            <div class="field rating">
                                <label for="rating_id">Рейтинг</label>
                                <select id="rating_id" name="rating_id" required>
                                    <option value="">Выберите рейтинг</option>
                                    <?php foreach ($ratingOptions as $ratingId => $rating): ?>
                                        <option value="<?= h((string)$ratingId) ?>" <?= (string)$form['rating_id'] === (string)$ratingId ? 'selected' : '' ?>><?= h($rating) ?></option>
                                    <?php endforeach; ?>
                                </select>
                            </div>
                            <div class="field name">
                                <label for="surname">Фамилия</label>
                                <input id="surname" name="surname" maxlength="40" required value="<?= h($form['surname']) ?>" placeholder="Свахин">
                            </div>
                            <button type="submit">Добавить</button>
                        </div>
                        <div class="pair" style="margin-top:16px;">
                            <div class="field">
                                <label for="first_name">Имя</label>
                                <input id="first_name" name="first_name" maxlength="40" required value="<?= h($form['first_name']) ?>" placeholder="Алексей">
                            </div>
                            <div class="field">
                                <label for="patronymic">Отчество</label>
                                <input id="patronymic" name="patronymic" maxlength="40" value="<?= h($form['patronymic']) ?>" placeholder="Сергеевич">
                            </div>
                        </div>
                        <label class="engineer-check">
                            <input type="checkbox" name="is_engineer" value="1" <?= !empty($form['is_engineer']) ? 'checked' : '' ?>>
                            <span>Дать доступ к инженерной панели</span>
                        </label>
                    </form>
                    <div class="card">
                        <div class="top">
                            <h2>Пользователи <span class="count"><?= count($rows) ?></span></h2>
                            <div class="search">
                                <input id="filter" type="search" placeholder="Поиск по имени или CID" aria-label="Поиск">
                            </div>
                        </div>
                        <?php if (!$rows): ?>
                            <p class="empty">Пока никого нет.</p>
                        <?php else: ?>
                            <div class="table-wrap">
                                <table>
                                    <thead>
                                        <tr>
                                            <th>CID</th>
                                            <th>Рейтинг</th>
                                            <th>Фамилия</th>
                                            <th>Имя</th>
                                            <th>Отчество</th>
                                            <th>Инженер</th>
                                            <th>Изменено, UTC</th>
                                            <th></th>
                                        </tr>
                                    </thead>
                                    <tbody>
                                        <?php foreach ($rows as $row): ?>
                                            <tr class="user-row" data-search="<?= h($row['cid'] . ' ' . $row['surname'] . ' ' . $row['first_name'] . ' ' . $row['patronymic']) ?>">
                                                <td class="num">
                                                    <span class="view-value"><?= h($row['cid']) ?></span>
                                                    <input class="edit-field" type="text" value="<?= h($row['cid']) ?>" readonly form="edit-user-<?= h($row['cid']) ?>">
                                                </td>
                                                <td>
                                                    <span class="view-value">
                                                        <?= h(controller_rating_short((int)$row['rating_id']) ?? '—') ?>
                                                    </span>
                                                    <select class="edit-field" name="rating_id" form="edit-user-<?= h($row['cid']) ?>">
                                                        <?php foreach ($ratingOptions as $ratingId => $rating): ?>
                                                            <option value="<?= h((string)$ratingId) ?>" <?= (int)$row['rating_id'] === (int)$ratingId ? 'selected' : '' ?>>
                                                                <?= h($rating) ?>
                                                            </option>
                                                        <?php endforeach; ?>
                                                    </select>
                                                </td>
                                                <td>
                                                    <span class="view-value"><?= h($row['surname']) ?></span>
                                                    <input class="edit-field" type="text" name="surname" maxlength="40" value="<?= h($row['surname']) ?>" form="edit-user-<?= h($row['cid']) ?>">
                                                </td>
                                                <td>
                                                    <span class="view-value"><?= h($row['first_name']) ?></span>
                                                    <input class="edit-field" type="text" name="first_name" maxlength="40" value="<?= h($row['first_name']) ?>" form="edit-user-<?= h($row['cid']) ?>">
                                                </td>
                                                <td>
                                                    <span class="view-value">
                                                        <?= $row['patronymic'] !== ''
                                                            ? h($row['patronymic'])
                                                            : '—' ?>
                                                    </span>
                                                    <input class="edit-field" type="text" name="patronymic" maxlength="40" value="<?= h($row['patronymic']) ?>" form="edit-user-<?= h($row['cid']) ?>">
                                                </td>
                                                <td>
                                                    <span class="view-value">
                                                        <?php if ((int)$row['is_engineer'] === 1): ?>
                                                            <span class="chip on">да</span>
                                                        <?php else: ?>
                                                            <span class="chip off">нет</span>
                                                        <?php endif; ?>
                                                    </span>
                                                    <label class="edit-field engineer-cell">
                                                        <input type="checkbox" name="is_engineer" value="1" form="edit-user-<?= h($row['cid']) ?>" <?= (int)$row['is_engineer'] === 1 ? 'checked' : '' ?>>доступ</label>
                                                </td>
                                                <td class="when">
                                                    <?= h(substr((string)$row['updated_at'], 0, 16)) ?>
                                                </td>
                                                <td class="actions">
                                                    <div class="acts">
                                                        <button type="button" class="link js-edit">Изменить</button>
                                                        <form id="edit-user-<?= h($row['cid']) ?>" method="post" class="inline-edit-form">
                                                            <input type="hidden" name="action" value="update">
                                                            <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                            <input type="hidden" name="cid" value="<?= h($row['cid']) ?>">
                                                            <button type="submit" class="small js-save edit-field">Сохранить</button>
                                                            <button type="button" class="small secondary js-cancel edit-field">Отмена</button>
                                                        </form>
                                                        <form method="post" class="js-confirm" data-question="Удалить <?= h($row['name']) ?>?">
                                                            <input type="hidden" name="action" value="delete">
                                                            <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                            <input type="hidden" name="cid" value="<?= h($row['cid']) ?>">
                                                            <button type="submit" class="small danger">Удалить</button>
                                                        </form>
                                                    </div>
                                                </td>
                                            </tr>
                                        <?php endforeach; ?>
                                    </tbody>
                                </table>
                            </div>
                        <?php endif; ?>
                    </div>
                    <script>
                        document.querySelectorAll('.js-edit').forEach(function (button) {
                            button.addEventListener('click', function () {
                                document.querySelectorAll('.user-row.is-editing').forEach(function (row) {
                                    row.classList.remove('is-editing');
                                });

                                button.closest('.user-row').classList.add('is-editing');
                            });
                        });

                        document.querySelectorAll('.js-cancel').forEach(function (button) {
                            button.addEventListener('click', function () {
                                location.reload();
                            });
                        });

                        document.querySelectorAll('.js-confirm').forEach(function (form) {
                            form.addEventListener('submit', function (event) {
                                if (!confirm(form.dataset.question)) {
                                    event.preventDefault();
                                }
                            });
                        });

                        var filter = document.getElementById('filter');

                        if (filter) {
                            filter.addEventListener('input', function () {
                                var query = filter.value.trim().toLowerCase();

                                document.querySelectorAll('tbody tr').forEach(function (row) {
                                    if (row.dataset.search) {
                                        row.hidden =
                                            query !== ''
                                            && row.dataset.search.toLowerCase().indexOf(query) === -1;
                                    }
                                });
                            });
                        }
                    </script>
                </main>
            </div>
        <?php endif; ?>
        <?= page_footer() ?>
    </body>
</html>