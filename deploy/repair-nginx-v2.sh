#!/bin/bash
# repair-nginx-v2.sh - Restore from the TRUE pre-cutover config.
#
# The first repair scored configs only on /admin/, /bomberman/, /megasd/ and
# /ws, and picked saturncoup.bak-20260806-162402. That file has all four - and
# ZERO mentions of "editor", because it was snapshotted during earlier
# /staging/ work, before the editor routes existed. Restoring it brought the
# admin portal back and left /admin/editor/ dead.
#
# The scoring was the flaw: it measured the locations I already knew about, so
# it could never notice one I did not. saturncoup.bak-cutover-20260806-220531
# is the config as it stood immediately before the cutover - 7 editor mentions
# - and is the only correct source.
set -uo pipefail

BAKDIR=/root/nginx-backups
SRC="$BAKDIR/saturncoup.bak-cutover-20260806-220531"
CONF=/etc/nginx/sites-enabled/saturncoup
AVAIL=/etc/nginx/sites-available/saturncoup
TS=$(date +%Y%m%d-%H%M%S)
CAND=/tmp/saturncoup.candidate2

[ -f "$SRC" ] || { echo "REFUSING: $SRC missing"; exit 1; }

echo "=== source: $(basename "$SRC") ==="
grep -c 'location' "$SRC" | sed 's/^/  location blocks: /'
grep -o 'location[^{]*' "$SRC" | sed 's/^/  /'

cp -a "$CONF" "$BAKDIR/saturncoup.bak-prerepair2-$TS"
echo "  backup of current: saturncoup.bak-prerepair2-$TS"

# ONLY the root swap.
sed -e 's#root /opt/coup-server/web;#root /opt/coup-server/web-staging;#' \
    "$SRC" > "$CAND"

# /staging/ -> the previous client. ^~ is required: this config contains a
# `location ~* \.(js|css)$` REGEX block, and nginx tests regex before a plain
# prefix wins, so without it every asset under /staging/ is served from the
# wrong root.
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
        lines.insert(i, block); break
else:
    sys.exit("could not find the catch-all anchor")
open(p, 'w').writelines(lines)
print("  /staging/ block inserted")
PY
fi

# Compare against the SOURCE rather than a hardcoded list, so a route I have
# never heard of still has to survive. That is what the last check got wrong.
echo "=== every location in the source must survive ==="
missing=0
while read -r loc; do
    [ -z "$loc" ] && continue
    if grep -qF "$loc" "$CAND"; then
        echo "  OK   $loc"
    else
        echo "  MISS $loc"
        missing=1
    fi
done < <(grep -o 'location[^{]*' "$SRC" | sed 's/[[:space:]]*$//' | sort -u)

grep -q 'root /opt/coup-server/web-staging;' "$CAND" \
    && echo "  OK   root -> web-staging" || { echo "  MISS root swap"; missing=1; }

[ "$missing" -ne 0 ] && { echo "REFUSING: a location from the source is absent. Nothing changed."; exit 1; }

cp "$CAND" "$CONF"
[ -f "$AVAIL" ] && cp "$CAND" "$AVAIL"

if nginx -t 2>&1 | tail -2; then
    systemctl reload nginx
    echo "OK: reloaded"
else
    echo "nginx -t FAILED - restoring, NOT reloading"
    cp -a "$BAKDIR/saturncoup.bak-prerepair2-$TS" "$CONF"
    [ -f "$AVAIL" ] && cp -a "$BAKDIR/saturncoup.bak-prerepair2-$TS" "$AVAIL"
    exit 1
fi
