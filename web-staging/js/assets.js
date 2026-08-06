/**
 * assets.js - Asset registry for the staging client.
 *
 * PATH SAFETY (this is the whole reason this file exists):
 *
 * The staging client is served from https://saturncoup.duckdns.org/staging/
 * while the LIVE client keeps the site root. An absolute URL like
 * "/assets/bg/title.webp" would resolve against the live root and silently
 * serve the OLD art from a staging URL - the one failure that makes staging
 * look like it works while you are testing the wrong build.
 *
 * So every path here is derived from `import.meta.url`, which is the absolute
 * URL of THIS module. Under /staging/js/assets.js that yields
 * /staging/assets/... ; from a local file:// or a root deploy it yields the
 * matching sibling. There is no configuration to get wrong and no absolute
 * path anywhere in the client.
 *
 * CSS does the same trick structurally: every url() in style.css is written
 * "../assets/..." , relative to /staging/css/style.css.
 */

export const ASSET_BASE = new URL('../assets/', import.meta.url).href;

/** Resolve an asset-relative path to an absolute URL. */
export function asset(path) {
    return ASSET_BASE + path;
}

/* --- Painted per-screen backdrops (B1-B7, the Saturn NBG1 scenes) --- */
export const BG = {
    title: asset('bg/title.webp'),
    table: asset('bg/game_table.webp'),
    lobby: asset('bg/lobby.webp'),
    connecting: asset('bg/connecting.webp'),
    victory: asset('bg/victory.webp'),
    defeat: asset('bg/defeat.webp'),
    rules: asset('bg/rules.webp'),
};

export const LOGO = {
    wordmark: asset('logo/wordmark.webp'),
    boxart: asset('logo/boxart.webp'),
};

/* --- Character identity ---------------------------------------------------
 * Index matches the CHAR_* constants in protocol.js: 0=Duke .. 4=Contessa.
 * The colours are the same hues the Saturn UI uses per character.
 */
export const CHARACTERS = [
    { key: 'duke',       name: 'Duke',       color: '#A050D0' },
    { key: 'assassin',   name: 'Assassin',   color: '#C03030' },
    { key: 'captain',    name: 'Captain',    color: '#3060C0' },
    { key: 'ambassador', name: 'Ambassador', color: '#30A040' },
    { key: 'contessa',   name: 'Contessa',   color: '#D09020' },
];

/** Card face art (the official 6-face set). `null` character -> the back. */
export function cardArt(character) {
    const c = CHARACTERS[character];
    return asset('cards/' + (c ? c.key : 'back') + '.webp');
}

export const CARD_BACK = asset('cards/back.webp');

/** 8-frame idle strip for a character portrait. */
export function portraitStrip(character) {
    const c = CHARACTERS[character];
    return c ? asset('portraits/' + c.key + '.webp') : null;
}

export const PORTRAIT_FRAMES = 8;

/* --- Action effect filmstrips (E1-E7) -------------------------------------
 * frames = how many cells are packed across the strip; the CSS steps() count
 * must match exactly or the animation shows slivers of two frames at once.
 */
export const FX = {
    coup:        { src: asset('fx/coup.webp'),        frames: 8, ratio: 1 },
    assassinate: { src: asset('fx/assassinate.webp'), frames: 8, ratio: 1 },
    steal:       { src: asset('fx/steal.webp'),       frames: 6, ratio: 2 },
    tax:         { src: asset('fx/tax.webp'),         frames: 6, ratio: 1 },
    exchange:    { src: asset('fx/exchange.webp'),    frames: 8, ratio: 2 },
    block:       { src: asset('fx/block.webp'),       frames: 6, ratio: 1 },
    challenge:   { src: asset('fx/challenge.webp'),   frames: 6, ratio: 1 },
};

export const UI = {
    coin1: asset('ui/coin1.webp'),
    coin2: asset('ui/coin2.webp'),
    coin3: asset('ui/coin3.webp'),
    coin5: asset('ui/coin5.webp'),
    coin10: asset('ui/coin10.webp'),
    treasury: asset('ui/treasury.webp'),
    victory: asset('ui/victory.webp'),
    defeat: asset('ui/defeat.webp'),
    skull: asset('ui/skull.webp'),
    shield: asset('ui/shield.webp'),
    question: asset('ui/question.webp'),
    crown: asset('ui/crown.webp'),
};

/** Pick the coin-stack sprite that best represents a purse size. */
export function coinIcon(coins) {
    if (coins >= 10) return UI.coin10;
    if (coins >= 5) return UI.coin5;
    if (coins >= 3) return UI.coin3;
    if (coins >= 2) return UI.coin2;
    return UI.coin1;
}

export const BGM = asset('rebellion.mp3');

/* The Contessa easter egg. Kept from the live client - the official pack has
 * no equivalent, so dropping it would remove shipped functionality. */
export const EASTER_EGG_VIDEO = asset('ContessaBoobs.mp4');

/**
 * Warm the browser cache for the art a screen is about to need, so a scene
 * transition does not fade in onto a blank panel. Fire-and-forget.
 */
export function preload(urls) {
    for (const u of urls) {
        if (!u) continue;
        const img = new Image();
        img.decoding = 'async';
        img.src = u;
    }
}
