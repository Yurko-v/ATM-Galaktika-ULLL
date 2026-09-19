#!/bin/sh
# Deploys the server application to the domain's web root.
#
# Repository layout:
#   server/register/      -> public_html/register/
#   server/admin/         -> public_html/admin/
#   server/lib/api/       -> public_html/api/
#   server/lib/           -> private lib/ (except lib/api/)
#
# config.php is never touched here. It contains secrets and is created
# manually outside the git checkout.

set -eu

if [ $# -ne 2 ]; then
    echo "usage: deploy.sh <repo-server-dir> <web-root>" >&2
    exit 1
fi

SERVER_DIR=$1
WEB_ROOT=$2

if [ ! -f "$SERVER_DIR/lib/api/health.php" ]; then
    echo "deploy.sh: $SERVER_DIR does not look like a server/ checkout" >&2
    exit 1
fi

if [ ! -d "$SERVER_DIR/register" ]; then
    echo "deploy.sh: $SERVER_DIR/register does not exist" >&2
    exit 1
fi

if [ ! -d "$SERVER_DIR/admin" ]; then
    echo "deploy.sh: $SERVER_DIR/admin does not exist" >&2
    exit 1
fi

if [ ! -d "$WEB_ROOT" ]; then
    echo "deploy.sh: web root $WEB_ROOT does not exist" >&2
    exit 1
fi

mkdir -p \
    "$WEB_ROOT/public_html" \
    "$WEB_ROOT/lib"

# Public web pages.
rsync -a --delete \
    "$SERVER_DIR/register/" \
    "$WEB_ROOT/public_html/register/"

rsync -a --delete \
    "$SERVER_DIR/admin/" \
    "$WEB_ROOT/public_html/admin/"

# Public API.
rsync -a --delete \
    "$SERVER_DIR/lib/api/" \
    "$WEB_ROOT/public_html/api/"

# Private library.
#
# lib/api is stored inside the repository's lib/ directory for organization,
# but the deployed API belongs in public_html/api and must not be exposed
# through the private library directory.
rm -rf "$WEB_ROOT/lib/api"

rsync -a --delete \
    --exclude='api/' \
    "$SERVER_DIR/lib/" \
    "$WEB_ROOT/lib/"

# The error log, and the .user.ini that sends PHP's own errors to it.
#
# lib/errors.php sets the same file from inside the application, but a request
# that dies before it is loaded - a require that finds nothing, a file with a
# syntax error in it - never reaches that code, and on this hosting leaves
# nothing behind but a blank 500. PHP reads .user.ini before running anything,
# so those are logged too.
#
# The path has to be absolute and is therefore written here, at deploy time,
# rather than kept in git: it holds the account's home directory.
LOG_DIR=$(cd "$WEB_ROOT" && pwd)/logs
LOG_FILE=$LOG_DIR/php-error.log

mkdir -p "$LOG_DIR"

cat > "$WEB_ROOT/public_html/.user.ini" <<EOF
; Written by server/deploy.sh - edit that, not this.
log_errors = On
error_log = $LOG_FILE
error_reporting = E_ALL
; Never shown to a visitor: read them in the log above.
display_errors = Off
EOF

echo "deploy.sh: synced register/, admin/ and lib/api/ to public_html/"
echo "deploy.sh: synced private library to $WEB_ROOT/lib/"
echo "deploy.sh: PHP errors go to $LOG_FILE"
