# Official Coup Art — Inventory and Conversion Plan

Source of truth for the visual facelift. Everything here is already on disk;
none of it needs generating.

## Sheets and what has been sliced from them

| File | Contents | Sliced to |
|---|---|---|
| `Backgrounds.png` | 2×2 sheet, 836×470 each | `../bg_src/{skyline,council,throne,plaza}.png` |
| `Cards.png` | 3×2 sheet, 374×701 each | `../card_src/{duke,assassin,captain,ambassador,contessa,cardback}.png` |
| `Sprite Effects.png` | UI/effect sheet, ~1024×1500 | **not yet sliced** |
| `Duke/Assassin/Captain/Ambassador/Contessa.png` | Full-bleed single cards, ~886×1300 | — (use `card_src/` instead) |
| `Card Back.png`, `Coin.png` | Standalone | — |
| `Coup.jpg`, `Coup.webp` | Logo/box art | — |

## Backgrounds — 2 of 4 shipping

`skyline` (title, all non-game screens) and `council` (game screen) are
converted and embedded. Each is 131,072 bytes as a 512×256 8bpp VDP2 bitmap.

**Constraint: 273 KB of WRAM-H headroom remains — room for exactly ONE more.**
`throne` and `plaza` would need CD streaming, which is a new subsystem. Check
with `make qa-binary` before adding anything.

The council chamber was chosen for the game screen deliberately: it depicts a
round table ringed with thrones, which is what Coup actually is.

## Cards — sliced, not yet converted

Each 374×701 card has a coloured border, a portrait region in roughly the top
60%, a name plate, and ability text. The per-character colour identity is
usable directly:

| Character | Border | Suggested `COUP_CHAR_*` accent |
|---|---|---|
| Duke | gold on purple | gold |
| Assassin | grey on black | steel |
| Captain | blue | blue |
| Ambassador | green on olive | green |
| Contessa | red | crimson |
| Card back | grey, "COUP" | neutral |

**Highest-value next step.** Cropping the portrait region gives clean,
well-lit busts on an opaque illustrated background, which fixes the
transparency problem at its ROOT: the current animated portraits fade to black
at the bottom, so 56-74% of each sprite is transparent (MEASURED) and the
backdrop shows through the characters. That is what forced the medallion
workaround in `coup_render.c`. With official art the medallion becomes a
deliberate frame rather than a patch.

Caveat: these are static. The existing 24-frame animation came from MP4s that
are NOT in the repo. Either accept static portraits, or synthesise motion (a
slow drift/zoom across the illustration reads well and costs only the frames
you generate).

## Sprite Effects — unsliced, high value

Contains, by row:

1. Coin stacks labelled 1, 2, 3, 5, 10, plus treasure piles
2. Torch flame, more coin stacks, gold ingots
3. Targeting reticle, gold arrow, blue energy bolt
4. Spark burst, blue flash, blue ring, smoke puff
5. **VICTORY banner**, **DEFEAT plate**, shield-with-?
6. Sparkle, ring, shadow blob, ! markers (blue and red)
7. VICTORY banner (alt), skull, blue shield, loading dots
8. Blank ribbon, shadow, ?/! speech markers

Direct uses:
- **Coin stacks** replace the single 16×16 coin, tiered by the player's count.
  Coins are on screen constantly, so this is the most-seen improvement.
- **VICTORY / DEFEAT** replace the 7-strip VDP1 game-over hack, freeing ~36 KB
  of VDP1 VRAM and CRAM banks 25-31.
- **Spark / flash / ring** are challenge and coup resolution effects.
- **? and !** markers suit the challenge and block prompts.

## Conversion pipeline

Backgrounds:
```bash
python convert_backgrounds.py --scene game=bg_src/council.png \
    --scene title=bg_src/skyline.png --output ../saturn/coup_bg_data.h \
    --preview-dir ../../../docs/saturn/captures/baseline
```
Emits a 512×256 8bpp bitmap plus a 256-entry RGB555 palette per scene, with
index 0 reserved transparent. Asserts every hardware invariant before writing.

Sprites go through `convert_assets.py` (static, 4bpp, 16-colour) or
`convert_animated.py` (multi-frame, shared palette). Both reserve index 0 for
transparency and map near-black to it — which is exactly why the current
portraits lose their bodies.

## Rules that bit us, do not relearn them

- VDP1 polygon colours **must** set bit 15 or VDP2 reads them as CRAM indices
  rather than RGB (ST-013-R3 2.1). This corrupted every polygon colour in the
  game until it was found.
- A 256-colour bitmap palette must start on a 256-colour boundary, so only
  CRAM `0x000/0x200/0x400/0x600` are legal. The first three are taken; the
  background owns bank 3 at `0x600`.
- Asset loaders **chain** their VDP1 and CRAM allocations. Never recompute a
  base from constants — that assumption broke twice.
