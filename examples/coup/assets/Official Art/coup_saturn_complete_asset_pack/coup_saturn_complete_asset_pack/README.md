# Coup Saturn Complete Asset Pack

This pack was built from the generated high-quality atlases based on the supplied reference art and `ASSET_REQUEST(1).md`.

## Contents

- 127 unique requested production assets/frames
- 381 individually named production PNGs across three tiers:
  - `source_fullcolor/` — requested 4x authoring sizes
  - `target_fullcolor/` — native game dimensions
  - `saturn_ready/` — native dimensions with hardware palette limits applied
- 13 animated GIF previews
- 13 sequence sprite sheets
- Source atlases, reference art, and the original MD
- `QA_MANIFEST.csv` with dimensions, palette counts, MD5 hashes, and pass/fail checks
- `PACK_SUMMARY.json`

## Hardware treatment

- Background Saturn-ready files: <=255 colors
- Opaque portrait/card sprites: <=15 colors
- Keyed UI/effect sprites: exact #FF00FF background and <=16 total colors
- No alpha channel is used in production PNGs
- Filenames follow the IDs and zero-padded animation conventions from the request

## QA result

Automated size/palette failures: 0
