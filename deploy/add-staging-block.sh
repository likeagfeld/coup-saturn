#!/bin/bash
# add-staging-block.sh - Insert the /staging/ location into the LIVE nginx
# config, without touching anything already in it.
#
# WHY THIS EXISTS INSTEAD OF COPYING deploy/nginx-saturncoup.conf OVER
#   The repo's copy of that file is NOT a superset of what is deployed. The
#   live config also serves /admin/, /bomberman/ and /megasd/, none of which
#   are in the repo. MEASURED by diffing the two before touching anything -
#   an order-independent compare showed `try_files $uri $uri/
#   /megasd/index.html` present live and absent from the repo.
#
#   Copying the repo file over the live one would therefore have silently
#   deleted three working sites. This script adds one block and changes
#   nothing else.
#
# SAFETY
#   - Backs up the live config with a timestamp before touching it.
#   - Idempotent: does nothing if a /staging/ block already exists.
#   - `nginx -t` decides. If the test fails the backup is restored and nginx
#     is never reloaded, so the running config is untouched either way.
set -euo pipefail

# WHICH FILE NGINX ACTUALLY READS
#   On this host /etc/nginx/sites-enabled/saturncoup is a REGULAR FILE, not
#   the usual symlink into sites-available. Editing sites-available therefore
#   changes nothing: MEASURED - the first run edited it, `nginx -t` passed,
#   the reload succeeded, and /staging/ still served the live client, because
#   nginx.conf includes sites-enabled/* and nothing else.
#
#   So the target is an argument, and BOTH copies get the block: the enabled
#   one so it takes effect, the available one so the two do not drift and
#   mislead whoever reads them next.
CONF="${1:-/etc/nginx/sites-enabled/saturncoup}"
STAGING_ROOT=/opt/coup-server/web-staging
STAMP=$(date +%Y%m%d-%H%M%S)
BAK="${CONF}.bak-${STAMP}"

if [ ! -d "$STAGING_ROOT" ]; then
    echo "REFUSING: $STAGING_ROOT does not exist - upload the tree first."
    exit 1
fi

if grep -q 'location /staging/' "$CONF"; then
    echo "Already present; nothing to do."
    nginx -t
    exit 0
fi

cp -a "$CONF" "$BAK"
echo "backup: $BAK"

python3 - "$CONF" <<'PY'
import sys

path = sys.argv[1]
lines = open(path).readlines()

block = """    # --- Staging: the redesigned client, alongside the live one ---------
    #
    # Additive. The live site keeps its own root and behaviour, and staging
    # lives in a SEPARATE directory, so a bad deploy here cannot damage what
    # is already serving players.
    #
    # `alias`, not `root`: with root, nginx appends the whole request path
    # and looks for .../web-staging/staging/... . The trailing slashes on
    # both the location and the alias are required.
    #
    # try_files falls back to the FULL /staging/index.html. With a bare
    # /index.html a deep link under /staging would fall through to the LIVE
    # index and serve the OLD client from a staging URL - which would make
    # staging look like it works while testing the wrong build.
    location /staging/ {
        alias /opt/coup-server/web-staging/;
        try_files $uri $uri/ /staging/index.html;

        # No caching. Staging exists to be redeployed constantly, and a
        # tester reporting a bug as fixed because they held a cached bundle
        # is worse than a slow load.
        expires -1;
        add_header Cache-Control "no-store, no-cache, must-revalidate";
    }

    # Bare /staging -> /staging/ so the alias and try_files above apply.
    location = /staging {
        return 301 /staging/;
    }

"""

# Insert immediately before the catch-all `location / {`, so the more
# specific /staging/ prefix is declared alongside the other prefix locations
# and never shadowed by the catch-all's try_files.
for i, ln in enumerate(lines):
    if ln.strip() == 'location / {':
        lines.insert(i, block)
        break
else:
    sys.exit("could not find the `location / {` anchor - not modifying")

open(path, 'w').writelines(lines)
print("inserted /staging/ block")
PY

if nginx -t; then
    systemctl reload nginx
    echo "OK: nginx reloaded with /staging/"
else
    echo "nginx -t FAILED - restoring backup, nginx NOT reloaded"
    cp -a "$BAK" "$CONF"
    exit 1
fi
