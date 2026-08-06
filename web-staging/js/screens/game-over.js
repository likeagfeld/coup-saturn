/**
 * game-over.js - Victory / defeat.
 *
 * Saturn treatment: the win/lose scene on the NBG1 bitmap path, the winner's
 * portrait scaled up under a gouraud spotlight, mesh + colour-offset dissolve
 * into it. Here: the B5/B6 backdrop, the official VICTORY/DEFEAT plate, and
 * the winner's surviving character idling under a radial spotlight.
 */

import { UI, CARD_BACK } from '../assets.js';
import { esc, screenShell, applyPortrait } from '../ui.js';
import { stopTimer, getLogRing } from './game.js';
import { recapRows, RECAP_ROWS } from '../log-ring.js';
import { sfx, SFX } from '../sfx.js';

export function createGameOverScreen(app, winnerId) {
    const isMe = winnerId === app.engine.myPid;
    const el = screenShell('game-over', isMe ? 'victory' : 'defeat');

    // The game screen is gone; make sure its timer bar is not still running.
    stopTimer();

    const winnerName = app.playerNames[winnerId] || `Player ${winnerId + 1}`;

    // The winner's surviving influence, if the local engine knows it.
    let survivor = -1;
    try {
        const view = app.engine.getPlayerView(winnerId);
        if (view && view.cards) {
            const card = view.cards.find(c => !c.revealed && c.character <= 4);
            if (card) survivor = card.character;
        }
    } catch (_) { /* engine already reset */ }

    el.insertAdjacentHTML('beforeend', `
        <div class="gameover-inner">
            <img class="gameover-plate" src="${isMe ? UI.victory : UI.defeat}"
                 alt="${isMe ? 'Victory' : 'Defeat'}" draggable="false" />
            ${survivor >= 0
                ? `<div class="portrait-frame gameover-portrait"><div class="portrait"></div></div>`
                : `<div class="portrait-frame gameover-portrait"
                        style="background-image:url('${CARD_BACK}');background-size:cover"></div>`}
            <div class="gameover-winner">${esc(winnerName)} wins</div>
            <div class="gameover-sub">
                ${isMe ? 'The last one standing.' : 'Better luck next round.'}
            </div>
            <div class="recap" id="gameover-recap">
                <div class="recap-head">
                    <span class="recap-title">How it ended</span>
                    <span class="recap-hint" id="recap-hint"></span>
                </div>
                <div class="recap-rows" id="recap-rows"></div>
            </div>
            <div class="row-buttons">
                <button class="btn btn-gold btn-lg" id="btn-return-lobby">Return to lobby</button>
                <button class="btn btn-dim" id="btn-rules-gameover">Rules</button>
            </div>
        </div>
    `);

    if (survivor >= 0) {
        applyPortrait(el.querySelector('.gameover-portrait .portrait'), survivor);
    }

    const detachRecap = installRecap(el);

    el.querySelector('#btn-return-lobby').addEventListener('click', () => {
        sfx.play(SFX.UI_CONFIRM);
        detachRecap();
        app.engine.reset();
        app.changeScreen('lobby');
    });

    el.querySelector('#btn-rules-gameover').addEventListener('click', () => {
        sfx.play(SFX.UI_CONFIRM);
        app.showRules();
    });

    return el;
}

/* ==========================================================================
   RECAP
   ==========================================================================
   Saturn draws a scrollable window of the last COUP_GAMEOVER_RECAP_ROWS log
   entries, the newest of them marked with ">" in gold as the winning action
   and everything above it dimmed (coup_render.c, the "Scrollable recap of how
   the match ended" block). This is that, in the DOM.

   The rows come from log-ring.js recapRows(), which is the ONLY place the
   window arithmetic lives - see the long comment there about the ordering bug
   this recap is a port of, and scripts/qa/qa_web_log_ring.mjs, which asserts
   the read-back stays chronological after the ring has wrapped.
   ========================================================================== */

function installRecap(el) {
    const ring = getLogRing();
    const rowsEl = el.querySelector('#recap-rows');
    const hintEl = el.querySelector('#recap-hint');
    const total = ring ? ring.count : 0;
    const maxScroll = Math.max(0, total - RECAP_ROWS);
    let scroll = 0;

    function draw() {
        rowsEl.innerHTML = '';
        if (!total) {
            rowsEl.innerHTML =
                '<div class="recap-row text-dim">No actions were logged.</div>';
            hintEl.textContent = '';
            return;
        }
        for (const r of recapRows(ring, RECAP_ROWS, scroll)) {
            const row = document.createElement('div');
            row.className = 'recap-row' + (r.winning ? ' winning' : '');
            const mark = document.createElement('span');
            mark.className = 'recap-mark';
            mark.textContent = r.winning ? '▸' : '';
            const text = document.createElement('span');
            text.className = 'recap-text';
            text.textContent = r.text;
            row.append(mark, text);
            rowsEl.appendChild(row);
        }
        // Saturn swaps its footer hint between "[UP/DOWN] Recap" and
        // "[A] Lobby" on the same condition: is there anything above this
        // window to scroll to. At the top of the log there is not, so the
        // prompt goes away rather than reading "0 earlier".
        const earlier = maxScroll - scroll;
        hintEl.textContent = earlier > 0 ? `${earlier} earlier  ↑↓` : '';
    }

    function move(delta) {
        // `scroll` counts entries BACK from the newest, so up = +1.
        const next = Math.max(0, Math.min(maxScroll, scroll + delta));
        if (next === scroll) return;
        scroll = next;
        sfx.play(SFX.UI_MOVE);
        draw();
    }

    const onWheel = (e) => {
        if (!maxScroll) return;
        e.preventDefault();
        move(e.deltaY < 0 ? 1 : -1);
    };
    const onKey = (e) => {
        // Self-cleaning: changeScreen() removes the screen element without
        // telling anyone, so a document-level listener would otherwise outlive
        // it and scroll a recap that is no longer on screen.
        if (!el.isConnected) { document.removeEventListener('keydown', onKey); return; }
        if (e.key === 'ArrowUp') { move(1); e.preventDefault(); }
        else if (e.key === 'ArrowDown') { move(-1); e.preventDefault(); }
    };

    rowsEl.addEventListener('wheel', onWheel, { passive: false });
    document.addEventListener('keydown', onKey);

    draw();

    // The keydown listener is on document, so it outlives this screen unless
    // it is taken off again - the same class of leak stopTimer() exists for.
    return () => document.removeEventListener('keydown', onKey);
}
