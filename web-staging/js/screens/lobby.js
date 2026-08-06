/**
 * lobby.js - Waiting room.
 *
 * Saturn treatment: B3_lobby painted backdrop whose art already contains the
 * P1-P8 slot frames, with VDP1 drawing only the fill/state inside each frame
 * and a palette-cycle pulse on ready slots. The web cannot rely on the art's
 * pixel regions (the layout is fluid), so it draws its own plates in the same
 * style over the same backdrop, and reproduces the ready pulse in CSS.
 *
 * Every control sends exactly the message the live client sends. Bot add /
 * remove / difficulty and START are all server-authoritative; this screen only
 * encodes intent.
 */

import {
    encodeReady, encodeStartGame, encodeAddBot,
    encodeRemoveBot,
} from '../protocol.js';
import { esc, screenShell } from '../ui.js';
import { BG, preload } from '../assets.js';
import { audio } from '../audio.js';

const DIFF_LABELS = ['Easy', 'Medium', 'Hard'];
const DIFF_CLASSES = ['easy', 'medium', 'hard'];
const MAX_SLOTS = 7;

export function createLobbyScreen(app) {
    const el = screenShell('lobby', 'lobby');

    el.insertAdjacentHTML('beforeend', `
        <div class="bar">
            <span class="bar-title">Waiting Room</span>
            <span class="bar-meta" id="lobby-header-status"></span>
        </div>
        <div class="lobby-body">
            <div class="panel lobby-slots" id="lobby-players"></div>
            <div class="log lobby-log" id="lobby-log"></div>
            <div class="panel lobby-controls">
                <button class="btn btn-green" id="btn-ready">Ready</button>
                <button class="btn btn-gold" id="btn-start">Start Game</button>
                <button class="btn btn-purple" id="btn-add-bot">+ Bot</button>
                <button class="btn btn-dim" id="btn-remove-bot">- Bot</button>
                <select class="btn btn-dim" id="bot-difficulty" aria-label="Bot difficulty">
                    <option value="0">Easy</option>
                    <option value="1" selected>Medium</option>
                    <option value="2">Hard</option>
                </select>
                <span class="spacer"></span>
                <button class="btn btn-dim btn-sm" id="btn-rules-lobby">Rules</button>
                <button class="btn btn-dim btn-sm" id="btn-mute-lobby">${audio.muted ? 'Unmute' : 'Mute'}</button>
            </div>
        </div>
        <div class="status-bar">
            <span id="lobby-status-left">Players: 0/6</span>
            <span id="lobby-status-right">Press READY when you are</span>
        </div>
    `);

    let isReady = false;
    const readyBtn = el.querySelector('#btn-ready');

    readyBtn.addEventListener('click', () => {
        isReady = !isReady;
        readyBtn.textContent = isReady ? 'Not Ready' : 'Ready';
        readyBtn.className = isReady ? 'btn btn-red' : 'btn btn-green';
        app.connection.send(encodeReady(isReady));
    });

    el.querySelector('#btn-start').addEventListener('click', () => {
        app.connection.send(encodeStartGame());
    });

    el.querySelector('#btn-add-bot').addEventListener('click', () => {
        const diff = parseInt(el.querySelector('#bot-difficulty').value, 10);
        app.connection.send(encodeAddBot(diff));
    });

    el.querySelector('#btn-remove-bot').addEventListener('click', () => {
        app.connection.send(encodeRemoveBot());
    });

    el.querySelector('#btn-rules-lobby').addEventListener('click', () => app.showRules());
    el.querySelector('#btn-mute-lobby').addEventListener('click', (e) => {
        e.currentTarget.textContent = audio.toggleMute() ? 'Unmute' : 'Mute';
    });

    preload([BG.table]);

    /* Repaint the roster from the cached lobby state.
     *
     * createLobbyScreen() builds an EMPTY list; the only thing that ever
     * filled it was the COUP_MSG_LOBBY_STATE handler, which populates right
     * after it calls changeScreen('lobby'). That covers arriving at the
     * lobby because a lobby-state message arrived - but not RETURNING to it
     * from the game or the game-over screen, where changeScreen('lobby') is
     * called on its own. The roster then stayed blank until the server
     * happened to broadcast again, which is what pressing Ready does. So the
     * players were there all along and simply were not drawn.
     *
     * main.js's changeScreen already does exactly this for the game screen
     * (`setTimeout(() => renderGameState(this), 50)`); the lobby case just
     * never got its equivalent.
     *
     * Deferred because updateLobbyPlayers() looks the list up by id, and
     * this element is not in the document until changeScreen appends it. */
    setTimeout(() => {
        if (document.getElementById('lobby-players')) {
            updateLobbyPlayers(app._lobbyPlayers || [], app.myUserId);
        }
    }, 0);

    return el;
}

export function updateLobbyPlayers(players, myUserId) {
    const listEl = document.getElementById('lobby-players');
    if (!listEl) return;

    listEl.innerHTML = '';
    let readyCount = 0;

    for (let i = 0; i < MAX_SLOTS; i++) {
        const p = players[i];
        const slot = document.createElement('div');

        if (!p) {
            slot.className = 'slot empty';
            slot.innerHTML = `
                <span class="slot-id">P${i + 1}</span>
                <span class="slot-name text-dim">open seat</span>
                <span></span><span></span>
            `;
            listEl.appendChild(slot);
            continue;
        }

        const classes = ['slot'];
        if (p.id === myUserId) classes.push('self');
        if (p.ready) { classes.push('ready'); readyCount++; }
        if (p.isBot) classes.push('bot');
        slot.className = classes.join(' ');

        const diffIdx = Math.min(p.difficulty, 2);
        const diffHtml = p.isBot
            ? `<span class="slot-tag ${DIFF_CLASSES[diffIdx]}">${DIFF_LABELS[diffIdx]}</span>`
            : '<span></span>';

        slot.innerHTML = `
            <span class="slot-id">P${i + 1}</span>
            <span class="slot-name">${esc(p.name)}${p.id === myUserId ? ' <span class="text-dim">(you)</span>' : ''}</span>
            ${p.isBot ? '<span class="slot-tag tag-bot">Bot</span>' : '<span></span>'}
            <span class="slot-tag ${p.ready ? 'tag-ready' : 'tag-wait'}">${p.ready ? 'Ready' : 'Waiting'}</span>
            ${diffHtml}
        `;
        listEl.appendChild(slot);
    }

    const total = players.length;
    const left = document.getElementById('lobby-status-left');
    const right = document.getElementById('lobby-status-right');
    const header = document.getElementById('lobby-header-status');

    if (left) left.textContent = `Players: ${total}/6   Ready: ${readyCount}/${total}`;
    if (header) header.textContent = `${total} player${total !== 1 ? 's' : ''}`;

    if (right) {
        const allReady = readyCount === total && total >= 2;
        right.textContent = allReady
            ? 'All ready - press START to begin'
            : 'Press READY when you are';
        right.className = allReady ? 'all-ready' : '';
    }
}

export function addLobbyLog(text) {
    const logEl = document.getElementById('lobby-log');
    if (!logEl) return;
    const line = document.createElement('div');
    line.className = 'log-line';
    line.textContent = text;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
    while (logEl.children.length > 40) logEl.removeChild(logEl.firstChild);
}
