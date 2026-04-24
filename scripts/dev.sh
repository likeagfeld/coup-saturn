#!/usr/bin/env bash
# dev.sh -- single-command local dev launcher.
#
# Spawns the Python game server and the static web server together,
# with labeled, interleaved output. Ctrl-C stops both.
#
# Production (nginx + systemd per deploy/) is unaffected. This is a
# dev-only convenience replacing the two-terminal dance.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/dev.sh [--help]

Starts both local dev servers in one terminal:
  - Game relay server on ws://localhost:4823 (TCP 4821 for Saturn NetLink)
  - Static web server on http://localhost:8000 (serves web/)

Preflight:
  - Ensures build/libcoup_rules.dylib exists (runs 'make coup-lib' if not)
  - Ensures the Python 'websockets' package is importable

Ctrl-C stops both processes.
EOF
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    "") ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Preflight: rules library.
if [ ! -f build/libcoup_rules.dylib ] && [ ! -f build/libcoup_rules.so ]; then
    echo "[dev] libcoup_rules not built; running 'make coup-lib'..."
    make coup-lib
fi

# Preflight: websockets package.
if ! python3 -c "import websockets" >/dev/null 2>&1; then
    echo "[dev] python3 'websockets' package missing" >&2
    echo "[dev] run 'pip3 install websockets' and retry" >&2
    exit 1
fi

# FIFOs let us background python directly (so $! is the python PID)
# while still running its output through sed for labeled lines.
FIFO_DIR="$(mktemp -d -t coup-dev-XXXXXX)"
mkfifo "$FIFO_DIR/game" "$FIFO_DIR/web"

GAME_PID=""
WEB_PID=""

cleanup() {
    trap - EXIT INT TERM
    [ -n "$GAME_PID" ] && kill "$GAME_PID" 2>/dev/null || true
    [ -n "$WEB_PID" ]  && kill "$WEB_PID"  2>/dev/null || true
    pkill -P $$ 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$FIFO_DIR" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Game server:  ws://localhost:4823  (TCP also on 4821 for Saturn NetLink)"
echo "Browser:      http://localhost:8000"
echo "Ctrl-C to stop both."
echo

# Unbuffered python (-u) + sed (-u) so labels show up promptly.
sed -u 's/^/[game] /' <"$FIFO_DIR/game" &
sed -u 's/^/[web]  /' <"$FIFO_DIR/web"  &

python3 -u tools/coup_server/server.py --ws-port 4823 >"$FIFO_DIR/game" 2>&1 &
GAME_PID=$!

( cd web && exec python3 -u -m http.server 8000 ) >"$FIFO_DIR/web" 2>&1 &
WEB_PID=$!

# Block here until a signal (Ctrl-C / SIGTERM) fires the trap; the trap
# then kills children and exits. 'wait' without -n on bash 3.2 (macOS
# default) waits for all children and is interruptible by signals.
wait
