#!/bin/bash
#
# run.sh - Launch the built Saturn coup disc in Mednafen.
#
# Usage:
#   ./run.sh coup           Launch build/coup_game/game.cue in Mednafen
#   ./run.sh coup/saturn    Alias for "coup"
#
# See README "Quick start" and "Running locally" for build prerequisites.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat >&2 <<EOF
Usage: ./run.sh <target>

Targets:
  coup            Launch build/coup_game/game.cue in Mednafen
  coup/saturn     Alias for "coup"

Build first with: ./build.sh coup
EOF
    exit 1
}

launch_coup() {
    local cue="build/coup_game/game.cue"
    if [ ! -f "$cue" ]; then
        echo "run.sh: $cue not found. Build first: ./build.sh coup" >&2
        exit 1
    fi
    if ! command -v mednafen >/dev/null 2>&1; then
        echo "run.sh: mednafen not found in PATH." >&2
        echo "See README 'Quick start' -- on macOS: brew install mednafen" >&2
        exit 1
    fi
    exec mednafen -force_module ss "$cue"
}

case "${1:-}" in
    coup|coup/saturn) launch_coup ;;
    *)                usage ;;
esac
