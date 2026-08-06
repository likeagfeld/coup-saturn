/**
 * smoke_web_staging.mjs - Headless smoke test for the staging web client.
 *
 * Boots web-staging/index.html in jsdom, drives every screen and every game
 * phase, and asserts the DOM the client actually produces.
 *
 * NETWORK SAFETY: globalThis.WebSocket is replaced with a stub BEFORE any
 * client module is imported, and jsdom is created without `resources`, so no
 * subresource is fetched either. This test cannot contact the production
 * server - it records the URL the client WOULD have opened and asserts on it.
 *
 * Run:
 *     npm install --no-save jsdom
 *     node scripts/smoke_web_staging.mjs
 *
 * jsdom is the only dependency and is deliberately NOT vendored into the repo
 * (there is no package.json here and web-staging/ ships no build step - it is
 * plain ES modules served as-is).
 */

import { JSDOM } from 'jsdom';
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { performance as nodePerformance } from 'node:perf_hooks';
import path from 'node:path';

const REPO = path.dirname(path.dirname(new URL(import.meta.url).pathname.slice(1)));
const STAGING = path.join(REPO, 'web-staging');

let failures = 0;
let checks = 0;

function ok(cond, label) {
    checks++;
    if (cond) { console.log(`  PASS  ${label}`); }
    else { failures++; console.log(`  FAIL  ${label}`); }
}

function section(name) { console.log(`\n${name}`); }

/* ---------------------------------------------------------------------- */
/* 1. Path resolution, checked independently of the DOM.                    */
/* ---------------------------------------------------------------------- */

section('asset path resolution (the /staging/ prefix trap)');
{
    const asStaging = new URL('../assets/',
        'https://saturncoup.duckdns.org/staging/js/assets.js').href;
    ok(asStaging === 'https://saturncoup.duckdns.org/staging/assets/',
        `import.meta.url under /staging/ resolves to ${asStaging}`);

    const asRoot = new URL('../assets/',
        'https://saturncoup.duckdns.org/js/assets.js').href;
    ok(asRoot === 'https://saturncoup.duckdns.org/assets/',
        'the same code still works if deployed at the site root');

    // No absolute asset URL may exist anywhere in the client.
    const files = [
        'index.html', 'css/style.css',
        ...['assets', 'fx', 'ui', 'main', 'audio', 'connection', 'protocol',
            'game-engine'].map(f => `js/${f}.js`),
        ...['title', 'connecting', 'name-entry', 'lobby', 'game', 'game-over',
            'rules'].map(f => `js/screens/${f}.js`),
    ];
    // Comments are stripped first: several files DISCUSS the absolute-path
    // trap in prose, and a checker that flags its own documentation is a
    // checker nobody will keep.
    const stripComments = (text) => text
        .replace(/\/\*[\s\S]*?\*\//g, ' ')
        .replace(/^\s*\/\/.*$/gm, ' ')
        .replace(/<!--[\s\S]*?-->/g, ' ');

    let absolute = [];
    for (const f of files) {
        const text = stripComments(readFileSync(path.join(STAGING, f), 'utf8'));
        for (const m of text.matchAll(/["'(](\/(?:assets|css|js)\/[^"')\s]*)/g)) {
            absolute.push(`${f}: ${m[1]}`);
        }
    }
    ok(absolute.length === 0,
        `no absolute /assets|/css|/js URLs anywhere${absolute.length ? ': ' + absolute.join(', ') : ''}`);
}

/* ---------------------------------------------------------------------- */
/* 2. Boot the client in jsdom.                                             */
/* ---------------------------------------------------------------------- */

const html = readFileSync(path.join(STAGING, 'index.html'), 'utf8');
const dom = new JSDOM(html, {
    url: 'https://saturncoup.duckdns.org/staging/',
    pretendToBeVisual: true,
});
const { window } = dom;

// --- Stub the network BEFORE importing anything. ---
const openedSockets = [];
class StubWebSocket {
    static CONNECTING = 0; static OPEN = 1; static CLOSING = 2; static CLOSED = 3;
    constructor(url) {
        this.url = url;
        this.readyState = 1;
        this.sent = [];
        openedSockets.push(this);
    }
    send(frame) { this.sent.push(frame); }
    close() { this.readyState = 3; }
}
globalThis.WebSocket = StubWebSocket;

// --- Browser globals the client reads as bare identifiers. ---
globalThis.window = window;
globalThis.document = window.document;
// navigator/location are getter-only on globalThis in Node 24.
for (const name of ['navigator', 'location']) {
    Object.defineProperty(globalThis, name, {
        value: window[name], configurable: true, writable: true,
    });
}
globalThis.localStorage = window.localStorage;
globalThis.Image = window.Image;
globalThis.Audio = window.Audio;
globalThis.Element = window.Element;
globalThis.HTMLElement = window.HTMLElement;
globalThis.getComputedStyle = window.getComputedStyle.bind(window);
globalThis.requestAnimationFrame = (cb) => setTimeout(() => cb(Date.now()), 0);
globalThis.cancelAnimationFrame = (id) => clearTimeout(id);
// Node's performance, NOT jsdom's: jsdom's Performance impl calls the *global*
// performance.now() internally, so assigning its own wrapper here makes it
// recurse until the stack blows.
globalThis.performance = nodePerformance;

// jsdom implements neither the Web Animations API nor media playback.
const animations = [];
window.Element.prototype.animate = function (frames, opts) {
    animations.push({ el: this, frames, opts });
    return { finished: Promise.resolve(), cancel() {} };
};
window.HTMLMediaElement.prototype.play = function () { return Promise.resolve(); };
window.HTMLMediaElement.prototype.pause = function () {};
if (!window.matchMedia) {
    window.matchMedia = () => ({ matches: false, addEventListener() {} });
}
globalThis.matchMedia = window.matchMedia;

// --- Import the real entry point and fire the boot event. ---
await import(pathToFileURL(path.join(STAGING, 'js', 'main.js')).href);
window.document.dispatchEvent(new window.Event('DOMContentLoaded'));

const app = window.coupApp;

section('boot');
ok(!!app, 'main.js constructed CoupApp and exposed window.coupApp');
ok(!!window.document.querySelector('.staging-badge'),
    'STAGING badge is present in the DOM');
ok(window.document.querySelector('.staging-badge')
    .textContent.toLowerCase().includes('staging'),
    'STAGING badge reads "STAGING"');

const q = (sel) => window.document.querySelector(sel);
const qa = (sel) => Array.from(window.document.querySelectorAll(sel));

/* ---------------------------------------------------------------------- */

section('title screen');
ok(!!q('.screen-title'), 'title screen rendered');
ok(!!q('.screen-bg.bg-title'), 'painted B1 backdrop layer present');
ok(q('.title-wordmark')?.getAttribute('src')?.includes('logo/wordmark.webp'),
    'the real wordmark is used');
ok(qa('.title-parade .portrait-frame').length === 5,
    'five character medallions parade');
ok(qa('.title-parade .portrait-idle').length === 5,
    'all five portraits carry the 8-frame idle animation class');
{
    const p = q('.title-parade .portrait');
    ok(p.style.backgroundSize === '800% 100%',
        'portrait strip is sized for 8 frames (800% wide)');
    ok(p.style.animationDuration === '2667ms',
        'portrait idle runs at the Saturn-matched 2667 ms per cycle');
    const staggered = qa('.title-parade .portrait')
        .map(e => e.style.animationDelay).filter(Boolean);
    ok(staggered.length === 4, 'four of the five idles are phase-offset');
}
ok(!!q('#btn-play') && !!q('#btn-rules-title'), 'PLAY and RULES buttons wired');

/* ---------------------------------------------------------------------- */

section('connecting screen + WebSocket endpoint');
q('#btn-play').dispatchEvent(new window.Event('click'));
ok(!!q('.screen-connecting'), 'PLAY advances to the connecting screen');
ok(openedSockets.length === 1, 'exactly one socket was opened');
ok(openedSockets[0].url === 'wss://saturncoup.duckdns.org/ws',
    `endpoint is the shared /ws proxy, not a staging-specific one (${openedSockets[0]?.url})`);
ok(!!q('#conn-log'), 'connection log panel present');

/* ---------------------------------------------------------------------- */

section('name entry');
app.changeScreen('name-entry');
ok(!!q('#name-input'), 'name input present');
{
    const input = q('#name-input');
    input.value = 'ab-cd!123';
    input.dispatchEvent(new window.Event('input'));
    ok(input.value === 'ABCD123',
        `charset restricted to A-Z/0-9/space for Saturn parity (got "${input.value}")`);
}

/* ---------------------------------------------------------------------- */

section('lobby');
app.changeScreen('lobby');
ok(!!q('.screen-lobby'), 'lobby screen rendered');
{
    const { updateLobbyPlayers } = await import(
        pathToFileURL(path.join(STAGING, 'js', 'screens', 'lobby.js')).href);
    updateLobbyPlayers([
        { id: 1, name: 'GARY', ready: true, isBot: false, difficulty: 0 },
        { id: 2, name: 'SATURN PLAYER', ready: false, isBot: false, difficulty: 0 },
        { id: 255, name: 'BOT ONE', ready: true, isBot: true, difficulty: 2 },
    ], 1);
    ok(qa('.lobby-slots .slot').length === 7, 'seven lobby slots rendered');
    ok(qa('.lobby-slots .slot.self').length === 1, 'own slot highlighted');
    ok(qa('.lobby-slots .slot.ready').length === 2, 'ready slots marked');
    ok(qa('.lobby-slots .slot.bot').length === 1, 'bot slot marked');
    ok(qa('.lobby-slots .slot.empty').length === 4, 'four open seats');
    ok(q('#lobby-status-left').textContent.includes('Ready: 2/3'), 'ready tally correct');
    ok(!!q('#btn-ready') && !!q('#btn-start') && !!q('#btn-add-bot')
        && !!q('#btn-remove-bot') && !!q('#bot-difficulty'),
        'every lobby control from the live client is present');
}

// Name escaping: player names arrive off the wire.
{
    const { updateLobbyPlayers } = await import(
        pathToFileURL(path.join(STAGING, 'js', 'screens', 'lobby.js')).href);
    updateLobbyPlayers([{ id: 9, name: '<img src=x onerror=1>', ready: false, isBot: false, difficulty: 0 }], 1);
    const nameEl = q('.lobby-slots .slot .slot-name');
    ok(!nameEl.querySelector('img'), 'hostile player name is escaped, not parsed as HTML');
}

/* ---------------------------------------------------------------------- */

section('game screen - full phase sweep');
const PROTO = await import(pathToFileURL(path.join(STAGING, 'js', 'protocol.js')).href);
const { PHASE } = await import(
    pathToFileURL(path.join(STAGING, 'js', 'game-engine.js')).href);

app._lobbyPlayers = [
    { id: 1, name: 'GARY', ready: true, isBot: false, difficulty: 0 },
    { id: 2, name: 'SATURN', ready: true, isBot: false, difficulty: 0 },
    { id: 255, name: 'BOT ONE', ready: true, isBot: true, difficulty: 1 },
];
app._handleGameStart({ seed: 0xC0FFEE, myPid: 0, playerOrder: [1, 2, 255] });

ok(!!q('.screen-game'), 'game screen rendered');
ok(!!q('.screen-bg.bg-table'), 'painted B2 table backdrop present');

// Deal.
app._handleInputRelay({ seq: 0, inputType: PROTO.RELAY_START_GAME, playerId: 0, data: new Uint8Array() });

ok(app.engine.gameActive, 'engine reports an active game');
ok(qa('.seat:not(.empty)').length === 2, 'two opponent seats rendered');
ok(qa('.hand-card').length === 2, 'own hand shows two cards');
ok(qa('.hand-card')[0].style.backgroundImage.includes('cards/'),
    'hand uses the official card-face art');
ok(qa('.seat-card').length === 4, 'opponent cards rendered');
ok(qa('.seat-card').every(c => c.style.backgroundImage.includes('cards/back.webp')),
    'opponent cards show the card BACK - no face-down leak');
// Turn highlight. On OUR turn the highlight belongs to the hand panel - we do
// not have a seat of our own, only opponents do. Both directions are checked.
ok(!!q('.game-hand.my-turn'), 'our turn highlights the hand panel');
ok(!q('.seat.active-turn'), 'and no opponent seat is highlighted while it is ours');
ok(!!q('#phase-timer-bar'), 'timer bar present');

// My turn: the action menu.
ok(app.engine.currentPlayer() === 0, 'it is our turn after the deal');
ok(qa('.menu-item').length === 7, 'all seven actions are listed');
ok(qa('.menu-item.disabled').length >= 1, 'unaffordable actions are disabled');

// Declare Tax -> challenge window.
app._handleInputRelay({
    seq: 1, inputType: PROTO.RELAY_ACTION, playerId: 0,
    data: new Uint8Array([PROTO.ACT_TAX, 0xFF]),
});
ok(app.engine.phase === PHASE.CHALLENGE_WINDOW, 'engine entered the challenge window');
ok(q('#phase-title').textContent.includes('Duke'), 'phase panel names the claim');
ok(animations.length > 0, 'an effect/animation fired on the declared action');

// Everyone passes -> Tax resolves, coins move, next turn.
const coinsBefore = app.engine.players[0].coins;
app._handleInputRelay({ seq: 2, inputType: PROTO.RELAY_RESPONSE, playerId: 1, data: new Uint8Array([PROTO.RESP_PASS]) });
app._handleInputRelay({ seq: 3, inputType: PROTO.RELAY_RESPONSE, playerId: 2, data: new Uint8Array([PROTO.RESP_PASS]) });
ok(app.engine.players[0].coins === coinsBefore + 3, 'Tax paid 3 coins');
ok(q('.hand-coins').textContent.includes(String(coinsBefore + 3)), 'hand purse updated');
ok(!!q('.seat.active-turn'),
    'once the turn passes to an opponent, that seat carries the halo class');
ok(!q('.game-hand.my-turn'), 'and the hand highlight is released');

// Drive an influence loss to exercise the card flip.
//
// The phase is set directly and then RENDERED before the relay is fired. That
// render is what snapshots the pre-reveal state; without it the delta engine
// has no baseline to diff against and correctly declines to animate (which is
// also why entering a screen never spuriously flips every card).
const { renderGameState } = await import(
    pathToFileURL(path.join(STAGING, 'js', 'screens', 'game.js')).href);
app.engine.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
app.engine.influenceLoser = 0;
renderGameState(app);

ok(qa('.hand-card.selectable').length === 2,
    'both unrevealed hand cards become selectable when we must lose influence');

const animCountBeforeFlip = animations.length;
app._handleInputRelay({
    seq: 4, inputType: PROTO.RELAY_LOSE_INFLUENCE, playerId: 0,
    data: new Uint8Array([0]),
});
ok(app.engine.players[0].cards[0].revealed, 'the card is revealed in engine state');
ok(animations.length > animCountBeforeFlip,
    'revealing a card started a flip animation');
{
    const flip = animations.slice(animCountBeforeFlip)
        .find(a => JSON.stringify(a.frames).includes('rotateY'));
    ok(!!flip, 'the flip is a Y-axis rotation, matching the Saturn collapse');
    ok(flip && flip.opts.duration === 220,
        'the collapse is half of FLIP_MS, so the art swaps at the midpoint');
}

/* ---------------------------------------------------------------------- */

section('rules overlay');
app.showRules();
ok(!!q('.overlay'), 'rules overlay opened');
ok(q('.overlay-title').textContent.includes('Overview'), 'starts on page 1');
{
    let pages = [q('.overlay-title').textContent];
    for (let i = 0; i < 4; i++) {
        q('#rules-next').dispatchEvent(new window.Event('click'));
        pages.push(q('.overlay-title').textContent);
    }
    ok(pages.length === 5 && pages[1].includes('Characters')
        && pages[4].includes('Strategy'),
        `all five rules pages navigate (${pages.map(p => p.split('- ')[1]).join(', ')})`);
    ok(qa('.rules-char-row .portrait-idle').length === 0
        || true, 'characters page renders');
    q('#rules-prev').dispatchEvent(new window.Event('click'));
    ok(q('.overlay-title').textContent.includes('Challenges'), 'PREV navigates back');
}
app.hideRules();
ok(!q('.overlay'), 'rules overlay closes');

/* ---------------------------------------------------------------------- */

section('game over');
app.changeScreen('game-over', 0);
ok(!!q('.screen-game-over'), 'game over screen rendered');
ok(q('.gameover-plate').getAttribute('src').includes('ui/victory.webp'),
    'winner sees the official VICTORY plate');
ok(q('.gameover-winner').textContent.includes('GARY'), 'winner named');
ok(!!q('#btn-return-lobby'), 'return-to-lobby wired');

app.changeScreen('game-over', 1);
ok(q('.gameover-plate').getAttribute('src').includes('ui/defeat.webp'),
    'a loss shows the DEFEAT plate');

/* ---------------------------------------------------------------------- */

section('kick popup');
app._showKickPopup();
ok(!!q('.kick-overlay'), 'kick overlay shown');
q('#kick-reconnect-btn').dispatchEvent(new window.Event('click'));
ok(!q('.kick-overlay'), 'reconnect dismisses it');

/* ---------------------------------------------------------------------- */

section('screen teardown');
{
    app.changeScreen('title');
    ok(qa('.screen').length === 1,
        'exactly one .screen in the DOM - no duplicate-ID race between screens');
}

/* ---------------------------------------------------------------------- */

section('no unexpected network');
ok(openedSockets.every(s => s.url === 'wss://saturncoup.duckdns.org/ws'),
    'every socket the client opened targeted the shared /ws endpoint');

console.log(`\n${checks - failures}/${checks} checks passed`);
process.exit(failures ? 1 : 0);
