#!/usr/bin/env bash
#
# capture_all_screens.sh - Photograph every screen, including the ones behind
# online play.
#
# Lobby, game and game over need a live server session, so an offline capture
# can never reach them by playing. This builds a QA-only disc per screen with
# -DCOUP_QA_SCREEN, boots it, and captures a frame. The Saturn skill is
# explicit that a rendering bug's gate is the rendered frame (gotcha #4), and
# these are the frames that were otherwise never being looked at.
#
# The QA discs are throwaway. verify_facelift gate J fails any build that
# defines COUP_QA_SCREEN, so one cannot be shipped by accident, and this
# script restores the normal disc when it finishes.
#
# Usage: bash scripts/qa/capture_all_screens.sh [seconds]

set -o pipefail
cd "$(dirname "$0")/../.." || exit 1

SECONDS_WAIT="${1:-40}"
OUT=build/qa/screens
mkdir -p "$OUT"

# COUP_SCREEN_* enum order, from examples/coup/coup.h
SCREENS=(
  "0:title"
  "1:settings"
  "2:rules"
  "3:connecting"
  "4:name_entry"
  "5:lobby"
  "6:game"
  "7:game_over"
)

for entry in "${SCREENS[@]}"; do
  n="${entry%%:*}"
  name="${entry##*:}"

  echo "=== ${name} (COUP_QA_SCREEN=${n}) ==="
  if ! CCFLAGS_EXTRA="-DCOUP_QA_SCREEN=${n}" \
       bash scripts/docker-saturn-build.sh examples/coup/saturn \
       > "build/qa/build_${name}.log" 2>&1; then
    echo "  BUILD FAILED - see build/qa/build_${name}.log"
    tail -5 "build/qa/build_${name}.log"
    continue
  fi

  cp -f examples/coup/saturn/_build/track01.bin build/coup_game/track01.bin
  cp -f examples/coup/saturn/_build/game.cue    build/coup_game/game.cue

  python scripts/qa/qa_retroarch.py --shot build/coup_game/game.cue \
      --seconds "$SECONDS_WAIT" --out "$OUT/${name}.png" || \
      echo "  capture failed for ${name}"
done

echo
echo "=== restoring the shippable disc ==="
bash scripts/docker-saturn-build.sh examples/coup/saturn > build/qa/build.log 2>&1 \
  && echo "  rebuilt without COUP_QA_SCREEN" \
  || echo "  RESTORE BUILD FAILED"
cp -f examples/coup/saturn/_build/track01.bin build/coup_game/track01.bin
cp -f examples/coup/saturn/_build/game.cue    build/coup_game/game.cue
python scripts/qa/verify_facelift.py | tail -2
