# Coup for Sega Saturn (NetLink)

**The first publicly released Sega Saturn NetLink homebrew game.**

A full Saturn port of the popular bluffing-and-deduction card game **Coup**, playable online via the Sega NetLink modem against other Saturn players — or cross-play against players on web browsers and mobile phones.

---

## Download

Grab the latest build from the [**Releases**](../../releases) page:

- [**Coup - Beta Build - 1.0**](../../releases/latest) — the current public beta CD image (CUE / BIN / WAV).

Burn the image to a CD-R or load it in your preferred Saturn optical drive emulator (ODE).

---

## Cross-Play

You don't need a Saturn to play. The same live server hosts web and mobile clients at:

**https://saturncoup.duckdns.org**

Saturn, PC browsers, and phones all share the same lobby and game tables. Challenge your friends regardless of which platform they're on.

---

## Game Overview

Coup is a game of bluffing, deduction, and political survival set in a dystopian future where the government is run by a corrupt oligarchy.

Each player starts with **2 coins** and **2 hidden character cards** drawn from the royal court. Every character grants a unique ability — but you don't have to actually hold a character to *claim* its power. Lie convincingly and you win. Get caught bluffing (or wrongly accuse someone) and you lose an influence card.

When you lose both of your cards, you are out of the game.

**The last player with any influence remaining wins.**

---

## The Characters

| Character | Ability | Blocks |
|-----------|---------|--------|
| **Duke** | Take 3 coins from the treasury (Tax) | Blocks Foreign Aid |
| **Assassin** | Pay 3 coins to force a player to lose an influence | — |
| **Captain** | Steal 2 coins from another player | Blocks Stealing |
| **Ambassador** | Draw 2 cards from the court, keep 0–2, return the rest | Blocks Stealing |
| **Contessa** | — | Blocks Assassination |

---

## General Actions (no character required)

- **Income** — Take 1 coin.
- **Foreign Aid** — Take 2 coins. *(Can be blocked by a Duke.)*
- **Coup** — Pay 7 coins to force any player to lose an influence. **Required if you have 10 or more coins.**

---

## How a Turn Works

1. The active player chooses an action.
2. Other players may **challenge** any claimed character ability, or **block** with their own claimed character.
3. If challenged, the acting player must reveal the claimed card or lose an influence.
4. If a block is challenged and proven false, the blocker loses an influence.
5. Action resolves (or is cancelled) and play passes to the next player.

Bluff early. Bluff often. And remember — everyone else is bluffing too.

---

## How to Connect (Sega Saturn NetLink)

The Saturn NetLink connects to the game server via a real-world phone number that is tunneled to the internet. There are two supported ways to do this:

### Option 1 — DreamPi (recommended)

The easiest path. A Raspberry Pi running [DreamPi](https://dreamcast.online/dreampi/) acts as a virtual phone line that the Saturn modem dials into.

**Important: make sure your DreamPi is on the latest build before connecting.**

1. Power down your DreamPi and remove the SD card.
2. Mount the SD card's boot volume on a PC.
3. **Delete the file `noautoupdates.txt`** from the boot volume (if present).
4. Safely eject the SD card and reinsert it into the DreamPi.
5. Boot the DreamPi and let it fully auto-update. This may take several minutes and will reboot once or twice — be patient.
6. Once it's idle on the main screen, connect your Saturn's NetLink to the DreamPi's phone line and dial the game server's number (provided in-game).

### Option 2 — PC Tunnel (Eaudunord's NetLink project)

If you don't have a DreamPi, you can tunnel directly from a Windows PC using **Eaudunord's NetLink tunnel**:

**https://github.com/eaudunord/Netlink**

Follow the instructions in that repository to get your PC acting as a dial-up bridge for the Saturn NetLink modem, then dial the game server.

---

## Controls (Saturn)

- **D-Pad** — Navigate menus / select targets
- **A** — Confirm / act
- **B** — Back / cancel
- **C** — Challenge prompt shortcut
- **Start** — Pause / open menu

---

## Credits

Coup is designed by **Rikki Tahta** and published by **Indie Boards and Cards**. This homebrew is an unofficial fan implementation for the Sega Saturn NetLink and is not affiliated with or endorsed by the original publisher.

---

## Status

**Beta 1.0** — public beta. Feedback, bug reports, and connection logs are welcome via GitHub Issues.
