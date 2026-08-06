#!/bin/bash
# fix-staging-precedence.sh
#
# THE BUG THIS FIXES, measured from nginx's own error log:
#
#   open("/opt/coup-server/web/staging/css/style.css") failed (2: No such
#   file or directory), request: "GET /staging/css/style.css"
#
# nginx was resolving against the LIVE root plus the full URI instead of the
# staging alias. The /staging/ block was not matching those requests at all.
#
# Cause: the config already contains
#
#   location ~* \.(?:js|css)$ { ... }
#
# a REGEX location. In nginx, regex locations are tested BEFORE prefix
# locations win, so every .js and .css under /staging/ was captured by that
# block, which has no root or alias of its own and therefore fell back to the
# server-level root - the live site. The staging index loaded, because .html
# does not match that regex; every asset it referenced 404'd.
#
# `^~` on the prefix tells nginx: if this prefix matches, do not test regex
# locations at all. That is the documented fix and the only correct one here -
# reordering blocks would not help, because prefix/regex precedence is not
# positional.
#
# Also moves any *.bak-* out of sites-enabled/. nginx.conf includes
# sites-enabled/* with no extension filter, so a backup left there is parsed
# as a second server block - which is where the "conflicting server name
# saturncoup.duckdns.org ... ignored" warnings came from.
set -euo pipefail

CONF=/etc/nginx/sites-enabled/saturncoup
STAMP=$(date +%Y%m%d-%H%M%S)
BAKDIR=/root/nginx-backups
mkdir -p "$BAKDIR"

cp -a "$CONF" "$BAKDIR/saturncoup.bak-$STAMP"
echo "backup: $BAKDIR/saturncoup.bak-$STAMP"

# Get backups out of the included glob.
shopt -s nullglob
for f in /etc/nginx/sites-enabled/*.bak-* /etc/nginx/sites-available/*.bak-*; do
    mv "$f" "$BAKDIR/"
    echo "moved out of include path: $f"
done
shopt -u nullglob

for target in /etc/nginx/sites-enabled/saturncoup /etc/nginx/sites-available/saturncoup; do
    [ -f "$target" ] || continue
    if grep -q 'location \^~ /staging/' "$target"; then
        echo "already ^~ in $target"
    else
        sed -i 's|location /staging/ {|location ^~ /staging/ {|' "$target"
        echo "patched $target"
    fi
done

if nginx -t; then
    systemctl reload nginx
    echo "OK: reloaded"
else
    echo "nginx -t FAILED - restoring, NOT reloading"
    cp -a "$BAKDIR/saturncoup.bak-$STAMP" "$CONF"
    exit 1
fi
