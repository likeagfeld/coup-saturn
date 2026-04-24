#!/bin/bash
#
# Build the libcoup_rules shared library for the Coup server.
#
# Compiles the C rule engine into a .so/.dylib placed next to this script
# so the server can load it without needing the full project tree.
#
# Prefer: make coup-server (from project root) — this script is the
# fallback used by deploy/install.sh on the production Debian server.
#
# Usage:
#   ./build_lib.sh          Build the shared library
#   ./build_lib.sh clean    Remove built artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/../../examples/coup"
BUILD_TMP="$SCRIPT_DIR/.build"

CC="${CC:-cc}"
CFLAGS="-Wall -Wextra -std=c99 -O2 -fPIC"

SRCS=(
    "$SRC_DIR/coup_rules.c"
    "$SRC_DIR/coup_table_view.c"
    "$SRC_DIR/coup_event_log.c"
    "$SRC_DIR/coup_rules_bridge.c"
    "$SRC_DIR/coup_bot.c"
    "$SRC_DIR/coup_bot_bridge.c"
)

case "$(uname -s)" in
    Darwin*) LIB_NAME="libcoup_rules.dylib"; SHARED_FLAGS="-dynamiclib" ;;
    *)       LIB_NAME="libcoup_rules.so";    SHARED_FLAGS="-shared" ;;
esac

if [ "${1:-}" = "clean" ]; then
    rm -rf "$BUILD_TMP" "$SCRIPT_DIR/$LIB_NAME"
    echo "Cleaned."
    exit 0
fi

# Verify sources exist
for src in "${SRCS[@]}"; do
    if [ ! -f "$src" ]; then
        echo "Error: source file not found: $src" >&2
        echo "Run this from within the coup-saturn project tree." >&2
        exit 1
    fi
done

mkdir -p "$BUILD_TMP"

OBJS=()
for src in "${SRCS[@]}"; do
    obj="$BUILD_TMP/$(basename "${src%.c}.o")"
    echo "  CC  $(basename "$src")"
    $CC $CFLAGS -I"$SRC_DIR" -c -o "$obj" "$src"
    OBJS+=("$obj")
done

echo "  LD  $LIB_NAME"
$CC $SHARED_FLAGS -o "$SCRIPT_DIR/$LIB_NAME" "${OBJS[@]}"

rm -rf "$BUILD_TMP"
echo "Built: $SCRIPT_DIR/$LIB_NAME"
