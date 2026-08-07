#!/bin/bash
# cap-journal.sh - Bound journald so logs cannot fill the disk again.
#
# MEASURED: the root filesystem was 89% full with 1.1 GB free on a 9.7 GB
# disk, and /var/log held 2.2 GB of it - journald alone 981 MB, about a tenth
# of the whole disk. Vacuuming to 7 days recovered 468 MB, but without a cap
# it simply regrows.
#
# SystemMaxUse bounds the total journal on disk. 200M keeps roughly a week of
# this box's volume while leaving headroom, and journald enforces it
# continuously rather than only when something runs a vacuum.
#
# Idempotent: rewrites the setting if present, appends it if not, and only
# restarts journald when the file actually changed.
set -uo pipefail

CONF=/etc/systemd/journald.conf
TS=$(date +%Y%m%d-%H%M%S)

cp -a "$CONF" "/root/journald.conf.bak-$TS"
echo "backup: /root/journald.conf.bak-$TS"

before=$(md5sum "$CONF" | cut -d' ' -f1)

if grep -qE '^\s*#?\s*SystemMaxUse=' "$CONF"; then
    sed -i -E 's/^\s*#?\s*SystemMaxUse=.*/SystemMaxUse=200M/' "$CONF"
else
    printf '\n# Capped 2026-08-07: journald had grown to 981M on a 9.7G disk.\nSystemMaxUse=200M\n' >> "$CONF"
fi

after=$(md5sum "$CONF" | cut -d' ' -f1)
if [ "$before" = "$after" ]; then
    echo "already capped; journald not restarted"
else
    systemctl restart systemd-journald
    echo "SystemMaxUse=200M applied, journald restarted"
fi

echo "--- effective setting ---"
grep -E '^SystemMaxUse=' "$CONF" | sed 's/^/  /'
echo "--- journal now ---"
journalctl --disk-usage
echo "--- disk ---"
df -h / | tail -1
