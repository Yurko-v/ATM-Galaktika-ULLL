<?php
// Where PHP's own complaints go: the site's error log.
//
// Beget's shared hosting shows nothing in the browser - a failed request is a
// blank "HTTP ERROR 500" and no more - so unless the errors are written down
// somewhere, every mistake looks the same from outside. They are written to
// logs/php-error.log, next to lib/ and outside public_html, where the web
// server cannot serve it to anyone.
//
// This covers everything from the moment bootstrap.php starts. What comes
// before it - a require that finds nothing, a syntax error in the file the web
// server was asked for - is over before any of this code runs, and is caught
// instead by the .user.ini deploy.sh writes into public_html. Both point at
// the same file.

declare(strict_types=1);

// lib/ sits beside logs/ in the repository and in the deployed site alike, so
// the same path works in both places.
function error_log_path(): string
{
    return __DIR__ . '/../logs/php-error.log';
}

function setup_error_log(): void
{
    $path = error_log_path();
    $dir  = dirname($path);

    if (!is_dir($dir)) {
        @mkdir($dir, 0750, true);
    }

    error_reporting(E_ALL);
    ini_set('log_errors', '1');
    ini_set('error_log', $path);

    // Never to the browser: a warning printed into a JSON reply breaks it, and
    // a path or a query printed into an HTML page is not for a visitor to see.
    // On the command line the cron job's own log is the place for them, so
    // there they stay on stderr as well.
    ini_set('display_errors', PHP_SAPI === 'cli' ? '1' : '0');

    // PHP logs a fatal error with its file and line, but not what was being
    // asked for - and the same line in codes.php fails for a reason worth
    // knowing when it was api/assign.php that led there. One line more, only
    // for the errors that end the request.
    register_shutdown_function(function (): void {
        $last = error_get_last();
        if ($last === null || !in_array($last['type'], [E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR], true)) {
            return;
        }
        if (PHP_SAPI === 'cli') {
            return;
        }
        error_log(sprintf(
            'squawk: the request that failed was %s %s',
            $_SERVER['REQUEST_METHOD'] ?? '?',
            $_SERVER['REQUEST_URI'] ?? '?'
        ));
    });
}

setup_error_log();
