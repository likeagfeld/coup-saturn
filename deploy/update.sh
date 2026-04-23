#!/bin/bash
# update.sh - Pull latest code and restart coup-server
set -e

cd /opt/coup-server
git pull origin main

# Rebuild C rule engine shared library
if [ -f tools/coup_server/build_lib.sh ]; then
    bash tools/coup_server/build_lib.sh
fi

# Restart server (web client changes take effect immediately via Nginx)
systemctl restart coup-server
echo "Updated and restarted."
