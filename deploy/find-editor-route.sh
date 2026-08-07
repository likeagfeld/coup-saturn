#!/bin/bash
# find-editor-route.sh - Did any config ever route /admin/editor/ ?
#
# /admin/ proxies to 127.0.0.1:9099 (saturn-admin.service), which is alive.
# But there are separate utenyaa-editor services, and the config currently
# installed was restored from a backup taken BEFORE the cutover - so it may
# predate an editor route added since.
set -uo pipefail

echo "=== 'editor' mentions per config ==="
for f in /root/nginx-backups/* /etc/nginx/sites-enabled/* /etc/nginx/sites-available/*; do
    [ -f "$f" ] || continue
    n=$(grep -c editor "$f" 2>/dev/null)
    [ -z "$n" ] && n=0
    printf '  %3s  %s\n' "$n" "$f"
done

echo
echo "=== editor services ==="
for u in utenyaa-editor utenyaa-editor-public saturn-admin; do
    printf '  %-24s %s\n' "$u" "$(systemctl is-active $u 2>/dev/null)"
    systemctl show "$u" -p ExecStart --value 2>/dev/null | tr ';' '\n' | grep -o 'argv\[\]=.*' | head -1 | sed 's/^/      /'
done

echo
echo "=== every listening python port ==="
ss -tlnp 2>/dev/null | grep python3 | awk '{print "  " $4}' | sort

echo
echo "=== does the admin portal itself serve /editor/ ? ==="
code=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:9099/editor/ 2>/dev/null)
echo "  127.0.0.1:9099/editor/ -> $code"
for p in 9091 9092 9093 9094 9095 9096; do
    c=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 http://127.0.0.1:$p/ 2>/dev/null)
    echo "  127.0.0.1:$p/ -> $c"
done
