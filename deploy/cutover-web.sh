#!/bin/bash
# cutover-web.sh - Promote the redesigned client to the site root.
#
# AFTER this runs:
#   /          -> /opt/coup-server/web-staging   (the redesigned client)
#   /staging/  -> /opt/coup-server/web           (the previous client)
#
# The directory names deliberately stay as they are; see the note in
# nginx-saturncoup.conf.
#
# THE CACHE HAZARD THIS HANDLES
#   Both clients ship a rebellion.mp3, and they are DIFFERENT files -
#   10,013,838 B against 4,794,350 B, different md5. nginx serves /assets/
#   with `expires 7d, immutable`. Sharing that URL across the cutover would
#   hand every returning visitor the old 10 MB track for a week, and no
#   cache-busting query would help, because `immutable` means the browser
#   will not even ask. The new client's copy therefore moves to
#   assets/music/, a path the old root never had.
#
#   Every other shared path (js, css, index.html) is served
#   `expires 5m, must-revalidate`, so those turn over on their own.
#
# SAFETY
#   - the config is backed up, `nginx -t` decides, and a failed test restores
#     the backup without ever reloading
#   - the previous client is not deleted, only re-routed, so rolling back is
#     a config swap rather than a restore
set -euo pipefail

TS=$(date +%Y%m%d-%H%M%S)
BAKDIR=/root/nginx-backups
CONF_ENABLED=/etc/nginx/sites-enabled/saturncoup
CONF_AVAIL=/etc/nginx/sites-available/saturncoup

[ -f /tmp/nginx-new.conf ] || { echo "REFUSING: /tmp/nginx-new.conf missing"; exit 1; }
[ -d /opt/coup-server/web-staging ] || { echo "REFUSING: web-staging missing"; exit 1; }
[ -d /opt/coup-server/web ] || { echo "REFUSING: web missing"; exit 1; }

# 1. the relocated BGM + the assets.js that points at it
if [ -f /tmp/cut.tgz ]; then
    tar -xzf /tmp/cut.tgz -C /opt/coup-server/web-staging
    rm -f /opt/coup-server/web-staging/assets/rebellion.mp3
    chmod -R a+rX /opt/coup-server/web-staging
    echo "BGM relocated to assets/music/, old copy removed from the shared path"
fi

# 2. the routing swap
mkdir -p "$BAKDIR"
cp -a "$CONF_ENABLED" "$BAKDIR/saturncoup.bak-cutover-$TS"
echo "backup: $BAKDIR/saturncoup.bak-cutover-$TS"

cp /tmp/nginx-new.conf "$CONF_ENABLED"
[ -f "$CONF_AVAIL" ] && cp /tmp/nginx-new.conf "$CONF_AVAIL"

if nginx -t; then
    systemctl reload nginx
    echo "OK: cutover live"
else
    echo "nginx -t FAILED - restoring, NOT reloading"
    cp -a "$BAKDIR/saturncoup.bak-cutover-$TS" "$CONF_ENABLED"
    [ -f "$CONF_AVAIL" ] && cp -a "$BAKDIR/saturncoup.bak-cutover-$TS" "$CONF_AVAIL"
    exit 1
fi
