<?php
// Shared by the site's two HTML pages - the admin page and registration: the
// headers and error page, escaping, the one-shot message after a POST, the
// header with the logos, and the look.
//
// The look follows the FIR's own site, vatsim-petersburg.com, so the two feel
// like one: Ubuntu Sans, a light blue-grey page, a white pill navbar and
// rounded white cards over it, one navy accent (#3D5376), everything rounded.
// The names of the colours below are that site's own, copied so that a change
// there is easy to follow here.
//
// The API endpoints answer in JSON and use none of this.

declare(strict_types=1);

// An HTML page, not the API's JSON: a failure here is a page too. $logTag
// starts the page's lines in the site's error log.
function html_page_setup(string $logTag): void
{
    set_exception_handler(function (Throwable $e) use ($logTag): void {
        error_log(sprintf(
            '%s: %s in %s:%d',
            $logTag,
            $e->getMessage(),
            $e->getFile(),
            $e->getLine()
        ));
        if (!headers_sent()) {
            http_response_code(500);
            header('Content-Type: text/html; charset=utf-8');
        }
        echo '<!doctype html><meta charset="utf-8"><p>Ошибка сервера. Подробности - в журнале ошибок сайта.</p>';
        exit;
    });

    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Frame-Options: DENY');
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: no-referrer');
}

function h($value): string
{
    return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function client_ip(): string
{
    return (string)($_SERVER['REMOTE_ADDR'] ?? '');
}

// A rate-limit bucket for the caller's address, under a prefix of the page's own.
function client_bucket(string $prefix): string
{
    return $prefix . substr(hash('sha256', client_ip()), 0, 32);
}

// A session of the page's own, with a CSRF token in it. Lax rather than
// Strict, so the page opened from a link in a messenger still finds the
// session; the forms carry the token either way.
function start_page_session(string $name): void
{
    session_name($name);
    session_set_cookie_params(['lifetime' => 0, 'path' => '/', 'httponly' => true, 'samesite' => 'Lax']);
    session_start();
    if (!isset($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
    }
}

function csrf_ok(): bool
{
    return hash_equals((string)$_SESSION['csrf'], (string)($_POST['csrf'] ?? ''));
}

function flash(string $kind, string $text): void
{
    $_SESSION['flash'] = [$kind, $text];
}

// The message a POST left, once.
function take_flash(): ?array
{
    $flash = $_SESSION['flash'] ?? null;
    unset($_SESSION['flash']);
    return is_array($flash) ? $flash : null;
}

// After every POST, back to a plain GET of the page, so a reload never sends
// the form again. $query keeps an optional query string for the redirect.
function back_to_page(string $query = ''): void
{
    header('Location: ' . $_SERVER['SCRIPT_NAME'] . $query, true, 303);
    exit;
}

// Ubuntu Sans, the face the FIR's site is set in. If the fonts do not load,
// the page falls back to the system sans and reads the same.
function page_fonts(): string
{
    return '<link rel="preconnect" href="https://fonts.googleapis.com">'
        . '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
        . '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?'
        . 'family=Ubuntu+Sans:ital,wght@0,300..700;1,300..700&amp;display=swap">';
}

// The two logos side by side - AZIMUT x ULLL FIR - drawn here rather than
// served as files: AZIMUT's ring over a half-disc with the word beside it, and
// the FIR's airliner in a dashed square beside "ULLL FIR" / "VATSIM". Both are
// drawn in currentColor, so the header sets the one colour.
function brand_logo(): string
{
    return '<div class="brand" aria-label="AZIMUT x ULLL FIR">'
        // . '<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">'
        // . '<path d="M3 16 A13 13 0 0 1 29 16" fill="none" stroke="currentColor" stroke-width="5"/>'
        // . '<path d="M0.8 19 H31.2 A15.5 15.5 0 0 1 0.8 19 Z" fill="currentColor"/>'
        // . '</svg>'
        // . '<span class="word">AZIMUT</span>'
        // . '<span class="cross" aria-hidden="true">&times;</span>'
        // . '<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">'
        // . '<rect x="1" y="1" width="30" height="30" rx="3" fill="none" stroke="currentColor" stroke-width="0.8" stroke-dasharray="1.6 1.2"/>'
        // . '<path d="M16 5 C17 5 17.5 6.2 17.5 7.4 V13 L26 18 V20 L17.5 17.4 V22.6 L20.5 24.8 V26.3 L16 25.2'
        // . ' L11.5 26.3 V24.8 L14.5 22.6 V17.4 L6 20 V18 L14.5 13 V7.4 C14.5 6.2 15 5 16 5 Z" fill="currentColor"/>'
        // . '</svg>'
        // . '<span class="fir"><span class="word">ULLL FIR</span><small>VATSIM</small></span>'
        . '<img src="https://cdn.vatsim-petersburg.com/e0313a71-751a-11f0-9908-0242ac130004" alt="logo" style="width: 30px">';
        . '</div>';
}

// The floating white pill both pages open with: the logos on the left, and
// whatever the page puts on the right - a link across to the other page, or
// the admin's "Выйти".
function page_header(string $right = ''): string
{
    return '<header class="nav">' . brand_logo()
        . '<div class="nav-right">' . $right . '</div></header>';
}

// The line under the page, the way the FIR's site closes its own.
function page_footer(): string
{
    return '<footer class="foot">'
        . '<span>КСА УВД «Галактика» · ULLL FIR</span>'
        . '<a href="https://vatsim-petersburg.com/" rel="noopener">vatsim-petersburg.com</a>'
        . '</footer>';
}

// The look both pages share. The custom properties, their names and their
// values are the FIR's site's own (its ThemeProvider), so what is written here
// is only how this site's own pieces - fields, cards, tables - are built out
// of them.
function page_css(): string
{
    return <<<'CSS'
    :root {
        --text-main: #253247;
        --text-main-alt: #FAFAFA;
        --text-primary: #18212F;
        --text-secondary: #5F728F;
        --text-tertiary: #8190A8;
        --text-accent: #3D5376;
        --text-error: #B35D40;
        --background-page: #FAFAFA;
        --background-section-light: #F0F3F7;
        --background-section-dark: #E6EBF2;
        --background-table-head: #F0F2F5;
        --background-primary: #3D5376;
        --background-primary-hover: #31425E;
        --background-surface-accent: #E6EBF2;
        --background-surface-accent-hover: #D7DDE6;
        --stroke-primary: #8190A8;
        --stroke-secondary: #A4AFC1;
        --stroke-error: #B35D40;
        --icon-success: #00D400;
        --icon-warning: #FFCC00;
        --radius-pill: 1000px;
        --radius-xs: 8px;
        --radius-s: 16px;
        --radius-m: 32px;
        --shadow-m: 0 2px 4px rgba(129,144,168,.10), 0 1px 2px rgba(129,144,168,.06);
        --shadow-l: 0 6px 1px rgba(215,221,230,.35), 0 24px 32px rgba(129,144,168,.14);
    }

    * {
        box-sizing: border-box;
    }

    html {
        scroll-behavior: smooth;
    }

    @media (prefers-reduced-motion: reduce) {
        html {
            scroll-behavior: auto;
        }
    }

    body {
        margin: 0;
        padding: 24px 16px 40px;
        background: var(--background-section-light);
        color: var(--text-main);
        font: 400 16px/22px "Ubuntu Sans", "Segoe UI", Roboto, Arial, sans-serif;
        -webkit-font-smoothing: antialiased;
    }

    a {
        color: var(--text-accent);
        text-decoration: none;
    }

    a:hover {
        text-decoration: underline;
    }

    .shell {
        max-width: 900px;
        margin: 0 auto;
    }

    h1 {
        font-size: 36px;
        font-weight: 500;
        line-height: 1.15;
        margin: 0 0 8px;
        color: var(--text-primary);
        text-wrap: balance;
    }

    h2 {
        font-size: 24px;
        font-weight: 500;
        line-height: 1.2;
        margin: 0 0 20px;
        color: var(--text-primary);
        text-wrap: balance;
    }

    .sub {
        font-size: 18px;
        line-height: 24px;
        color: var(--text-secondary);
        margin: 0 0 28px;
    }

    /* The pill at the top, and the line at the bottom. */
    .nav {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px 20px;
        flex-wrap: wrap;
        min-height: 72px;
        padding: 12px 16px 12px 28px;
        margin: 0 0 32px;
        background: var(--background-page);
        border-radius: var(--radius-pill);
        box-shadow: var(--shadow-l);
    }

    .nav-right {
        display: flex;
        align-items: center;
        gap: 4px;
        flex-wrap: wrap;
    }

    .brand {
        display: flex;
        align-items: center;
        gap: 10px;
        color: var(--text-accent);
    }

    .brand .word {
        font-weight: 700;
        font-size: 18px;
        letter-spacing: .06em;
    }

    .brand .cross {
        font-weight: 300;
        font-size: 18px;
        color: var(--text-tertiary);
    }

    .brand .fir {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 2px;
    }

    .brand small {
        font-weight: 500;
        font-size: 8px;
        letter-spacing: .4em;
        margin-right: -.4em;
        color: var(--text-secondary);
    }

    .foot {
        max-width: 900px;
        margin: 32px auto 0;
        display: flex;
        flex-wrap: wrap;
        justify-content: space-between;
        gap: 4px 24px;
        color: var(--text-tertiary);
        font-size: 14px;
    }

    .foot a {
        color: var(--text-secondary);
    }

    /* Cards, fields, buttons. */
    .card {
        background: var(--background-page);
        border-radius: var(--radius-m);
        padding: 32px;
        margin-bottom: 24px;
        box-shadow: var(--shadow-m);
    }

    .field {
        margin-bottom: 16px;
    }

    label {
        display: block;
        font-size: 14px;
        font-weight: 500;
        color: var(--text-secondary);
        margin: 0 0 8px 22px;
    }

    input {
        width: 100%;
        height: 52px;
        padding: 0 22px;
        font: inherit;
        background: var(--background-section-light);
        color: var(--text-primary);
        border: 1px solid transparent;
        border-radius: var(--radius-pill);
        transition: background-color .2s ease-in-out, border-color .2s ease-in-out;
    }

    input::placeholder {
        color: var(--text-tertiary);
    }

    input:hover {
        background: var(--background-surface-accent);
    }

    input:focus {
        outline: none;
        background: var(--background-page);
        border-color: var(--stroke-primary);
        box-shadow: 0 0 0 3px rgba(129,144,168,.18);
    }

    select {
        width: 100%;
        height: 52px;
        padding: 0 22px;
        font: inherit;
        background: var(--background-section-light);
        color: var(--text-primary);
        border: 1px solid transparent;
        border-radius: var(--radius-pill);
        cursor: pointer;
        transition: background-color .2s ease-in-out, border-color .2s ease-in-out;
    }

    select:hover {
        background: var(--background-surface-accent);
    }

    select:focus {
        outline: none; background: var(--background-page);
        border-color: var(--stroke-primary);
        box-shadow: 0 0 0 3px rgba(129,144,168,.18);
    }

    button {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        gap: 8px;
        height: 48px;
        padding: 0 28px;
        border: none;
        border-radius: var(--radius-pill);
        background: var(--background-primary);
        color: var(--text-main-alt);
        font-family: inherit;
        font-size: 16px;
        font-weight: 500;
        line-height: 1;
        cursor: pointer;
        white-space: nowrap;
        transition: background-color .3s ease-in-out;
    }

    button:hover {
        background: var(--background-primary-hover);
    }

    button.secondary {
        background: var(--background-surface-accent);
        color: var(--text-primary);
    }

    button.secondary:hover {
        background: var(--background-surface-accent-hover);
    }

    button.danger {
        background: transparent;
        color: var(--text-error);
        box-shadow: inset 0 0 0 1px rgba(179,93,64,.5);
    }

    button.danger:hover {
        background: rgba(179,93,64,.10);
    }

    button.small {
        height: 40px;
        padding: 0 20px;
        font-size: 14px;
    }

    button.link {
        height: auto;
        padding: 0;
        background: none;
        color: var(--text-secondary);
        font-size: 15px;
        text-decoration: underline;
    }

    button.link:hover {
        background: none;
        color: var(--text-accent);
    }

    .btn-pill {
        display: inline-flex;
        align-items: center;
        height: 44px;
        padding: 0 24px;
        border-radius: var(--radius-pill);
        background: var(--background-primary);
        color: var(--text-main-alt);
        font-weight: 500;
        transition: background-color .3s ease-in-out;
    }

    .btn-pill:hover {
        background: var(--background-primary-hover);
        text-decoration: none; 
    }

    .nav-link {
        display: inline-flex;
        align-items: center;
        height: 44px;
        padding: 0 18px;
        border-radius: var(--radius-pill);
        color: var(--text-secondary);
        font-weight: 500;
        transition: background-color .2s ease-in-out;
    }

    .nav-link:hover {
        background: var(--background-section-light);
        color: var(--text-accent);
        text-decoration: none;
    }

    .hint {
        color: var(--text-secondary);
        font-size: 14px;
        line-height: 20px;
        margin: 16px 0 0;
    }

    /* The message a POST leaves behind. */
    .flash {
        border-radius: var(--radius-s);
        padding: 14px 22px;
        margin-bottom: 24px;
        font-size: 16px;
        line-height: 22px;
    }

    .flash.ok {
        background: rgba(0,212,0,.10);
        color: var(--text-primary);
        box-shadow: inset 0 0 0 1px rgba(0,216,26,.35);
    }

    .flash.error {
        background: rgba(179,93,64,.10);
        color: var(--text-error);
        box-shadow: inset 0 0 0 1px rgba(179,93,64,.35);
    }

    /* Lists. */
    .table-wrap {
        overflow-x: auto;
        margin: 0 -8px;
        padding: 0 8px;
    }

    table {
        width: 100%;
        border-collapse: separate;
        border-spacing: 0;
        font-size: 15px;
    }

    th {
        text-align: left;
        font-size: 13px;
        font-weight: 500;
        color: var(--text-secondary);
        padding: 10px 16px;
        background: var(--background-table-head);
        white-space: nowrap;
    }

    th:first-child {
        border-radius: var(--radius-xs) 0 0 var(--radius-xs);
    }

    th:last-child {
        border-radius: 0 var(--radius-xs) var(--radius-xs) 0;
    }

    td {
        padding: 14px 16px;
        border-bottom: 1px solid var(--background-section-dark);
        vertical-align: middle;
    }

    tbody tr:last-child td {
        border-bottom: 0;
    }

    td.num {
        font-variant-numeric: tabular-nums;
    }

    td.when {
        color: var(--text-tertiary);
        font-size: 14px;
        white-space: nowrap;
    }

    td.actions {
        white-space: nowrap;
    }

    td.actions .acts {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 12px;
    }

    .chip {
        display: inline-flex;
        align-items: center;
        height: 28px;
        padding: 0 12px;
        border-radius: var(--radius-xs);
        font-size: 13px;
        font-weight: 500;
    }

    .chip.on {
        background: var(--background-surface-accent);
        color: var(--text-primary);
    }
    .chip.off {
        padding: 0;
        color: var(--text-tertiary);
    }
    .empty {
        color: var(--text-tertiary);
        margin: 0; 
    }

    @media (max-width: 600px) {
        .nav {
            border-radius: var(--radius-m);
            padding: 16px 20px;
            justify-content: center;
        }
        .nav-right {
            width: 100%;
            justify-content: center;
        }
        .card {
            padding: 24px 20px;
            border-radius: var(--radius-s);
        }
        h1 {
            font-size: 28px; 
        }
    }
CSS;
}
