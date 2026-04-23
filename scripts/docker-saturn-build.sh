#!/bin/bash
#
# docker-saturn-build.sh - Build Saturn ROM using Docker
#
# Uses Docker to run the Linux Jo Engine toolchain on macOS.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default Jo Engine location
JOENGINE_ROOT="${JOENGINE_ROOT:-$HOME/Projects/retro/saturn/engines/joengine}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check prerequisites
if ! command -v docker &> /dev/null; then
    echo_error "Docker is required but not installed."
    exit 1
fi

if [ ! -d "$JOENGINE_ROOT" ]; then
    echo_error "Jo Engine not found at: $JOENGINE_ROOT"
    echo "Set JOENGINE_ROOT to your Jo Engine installation"
    exit 1
fi

# Build directory to compile
BUILD_DIR="${1:-$PROJECT_ROOT/tests/saturn/rom}"
if [ ! -d "$BUILD_DIR" ]; then
    echo_error "Build directory not found: $BUILD_DIR"
    exit 1
fi

# Resolution define (optional, e.g., -DSATURN_RES_352)
SATURN_RES_DEFINE="${2:-}"

echo_info "Building Saturn ROM using Docker..."
echo_info "  Jo Engine: $JOENGINE_ROOT"
echo_info "  Build dir: $BUILD_DIR"
if [ -n "$SATURN_RES_DEFINE" ]; then
    echo_info "  Resolution: $SATURN_RES_DEFINE"
fi

# Convert paths for Docker volume mounts.
# On MSYS/Git Bash (Windows), paths like /d/foo get mangled to C:/Program Files/Git/d/foo
# by the shell. Use MSYS_NO_PATHCONV=1 to prevent this, and convert Windows drive
# letter paths (D:\foo) to Docker-compatible /d/foo format.
docker_path() {
    local p="$1"
    # Convert backslashes to forward slashes
    p="${p//\\//}"
    # Convert D:/foo to /d/foo for Docker on Windows
    if [[ "$p" =~ ^([A-Za-z]):/ ]]; then
        local drive="${BASH_REMATCH[1]}"
        drive="${drive,,}"  # lowercase
        p="/${drive}/${p:3}"
    fi
    echo "$p"
}

DOCKER_PROJECT=$(docker_path "$PROJECT_ROOT")
DOCKER_JOENGINE=$(docker_path "$JOENGINE_ROOT")

# Get relative path from project root to build dir
REL_BUILD_DIR="${BUILD_DIR#$PROJECT_ROOT/}"

# Run build in Docker using Ubuntu with necessary tools
# Use --platform linux/amd64 for Apple Silicon Macs (Jo Engine ships x86_64 Linux binaries)
# MSYS_NO_PATHCONV=1 prevents Git Bash from mangling paths passed to docker
MSYS_NO_PATHCONV=1 docker run --rm \
    --platform linux/amd64 \
    -v "$DOCKER_PROJECT:/cui" \
    -v "$DOCKER_JOENGINE:/joengine" \
    -w "/cui/$REL_BUILD_DIR" \
    -e "JOENGINE_ROOT=/joengine" \
    -e "SATURN_RES_DEFINE=$SATURN_RES_DEFINE" \
    ubuntu:22.04 \
    bash -c '
        # Install minimal dependencies
        apt-get update -qq && apt-get install -y -qq make mkisofs > /dev/null 2>&1

        # Make the Linux compiler executable
        chmod +x /joengine/Compiler/LINUX/bin/*

        # Build
        make clean 2>/dev/null || true
        make all SATURN_RES_DEFINE="$SATURN_RES_DEFINE"
    '

# Check if build succeeded
if [ -f "$BUILD_DIR/_build/track01.bin" ] && [ -f "$BUILD_DIR/_build/game.cue" ]; then
    echo_info "Build successful!"
    echo_info "Output files:"
    ls -la "$BUILD_DIR/_build"/track01.bin "$BUILD_DIR/_build"/game.cue 2>/dev/null || true
else
    echo_error "Build failed - no output files found"
    exit 1
fi
