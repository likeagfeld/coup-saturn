/**
 * lobby.js - Player lobby matching Saturn layout
 * Header bar, 7 player slots, controls, status bar, log
 */
import {
    encodeReady, encodeStartGame, encodeAddBot,
    encodeRemoveBot, encodeSetBotDifficulty
} from '../protocol.js';
import { audio } from '../audio.js';

const DIFF_LABELS = ['Easy', 'Medium', 'Hard'];
const DIFF_CLASSES = ['easy', 'medium', 'hard'];

export function createLobbyScreen(app) {
    const el = document.createElement('div');
    el.className = 'screen screen-lobby';

    el.innerHTML = `
        <div class="lobby-header">
            <div class="lobby-header-title">COUP - WAITING ROOM</div>
            <div class="lobby-header-status" id="lobby-header-status"></div>
        </div>
        <div class="lobby-body">
            <div class="lobby-player-list" id="lobby-players"></div>
            <div class="lobby-controls">
                <button class="btn btn-green" id="btn-ready">READY</button>
                <button class="btn btn-gold" id="btn-start">START GAME</button>
                <button class="btn btn-purple" id="btn-add-bot">+ BOT</button>
                <button class="btn btn-dim" id="btn-remove-bot">- BOT</button>
                <select class="btn btn-dim" id="bot-difficulty" style="min-width:80px">
                    <option value="0">Easy</option>
                    <option value="1" selected>Medium</option>
                    <option value="2">Hard</option>
                </select>
                <button class="btn btn-dim" id="btn-rules-lobby">RULES</button>
                <button class="btn btn-dim" id="btn-mute-lobby">${audio.muted ? 'UNMUTE' : 'MUTE'}</button>
            </div>
            <div class="lobby-log" id="lobby-log"></div>
        </div>
        <div class="lobby-status-bar">
            <span class="lobby-status-left" id="lobby-status-left">Players: 0/6</span>
            <span class="lobby-status-right" id="lobby-status-right">Press READY when ready</span>
        </div>
    `;

    let isReady = false;
    const readyBtn = el.querySelector('#btn-ready');

    readyBtn.addEventListener('click', () => {
        isReady = !isReady;
        readyBtn.textContent = isReady ? 'NOT READY' : 'READY';
        readyBtn.className = isReady ? 'btn btn-red' : 'btn btn-green';
        app.connection.send(encodeReady(isReady));
    });

    el.querySelector('#btn-start').addEventListener('click', () => {
        app.connection.send(encodeStartGame());
    });

    el.querySelector('#btn-add-bot').addEventListener('click', () => {
        const diff = parseInt(el.querySelector('#bot-difficulty').value);
        app.connection.send(encodeAddBot(diff));
    });

    el.querySelector('#btn-remove-bot').addEventListener('click', () => {
        app.connection.send(encodeRemoveBot());
    });

    el.querySelector('#btn-rules-lobby').addEventListener('click', () => {
        app.showRules();
    });
    el.querySelector('#btn-mute-lobby').addEventListener('click', (e) => {
        const muted = audio.toggleMute();
        e.target.textContent = muted ? 'UNMUTE' : 'MUTE';
    });

    return el;
}

export function updateLobbyPlayers(players, myUserId) {
    const listEl = document.getElementById('lobby-players');
    if (!listEl) return;

    listEl.innerHTML = '';
    let readyCount = 0;
    let humanCount = 0;

    for (let i = 0; i < 7; i++) {
        const p = players[i];
        const slot = document.createElement('div');

        if (!p) {
            slot.className = 'lobby-slot empty';
            slot.innerHTML = `
                <span class="lobby-slot-id">P${i + 1}</span>
                <span class="lobby-slot-name text-gray">-----</span>
                <span class="lobby-slot-status">-----</span>
            `;
        } else {
            const isSelf = p.id === myUserId;
            const classes = ['lobby-slot'];
            if (isSelf) classes.push('self');
            if (p.ready) { classes.push('ready'); readyCount++; }
            if (p.isBot) classes.push('bot');
            if (!p.isBot) humanCount++;

            slot.className = classes.join(' ');
            let diffHtml = '';
            if (p.isBot) {
                const di = Math.min(p.difficulty, 2);
                diffHtml = `<span class="lobby-slot-difficulty ${DIFF_CLASSES[di]}">${DIFF_LABELS[di]}</span>`;
            }
            slot.innerHTML = `
                <div class="lobby-slot-ready-bar"></div>
                <span class="lobby-slot-id">P${i + 1}</span>
                <span class="lobby-slot-name">${esc(p.name)}</span>
                <span class="lobby-slot-type">${p.isBot ? 'BOT' : ''}</span>
                <span class="lobby-slot-status">${p.ready ? 'READY' : 'WAITING'}</span>
                ${diffHtml}
            `;
        }
        listEl.appendChild(slot);
    }

    const total = players.length;
    const left = document.getElementById('lobby-status-left');
    const right = document.getElementById('lobby-status-right');
    const header = document.getElementById('lobby-header-status');
    if (left) left.textContent = `Players: ${total}/6  Ready: ${readyCount}/${total}`;
    if (header) header.textContent = `${total} player${total !== 1 ? 's' : ''}`;

    if (right) {
        if (readyCount === total && total >= 2) {
            right.textContent = 'READY! Press START to begin!';
            right.className = 'lobby-status-right all-ready';
        } else {
            right.textContent = 'Press READY when ready';
            right.className = 'lobby-status-right';
        }
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
    while (logEl.children.length > 20) logEl.removeChild(logEl.firstChild);
}

function esc(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
