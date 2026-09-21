#!/bin/sh

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
