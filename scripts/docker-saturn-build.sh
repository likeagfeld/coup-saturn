#!/bin/bash
#
# docker-saturn-build.sh - Build Saturn ROM using Docker
#
# Uses Docker to run the Linux Jo Engine toolchain on macOS.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Baked image with Jo Engine + SH-2 toolchain. Built from
# scripts/saturn-build.Dockerfile on first use; the joengine SHA is
# pinned there. Rebuild with: docker build --no-cache \
#   -f scripts/saturn-build.Dockerfile -t coup-saturn-build:latest scripts/
IMAGE_TAG="coup-saturn-build:latest"
DOCKERFILE="$SCRIPT_DIR/saturn-build.Dockerfile"

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

# Build the baked image if missing. --platform pin matches runtime.
if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo_info "Building $IMAGE_TAG (first run; ~1-2 min)..."
    docker build --platform linux/amd64 \
        -f "$DOCKERFILE" \
        -t "$IMAGE_TAG" \
        "$SCRIPT_DIR"
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
echo_info "  Image:     $IMAGE_TAG"
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

# Get relative path from project root to build dir
REL_BUILD_DIR="${BUILD_DIR#$PROJECT_ROOT/}"

# Optional: bind-mount a local Jo Engine checkout over the image's
# baked copy. Use this when iterating on the engine fork itself.
#   JOENGINE_LOCAL=~/Projects/retro/saturn/engines/joengine make coup-saturn
LOCAL_JOENGINE_ARG=()
if [ -n "${JOENGINE_LOCAL:-}" ]; then
    if [ ! -d "$JOENGINE_LOCAL" ]; then
        echo_error "JOENGINE_LOCAL not found: $JOENGINE_LOCAL"
        exit 1
    fi
    echo_info "  Override:  $JOENGINE_LOCAL -> /joengine"
    LOCAL_JOENGINE_ARG=(-v "$(docker_path "$JOENGINE_LOCAL"):/joengine")
fi

# Run build using the baked image. JOENGINE_ROOT=/joengine is already
# set in the image ENV; makefiles can consume it as-is.
MSYS_NO_PATHCONV=1 docker run --rm \
    --platform linux/amd64 \
    -v "$DOCKER_PROJECT:/workspace" \
    "${LOCAL_JOENGINE_ARG[@]}" \
    -w "/workspace/$REL_BUILD_DIR" \
    -e "SATURN_RES_DEFINE=$SATURN_RES_DEFINE" \
    "$IMAGE_TAG" \
    bash -c '
        # Local-mount case: bind-mounted checkouts may lack +x on the
        # Linux toolchain binaries (common when cloned on macOS/Windows).
        chmod +x /joengine/Compiler/LINUX/bin/* 2>/dev/null || true
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
