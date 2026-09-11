#!/bin/sh
# Copies the files the web server actually serves into the site's own
# document root. On at least one real Beget account, a "прилинкованный
# домен"'s public_html returned 500 for every request - even a bare
# phpinfo() - the moment it was a symlink out to the git checkout; PHP/Apache
# there refuses to follow it. So public/ and lib/ are copied for real
# instead, laid out next to a config.php created once by hand - see
# server/README.md for the one-time setup this expects to find in place.
#
# Usage: deploy.sh <repo-server-dir> <web-root>
#   <repo-server-dir>  the checked-out server/ folder, e.g. ~/squawk/repo/server
#   <web-root>         the site's own folder, e.g. ~/squawk.example.beget.tech
#                       (Beget's container for the domain - holds public_html)
#
# config.php is never touched here: it holds secrets, is not in git, and is
# created once directly in <web-root>/config.php (a second copy from the one
# in <repo-server-dir>/config.php, which cron/vatsim_sync.php reads straight
# out of the checkout) - update both by hand if the database or key ever change.

set -eu

if [ $# -ne 2 ]; then
    echo "usage: deploy.sh <repo-server-dir> <web-root>" >&2
    exit 1
fi

SERVER_DIR=$1
WEB_ROOT=$2

if [ ! -f "$SERVER_DIR/public/api/health.php" ]; then
    echo "deploy.sh: $SERVER_DIR does not look like a server/ checkout" >&2
    exit 1
fi
if [ ! -d "$WEB_ROOT" ]; then
    echo "deploy.sh: web root $WEB_ROOT does not exist" >&2
    exit 1
fi

mkdir -p "$WEB_ROOT/public_html" "$WEB_ROOT/lib"
rsync -a --delete "$SERVER_DIR/public/" "$WEB_ROOT/public_html/"
rsync -a --delete "$SERVER_DIR/lib/" "$WEB_ROOT/lib/"

echo "deploy.sh: synced public/ and lib/ into $WEB_ROOT"
