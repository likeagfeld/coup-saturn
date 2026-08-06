/**
 * game.js - The table.
 *
 * Saturn treatment (spec 4.2): B2_game_table backdrop, seats as gouraud
 * panels whose gradient carries the depth cue, card reveal as a Y-axis
 * trapezoid collapse with a texture swap at the midpoint, influence loss as a
 * flip to the back, a timer bar that ramps green->amber->red, coin payouts as
 * scaled sprite pops, and the current turn marked by an amber halo band.
 * Every one of those has an equivalent here; see js/fx.js.
 *
 * WIRE BEHAVIOUR IS UNCHANGED. Every send below is the same encoder with the
 * same arguments the live client uses:
 *   encodeAction(action, target)   encodeResponse(RESP_*)
 *   encodeBlockClaim(character)    encodeLoseInfluence(cardIndex)
 *   encodeExchangeChoice(a, b)
 * Nothing new is sent, nothing is sent at a different time, and the client
 * never sends a timeout - the server owns every clock.
 */

import {
    encodeAction, encodeResponse, encodeBlockClaim,
    encodeLoseInfluence, encodeExchangeChoice,
    ACT_COUP, ACT_ASSASSINATE, ACT_STEAL,
    ACT_INCOME, ACT_FOREIGN_AID, ACT_TAX, ACT_EXCHANGE,
    RESP_PASS, RESP_CHALLENGE, RESP_BLOCK,
    CHAR_DUKE, CHAR_CAPTAIN, CHAR_AMBASSADOR, CHAR_CONTESSA,
    RELAY_ACTION, RELAY_RESPONSE, RELAY_BLOCK_CLAIM, RELAY_LOSE_INFLUENCE,
} from '../protocol.js';
import { PHASE, CHARACTER_NAMES, ACTION_NAMES } from '../game-engine.js';
import {
    CHARACTERS, cardArt, CARD_BACK, coinIcon, UI, EASTER_EGG_VIDEO,
} from '../assets.js';
import {
    flipCard, coinArc, playEffect, startTimerBar, stopTimerBar,
    pulse, flash, ACTION_EFFECT,
} from '../fx.js';
import { esc, screenShell, fxLayer, applyPortrait } from '../ui.js';
import { audio } from '../audio.js';

const CHAR_CSS = ['duke', 'assassin', 'captain', 'ambassador', 'contessa'];

/* Response windows, in milliseconds.
 *
 * These MIRROR the server's windows (server.py: CHALLENGE/BLOCK = 12.0 s,
 * INFLUENCE/EXCHANGE = 30.0 s). They are display only - reaching zero sends
 * nothing. Keeping them equal to the server's means the bar empties when the
 * server actually acts, instead of inventing a second, disagreeing clock. */
const WINDOW_RESPONSE_MS = 12000;
const WINDOW_CHOICE_MS = 30000;

const BLOCKABLE_BY = {
    [ACT_FOREIGN_AID]: [CHAR_DUKE],
    [ACT_ASSASSINATE]: [CHAR_CONTESSA],
    [ACT_STEAL]: [CHAR_CAPTAIN, CHAR_AMBASSADOR],
};

const ACTIONS = [
    { id: ACT_INCOME,      label: 'Income',      detail: '+1 coin',          cost: '' },
    { id: ACT_FOREIGN_AID, label: 'Foreign Aid', detail: '+2 coins',         cost: '' },
    { id: ACT_COUP,        label: 'Coup',        detail: 'Eliminate target', cost: '-7' },
    { id: ACT_TAX,         label: 'Tax',         detail: '+3 coins',         cost: '', char: 'duke' },
    { id: ACT_ASSASSINATE, label: 'Assassinate', detail: 'Kill target',      cost: '-3', char: 'assassin' },
    { id: ACT_STEAL,       label: 'Steal',       detail: 'Take 2 coins',     cost: '', char: 'captain' },
    { id: ACT_EXCHANGE,    label: 'Exchange',    detail: 'Swap cards',       cost: '', char: 'ambassador' },
];

const NEEDS_TARGET = [ACT_COUP, ACT_ASSASSINATE, ACT_STEAL];

/* ------------------------------------------------------------------------ */

let _fxLayer = null;
let _prev = null;          // last rendered snapshot, for animation deltas
let _pending = null;       // deltas to animate on the next render

export function createGameScreen(app) {
    const el = screenShell('game', 'table');

    el.insertAdjacentHTML('beforeend', `
        <div class="bar">
            <div class="game-bar-actions">
                <button class="btn btn-dim btn-sm" id="btn-rules-game">Rules</button>
                <button class="btn btn-dim btn-sm" id="btn-log-game">Log</button>
                <button class="btn btn-dim btn-sm" id="btn-mute-game">${audio.muted ? 'Unmute' : 'Mute'}</button>
            </div>
            <span class="turn-label" id="game-turn-label"></span>
        </div>
        <div class="game-grid">
            <div class="game-seats-wrap">
                <div class="seats-col left" id="seats-left"></div>
                <div class="seats-col right" id="seats-right"></div>
            </div>
            <div class="game-center">
                <div class="panel phase-panel">
                    <div class="panel-title" id="phase-title">Waiting</div>
                    <div class="phase-body" id="phase-body"></div>
                    <div class="timer-track"><div class="timer-bar" id="phase-timer-bar"></div></div>
                </div>
            </div>
            <div class="game-hand" id="hand-panel"></div>
            <div class="log game-log" id="game-log"></div>
        </div>
    `);

    _fxLayer = fxLayer(el);
    _prev = null;
    _pending = null;

    el.querySelector('#btn-rules-game').addEventListener('click', () => app.showRules());
    el.querySelector('#btn-log-game').addEventListener('click', () => toggleLogOverlay(app));
    el.querySelector('#btn-mute-game').addEventListener('click', (e) => {
        e.currentTarget.textContent = audio.toggleMute() ? 'Unmute' : 'Mute';
    });

    return el;
}

/* ==========================================================================
   ANIMATION DELTAS
   ==========================================================================
   renderGameState() rebuilds the DOM wholesale (simple, and cheap at this
   size). That would erase any animation, so the deltas worth showing are
   computed BEFORE the rebuild, the affected cards are rendered in their OLD
   state, and the animation is started afterwards. Without this a card would
   simply pop from back to face with no flip at all.
   ========================================================================== */

function snapshot(engine) {
    const coins = [];
    const revealed = [];
    for (let i = 0; i < engine.playerCount; i++) {
        const p = engine.players[i];
        if (!p) continue;
        coins[i] = p.coins;
        revealed[i] = p.cards.map(c => !!c.revealed);
    }
    return { coins, revealed };
}

function computeDeltas(prev, next) {
    if (!prev) return null;
    const coinDeltas = [];
    const flips = [];
    for (let i = 0; i < next.coins.length; i++) {
        const d = (next.coins[i] || 0) - (prev.coins[i] || 0);
        if (d) coinDeltas.push({ pid: i, delta: d });
        const pr = prev.revealed[i] || [];
        const nr = next.revealed[i] || [];
        for (let c = 0; c < nr.length; c++) {
            if (nr[c] && !pr[c]) flips.push({ pid: i, idx: c });
        }
    }
    return (coinDeltas.length || flips.length) ? { coinDeltas, flips } : null;
}

function justFlipped(pid, idx) {
    return !!(_pending && _pending.flips.some(f => f.pid === pid && f.idx === idx));
}

/* ==========================================================================
   FULL RENDER
   ========================================================================== */

export function renderGameState(app) {
    const engine = app.engine;
    if (!engine || !engine.gameActive) return;

    const next = snapshot(engine);
    _pending = computeDeltas(_prev, next);

    renderSeats(app);
    renderHand(app);
    renderPhasePanel(app);
    renderTurnLabel(app);

    if (_pending) runDeltaAnimations(app);
    _prev = next;
    _pending = null;
}

function runDeltaAnimations(app) {
    // Card flips: the element was rendered showing the back, swap to the face.
    for (const f of _pending.flips) {
        const engine = app.engine;
        const p = engine.players[f.pid];
        if (!p) continue;
        const face = cardArt(p.cards[f.idx].character);
        document
            .querySelectorAll(`[data-card="${f.pid}-${f.idx}"]`)
            .forEach(el => flipCard(el, face));
    }

    // Coin movement: arc from the treasury (or the payer) to the receiver.
    const gainers = _pending.coinDeltas.filter(d => d.delta > 0);
    const losers = _pending.coinDeltas.filter(d => d.delta < 0);
    for (const g of gainers) {
        const to = seatAnchor(app, g.pid);
        // A matching loss means coins changed hands (a Steal); otherwise they
        // came from the bank, so they fly out of the centre panel.
        const from = losers.length
            ? seatAnchor(app, losers[0].pid)
            : document.querySelector('.phase-panel');
        coinArc(from, to, Math.min(g.delta, 5));
        const coinEl = document.querySelector(`[data-coins="${g.pid}"]`);
        if (coinEl) { coinEl.classList.remove('bump'); void coinEl.offsetWidth; coinEl.classList.add('bump'); }
    }
}

function seatAnchor(app, pid) {
    if (pid === app.engine.myPid) {
        return document.querySelector('.hand-coins') || document.querySelector('#hand-panel');
    }
    return document.querySelector(`[data-seat="${pid}"]`);
}

/* ==========================================================================
   SEATS
   ========================================================================== */

function renderSeats(app) {
    const engine = app.engine;
    const leftEl = document.getElementById('seats-left');
    const rightEl = document.getElementById('seats-right');
    if (!leftEl || !rightEl) return;

    const opponents = [];
    for (let i = 0; i < engine.playerCount; i++) {
        if (i !== engine.myPid) opponents.push(i);
    }

    const leftPids = opponents.slice(0, 3);
    const rightPids = opponents.slice(3, 6);

    leftEl.innerHTML = '';
    rightEl.innerHTML = '';

    leftPids.forEach(pid => leftEl.appendChild(createSeat(app, pid)));
    for (let i = leftPids.length; i < 3; i++) leftEl.appendChild(emptySeat());

    rightPids.forEach(pid => rightEl.appendChild(createSeat(app, pid)));
    for (let i = rightPids.length; i < 3; i++) rightEl.appendChild(emptySeat());
}

function createSeat(app, pid) {
    const engine = app.engine;
    const view = engine.getPlayerView(pid);
    const name = app.playerNames[pid] || `P${pid + 1}`;
    const isCurrent = engine.currentPlayer() === pid;
    const targeting = app._pendingTargetAction !== undefined && view.alive;

    const seat = document.createElement('div');
    seat.className = 'seat'
        + (view.alive ? '' : ' dead')
        + (isCurrent ? ' active-turn' : '')
        + (targeting ? ' targetable' : '');
    seat.dataset.seat = String(pid);

    let cardsHtml = '';
    view.cards.forEach((card, idx) => {
        // A card that just flipped is rendered face-DOWN here; the flip
        // animation swaps it to its face a moment later.
        const showBack = !card.revealed || justFlipped(pid, idx);
        const src = showBack ? CARD_BACK : cardArt(card.character);
        cardsHtml += `<div class="seat-card card${card.revealed && !showBack ? ' revealed' : ''}"
                           data-card="${pid}-${idx}"
                           style="background-image:url('${src}')"></div>`;
    });

    seat.innerHTML = `
        <div class="portrait-frame seat-portrait"><div class="portrait"></div></div>
        <div class="seat-head">
            <span class="seat-name">${esc(name)}</span>
            <span class="seat-coins" data-coins="${pid}">
                <img src="${coinIcon(view.coins)}" alt="" />${view.coins}
            </span>
        </div>
        <div class="seat-cards">${cardsHtml}</div>
        ${view.alive ? '' : '<div class="seat-dead-flag">Out</div>'}
    `;

    // A seat portrait shows a revealed character if there is one; otherwise the
    // seat stays a silhouette - it must never leak a face-down card.
    const revealedChar = view.cards.find(c => c.revealed && c.character <= 4);
    applyPortrait(seat.querySelector('.portrait'),
        revealedChar ? revealedChar.character : -1, pid * 311);

    seat.addEventListener('click', () => {
        if (app._pendingTargetAction !== undefined && view.alive) {
            app.connection.send(encodeAction(app._pendingTargetAction, pid));
            app._pendingTargetAction = undefined;
            renderGameState(app);
        } else {
            showSeatZoom(app, pid);
        }
    });

    return seat;
}

function emptySeat() {
    const seat = document.createElement('div');
    seat.className = 'seat empty';
    seat.innerHTML = '<div class="portrait-frame seat-portrait"><div class="portrait portrait-unknown"></div></div>'
        + '<div class="seat-head"><span class="seat-name text-dim">empty</span></div>'
        + '<div class="seat-cards"></div>';
    return seat;
}

/* ==========================================================================
   HAND
   ========================================================================== */

function renderHand(app) {
    const engine = app.engine;
    const panel = document.getElementById('hand-panel');
    if (!panel) return;

    const isMyTurn = engine.currentPlayer() === engine.myPid;
    panel.className = 'game-hand' + (isMyTurn ? ' my-turn' : '');

    if (engine.myPid === 0xFF || engine.myPid < 0) {
        panel.innerHTML = '<div class="hand-meta"><span class="hand-name">Spectating</span></div>';
        return;
    }

    const me = engine.getPlayerView(engine.myPid);
    const myName = app.playerNames[engine.myPid] || 'You';

    if (!me.alive) {
        panel.innerHTML = `
            <div class="hand-meta">
                <span class="hand-name text-dim">${esc(myName)}</span>
                <span class="hand-note">Eliminated</span>
            </div>
            <img src="${UI.skull}" alt="" style="width:44px;margin-left:auto" />
        `;
        return;
    }

    const hand = engine.getMyHand();
    const mustDiscard = engine.phase === PHASE.WAITING_FOR_INFLUENCE_LOSS
        && engine.influenceLoser === engine.myPid;

    let cardsHtml = '';
    hand.forEach((card, i) => {
        const showBack = card.revealed && justFlipped(engine.myPid, i);
        const src = showBack ? CARD_BACK : cardArt(card.character);
        const selectable = mustDiscard && !card.revealed;
        cardsHtml += `<div class="hand-card card${card.revealed && !showBack ? ' revealed' : ''}${selectable ? ' selectable' : ''}"
                           data-card="${engine.myPid}-${i}" data-idx="${i}"
                           style="background-image:url('${src}')"
                           title="${esc(CHARACTER_NAMES[card.character] || '')}"></div>`;
    });

    panel.innerHTML = `
        <div class="hand-meta">
            <span class="hand-name">${esc(myName)}</span>
            <span class="hand-coins" data-coins="${engine.myPid}">
                <img src="${coinIcon(me.coins)}" alt="" />${me.coins}
            </span>
            ${mustDiscard ? '<span class="hand-note">Choose a card to lose</span>' : ''}
        </div>
        <div class="hand-cards">${cardsHtml}</div>
    `;

    panel.querySelectorAll('.hand-card').forEach(cardEl => {
        let tapCount = 0;
        let tapTimer = null;
        cardEl.addEventListener('click', () => {
            const idx = parseInt(cardEl.dataset.idx, 10);
            if (mustDiscard && !hand[idx].revealed) {
                app.connection.send(encodeLoseInfluence(idx));
                return;
            }
            if (hand[idx].revealed) return;

            // Triple-tap easter egg, carried over from the live client.
            tapCount++;
            if (tapTimer) clearTimeout(tapTimer);
            tapTimer = setTimeout(() => {
                showCardZoom(hand[idx].character,
                    tapCount >= 3 && hand[idx].character === CHAR_CONTESSA);
                tapCount = 0;
            }, 340);
        });
    });
}

/* ==========================================================================
   PHASE PANEL
   ========================================================================== */

function renderPhasePanel(app) {
    const engine = app.engine;
    const titleEl = document.getElementById('phase-title');
    const bodyEl = document.getElementById('phase-body');
    const timerBar = document.getElementById('phase-timer-bar');
    if (!titleEl || !bodyEl) return;

    bodyEl.innerHTML = '';
    titleEl.className = 'panel-title';

    // Timer: response windows get the 12 s bar, choice phases the 30 s bar,
    // everything else no bar at all.
    if (engine.phase >= PHASE.CHALLENGE_WINDOW
        && engine.phase <= PHASE.BLOCK_CHALLENGE_WINDOW) {
        startTimerBar(timerBar, WINDOW_RESPONSE_MS);
    } else if (engine.phase === PHASE.WAITING_FOR_INFLUENCE_LOSS
        || engine.phase === PHASE.WAITING_FOR_EXCHANGE) {
        startTimerBar(timerBar, WINDOW_CHOICE_MS);
    } else {
        stopTimerBar(timerBar);
        timerBar.style.width = '0%';
    }

    if (app._pendingTargetAction !== undefined) {
        titleEl.textContent = `${ACTION_NAMES[app._pendingTargetAction]} - choose a target`;
        titleEl.classList.add('accent-blue');
        renderTargetSelection(app, bodyEl);
        return;
    }

    switch (engine.phase) {
        case PHASE.WAITING_FOR_ACTION:
            if (engine.currentPlayer() === engine.myPid) {
                renderActionSelection(app, titleEl, bodyEl);
            } else {
                const cp = engine.currentPlayer();
                const name = app.playerNames[cp] || `Player ${cp + 1}`;
                titleEl.textContent = `Waiting for ${name}`;
                note(bodyEl, `${name} is choosing an action`);
            }
            break;
        case PHASE.CHALLENGE_WINDOW:
            renderChallengePrompt(app, titleEl, bodyEl);
            break;
        case PHASE.BLOCK_WINDOW:
            renderBlockPrompt(app, titleEl, bodyEl);
            break;
        case PHASE.BLOCK_CHALLENGE_WINDOW:
            renderBlockChallengePrompt(app, titleEl, bodyEl);
            break;
        case PHASE.RESOLVING:
            if (engine.blockerId === engine.myPid) {
                renderBlockClaimSelection(app, titleEl, bodyEl);
            } else {
                titleEl.textContent = 'Resolving';
                note(bodyEl, 'Waiting for the block claim...');
            }
            break;
        case PHASE.WAITING_FOR_INFLUENCE_LOSS:
            renderInfluenceLoss(app, titleEl, bodyEl);
            break;
        case PHASE.WAITING_FOR_EXCHANGE:
            renderExchange(app, titleEl, bodyEl);
            break;
        default:
            titleEl.textContent = 'Waiting';
    }
}

function note(bodyEl, text) {
    const d = document.createElement('div');
    d.className = 'phase-note';
    d.textContent = text;
    bodyEl.appendChild(d);
}

function menuItem(bodyEl, { label, detail, cost, cssClass, danger, onClick, disabled }) {
    const item = document.createElement('div');
    item.className = 'menu-item'
        + (disabled ? ' disabled' : '')
        + (cssClass ? ' ' + cssClass : '');
    item.innerHTML = `
        <span class="item-label${danger ? ' danger' : ''}">${esc(label)}</span>
        ${detail ? `<span class="item-detail">${esc(detail)}</span>` : '<span></span>'}
        ${cost ? `<span class="item-cost">${esc(cost)}</span>` : '<span></span>'}
    `;
    if (!disabled && onClick) item.addEventListener('click', onClick);
    bodyEl.appendChild(item);
    return item;
}

function renderActionSelection(app, titleEl, bodyEl) {
    const engine = app.engine;
    titleEl.textContent = 'Your move';
    titleEl.classList.add('accent-green');

    const validMask = engine.validActions();

    for (const act of ACTIONS) {
        const enabled = !!(validMask & (1 << act.id));
        menuItem(bodyEl, {
            label: act.label,
            detail: act.detail,
            cost: act.cost,
            cssClass: act.char ? `char-${act.char}` : '',
            disabled: !enabled,
            onClick: () => {
                if (NEEDS_TARGET.includes(act.id)) {
                    app._pendingTargetAction = act.id;
                    renderGameState(app);
                } else {
                    app.connection.send(encodeAction(act.id, 0xFF));
                }
            },
        });
    }
}

function renderTargetSelection(app, bodyEl) {
    const engine = app.engine;
    for (let i = 0; i < engine.playerCount; i++) {
        if (i === engine.myPid || !engine._playerAlive(i)) continue;
        const view = engine.getPlayerView(i);
        menuItem(bodyEl, {
            label: app.playerNames[i] || `Player ${i + 1}`,
            cost: `${view.coins}`,
            onClick: () => {
                app.connection.send(encodeAction(app._pendingTargetAction, i));
                app._pendingTargetAction = undefined;
                renderGameState(app);
            },
        });
    }
    menuItem(bodyEl, {
        label: 'Cancel',
        danger: true,
        onClick: () => {
            app._pendingTargetAction = undefined;
            renderGameState(app);
        },
    });
}

function renderChallengePrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const actor = app.playerNames[engine.actionPlayer] || `Player ${engine.actionPlayer + 1}`;
    const claimed = engine.actionClaim >= 0 ? CHARACTER_NAMES[engine.actionClaim] : '?';

    titleEl.textContent = `${actor} claims ${claimed}`;
    titleEl.classList.add('accent-red');

    if (!engine.canRespond()) { note(bodyEl, 'Waiting for responses...'); return; }

    menuItem(bodyEl, { label: 'Allow', onClick: () => app.connection.send(encodeResponse(RESP_PASS)) });
    menuItem(bodyEl, {
        label: 'Challenge', danger: true,
        onClick: () => app.connection.send(encodeResponse(RESP_CHALLENGE)),
    });
}

function renderBlockPrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const actor = app.playerNames[engine.actionPlayer] || `Player ${engine.actionPlayer + 1}`;
    const actName = ACTION_NAMES[engine.currentAction] || '?';

    titleEl.textContent = `${actor}: ${actName}`;
    titleEl.classList.add('accent-red');

    if (!engine.canRespond()) { note(bodyEl, 'Waiting for responses...'); return; }

    menuItem(bodyEl, { label: 'Allow', onClick: () => app.connection.send(encodeResponse(RESP_PASS)) });

    // A block is TWO messages, exactly as the live client sends them: the
    // RESP_BLOCK first, then the character claim once the server moves the
    // phase to RESOLVING. Sending the claim here would arrive out of phase.
    for (const ch of (BLOCKABLE_BY[engine.currentAction] || [])) {
        menuItem(bodyEl, {
            label: `Block as ${CHARACTER_NAMES[ch]}`,
            cssClass: `char-${CHAR_CSS[ch]}`,
            onClick: () => app.connection.send(encodeResponse(RESP_BLOCK)),
        });
    }
}

function renderBlockChallengePrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const blocker = app.playerNames[engine.blockerId] || `Player ${engine.blockerId + 1}`;
    const claimed = engine.blockerClaim >= 0 ? CHARACTER_NAMES[engine.blockerClaim] : '?';

    titleEl.textContent = `${blocker} blocks with ${claimed}`;
    titleEl.classList.add('accent-purple');

    if (!engine.canRespond()) { note(bodyEl, 'Waiting for responses...'); return; }

    menuItem(bodyEl, { label: 'Allow', onClick: () => app.connection.send(encodeResponse(RESP_PASS)) });
    menuItem(bodyEl, {
        label: 'Challenge the block', danger: true,
        onClick: () => app.connection.send(encodeResponse(RESP_CHALLENGE)),
    });
}

function renderBlockClaimSelection(app, titleEl, bodyEl) {
    const engine = app.engine;
    titleEl.textContent = 'Block with which character?';
    titleEl.classList.add('accent-purple');

    for (let ch = 0; ch < 5; ch++) {
        if (!(engine.currentBlockableBy & (1 << ch))) continue;
        menuItem(bodyEl, {
            label: CHARACTER_NAMES[ch],
            cssClass: `char-${CHAR_CSS[ch]}`,
            onClick: () => app.connection.send(encodeBlockClaim(ch)),
        });
    }
}

function renderInfluenceLoss(app, titleEl, bodyEl) {
    const engine = app.engine;
    const isMe = engine.influenceLoser === engine.myPid;

    titleEl.textContent = 'Lose influence';
    titleEl.classList.add('accent-red');

    if (!isMe) {
        const name = app.playerNames[engine.influenceLoser]
            || `Player ${engine.influenceLoser + 1}`;
        note(bodyEl, `${name} must lose influence...`);
        return;
    }

    const hand = engine.getMyHand();
    hand.forEach((card, i) => {
        if (card.revealed) return;
        menuItem(bodyEl, {
            label: CHARACTER_NAMES[card.character],
            detail: 'Reveal this card',
            cssClass: `char-${CHAR_CSS[card.character]}`,
            onClick: () => app.connection.send(encodeLoseInfluence(i)),
        });
    });
    note(bodyEl, 'Or tap the card in your hand');
}

function renderExchange(app, titleEl, bodyEl) {
    const engine = app.engine;
    titleEl.textContent = 'Exchange - keep 2';
    titleEl.classList.add('accent-blue');

    if (engine.exchangePlayer !== engine.myPid) {
        const name = app.playerNames[engine.exchangePlayer]
            || `Player ${engine.exchangePlayer + 1}`;
        note(bodyEl, `${name} is exchanging cards...`);
        return;
    }

    const cards = engine.getExchangeCards();
    const selected = new Set();

    const row = document.createElement('div');
    row.className = 'hand-cards';
    row.style.justifyContent = 'center';
    row.style.margin = '4px 0 8px';
    bodyEl.appendChild(row);

    cards.forEach((ch, i) => {
        const el = document.createElement('div');
        el.className = 'hand-card card';
        el.style.backgroundImage = `url('${cardArt(ch)}')`;
        el.title = CHARACTER_NAMES[ch] || '';
        el.addEventListener('click', () => {
            if (selected.has(i)) {
                selected.delete(i);
                el.classList.remove('selectable');
            } else if (selected.size < 2) {
                selected.add(i);
                el.classList.add('selectable');
            }
            if (selected.size === 2) {
                const picks = Array.from(selected);
                app.connection.send(encodeExchangeChoice(picks[0], picks[1]));
            }
        });
        row.appendChild(el);
    });

    note(bodyEl, `Tap 2 of the ${cards.length} cards to keep`);
}

/* ==========================================================================
   TURN LABEL
   ========================================================================== */

function renderTurnLabel(app) {
    const engine = app.engine;
    const el = document.getElementById('game-turn-label');
    if (!el) return;

    const cp = engine.currentPlayer();
    const name = app.playerNames[cp] || `Player ${cp + 1}`;
    const isMyTurn = cp === engine.myPid;

    let text;
    switch (engine.phase) {
        case PHASE.WAITING_FOR_ACTION: text = isMyTurn ? 'Your turn' : `${name}'s turn`; break;
        case PHASE.CHALLENGE_WINDOW: text = 'Challenge window'; break;
        case PHASE.BLOCK_WINDOW: text = 'Block window'; break;
        case PHASE.BLOCK_CHALLENGE_WINDOW: text = 'Block challenge'; break;
        case PHASE.WAITING_FOR_INFLUENCE_LOSS: text = 'Losing influence'; break;
        case PHASE.WAITING_FOR_EXCHANGE: text = 'Exchange'; break;
        case PHASE.RESOLVING: text = 'Resolving'; break;
        default: text = 'Waiting';
    }

    el.textContent = text;
    el.className = 'turn-label' + (isMyTurn ? ' my-turn' : '');
}

/* ==========================================================================
   EFFECT TRIGGERS
   ==========================================================================
   Called by main.js from the INPUT_RELAY handler, BEFORE the engine processes
   the relay, so an effect can be chosen from pre-state where that matters.
   Purely presentational - it reads state and draws, and never sends.
   ========================================================================== */

export function playRelayFx(app, inputType, playerId, data) {
    if (!_fxLayer) return;

    switch (inputType) {
        case RELAY_ACTION: {
            const name = ACTION_EFFECT[data[0]];
            if (name) playEffect(name, _fxLayer);
            const seat = document.querySelector(`[data-seat="${playerId}"]`);
            if (seat) pulse(seat);
            break;
        }
        case RELAY_RESPONSE:
            if (data[0] === RESP_CHALLENGE) {
                playEffect('challenge', _fxLayer);
                flash(_fxLayer, 'white');
            } else if (data[0] === RESP_BLOCK) {
                playEffect('block', _fxLayer);
            }
            break;
        case RELAY_BLOCK_CLAIM:
            playEffect('block', _fxLayer);
            break;
        case RELAY_LOSE_INFLUENCE:
            flash(_fxLayer, 'red');
            break;
    }
}

/* ==========================================================================
   LOG
   ========================================================================== */

let _logOverlay = null;
const _logHistory = [];

export function addGameLog(text) {
    _logHistory.push(text);
    if (_logHistory.length > 200) _logHistory.shift();

    const logEl = document.getElementById('game-log');
    if (logEl) {
        logEl.appendChild(makeLogLine(text));
        logEl.scrollTop = logEl.scrollHeight;
        while (logEl.children.length > 60) logEl.removeChild(logEl.firstChild);
    }

    if (_logOverlay) {
        const body = _logOverlay.querySelector('#log-overlay-body');
        if (body) {
            body.appendChild(makeLogLine(text));
            body.scrollTop = body.scrollHeight;
        }
    }
}

function makeLogLine(text) {
    const line = document.createElement('div');
    line.className = 'log-line';
    const lower = text.toLowerCase();
    if (lower.includes('challenge')) line.classList.add('event-challenge');
    else if (lower.includes('block')) line.classList.add('event-block');
    else if (lower.includes('declares') || lower.includes('takes')
        || lower.includes('launches')) line.classList.add('event-action');
    line.textContent = text;
    return line;
}

function toggleLogOverlay(app) {
    if (_logOverlay) {
        _logOverlay.remove();
        _logOverlay = null;
        return;
    }

    _logOverlay = document.createElement('div');
    _logOverlay.className = 'overlay';
    _logOverlay.innerHTML = `
        <div class="overlay-head">
            <span class="overlay-title">Game Log</span>
            <button class="btn btn-red btn-sm" id="btn-close-log">Close</button>
        </div>
        <div class="overlay-body log" id="log-overlay-body"></div>
    `;

    const body = _logOverlay.querySelector('#log-overlay-body');
    if (!_logHistory.length) {
        body.innerHTML = '<div class="log-line text-dim">No entries yet.</div>';
    } else {
        _logHistory.forEach(t => body.appendChild(makeLogLine(t)));
        setTimeout(() => { body.scrollTop = body.scrollHeight; }, 40);
    }

    _logOverlay.querySelector('#btn-close-log').addEventListener('click', () => {
        _logOverlay.remove();
        _logOverlay = null;
    });

    app.viewport.appendChild(_logOverlay);
}

/** Called when the game screen is torn down, so a stale timer cannot tick on. */
export function stopTimer() {
    stopTimerBar(document.getElementById('phase-timer-bar'));
}

/* ==========================================================================
   ZOOM OVERLAYS
   ========================================================================== */

function showSeatZoom(app, pid) {
    const engine = app.engine;
    const view = engine.getPlayerView(pid);
    const name = app.playerNames[pid] || `Player ${pid + 1}`;

    const overlay = document.createElement('div');
    overlay.className = 'zoom-overlay';

    let cardsHtml = '';
    for (const card of view.cards) {
        const src = card.revealed ? cardArt(card.character) : CARD_BACK;
        cardsHtml += `<div class="zoom-card card" style="background-image:url('${src}')"></div>`;
    }

    overlay.innerHTML = `
        <div class="zoom-name" ${view.alive ? '' : 'style="color:var(--text-red)"'}>
            ${esc(name)}${view.alive ? '' : ' - out'}
        </div>
        <div class="zoom-meta">${view.coins} coins</div>
        <div class="zoom-cards">${cardsHtml}</div>
        <div class="zoom-hint">Tap anywhere to close</div>
    `;

    overlay.addEventListener('click', () => overlay.remove());
    document.body.appendChild(overlay);
}

function showCardZoom(character, easterEgg) {
    const c = CHARACTERS[character];
    if (!c) return;

    const overlay = document.createElement('div');
    overlay.className = 'zoom-overlay';

    if (easterEgg) {
        overlay.innerHTML = `
            <video class="zoom-video" src="${EASTER_EGG_VIDEO}" autoplay loop muted
                   playsinline webkit-playsinline disablepictureinpicture></video>
            <div class="zoom-name" style="color:${c.color}">${c.name}</div>
            <div class="zoom-hint">Tap anywhere to close</div>
        `;
    } else {
        overlay.innerHTML = `
            <div class="zoom-cards">
                <div class="zoom-card card" style="background-image:url('${cardArt(character)}')"></div>
                <div class="portrait-frame zoom-portrait"><div class="portrait"></div></div>
            </div>
            <div class="zoom-name" style="color:${c.color}">${c.name}</div>
            <div class="zoom-hint">Tap anywhere to close</div>
        `;
        applyPortrait(overlay.querySelector('.portrait'), character);
    }

    overlay.addEventListener('click', () => overlay.remove());
    document.body.appendChild(overlay);

    const vid = overlay.querySelector('video');
    if (vid) vid.play().catch(() => {});
}
