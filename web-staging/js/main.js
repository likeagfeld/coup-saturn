/**
 * main.js - Coup web client entry point. STAGING build.
 *
 * ============================ PROTOCOL CONTRACT ============================
 * This client talks to the SAME server as the live one, and staging players
 * share games with live players. Therefore:
 *
 *   - connection.js, protocol.js and game-engine.js are BYTE-IDENTICAL copies
 *     of the live client's. They are not edited here, at all.
 *   - The WebSocket URL derivation below is copied verbatim from the live
 *     client. Served from /staging/ it still resolves to the same wss://host/ws
 *     that nginx proxies to 127.0.0.1:4823, because the path is absolute and
 *     the host is taken from location.
 *   - Every message this file registers, decodes and sends is the same message
 *     the live client registers, decodes and sends, in the same order.
 *
 * What changed is presentation only: screens fade, the effect layer is
 * notified of relays, and a STAGING badge is pinned to the viewport.
 * ==========================================================================
 */

import { Connection } from './connection.js';
import { GameEngine, CHARACTER_NAMES } from './game-engine.js';
import {
    MSG_USERNAME_REQUIRED, MSG_WELCOME, MSG_WELCOME_BACK, MSG_USERNAME_TAKEN,
    COUP_MSG_LOBBY_STATE, COUP_MSG_GAME_START, COUP_MSG_LOG,
    COUP_MSG_INPUT_RELAY, COUP_MSG_RESYNC, COUP_MSG_RESYNC_FULL,
    COUP_MSG_ACTION_REJECTED,
    RELAY_ACTION, RELAY_RESPONSE, RELAY_BLOCK_CLAIM,
    RELAY_LOSE_INFLUENCE, RELAY_EXCHANGE_CHOICE, RELAY_TIMEOUT,
    ACT_INCOME, ACT_FOREIGN_AID, ACT_COUP, ACT_TAX,
    ACT_ASSASSINATE, ACT_STEAL, ACT_EXCHANGE,
    RESP_CHALLENGE, RESP_BLOCK,
    encodeConnect, encodeResyncReq
} from './protocol.js';

import { createTitleScreen } from './screens/title.js';
import { createConnectingScreen } from './screens/connecting.js';
import { createNameEntryScreen, handleUsernameTaken } from './screens/name-entry.js';
import { createLobbyScreen, updateLobbyPlayers, addLobbyLog } from './screens/lobby.js';
import {
    createGameScreen, renderGameState, addGameLog, playRelayFx, stopTimer,
} from './screens/game.js';
import { createGameOverScreen } from './screens/game-over.js';
import { createRulesOverlay } from './screens/rules.js';
import { fadeIn } from './fx.js';

/** Bumped by hand when the staging build is redeployed; shown in the badge. */
const STAGING_BUILD = '2026-08-06';

class CoupApp {
    constructor() {
        this.appRoot = document.getElementById('app');

        this.viewport = document.createElement('div');
        this.viewport.className = 'game-viewport';
        this.appRoot.appendChild(this.viewport);

        this._installStagingBadge();

        this.currentScreen = null;
        this.engine = new GameEngine();
        this.playerNames = {};
        this.playerOrder = [];
        this._lobbyPlayers = [];
        this.myUserId = 0;
        this.myUuid = '';
        this.lastRelaySeq = -1;
        this._pendingTargetAction = undefined;
        this._rulesOverlay = null;

        // --- WS URL: verbatim from the live client. Do not "improve" this. ---
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const host = location.hostname || 'saturncoup.duckdns.org';
        const port = location.port ? `:${location.port}` : '';
        let wsUrl;
        if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
            wsUrl = `ws://${host}:4823`;
        } else {
            wsUrl = `${proto}//${host}${port}/ws`;
        }
        this.connection = new Connection(wsUrl);
        this._setupMessageHandlers();

        this.connection.onKicked = () => this._showKickPopup();

        // Mobile autoplay workaround, kept for the one remaining <video> (the
        // easter egg): browsers refuse programmatic play() until a gesture.
        const playAllVideos = () => {
            document.querySelectorAll('video').forEach(v => {
                if (v.paused) v.play().catch(() => {});
            });
        };
        document.addEventListener('touchstart', playAllVideos, { passive: true });
        document.addEventListener('click', playAllVideos);

        document.addEventListener('keydown', (e) => {
            if ((e.key === 'r' || e.key === 'R') && !this._isTextInput(e.target)) {
                if (this._rulesOverlay) this.hideRules(); else this.showRules();
            }
            if (e.key === 'Escape' && this._rulesOverlay) this.hideRules();
        });
    }

    _installStagingBadge() {
        const badge = document.createElement('div');
        badge.className = 'staging-badge';
        badge.innerHTML = `Staging <span class="staging-build">${STAGING_BUILD}</span>`;
        document.body.appendChild(badge);
    }

    _isTextInput(el) {
        return el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
    }

    /* ==================================================================
       MESSAGE HANDLERS - identical in set, order and behaviour to live
       ================================================================== */

    _setupMessageHandlers() {
        const conn = this.connection;

        conn.on(MSG_USERNAME_REQUIRED, () => {
            this.changeScreen('name-entry');
        });

        conn.on(MSG_WELCOME, (data) => this._handleWelcome(data));
        conn.on(MSG_WELCOME_BACK, (data) => this._handleWelcome(data));

        conn.on(MSG_USERNAME_TAKEN, () => {
            handleUsernameTaken(this);
        });

        conn.on(COUP_MSG_LOBBY_STATE, (players) => {
            this._lobbyPlayers = players;

            if (this.playerOrder.length > 0) {
                this._rebuildNameMapping();
            }

            if (this.currentScreen !== 'lobby' && this.currentScreen !== 'game') {
                this.changeScreen('lobby');
            }
            updateLobbyPlayers(players, this.myUserId);
        });

        conn.on(COUP_MSG_GAME_START, (data) => this._handleGameStart(data));

        conn.on(COUP_MSG_LOG, (text) => {
            if (this.currentScreen === 'game') addGameLog(text);
            else addLobbyLog(text);
        });

        conn.on(COUP_MSG_INPUT_RELAY, (data) => this._handleInputRelay(data));

        conn.on(COUP_MSG_RESYNC, (entries) => {
            for (const entry of entries) {
                this.engine.processRelay(entry.inputType, entry.pid, entry.data);
                this.lastRelaySeq = entry.seq;
            }
            if (this.currentScreen === 'game') renderGameState(this);
        });

        conn.on(COUP_MSG_RESYNC_FULL, (data) => {
            this.engine.initGame(data.seed, data.myPid, this.playerOrder.length);
            this.lastRelaySeq = -1;
        });

        conn.on(COUP_MSG_ACTION_REJECTED, () => {
            addGameLog('Action rejected by server');
            this.connection.send(encodeResyncReq(this.lastRelaySeq));
        });
    }

    _handleWelcome(data) {
        this.myUserId = data.userId;
        this.myUuid = data.uuid;
        localStorage.setItem('coup_uuid', data.uuid);
        localStorage.setItem('coup_username', data.username);
    }

    _handleGameStart(data) {
        const { seed, myPid, playerOrder } = data;
        this.playerOrder = playerOrder;
        this._rebuildNameMapping();
        this.engine.initGame(seed, myPid, playerOrder.length);
        this.lastRelaySeq = -1;
        this.changeScreen('game');
    }

    _rebuildNameMapping() {
        const lobby = this._lobbyPlayers || [];
        for (let pid = 0; pid < this.playerOrder.length; pid++) {
            const uid = this.playerOrder[pid];

            const lobbyP = lobby.find(p => p.id === uid);
            if (lobbyP) {
                this.playerNames[pid] = lobbyP.name;
                continue;
            }

            if (uid === 0xFF) {
                const bots = lobby.filter(p => p.isBot);
                const humanCount = this.playerOrder.filter(u => u !== 0xFF).length;
                const botIndex = pid - humanCount;
                this.playerNames[pid] = (botIndex >= 0 && botIndex < bots.length)
                    ? bots[botIndex].name
                    : `BOT ${pid + 1}`;
                continue;
            }

            if (!this.playerNames[pid]) {
                this.playerNames[pid] = `Player ${pid + 1}`;
            }
        }
    }

    _handleInputRelay(data) {
        const { seq, inputType, playerId, data: relayData } = data;

        // Log text is generated BEFORE the engine processes the relay, so it
        // can read pre-state (a card's identity before it is revealed). The
        // effect layer is notified here for the same reason.
        this._logRelay(inputType, playerId, relayData);
        if (this.currentScreen === 'game') {
            playRelayFx(this, inputType, playerId, relayData);
        }

        this.engine.processRelay(inputType, playerId, relayData);
        this.lastRelaySeq = seq;

        const events = this.engine.flushEvents();
        for (const evt of events) {
            if (evt.type === 'game_over') {
                const winnerName = this.playerNames[evt.winnerId] || `Player ${evt.winnerId + 1}`;
                addGameLog(`${winnerName} wins!`);
                setTimeout(() => this.changeScreen('game-over', evt.winnerId), 2000);
            } else if (evt.type === 'eliminated') {
                const eName = this.playerNames[evt.playerId] || `Player ${evt.playerId + 1}`;
                addGameLog(`${eName} is eliminated!`);
            }
        }

        if (this.currentScreen === 'game') renderGameState(this);
    }

    _logRelay(inputType, playerId, data) {
        if (this.currentScreen !== 'game') return;

        const name = this.playerNames[playerId] || `Player ${playerId + 1}`;
        let text = '';

        switch (inputType) {
            case RELAY_ACTION: {
                const action = data[0];
                const target = data[1];
                const targetName = this.playerNames[target] || `Player ${target + 1}`;

                switch (action) {
                    case ACT_INCOME:      text = `${name} takes Income`; break;
                    case ACT_FOREIGN_AID: text = `${name} declares Foreign Aid`; break;
                    case ACT_COUP:        text = `${name} launches Coup on ${targetName}`; break;
                    case ACT_TAX:         text = `${name} declares Tax (Duke)`; break;
                    case ACT_ASSASSINATE: text = `${name} declares Assassinate on ${targetName}`; break;
                    case ACT_STEAL:       text = `${name} declares Steal from ${targetName}`; break;
                    case ACT_EXCHANGE:    text = `${name} declares Exchange (Ambassador)`; break;
                }
                break;
            }
            case RELAY_RESPONSE: {
                const response = data[0];
                if (response === RESP_CHALLENGE) text = `${name} challenges!`;
                else if (response === RESP_BLOCK) text = `${name} blocks!`;
                // PASS is deliberately not logged - too noisy.
                break;
            }
            case RELAY_BLOCK_CLAIM: {
                const charName = CHARACTER_NAMES[data[0]] || 'Unknown';
                text = `${name} claims ${charName} to block`;
                break;
            }
            case RELAY_LOSE_INFLUENCE: {
                const cardIdx = data[0];
                const player = this.engine.players[playerId];
                if (player) {
                    const card = player.cards[cardIdx];
                    if (card && !card.revealed) {
                        text = `${name} reveals ${CHARACTER_NAMES[card.character] || '??'}`;
                    } else {
                        const other = player.cards[cardIdx === 0 ? 1 : 0];
                        text = (other && !other.revealed)
                            ? `${name} reveals ${CHARACTER_NAMES[other.character] || '??'}`
                            : `${name} loses influence`;
                    }
                } else {
                    text = `${name} loses influence`;
                }
                break;
            }
            case RELAY_EXCHANGE_CHOICE:
                text = `${name} completes exchange`;
                break;
            case RELAY_TIMEOUT:
                text = 'Timeout - responses auto-passed';
                break;
        }

        if (text) addGameLog(text);
    }

    sendConnect(uuid) {
        this.connection.send(encodeConnect(uuid));
    }

    /* ==================================================================
       SCREEN STACK
       ================================================================== */

    changeScreen(name, ...args) {
        const leavingGame = this.currentScreen === 'game' && name !== 'game';
        this.currentScreen = name;

        // The outgoing screen is removed SYNCHRONOUSLY, before the new one is
        // built. A crossfade would be prettier, but two screens in the DOM at
        // once means duplicate element IDs, and every render path here looks
        // elements up by ID - the new screen would race the corpse of the old
        // one. The fade-in alone still reads as a scene transition.
        const existing = this.viewport.querySelector('.screen');
        if (existing) existing.remove();
        if (leavingGame) stopTimer();

        let el;
        switch (name) {
            case 'title':      el = createTitleScreen(this); break;
            case 'connecting': el = createConnectingScreen(this); break;
            case 'name-entry': el = createNameEntryScreen(this); break;
            case 'lobby':      el = createLobbyScreen(this); break;
            case 'game':
                el = createGameScreen(this);
                setTimeout(() => renderGameState(this), 50);
                break;
            case 'game-over':  el = createGameOverScreen(this, ...args); break;
            default:
                el = document.createElement('div');
                el.className = 'screen';
                el.textContent = `Unknown screen: ${name}`;
        }

        this.viewport.appendChild(el);
        fadeIn(el);
    }

    showRules() {
        if (this._rulesOverlay) return;
        this._rulesOverlay = createRulesOverlay(() => this.hideRules());
        this.viewport.appendChild(this._rulesOverlay);
    }

    hideRules() {
        if (this._rulesOverlay) {
            this._rulesOverlay.remove();
            this._rulesOverlay = null;
        }
    }

    _showKickPopup() {
        const existing = this.viewport.querySelector('.kick-overlay');
        if (existing) existing.remove();

        const overlay = document.createElement('div');
        overlay.className = 'zoom-overlay kick-overlay';
        overlay.style.cursor = 'default';
        overlay.innerHTML = `
            <div class="panel kick-box">
                <h2>Kicked</h2>
                <div class="text-gray">You were removed from the server.</div>
                <button class="btn btn-red" id="kick-reconnect-btn">Reconnect</button>
            </div>
        `;
        this.viewport.appendChild(overlay);

        overlay.querySelector('#kick-reconnect-btn').addEventListener('click', () => {
            overlay.remove();
            this.connection.kicked = false;
            this.connection.autoReconnect = true;
            this.changeScreen('connecting');
        });
    }

    start() {
        this.changeScreen('title');
    }
}

document.addEventListener('DOMContentLoaded', () => {
    const app = new CoupApp();
    window.coupApp = app;   // debug access, as on live
    app.start();
});
