#!/usr/bin/env python3
"""
build_staging_assets.py - Web asset pipeline for web-staging/

Converts the FULL-RESOLUTION official masters in
`examples/coup/assets/Official Art/` into web-sized, web-compressed art under
`web-staging/assets/`.

Why this exists rather than a hand-copy:

  * The masters are 4x authoring sizes (1280x896 backgrounds, 512x768 portrait
    frames). Shipping them raw would be ~120 MB. The Saturn build downscales
    them to 320x224 / 64x96 because of VDP VRAM; the web has no such limit but
    also no reason to serve 4x art to a phone. Every size below is chosen for
    the largest sensible CSS display size, not for the master's size.

  * The effect/UI/logo masters are CHROMA-KEYED with exact #FF00FF magenta
    (the Saturn pipeline needs a key colour, not an alpha channel - see the
    asset pack README, "Keyed UI/effect sprites: exact #FF00FF background").
    Served as-is on the web they would render as magenta rectangles. They are
    un-keyed to real alpha here, with edge de-fringing, because a browser
    composites with alpha.

  * The portrait idle is EIGHT separate frame files per character. CSS
    sprite animation wants one strip. They are packed here.

Run:  python scripts/build_staging_assets.py
Idempotent; safe to re-run. Writes only under web-staging/assets/.
"""

import os
import shutil
import subprocess
import sys
from collections import deque

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(REPO, "examples", "coup", "assets", "Official Art")
PACK = os.path.join(
    ART, "coup_saturn_complete_asset_pack", "coup_saturn_complete_asset_pack"
)
OUT = os.path.join(REPO, "web-staging", "assets")

# WebP everywhere: alpha support, ~30-40% smaller than PNG at equal quality,
# supported by every browser that can run this client's ES modules.
LOSSY = dict(format="WEBP", method=6)

_report = []


def _emit(dst, src_bytes):
    size = os.path.getsize(dst)
    rel = os.path.relpath(dst, OUT).replace("\\", "/")
    _report.append((rel, src_bytes, size))
    print(f"  {rel:44s} {src_bytes/1024:8.0f} KB -> {size/1024:7.1f} KB")


def _save(im, dst, quality, src_bytes):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    im.save(dst, quality=quality, **LOSSY)
    _emit(dst, src_bytes)


def _fit(im, w, h=None):
    if h is None:
        h = round(im.size[1] * w / im.size[0])
    return im.resize((w, h), Image.LANCZOS)


# --------------------------------------------------------------------------
# Magenta key -> alpha
# --------------------------------------------------------------------------

def unkey_magenta(im, tol=60):
    """Convert an exact-#FF00FF-keyed RGB image to RGBA with soft edges.

    A hard binary key leaves a magenta halo on every anti-aliased edge, which
    is exactly what "magenta fringe: 45 -> 0 palette entries" in the Saturn
    HANDOFF was about. Here the keyness is continuous:

        k = clamp((min(R,B) - G) / tol)        1.0 = pure key, 0.0 = opaque

    and the colour is un-premultiplied against the key so a 50%-magenta edge
    pixel resolves to its true colour at 50% alpha instead of staying pink.
    """
    im = im.convert("RGB")
    px = im.load()
    w, h = im.size
    out = Image.new("RGBA", (w, h))
    op = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            k = (min(r, b) - g) / tol
            if k <= 0:
                op[x, y] = (r, g, b, 255)
                continue
            if k >= 1:
                op[x, y] = (0, 0, 0, 0)
                continue
            # un-premultiply against pure magenta (255, 0, 255)
            a = 1.0 - k
            nr = min(255, max(0, int((r - 255 * k) / a)))
            ng = min(255, max(0, int(g / a)))
            nb = min(255, max(0, int((b - 255 * k) / a)))
            op[x, y] = (nr, ng, nb, int(a * 255))
    return out


def unkey_corners(im, thr=236):
    """Make the near-white margin OUTSIDE a rounded card border transparent.

    Flood-fills inward from the four corners only, so white *inside* the
    artwork (the Duke's collar highlights, the card-back's engraving) is never
    touched - it is not connected to a corner.
    """
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    seen = bytearray(w * h)
    q = deque()
    for c in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        q.append(c)
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h:
            continue
        i = y * w + x
        if seen[i]:
            continue
        r, g, b, _ = px[x, y]
        if min(r, g, b) < thr:
            continue
        seen[i] = 1
        px[x, y] = (r, g, b, 0)
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return im


def strip(frames, fw, fh, resample=Image.LANCZOS):
    """Pack frames into one horizontal filmstrip for CSS steps() animation."""
    sheet = Image.new("RGBA", (fw * len(frames), fh), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f.resize((fw, fh), resample), (i * fw, 0))
    return sheet


def _src_total(paths):
    return sum(os.path.getsize(p) for p in paths)


# --------------------------------------------------------------------------
# Jobs
# --------------------------------------------------------------------------

def backgrounds():
    """B1-B7 painted per-screen backdrops.

    From source_fullcolor (1280x896) NOT saturn_ready (320x224): the web has no
    VDP2 bitmap budget, and the 320x224 versions look like mud when scaled to
    fill a desktop viewport. 1280 wide covers a 2x phone and a 1x desktop; the
    scrim over them on every screen hides the rest.
    """
    print("backgrounds")
    for name in ("B1_title", "B2_game_table", "B3_lobby", "B4_connecting",
                 "B5_victory", "B6_defeat", "B7_rules"):
        src = os.path.join(PACK, "source_fullcolor", "backgrounds", name + ".png")
        im = Image.open(src).convert("RGB")
        key = name.split("_", 1)[1]
        _save(im, os.path.join(OUT, "bg", key + ".webp"), 68, os.path.getsize(src))


def logo():
    """The real wordmark (CouptitleLogo.png, 2172x724) + the boxart."""
    print("logo")
    src = os.path.join(ART, "CouptitleLogo.png")
    im = unkey_magenta(Image.open(src), tol=90)
    _save(_fit(im, 1086), os.path.join(OUT, "logo", "wordmark.webp"), 86,
          os.path.getsize(src))

    src = os.path.join(ART, "COUP SATURN BOXART.png")
    im = Image.open(src).convert("RGB")
    _save(_fit(im, 620), os.path.join(OUT, "logo", "boxart.webp"), 72,
          os.path.getsize(src))


def cards():
    """The six card faces + the card back, at a size that survives a flip."""
    print("cards")
    names = {
        "C1_card_back": "back", "C2_duke": "duke", "C3_assassin": "assassin",
        "C4_captain": "captain", "C5_ambassador": "ambassador",
        "C6_contessa": "contessa",
    }
    for src_name, key in names.items():
        src = os.path.join(PACK, "source_fullcolor", "cards", src_name + ".png")
        im = unkey_corners(Image.open(src))
        _save(_fit(im, 288), os.path.join(OUT, "cards", key + ".webp"), 80,
              os.path.getsize(src))


def portraits():
    """8-frame idle strips, one per character.

    The Saturn cycles these 8 frames in 2.67 s. The CSS does the same with
    steps(8) over the same duration - see PORTRAIT_CYCLE_MS in js/fx.js.
    """
    print("portraits (8-frame idle strips)")
    FW, FH = 240, 360
    for ch in ("duke", "assassin", "captain", "ambassador", "contessa"):
        d = os.path.join(PACK, "source_fullcolor", "portraits", ch)
        files = sorted(os.path.join(d, f) for f in os.listdir(d)
                       if f.lower().endswith(".png"))
        frames = [Image.open(f).convert("RGBA") for f in files]
        _save(strip(frames, FW, FH), os.path.join(OUT, "portraits", ch + ".webp"),
              72, _src_total(files))


def effects():
    """E1-E7 action-effect filmstrips, magenta un-keyed to alpha.

    E8_card_flip is deliberately NOT shipped: the web does the flip with a CSS
    3D transform on the real card art, which is sharper than a 12-frame
    pre-rendered strip and costs no bytes.
    """
    print("effects (filmstrips, magenta -> alpha)")
    jobs = {
        "E1_coup": (224, 224), "E2_assassinate": (224, 224),
        "E3_steal": (224, 112), "E4_tax": (144, 144),
        "E5_exchange": (224, 112), "E6_block": (224, 224),
        "E7_challenge": (224, 224),
    }
    for name, (fw, fh) in jobs.items():
        d = os.path.join(PACK, "source_fullcolor", "effects", name)
        files = sorted(os.path.join(d, f) for f in os.listdir(d)
                       if f.lower().endswith(".png"))
        frames = [unkey_magenta(Image.open(f)) for f in files]
        key = name.split("_", 1)[1]
        _save(strip(frames, fw, fh), os.path.join(OUT, "fx", key + ".webp"), 80,
              _src_total(files))
        _report.append(("#frames:" + key, 0, len(files)))


def ui():
    """Coin stacks, treasury, VICTORY/DEFEAT plates, skull/shield/crown."""
    print("ui")
    jobs = {
        "U1_coin1": ("coin1", 80), "U2_coin2": ("coin2", 80),
        "U3_coin3": ("coin3", 80), "U4_coin5": ("coin5", 120),
        "U5_coin10": ("coin10", 120), "U6_treasury": ("treasury", 160),
        "U7_victory": ("victory", 640), "U8_defeat": ("defeat", 640),
        "U9_skull": ("skull", 80), "U10_shield": ("shield", 80),
        "U11_question": ("question", 80), "U12_crown": ("crown", 80),
    }
    for src_name, (key, w) in jobs.items():
        src = os.path.join(PACK, "source_fullcolor", "ui", src_name + ".png")
        im = unkey_magenta(Image.open(src))
        _save(_fit(im, w), os.path.join(OUT, "ui", key + ".webp"), 82,
              os.path.getsize(src))


def music():
    """Re-encode the BGM.

    rebellion.mp3 ships at 200 kbps / 48 kHz - 9.8 MB for one looping track,
    by far the largest single asset and a real cost on a phone. 96 kbps joint
    stereo is transparent enough for looping background music and halves it.
    Requires ffmpeg; skipped (with the original copied) if absent.
    """
    print("music")
    src = os.path.join(REPO, "web", "assets", "rebellion.mp3")
    dst = os.path.join(OUT, "rebellion.mp3")
    if not shutil.which("ffmpeg"):
        print("  ffmpeg not found - leaving the original in place")
        return
    tmp = dst + ".tmp.mp3"
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", src,
         "-codec:a", "libmp3lame", "-b:a", "96k", "-ar", "44100",
         "-joint_stereo", "1", tmp],
        check=True,
    )
    os.replace(tmp, dst)
    _emit(dst, os.path.getsize(src))


def prune_superseded():
    """Delete the legacy art that the official masters replace.

    Every file here has a strictly better official counterpart now shipping:
      *Portrait.png / *.mp4  -> portraits/<char>.webp   (8-frame official idle)
      CoupTitleScreen.png    -> bg/title.webp
      gamescreen.png         -> bg/game_table.webp
      Waiting_Room2.png      -> bg/lobby.webp
      winscreen.png          -> bg/victory.webp
      Coup Game Over.png     -> bg/defeat.webp + ui/defeat.webp
      CardBackDesign.png     -> cards/back.webp
      GoldCoinIconAndBg.png  -> ui/coin1.webp

    ContessaBoobs.mp4 is KEPT: it backs an existing easter egg and the official
    pack has no equivalent, so removing it would drop shipped functionality.
    """
    print("pruning superseded legacy art")
    doomed = [
        "AmbassadorPortrait.png", "AssassinPortrait.png", "CaptainPortrait.png",
        "ContessaPortrait.png", "DukePortrait.png",
        "Ambassador.mp4", "Assassin.mp4", "Captain.mp4", "Contessa.mp4",
        "Duke.mp4",
        "CardBackDesign.png", "CoupTitleScreen.png", "Coup Game Over.png",
        "GoldCoinIconAndBackgroundTile.png", "Waiting_Room2.png",
        "gamescreen.png", "winscreen.png",
    ]
    freed = 0
    for f in doomed:
        p = os.path.join(OUT, f)
        if os.path.exists(p):
            freed += os.path.getsize(p)
            os.remove(p)
    print(f"  freed {freed/1024/1024:.1f} MB of superseded legacy art")


def main():
    if not os.path.isdir(PACK):
        sys.exit(f"asset pack not found: {PACK}")
    os.makedirs(OUT, exist_ok=True)

    backgrounds()
    logo()
    cards()
    portraits()
    effects()
    ui()
    music()
    prune_superseded()

    rows = [r for r in _report if not r[0].startswith("#")]
    src = sum(r[1] for r in rows)
    dst = sum(r[2] for r in rows)
    print(f"\nconverted {len(rows)} outputs: "
          f"{src/1024/1024:.1f} MB of masters -> {dst/1024/1024:.2f} MB")
    total = sum(
        os.path.getsize(os.path.join(dp, f))
        for dp, _, fs in os.walk(OUT) for f in fs
    )
    print(f"web-staging/assets total: {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    main()
