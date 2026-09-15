<?php
// Shared by the site's two HTML pages - the admin page and registration: the
// headers and error page, escaping, the one-shot message after a POST, the
// logo and the look. The API endpoints answer in JSON and use none of it.

declare(strict_types=1);

// An HTML page, not the API's JSON: a failure here is a page too. $logTag
// starts the page's lines in the site's error log.
function html_page_setup(string $logTag): void
{
    set_exception_handler(function (Throwable $e) use ($logTag): void {
        error_log($logTag . ': ' . $e->getMessage());
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
// the form again.
function back_to_page(): void
{
    header('Location: ' . $_SERVER['SCRIPT_NAME'], true, 303);
    exit;
}

// The two logos side by side in white - AZIMUT x ULLL FIR - drawn here rather
// than served as files: AZIMUT's ring over a half-disc with the word beside
// it, and the FIR's airliner in a dashed square beside "ULLL FIR" / "VATSIM".
function brand_logo(): string
{
    return '<div class="brand" aria-label="AZIMUT x ULLL FIR">'
        . '<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">'
        . '<path d="M3 16 A13 13 0 0 1 29 16" fill="none" stroke="#fff" stroke-width="5"/>'
        . '<path d="M0.8 19 H31.2 A15.5 15.5 0 0 1 0.8 19 Z" fill="#fff"/>'
        . '</svg>'
        . '<span class="word">AZIMUT</span>'
        . '<span class="cross" aria-hidden="true">&times;</span>'
        . '<svg viewBox="0 0 32 32" width="30" height="30" aria-hidden="true">'
        . '<rect x="1" y="1" width="30" height="30" rx="3" fill="none" stroke="#fff" stroke-width="0.8" stroke-dasharray="1.6 1.2"/>'
        . '<path d="M16 5 C17 5 17.5 6.2 17.5 7.4 V13 L26 18 V20 L17.5 17.4 V22.6 L20.5 24.8 V26.3 L16 25.2'
        . ' L11.5 26.3 V24.8 L14.5 22.6 V17.4 L6 20 V18 L14.5 13 V7.4 C14.5 6.2 15 5 16 5 Z" fill="#fff"/>'
        . '</svg>'
        . '<span class="fir"><span class="word">ULLL FIR</span><small>VATSIM</small></span>'
        . '</div>';
}

// The look both pages share: the panel's dark olive, a light frame, lime for
// what is in focus.
function page_css(): string
{
    return <<<'CSS'
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
    .hint { color: var(--dim); font-size: 13px; margin: 8px 0 0; }
    .flash { padding: 10px 14px; border-radius: 6px; margin-bottom: 16px; }
    .flash.ok { background: rgba(158,255,61,.12); border: 1px solid rgba(158,255,61,.4); }
    .flash.error { background: rgba(255,107,94,.12); border: 1px solid rgba(255,107,94,.45); }
    .brand { display: flex; align-items: center; flex-wrap: wrap; gap: 8px 10px; margin: 0 0 18px; color: #fff; }
    .brand .word { font: 800 20px/1 "Montserrat", "Segoe UI", Arial, sans-serif; letter-spacing: .06em; }
    .brand .cross { font: 300 22px/1 "Segoe UI", Arial, sans-serif; opacity: .75; margin: 0 2px; }
    .brand .fir { display: flex; flex-direction: column; align-items: center; gap: 3px; }
    .brand small { font: 500 9px/1 "Montserrat", "Segoe UI", Arial, sans-serif; letter-spacing: .4em; margin-right: -.4em; }
CSS;
}
