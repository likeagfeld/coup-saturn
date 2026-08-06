#!/bin/bash
# install-server-hotfix.sh - Replace server.py from /tmp, safely.
#
# Used for the mid-game-join fix: broadcast_lobby_state() reached clients
# inside an active match, and LOBBY_STATE rebuilds player identity, so the
# in-game view stopped tracking who was who and the match appeared frozen.
#
# SAFETY
#   - timestamped backup before anything is touched
#   - the NEW file is syntax-checked before it is installed, not after
#   - the service is only restarted if the install succeeded
#   - on a failed restart the backup is put back and the service restarted
#     again, so a bad deploy cannot leave the server down
set -euo pipefail

SRC=/tmp/server.py
DST=/opt/coup-server/tools/coup_server/server.py
BAKDIR=/root/coup-server-backups
TS=$(date +%Y%m%d-%H%M%S)

[ -f "$SRC" ] || { echo "REFUSING: $SRC not found"; exit 1; }

python3 -c "import ast,sys; ast.parse(open('$SRC').read())" \
    || { echo "REFUSING: new server.py does not parse"; exit 1; }
echo "new server.py parses clean"

mkdir -p "$BAKDIR"
cp -a "$DST" "$BAKDIR/server.py.bak-$TS"
echo "backup: $BAKDIR/server.py.bak-$TS"

cp "$SRC" "$DST"
echo "guard present: $(grep -c 'Suppressed lobby-state broadcast' "$DST")"

systemctl restart coup-server
sleep 4

if systemctl is-active --quiet coup-server; then
    echo "OK: coup-server active"
    systemctl show coup-server -p ActiveEnterTimestamp --value
else
    echo "RESTART FAILED - rolling back"
    cp -a "$BAKDIR/server.py.bak-$TS" "$DST"
    systemctl restart coup-server
    sleep 3
    systemctl is-active coup-server
    exit 1
fi
