<?php

declare(strict_types=1);

require __DIR__ . '/../../lib/bootstrap.php';
require __DIR__ . '/../../lib/page.php';
require __DIR__ . '/../../lib/launcher.php';

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

// Why an uploaded file did not arrive, for the engineer.
function upload_error_text(int $error): string
{
    switch ($error) {
        case UPLOAD_ERR_NO_FILE:
            return 'Выберите файл.';
        case UPLOAD_ERR_INI_SIZE:
        case UPLOAD_ERR_FORM_SIZE:
            return 'Файл больше, чем принимает сервер (upload_max_filesize = '
                . ini_get('upload_max_filesize') . ').';
        case UPLOAD_ERR_PARTIAL:
            return 'Файл загрузился не полностью — попробуйте ещё раз.';
        default:
            return 'Сервер не смог принять файл (ошибка ' . $error . ') — подробности в журнале ошибок.';
    }
}

// The release row by the id a form sent, or null.
function launcher_release(int $id): ?array
{
    $st = db()->prepare(
        'SELECT id, component, version, file, is_current
         FROM launcher_releases
         WHERE id = ?'
    );
    $st->execute([$id]);
    $row = $st->fetch();
    return $row === false ? null : $row;
}

start_page_session('galaxy_admin');

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    // A body over post_max_size leaves $_POST empty, the token with it, and
    // would otherwise read as a page that has gone stale.
    if (!$_POST && (int)($_SERVER['CONTENT_LENGTH'] ?? 0) > 0) {
        flash('error', 'Файл больше, чем принимает сервер (post_max_size = ' . ini_get('post_max_size') . ').');
        back_to_page('#releases');
    }

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

    // ---- Launcher releases ------------------------------------------------
    // The forms below carry no CID, so they are handled before the check on it
    // further down.

    if ($action === 'release_upload') {
        $component = clean_launcher_component($_POST['component'] ?? '');
        $version = clean_launcher_version($_POST['version'] ?? '');
        $changelog = clean_text($_POST['changelog'] ?? '');
        $makeCurrent = isset($_POST['make_current']);
        $upload = $_FILES['file'] ?? null;

        if ($component === null) {
            flash('error', 'Выберите компонент.');
            back_to_page('#releases');
        }

        if ($version === null) {
            flash('error', 'Версия — латинские буквы, цифры, точка, дефис или подчёркивание, до 32 знаков: например 2026.10.17.');
            back_to_page('#releases');
        }

        if (strlen($changelog) > 60000) {
            flash('error', 'Ченджлог слишком длинный.');
            back_to_page('#releases');
        }

        if (!is_array($upload) || !is_int($upload['error'] ?? null)) {
            flash('error', upload_error_text(UPLOAD_ERR_NO_FILE));
            back_to_page('#releases');
        }

        if ($upload['error'] !== UPLOAD_ERR_OK || !is_uploaded_file((string)$upload['tmp_name'])) {
            flash('error', upload_error_text($upload['error'] === UPLOAD_ERR_OK ? UPLOAD_ERR_NO_FILE : $upload['error']));
            back_to_page('#releases');
        }

        $ext = strtolower(pathinfo((string)$upload['name'], PATHINFO_EXTENSION));

        if (!in_array($ext, LAUNCHER_EXTENSIONS, true)) {
            flash('error', 'Принимаются только файлы .' . implode(' и .', LAUNCHER_EXTENSIONS) . '.');
            back_to_page('#releases');
        }

        $tmp = (string)$upload['tmp_name'];
        $size = (int)filesize($tmp);

        if ($size <= 0) {
            flash('error', 'Файл пустой.');
            back_to_page('#releases');
        }

        $st = db()->prepare(
            'SELECT id FROM launcher_releases WHERE component = ? AND version = ?'
        );
        $st->execute([$component, $version]);

        if ($st->fetchColumn() !== false) {
            flash('error', LAUNCHER_COMPONENTS[$component] . ' ' . $version . ' уже выложен — удалите его, чтобы залить заново, или поднимите версию.');
            back_to_page('#releases');
        }

        $dir = launcher_files_dir();

        if (!is_dir($dir)) {
            @mkdir($dir, 0755, true);
        }

        if (!is_dir($dir) || !is_writable($dir)) {
            error_log('squawk admin: launcher files folder ' . $dir . ' is missing or not writable');
            flash('error', 'Папка для файлов лаунчера недоступна для записи — подробности в журнале ошибок.');
            back_to_page('#releases');
        }

        // The name is built here, never taken from the upload: the component
        // and the version are both checked against fixed patterns above.
        $file = $component . '-' . $version . '.' . $ext;
        $path = $dir . '/' . $file;
        $sha256 = hash_file('sha256', $tmp);

        if (!is_string($sha256) || !move_uploaded_file($tmp, $path)) {
            error_log('squawk admin: could not store launcher file ' . $path);
            flash('error', 'Не удалось сохранить файл — подробности в журнале ошибок.');
            back_to_page('#releases');
        }

        @chmod($path, 0644);

        $pdo = db();

        try {
            $pdo->beginTransaction();

            if ($makeCurrent) {
                $pdo->prepare(
                    'UPDATE launcher_releases SET is_current = 0 WHERE component = ?'
                )->execute([$component]);
            }

            $pdo->prepare(
                'INSERT INTO launcher_releases
                    (component, version, file, sha256, size, changelog, created_at, is_current)
                 VALUES (?, ?, ?, ?, ?, ?, NOW(), ?)'
            )->execute([
                $component,
                $version,
                $file,
                $sha256,
                $size,
                $changelog,
                $makeCurrent ? 1 : 0,
            ]);

            $pdo->commit();
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            // No row points at the file, so nothing would ever offer it.
            @unlink($path);
            throw $e;
        }

        error_log(
            'squawk admin: ' . $engineer['cid'] . ' uploaded ' . $file
            . ' (' . $size . ' bytes, sha256 ' . $sha256 . ')'
            . ($makeCurrent ? ' as current' : '')
        );

        flash(
            'ok',
            'Выложен ' . LAUNCHER_COMPONENTS[$component] . ' ' . $version
            . ' — ' . launcher_size_label($size) . ', sha256 ' . substr($sha256, 0, 12) . '…'
            . ($makeCurrent ? ' Теперь это текущий выпуск.' : ' Текущим не сделан.')
        );

        back_to_page('#releases');
    }

    if ($action === 'release_current') {
        $release = launcher_release((int)($_POST['id'] ?? 0));

        if ($release === null) {
            flash('error', 'Такого выпуска уже нет.');
            back_to_page('#releases');
        }

        // One statement: this release on, every other of its component off.
        db()->prepare(
            'UPDATE launcher_releases SET is_current = (id = ?) WHERE component = ?'
        )->execute([(int)$release['id'], $release['component']]);

        error_log(
            'squawk admin: ' . $engineer['cid'] . ' made ' . $release['file'] . ' current'
        );

        flash('ok', 'Текущий выпуск: ' . LAUNCHER_COMPONENTS[$release['component']] . ' ' . $release['version'] . '.');
        back_to_page('#releases');
    }

    if ($action === 'release_delete') {
        $release = launcher_release((int)($_POST['id'] ?? 0));

        if ($release === null) {
            flash('error', 'Такого выпуска уже нет.');
            back_to_page('#releases');
        }

        db()->prepare('DELETE FROM launcher_releases WHERE id = ?')
            ->execute([(int)$release['id']]);

        $path = launcher_files_dir() . '/' . basename((string)$release['file']);

        if (is_file($path)) {
            @unlink($path);
        }

        error_log(
            'squawk admin: ' . $engineer['cid'] . ' deleted ' . $release['file']
        );

        flash(
            'ok',
            'Удалён ' . LAUNCHER_COMPONENTS[$release['component']] . ' ' . $release['version'] . '.'
            . ((int)$release['is_current'] === 1
                ? ' Это был текущий выпуск — сделайте текущим другой, иначе лаунчер не увидит компонент.'
                : '')
        );

        back_to_page('#releases');
    }

    // ---- Launcher news ----------------------------------------------------

    if ($action === 'news_save') {
        $id = (int)($_POST['id'] ?? 0);
        $title = preg_replace('/\s+/u', ' ', clean_text($_POST['title'] ?? ''));
        $body = clean_text($_POST['body'] ?? '');
        $level = clean_news_level($_POST['level'] ?? '');
        $pinned = isset($_POST['pinned']) ? 1 : 0;
        $published = isset($_POST['published']) ? 1 : 0;
        $back = $id > 0 ? '?news=' . $id . '#news' : '#news';

        if ($title === null || $title === '' || mb_strlen($title, 'UTF-8') > 200) {
            flash('error', 'Заголовок — от 1 до 200 знаков.');
            back_to_page($back);
        }

        if ($body === '' || strlen($body) > 60000) {
            flash('error', 'Текст новости пустой или слишком длинный.');
            back_to_page($back);
        }

        if ($level === null) {
            flash('error', 'Выберите тип новости.');
            back_to_page($back);
        }

        if ($id > 0) {
            // Looked up first: an UPDATE that changes nothing counts no rows
            // either, and says nothing about whether the news is still there.
            $st = db()->prepare('SELECT COUNT(*) FROM launcher_news WHERE id = ?');
            $st->execute([$id]);

            if ((int)$st->fetchColumn() === 0) {
                flash('error', 'Такой новости уже нет.');
                back_to_page('#news');
            }

            db()->prepare(
                'UPDATE launcher_news
                 SET title = ?, body = ?, level = ?, pinned = ?, published = ?
                 WHERE id = ?'
            )->execute([$title, $body, $level, $pinned, $published, $id]);

            flash('ok', 'Новость сохранена.');
        } else {
            db()->prepare(
                'INSERT INTO launcher_news (title, body, level, pinned, published, created_at)
                 VALUES (?, ?, ?, ?, ?, NOW())'
            )->execute([$title, $body, $level, $pinned, $published]);

            flash('ok', $published ? 'Новость опубликована.' : 'Новость сохранена скрытой.');
        }

        error_log(
            'squawk admin: ' . $engineer['cid'] . ' saved launcher news '
            . ($id > 0 ? '#' . $id : '(new)')
        );

        back_to_page('#news');
    }

    if ($action === 'news_publish' || $action === 'news_pin') {
        $column = $action === 'news_publish' ? 'published' : 'pinned';

        $st = db()->prepare(
            'UPDATE launcher_news SET ' . $column . ' = 1 - ' . $column . ' WHERE id = ?'
        );
        $st->execute([(int)($_POST['id'] ?? 0)]);

        flash(
            $st->rowCount() > 0 ? 'ok' : 'error',
            $st->rowCount() > 0 ? 'Новость обновлена.' : 'Такой новости уже нет.'
        );

        back_to_page('#news');
    }

    if ($action === 'news_delete') {
        $id = (int)($_POST['id'] ?? 0);

        $st = db()->prepare('DELETE FROM launcher_news WHERE id = ?');
        $st->execute([$id]);

        flash(
            'ok',
            $st->rowCount() > 0 ? 'Новость удалена.' : 'Такой новости уже нет.'
        );

        error_log(
            'squawk admin: ' . $engineer['cid'] . ' deleted launcher news #' . $id
        );

        back_to_page('#news');
    }

    $cid = trim((string)($_POST['cid'] ?? ''));

    if (!preg_match('/^\d{6,8}$/', $cid)) {
        flash('error', 'CID — это номер на VATSIM, только 6–8 цифр.');
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
            'Изменения сохранены у пользователя: ' . $cid
            . ' - ' . $name
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

// The launcher's releases and news. A database the two tables have not been
// added to yet still opens the page - with a note in place of both sections.
$launcherReady = false;
$releases = [];
$news = [];
$newsEdit = null;

if ($engineer !== null) {
    try {
        $releases = db()->query(
            'SELECT id, component, version, file, sha256, size, changelog, created_at, is_current
             FROM launcher_releases
             ORDER BY created_at DESC, id DESC'
        )->fetchAll();

        $news = db()->query(
            'SELECT id, title, body, level, pinned, published, created_at, updated_at
             FROM launcher_news
             ORDER BY pinned DESC, created_at DESC, id DESC'
        )->fetchAll();

        $launcherReady = true;
    } catch (PDOException $e) {
        error_log('squawk admin: launcher tables unavailable - ' . $e->getMessage());
    }

    $editId = (int)($_GET['news'] ?? 0);

    foreach ($news as $item) {
        if ((int)$item['id'] === $editId) {
            $newsEdit = $item;
        }
    }
}
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

            .shell:has(.user-row.is-editing) {
                max-width: 1400px;
            }

            .shell:has(.user-row.is-editing) .table-wrap {
                overflow-x: visible;
            }

            .shell:has(.user-row.is-editing) table {
                width: 100%;
                min-width: 1180px;
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

            /* Launcher releases and news. */
            .card {
                scroll-margin-top: 24px;
            }

            .card h3 {
                font-size: 18px;
                font-weight: 500;
                margin: 28px 0 16px;
                color: var(--text-primary);
            }

            .grid {
                display: grid;
                grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
                gap: 0 16px;
            }

            textarea {
                display: block;
                width: 100%;
                min-height: 140px;
                padding: 14px 22px;
                font: inherit;
                background: var(--background-section-light);
                color: var(--text-primary);
                border: 1px solid transparent;
                border-radius: var(--radius-s);
                resize: vertical;
                transition: background-color .2s ease-in-out, border-color .2s ease-in-out;
            }

            textarea:hover {
                background: var(--background-surface-accent);
            }

            textarea:focus {
                outline: none;
                background: var(--background-page);
                border-color: var(--stroke-primary);
                box-shadow: 0 0 0 3px rgba(129,144,168,.18);
            }

            input[type="file"] {
                height: auto;
                padding: 13px 22px;
                cursor: pointer;
            }

            .check {
                display: inline-flex;
                align-items: center;
                gap: 8px;
                margin: 0 20px 16px 22px;
                color: var(--text-primary);
                font-weight: 400;
                font-size: 15px;
                cursor: pointer;
            }

            .check input {
                width: auto;
                height: auto;
            }

            .form-actions {
                display: flex;
                align-items: center;
                flex-wrap: wrap;
                gap: 12px;
                margin-top: 8px;
            }

            .mono {
                font-family: ui-monospace, Consolas, monospace;
                font-size: 13px;
            }

            details summary {
                cursor: pointer;
                color: var(--text-secondary);
                font-size: 14px;
            }

            details pre {
                max-width: 480px;
                margin: 8px 0 0;
                white-space: pre-wrap;
                font: 13px/19px ui-monospace, Consolas, monospace;
                color: var(--text-secondary);
            }

            .chip.warn {
                background: rgba(255,204,0,.22);
                color: var(--text-primary);
            }

            .chip.maint {
                background: rgba(179,93,64,.12);
                color: var(--text-error);
            }

            tr.is-hidden td {
                opacity: .55;
            }

            tr.is-hidden td.actions {
                opacity: 1;
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
                    '<a class="nav-link" href="#users">Пользователи</a>'
                    . '<a class="nav-link" href="#releases">Выпуски</a>'
                    . '<a class="nav-link" href="#news">Новости</a>'
                    . '<a class="nav-link" href="../register/">Страница регистрации</a>'
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
                    <div class="card" id="users">
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
                    <?php if (!$launcherReady): ?>
                        <div class="card" id="releases">
                            <h2>Лаунчер</h2>
                            <p class="empty" id="news">
                                Таблиц лаунчера в базе ещё нет — импортируйте <span class="mono">schema.sql</span>
                                ещё раз (README, «Лаунчер»): он добавит только недостающие таблицы.
                            </p>
                        </div>
                    <?php else: ?>
                        <div class="card" id="releases">
                            <div class="top">
                                <h2>Выпуски <span class="count"><?= count($releases) ?></span></h2>
                            </div>
                            <form method="post" enctype="multipart/form-data">
                                <input type="hidden" name="action" value="release_upload">
                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                <div class="grid">
                                    <div class="field">
                                        <label for="release-component">Компонент</label>
                                        <select id="release-component" name="component" required>
                                            <?php foreach (LAUNCHER_COMPONENTS as $component => $label): ?>
                                                <option value="<?= h($component) ?>"><?= h($label) ?></option>
                                            <?php endforeach; ?>
                                        </select>
                                    </div>
                                    <div class="field">
                                        <label for="release-version">Версия</label>
                                        <input id="release-version" name="version" required maxlength="32" pattern="[0-9A-Za-z][0-9A-Za-z._\-]{0,31}" placeholder="<?= h(gmdate('Y.m.d')) ?>" autocomplete="off">
                                    </div>
                                </div>
                                <div class="field">
                                    <label for="release-file">Файл (.<?= h(implode(', .', LAUNCHER_EXTENSIONS)) ?>)</label>
                                    <input id="release-file" type="file" name="file" required accept=".<?= h(implode(',.', LAUNCHER_EXTENSIONS)) ?>">
                                </div>
                                <div class="field">
                                    <label for="release-changelog">Ченджлог (Markdown)</label>
                                    <textarea id="release-changelog" name="changelog" placeholder="- Что изменилось"></textarea>
                                </div>
                                <label class="check"><input type="checkbox" name="make_current" value="1" checked>Сразу сделать текущим</label>
                                <div class="form-actions">
                                    <button type="submit">Выложить</button>
                                </div>
                                <p class="hint">
                                    sha256 и размер посчитает сервер. Файл сохранится как
                                    <span class="mono">&lt;компонент&gt;-&lt;версия&gt;.&lt;расширение&gt;</span>
                                    и будет доступен по адресу <span class="mono"><?= h(launcher_file_url('')) ?></span>…
                                    Сервер принимает файлы до <?= h((string)ini_get('upload_max_filesize')) ?>.
                                </p>
                            </form>
                            <?php if ($releases): ?>
                                <h3>Все выпуски</h3>
                                <div class="table-wrap">
                                    <table>
                                        <thead>
                                            <tr>
                                                <th>Компонент</th>
                                                <th>Версия</th>
                                                <th>Файл</th>
                                                <th>sha256</th>
                                                <th>Выложен, UTC</th>
                                                <th>Текущий</th>
                                                <th></th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <?php foreach ($releases as $release): ?>
                                                <?php $releaseName = LAUNCHER_COMPONENTS[$release['component']] . ' ' . $release['version']; ?>
                                                <tr>
                                                    <td><?= h(LAUNCHER_COMPONENTS[$release['component']]) ?></td>
                                                    <td>
                                                        <?= h($release['version']) ?>
                                                        <?php if ($release['changelog'] !== ''): ?>
                                                            <details>
                                                                <summary>Ченджлог</summary>
                                                                <pre><?= h($release['changelog']) ?></pre>
                                                            </details>
                                                        <?php endif; ?>
                                                    </td>
                                                    <td>
                                                        <a href="<?= h(launcher_file_url($release['file'])) ?>" rel="noopener"><?= h($release['file']) ?></a><br>
                                                        <span class="hint"><?= h(launcher_size_label((int)$release['size'])) ?></span>
                                                    </td>
                                                    <td class="mono" title="<?= h($release['sha256']) ?>"><?= h(substr($release['sha256'], 0, 12)) ?>…</td>
                                                    <td class="when"><?= h(substr((string)$release['created_at'], 0, 16)) ?></td>
                                                    <td>
                                                        <?php if ((int)$release['is_current'] === 1): ?>
                                                            <span class="chip on">текущий</span>
                                                        <?php else: ?>
                                                            <span class="chip off">нет</span>
                                                        <?php endif; ?>
                                                    </td>
                                                    <td class="actions">
                                                        <div class="acts">
                                                            <?php if ((int)$release['is_current'] !== 1): ?>
                                                                <form method="post">
                                                                    <input type="hidden" name="action" value="release_current">
                                                                    <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                                    <input type="hidden" name="id" value="<?= h((string)$release['id']) ?>">
                                                                    <button type="submit" class="small secondary">Сделать текущим</button>
                                                                </form>
                                                            <?php endif; ?>
                                                            <form method="post" class="js-confirm" data-question="Удалить <?= h($releaseName) ?> вместе с файлом?<?= (int)$release['is_current'] === 1 ? ' Это текущий выпуск — лаунчер перестанет его видеть.' : '' ?>">
                                                                <input type="hidden" name="action" value="release_delete">
                                                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                                <input type="hidden" name="id" value="<?= h((string)$release['id']) ?>">
                                                                <button type="submit" class="small danger">Удалить</button>
                                                            </form>
                                                        </div>
                                                    </td>
                                                </tr>
                                            <?php endforeach; ?>
                                        </tbody>
                                    </table>
                                </div>
                            <?php else: ?>
                                <p class="hint">Пока ничего не выложено — лаунчер получит пустой список компонентов.</p>
                            <?php endif; ?>
                        </div>
                        <div class="card" id="news">
                            <div class="top">
                                <h2>Новости <span class="count"><?= count($news) ?></span></h2>
                            </div>
                            <form method="post">
                                <input type="hidden" name="action" value="news_save">
                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                <input type="hidden" name="id" value="<?= h((string)($newsEdit['id'] ?? 0)) ?>">
                                <?php if ($newsEdit !== null): ?>
                                    <h3>Правка новости от <?= h(substr((string)$newsEdit['created_at'], 0, 16)) ?> UTC</h3>
                                <?php endif; ?>
                                <div class="grid">
                                    <div class="field">
                                        <label for="news-title">Заголовок</label>
                                        <input id="news-title" name="title" required maxlength="200" value="<?= h($newsEdit['title'] ?? '') ?>" autocomplete="off">
                                    </div>
                                    <div class="field">
                                        <label for="news-level">Тип</label>
                                        <select id="news-level" name="level">
                                            <?php foreach (LAUNCHER_NEWS_LEVELS as $level => $label): ?>
                                                <option value="<?= h($level) ?>" <?= ($newsEdit['level'] ?? 'info') === $level ? 'selected' : '' ?>><?= h($label) ?></option>
                                            <?php endforeach; ?>
                                        </select>
                                    </div>
                                </div>
                                <div class="field">
                                    <label for="news-body">Текст (Markdown)</label>
                                    <textarea id="news-body" name="body" required><?= h($newsEdit['body'] ?? '') ?></textarea>
                                </div>
                                <label class="check"><input type="checkbox" name="published" value="1" <?= $newsEdit === null || (int)$newsEdit['published'] === 1 ? 'checked' : '' ?>>Опубликована</label>
                                <label class="check"><input type="checkbox" name="pinned" value="1" <?= $newsEdit !== null && (int)$newsEdit['pinned'] === 1 ? 'checked' : '' ?>>Закреплена</label>
                                <div class="form-actions">
                                    <button type="submit"><?= $newsEdit === null ? 'Добавить' : 'Сохранить' ?></button>
                                    <?php if ($newsEdit !== null): ?>
                                        <a class="nav-link" href="?#news">Отмена</a>
                                    <?php endif; ?>
                                </div>
                            </form>
                            <?php if ($news): ?>
                                <h3>Все новости</h3>
                                <div class="table-wrap">
                                    <table>
                                        <thead>
                                            <tr>
                                                <th>Заголовок</th>
                                                <th>Тип</th>
                                                <th>Создана, UTC</th>
                                                <th>Видна</th>
                                                <th></th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <?php foreach ($news as $item): ?>
                                                <?php $isPublished = (int)$item['published'] === 1; ?>
                                                <?php $isPinned = (int)$item['pinned'] === 1; ?>
                                                <tr class="<?= $isPublished ? '' : 'is-hidden' ?>">
                                                    <td>
                                                        <?= $isPinned ? '<span class="chip on">закреплена</span> ' : '' ?><?= h($item['title']) ?>
                                                        <details>
                                                            <summary>Текст</summary>
                                                            <pre><?= h($item['body']) ?></pre>
                                                        </details>
                                                    </td>
                                                    <td>
                                                        <span class="chip <?= $item['level'] === 'info' ? 'on' : h($item['level']) ?>"><?= h(LAUNCHER_NEWS_LEVELS[$item['level']] ?? $item['level']) ?></span>
                                                    </td>
                                                    <td class="when"><?= h(substr((string)$item['created_at'], 0, 16)) ?></td>
                                                    <td>
                                                        <?php if ($isPublished): ?>
                                                            <span class="chip on">да</span>
                                                        <?php else: ?>
                                                            <span class="chip off">скрыта</span>
                                                        <?php endif; ?>
                                                    </td>
                                                    <td class="actions">
                                                        <div class="acts">
                                                            <a class="nav-link" href="?news=<?= h((string)$item['id']) ?>#news">Изменить</a>
                                                            <form method="post">
                                                                <input type="hidden" name="action" value="news_publish">
                                                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                                <input type="hidden" name="id" value="<?= h((string)$item['id']) ?>">
                                                                <button type="submit" class="small secondary"><?= $isPublished ? 'Скрыть' : 'Показать' ?></button>
                                                            </form>
                                                            <form method="post">
                                                                <input type="hidden" name="action" value="news_pin">
                                                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                                <input type="hidden" name="id" value="<?= h((string)$item['id']) ?>">
                                                                <button type="submit" class="small secondary"><?= $isPinned ? 'Открепить' : 'Закрепить' ?></button>
                                                            </form>
                                                            <form method="post" class="js-confirm" data-question="Удалить новость «<?= h($item['title']) ?>»?">
                                                                <input type="hidden" name="action" value="news_delete">
                                                                <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
                                                                <input type="hidden" name="id" value="<?= h((string)$item['id']) ?>">
                                                                <button type="submit" class="small danger">Удалить</button>
                                                            </form>
                                                        </div>
                                                    </td>
                                                </tr>
                                            <?php endforeach; ?>
                                        </tbody>
                                    </table>
                                </div>
                            <?php else: ?>
                                <p class="hint">Новостей пока нет.</p>
                            <?php endif; ?>
                        </div>
                    <?php endif; ?>
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