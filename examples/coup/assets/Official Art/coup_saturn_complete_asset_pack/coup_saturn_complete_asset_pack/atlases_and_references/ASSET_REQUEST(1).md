# Coup Saturn — Asset Generation Request

Shot-for-shot list of every image needed, with the hardware constraints each
one must satisfy. Pair this with the reference art in
`examples/coup/assets/Official Art/` — generate **in that established style**:
sci-fi renaissance, painted realism, gold filigree on deep jewel tones,
purple/magenta/blue/gold house colours.

---

## Read this first — it decides what is worth generating

**Generate at 4× target size, minimum.** All art is downscaled hard. Supplying
1280×896 for a 320×224 background gives a clean result; supplying 400×280 does
not. Bigger is strictly better, there is no upper limit that hurts.

**Aspect ratio is 320:224 (10:7) for anything full-screen.** On a CRT this
displays as 4:3, so faces stretch slightly vertically. Composing at 10:7 is
correct; do not letterbox.

**Colour budget is brutal and non-negotiable.**
- Backgrounds get **255 colours** for the whole image.
- Sprites get **15 colours** each, plus transparency.
Art with smooth gradients across many hues quantizes badly. Art with a
dominant hue family and strong value contrast survives beautifully — the
council chamber reference is a good example, the busy city skyline is a
weaker one.

**Memory ceiling: about 242 KB remains.** Each full-screen background costs
131,072 bytes. **Only ONE more background fits** unless we add CD streaming.
So of the backgrounds listed below, mark your top priority — or accept that
extras ship later. Sprites are far cheaper and can nearly all fit.

**Nothing may rely on transparency inside a character.** The previous portraits
faded to black at the edges; the converter read that as transparent and the
background showed through the characters' bodies. Every sprite must be either
fully opaque or have a clean, unambiguous silhouette against a flat,
distinctly-coloured backing (pure magenta `#FF00FF` is ideal — it will be keyed
out).

---

## 1. Backgrounds — full screen

Each: **1280×896 px** (downscales to 320×224), 255 colours after conversion.

Composition rule: **the centre 60% must stay visually quiet.** UI panels, text
and cards are drawn on top. Detail belongs at the edges and corners; a busy
centre makes text illegible. The council chamber reference does this correctly
— the table surface is a calm mid-tone.

| # | Screen | Shot description |
|---|---|---|
| B1 | **Title** | Wide establishing shot. A palace balcony or plaza overlooking a sci-fi renaissance city at golden hour. Strong horizontal composition, calm sky across the upper third for the logo, quiet paving across the lower third for the menu. Warm gold and amber. |
| B2 | **Game table** ✅ *(have — council chamber)* | Already converted from reference. Only replace if you want a version with a quieter centre. |
| B3 | **Lobby / waiting room** | Antechamber outside the council hall. Seven empty high-backed chairs arranged along the sides, receding. Centre floor deliberately bare — player slots are drawn there. Cooler palette than the table: slate blue, dull silver, muted gold. |
| B4 | **Connecting / dialing** | Communications alcove. A single figure silhouetted at a glowing console, back to camera, transmission beam rising. Mostly dark with one strong light source. Vertical emphasis. Deep blue and cyan with a hot white core. |
| B5 | **Victory** | Throne room, low angle looking up at an occupied throne bathed in light from above. Triumphant, warm gold and cream, banners either side. Upper-centre must stay clear for a VICTORY banner. |
| B6 | **Defeat** | Same throne room, reversed. High angle looking down at an empty throne, cold light, banners fallen, dust in the air. Desaturated blues and greys with one dying ember of red. |
| B7 | **Rules / reference** | Library or archive wall — shelves, scrolls, a projected schematic. Deliberately flat and low-contrast; this screen is dense with text and the background must not compete. |

---

## 2. Character portraits — animated

**5 characters: Duke, Assassin, Captain, Ambassador, Contessa.**

Per character: **8 frames** of subtle idle animation.
Each frame: **512×768 px** (downscales to 64×96), 15 colours, **fully opaque**.

Shot: head-and-shoulders bust, centred, facing camera or three-quarter.
Match the faces in `Cards.png` exactly — same person, same costume, same house
colour. Each portrait sits on its own painted backing (their card's background
works well); the backing must fill the frame edge to edge with no fade to black.

Animation: **very subtle.** Breathing, a slow blink, cloth or hair drift, a
flicker of light across metal. Frame 8 must lead cleanly back into frame 1 —
generate as a seamless loop. Large movement reads as juddering at this size.

House colours, for consistency with the cards:
Duke gold/purple · Assassin steel/black · Captain blue · Ambassador
green/orange · Contessa red.

---

## 3. Card faces and back

| # | Asset | Size | Notes |
|---|---|---|---|
| C1 | Card back | 384×576 → **48×72** | The COUP emblem from `Card Back.png`, simplified. It shrinks to 48×72, so fine linework disappears — bold shapes only. |
| C2-C6 | Card face, one per character | 384×576 → **48×72** | Portrait plus house-colour border. Legible as a *silhouette* at 48×72; assume the name text will be unreadable and drawn separately. |

---

## 4. UI sprites and icons

All on **flat magenta `#FF00FF`** so the silhouette keys out cleanly.
15 colours each.

| # | Asset | Size | Notes |
|---|---|---|---|
| U1 | Coin ×1 | 128×128 → **16×16** | Single coin, face on |
| U2 | Coin stack ×2 | 128×128 → **16×16** | |
| U3 | Coin stack ×3 | 128×128 → **16×16** | |
| U4 | Coin stack ×5 | 192×128 → **24×16** | |
| U5 | Coin stack ×10 | 192×128 → **24×16** | |
| U6 | Treasury pile | 256×192 → **32×24** | For the centre pot |
| U7 | VICTORY banner | 1024×256 → **128×32** | Gold ribbon, bold |
| U8 | DEFEAT plate | 1024×256 → **128×32** | Cracked red/iron |
| U9 | Skull marker | 128×128 → **16×16** | Eliminated player |
| U10 | Shield marker | 128×128 → **16×16** | Blocked action |
| U11 | Question marker | 128×128 → **16×16** | Challenge prompt |
| U12 | Crown | 128×128 → **16×16** | Current leader |

---

## 5. Action effect animations

These play over the table when an action resolves. Each is a **sprite sequence
on flat magenta**, 15 colours, designed to read in under a second.

| # | Action | Frames | Size | Shot |
|---|---|---|---|---|
| E1 | **Coup** | 8 | 512×512 → **64×64** | Heavy gold impact burst, shockwave ring expanding outward then fading |
| E2 | **Assassinate** | 8 | 512×512 → **64×64** | A blade-slash arc, crimson, sweeping across then dissipating |
| E3 | **Steal** | 6 | 512×256 → **64×32** | Coins arcing from right to left, trailing sparkle |
| E4 | **Tax / Income** | 6 | 256×256 → **32×32** | Coins dropping into frame and settling, gold glint |
| E5 | **Exchange** | 8 | 512×256 → **64×32** | Two cards crossing and swapping places |
| E6 | **Block** | 6 | 512×512 → **64×64** | Blue shield flaring into existence, holding, fading |
| E7 | **Challenge** | 6 | 512×512 → **64×64** | White exclamation burst with radiating spokes |
| E8 | **Card flip** | 12 | 384×576 → **48×72** | A card rotating on its vertical axis, back to face. If awkward to generate, skip — VDP1 can do this in hardware from the card art alone. |

---

## 6. Logo and splash

| # | Asset | Size | Notes |
|---|---|---|---|
| L1 | COUP wordmark | 1024×256 → **256×64** | The existing one works; a version with more internal contrast would survive 15 colours better |
| L2 | Studio/boot splash | 1280×896 → **320×224** | Optional. Shown for ~2 seconds at boot |

---

## Priority, given the memory ceiling

If only some of this gets made, this is the order that improves the game most:

1. **Character portraits (§2)** — on screen constantly, and currently the
   weakest element
2. **Effect animations E1, E2, E6, E7** — coup, assassinate, block, challenge
   are the dramatic beats of the game and have no visuals at all today
3. **Coin stacks U1-U6** — coins are displayed permanently for every player
4. **VICTORY / DEFEAT (U7, U8)** — the current game-over screen is weak
5. **One background** — B3 lobby or B5 victory. Only one more fits.

---

## Delivery format

PNG, no alpha channel needed except where noted (use flat magenta instead).
Name files by the ID in the tables — `B1_title.png`, `U4_coin5.png`,
`E1_coup_f01.png` … `E1_coup_f08.png`. Animation frames numbered from 1 with
zero padding.

Drop them anywhere under `examples/coup/assets/` and I will convert, verify
against the hardware constraints, and adversarially QA every one before it
ships.
