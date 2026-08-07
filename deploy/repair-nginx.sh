#!/bin/bash
# repair-nginx.sh - Restore every location the cutover dropped, then audit.
#
# The cutover installed the REPO's config wholesale. The repo copy has never
# contained /admin/, /bomberman/ or /megasd/, so all three fell through to the
# catch-all and began serving the game client - measured byte-identical to /.
#
# This picks the most complete config available, applies ONLY the root swap,
# proves every location survives BEFORE installing, and then audits the box.
set -uo pipefail

BAKDIR=/root/nginx-backups
CONF=/etc/nginx/sites-enabled/saturncoup
AVAIL=/etc/nginx/sites-available/saturncoup
TS=$(date +%Y%m%d-%H%M%S)
CAND=/tmp/saturncoup.candidate

echo "=== which configs contain what ==="
best=""; best_n=-1
for f in "$BAKDIR"/* "$CONF"; do
    [ -f "$f" ] || continue
    a=$(grep -c 'location /admin/'     "$f" 2>/dev/null || echo 0)
    b=$(grep -c 'location /bomberman/' "$f" 2>/dev/null || echo 0)
    m=$(grep -c 'location /megasd/'    "$f" 2>/dev/null || echo 0)
    w=$(grep -c 'location /ws'         "$f" 2>/dev/null || echo 0)
    n=$((a + b + m + w))
    printf '  %-52s admin=%s bomber=%s megasd=%s ws=%s\n' "$(basename "$f")" "$a" "$b" "$m" "$w"
    if [ "$n" -gt "$best_n" ]; then best_n=$n; best="$f"; fi
done
echo "  most complete: $(basename "$best") (score $best_n)"

if [ "$best_n" -lt 4 ]; then
    echo "REFUSING: no config on this box has all four locations. Nothing changed."
    exit 1
fi

cp -a "$CONF" "$BAKDIR/saturncoup.bak-prerepair-$TS"
echo "  backup of current: saturncoup.bak-prerepair-$TS"

# Only the root swap. Nothing copied wholesale from the repo.
sed -e 's#root /opt/coup-server/web;#root /opt/coup-server/web-staging;#' \
    "$best" > "$CAND"

# Add /staging/ -> the previous client, if the chosen config predates it.
# ^~ is required: this config has a `location ~* \.(js|css)$` REGEX block and
# nginx tests regex locations before a plain prefix wins, so without it every
# asset under /staging/ is captured and served from the wrong root.
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

echo "=== candidate contents ==="
missing=""
for loc in 'location /admin/' 'location /bomberman/' 'location /megasd/' 'location /ws' 'location ^~ /staging/'; do
    if grep -q -- "$loc" "$CAND"; then echo "  OK   $loc"; else echo "  MISS $loc"; missing="$missing|$loc"; fi
done
grep -q 'root /opt/coup-server/web-staging;' "$CAND" && echo "  OK   root -> web-staging" || { echo "  MISS root swap"; missing="$missing|root"; }
[ -n "$missing" ] && { echo "REFUSING to install an incomplete config. Nothing changed."; exit 1; }

cp "$CAND" "$CONF"
[ -f "$AVAIL" ] && cp "$CAND" "$AVAIL"

if nginx -t 2>&1 | tail -2; then
    systemctl reload nginx
    echo "OK: reloaded"
else
    echo "nginx -t FAILED - restoring, NOT reloading"
    cp -a "$BAKDIR/saturncoup.bak-prerepair-$TS" "$CONF"
    [ -f "$AVAIL" ] && cp -a "$BAKDIR/saturncoup.bak-prerepair-$TS" "$AVAIL"
    exit 1
fi

echo
echo "=== SERVER AUDIT ==="
echo "-- services --"
for s in nginx coup-server; do printf '  %-14s %s\n' "$s" "$(systemctl is-active $s)"; done
echo "-- listening --"
ss -tlnp 2>/dev/null | grep -E ':(80|443|4821|4823|9090)' | awk '{print "  " $4}'
echo "-- web roots --"
for d in /opt/coup-server/web /opt/coup-server/web-staging; do
    printf '  %-34s %s files, index=%s\n' "$d" "$(find $d -type f 2>/dev/null | wc -l)" \
        "$([ -f $d/index.html ] && echo yes || echo NO)"
done
echo "-- disk --"
df -h / | tail -1 | awk '{print "  root fs " $5 " used, " $4 " free"}'
