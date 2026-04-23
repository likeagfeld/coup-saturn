#!/usr/bin/env bash
#
# build.sh - Thin dispatcher that delegates to the Makefile.
#
# All real build logic lives in Makefile targets (coup-lib, coup-server,
# coup-saturn, coup-all, test-coup). This script only maps friendly
# argument names to `make` invocations.
#
# Usage:
#   ./build.sh coup         Build game + server + host lib (= make coup-all)
#   ./build.sh all          Alias for coup
#   ./build.sh tests/coup   Run coup unit tests (= make test-coup)
#   ./build.sh clean        Remove build artifacts (= make clean)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat >&2 <<'EOF'
Usage: ./build.sh <target>

Targets:
  coup          Build game + server + host lib (make coup-all)
  all           Alias for coup
  tests/coup    Run coup unit tests (make test-coup)
  clean         Remove build artifacts (make clean)
EOF
    exit 1
}

[ $# -eq 1 ] || usage

case "$1" in
    coup|all)    exec make coup-all ;;
    tests/coup)  exec make test-coup ;;
    clean)       exec make clean ;;
    *)           usage ;;
esac
