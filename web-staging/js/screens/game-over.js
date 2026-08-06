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
import { stopTimer } from './game.js';

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
            <div class="row-buttons">
                <button class="btn btn-gold btn-lg" id="btn-return-lobby">Return to lobby</button>
                <button class="btn btn-dim" id="btn-rules-gameover">Rules</button>
            </div>
        </div>
    `);

    if (survivor >= 0) {
        applyPortrait(el.querySelector('.gameover-portrait .portrait'), survivor);
    }

    el.querySelector('#btn-return-lobby').addEventListener('click', () => {
        app.engine.reset();
        app.changeScreen('lobby');
    });

    el.querySelector('#btn-rules-gameover').addEventListener('click', () => app.showRules());

    return el;
}
