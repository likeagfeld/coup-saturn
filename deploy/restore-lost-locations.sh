#!/bin/bash
# restore-lost-locations.sh
#
# WHAT WENT WRONG
#   cutover-web.sh installed the REPO's nginx config wholesale. The repo copy
#   has never contained /admin/, /bomberman/ or /megasd/ - a fact established
#   and written down before the cutover, then not acted on. All three fell
#   through to the catch-all and began serving the game client: measured
#   byte-identical to / at 1591 bytes.
#
#   The post-cutover check asked for STATUS CODES and got 200 for each, which
#   read as healthy. 200 was exactly what a fall-through produces. Comparing
#   CONTENT would have caught it immediately.
#
# WHAT THIS DOES
#   Starts from the PRE-CUTOVER backup, which still has every original
#   location, and applies only the two-line root swap to it. Nothing is
#   copied wholesale from the repo.
#
#     root      -> /opt/coup-server/web-staging   (redesigned client)
#     /staging/ -> /opt/coup-server/web           (previous client)
#
# SAFETY
#   Backs up the current config first, verifies the three locations are
#   present in the candidate BEFORE installing, lets `nginx -t` decide, and
#   restores without reloading if the test fails.
set -euo pipefail

BAKDIR=/root/nginx-backups
SRC="$BAKDIR/saturncoup.bak-cutover-20260806-220531"
CONF=/etc/nginx/sites-enabled/saturncoup
AVAIL=/etc/nginx/sites-available/saturncoup
TS=$(date +%Y%m%d-%H%M%S)
CAND=/tmp/saturncoup.candidate

[ -f "$SRC" ] || { echo "REFUSING: pre-cutover backup $SRC not found"; exit 1; }

cp -a "$CONF" "$BAKDIR/saturncoup.bak-prerestore-$TS"
echo "backup of current: $BAKDIR/saturncoup.bak-prerestore-$TS"

# Apply ONLY the root swap to the known-good config.
sed -e 's#^\(\s*\)root /opt/coup-server/web;#\1root /opt/coup-server/web-staging;#' \
    "$SRC" > "$CAND"

# The old config had no /staging/ block at all, so add one pointing at the
# PREVIOUS client, inserted before the catch-all so the prefix is declared
# among the other prefix locations. ^~ is required: this config contains a
# `location ~* \.(js|css)$` REGEX block, and nginx tests regex locations
# before a plain prefix wins - without ^~ every asset under /staging/ is
# captured by it and served from the wrong root.
if ! grep -q 'location \^~ /staging/' "$CAND"; then
    python3 - "$CAND" <<'PY'
import sys
p = sys.argv[1]
lines = open(p).readlines()
block = """    location ^~ /staging/ {
        alias /opt/coup-server/web/;
        try_files $uri $uri/ /staging/index.html;
        expires -1;
        add_header Cache-Control "no-store, no-cache, must-revalidate";
    }

    location = /staging {
        return 301 /staging/;
    }

"""
for i, ln in enumerate(lines):
    if ln.strip() == 'location / {':
        lines.insert(i, block)
        break
else:
    sys.exit("could not find the catch-all anchor")
open(p, 'w').writelines(lines)
print("  /staging/ block inserted")
PY
fi

# Prove the candidate still has everything BEFORE installing it.
missing=""
for loc in "/admin/" "/bomberman/" "/megasd/" "/ws" "/staging/"; do
    grep -q "location.*${loc}" "$CAND" || missing="$missing $loc"
done
if [ -n "$missing" ]; then
    echo "REFUSING: candidate is missing:$missing"
    exit 1
fi
echo "candidate has: /admin/ /bomberman/ /megasd/ /ws /staging/"

cp "$CAND" "$CONF"
[ -f "$AVAIL" ] && cp "$CAND" "$AVAIL"

if nginx -t; then
    systemctl reload nginx
    echo "OK: reloaded"
else
    echo "nginx -t FAILED - restoring, NOT reloading"
    cp -a "$BAKDIR/saturncoup.bak-prerestore-$TS" "$CONF"
    [ -f "$AVAIL" ] && cp -a "$BAKDIR/saturncoup.bak-prerestore-$TS" "$AVAIL"
    exit 1
fi
