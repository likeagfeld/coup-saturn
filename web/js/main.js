/**
 * main.js - Coup Web Client Entry Point
 *
 * Screen state machine, message routing, game engine integration.
 * Uses a game-viewport wrapper for 16:9 aspect ratio.
 */

import { Connection } from './connection.js';
import { GameEngine, ACTION_NAMES, CHARACTER_NAMES } from './game-engine.js';
import {
    MSG_USERNAME_REQUIRED, MSG_WELCOME, MSG_WELCOME_BACK, MSG_USERNAME_TAKEN,
    COUP_MSG_LOBBY_STATE, COUP_MSG_GAME_START, COUP_MSG_LOG,
    COUP_MSG_INPUT_RELAY, COUP_MSG_RESYNC, COUP_MSG_RESYNC_FULL,
    COUP_MSG_ACTION_REJECTED,
    RELAY_ACTION, RELAY_RESPONSE, RELAY_BLOCK_CLAIM,
    RELAY_LOSE_INFLUENCE, RELAY_EXCHANGE_CHOICE, RELAY_TIMEOUT,
    ACT_INCOME, ACT_FOREIGN_AID, ACT_COUP, ACT_TAX,
    ACT_ASSASSINATE, ACT_STEAL, ACT_EXCHANGE,
    RESP_PASS, RESP_CHALLENGE, RESP_BLOCK,
    encodeConnect, encodeResyncReq
} from './protocol.js';

import { createTitleScreen } from './screens/title.js';
import { createConnectingScreen } from './screens/connecting.js';
import { createNameEntryScreen, handleUsernameTaken } from './screens/name-entry.js';
import { createLobbyScreen, updateLobbyPlayers, addLobbyLog } from './screens/lobby.js';
import { createGameScreen, renderGameState, addGameLog } from './screens/game.js';
import { createGameOverScreen } from './screens/game-over.js';
import { createRulesOverlay } from './screens/rules.js';
import { audio } from './audio.js';

class CoupApp {
    constructor() {
        this.appRoot = document.getElementById('app');

        // Create the 16:9 game viewport
        this.viewport = document.createElement('div');
        this.viewport.className = 'game-viewport';
        this.appRoot.appendChild(this.viewport);

        this.currentScreen = null;
        this.engine = new GameEngine();
        this.playerNames = {};       // pid -> name
        this.playerOrder = [];       // user_id list from GAME_START
        this._lobbyPlayers = [];     // last lobby state for name mapping
        this.myUserId = 0;
        this.myUuid = '';
        this.lastRelaySeq = -1;
        this._pendingTargetAction = undefined;
        this._rulesOverlay = null;

        // Determine WS URL based on current location
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

        // Kick popup — server closes with code 4001 when kicking
        this.connection.onKicked = () => {
            this._showKickPopup();
        };

        // Mobile autoplay workaround: play all videos on EVERY user interaction
        const playAllVideos = () => {
            document.querySelectorAll('video').forEach(v => {
                if (v.paused) v.play().catch(() => {});
            });
        };
        document.addEventListener('touchstart', playAllVideos, { passive: true });
        document.addEventListener('click', playAllVideos);

        // Auto-play new videos as they're added to the DOM (MutationObserver)
        const videoObserver = new MutationObserver((mutations) => {
            for (const mutation of mutations) {
                for (const node of mutation.addedNodes) {
                    if (node.nodeName === 'VIDEO') {
                        node.play().catch(() => {});
                    }
                    if (node.querySelectorAll) {
                        node.querySelectorAll('video').forEach(v => v.play().catch(() => {}));
                    }
                }
            }
        });
        videoObserver.observe(document.body, { childList: true, subtree: true });

        // Global keyboard shortcut for rules
        document.addEventListener('keydown', (e) => {
            if ((e.key === 'r' || e.key === 'R') && !this._isTextInput(e.target)) {
                if (this._rulesOverlay) {
                    this.hideRules();
                } else {
                    this.showRules();
                }
            }
            if (e.key === 'Escape' && this._rulesOverlay) {
                this.hideRules();
            }
        });
    }

    _isTextInput(el) {
        return el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
    }

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
            // Store lobby players for name mapping
            this._lobbyPlayers = players;

            // Build pid->name mapping if game is active
            if (this.playerOrder.length > 0) {
                this._rebuildNameMapping();
            }

            if (this.currentScreen !== 'lobby' && this.currentScreen !== 'game') {
                this.changeScreen('lobby');
            }
            updateLobbyPlayers(players, this.myUserId);
        });

        conn.on(COUP_MSG_GAME_START, (data) => {
            this._handleGameStart(data);
        });

        conn.on(COUP_MSG_LOG, (text) => {
            if (this.currentScreen === 'game') {
                addGameLog(text);
            } else {
                addLobbyLog(text);
            }
        });

        conn.on(COUP_MSG_INPUT_RELAY, (data) => {
            this._handleInputRelay(data);
        });

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
            // Resync to fix any state divergence
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

        // Build name mapping from lobby data + playerOrder
        this._rebuildNameMapping();

        this.engine.initGame(seed, myPid, playerOrder.length);
        this.lastRelaySeq = -1;
        this.changeScreen('game');
    }

    _rebuildNameMapping() {
        const lobby = this._lobbyPlayers || [];
        for (let pid = 0; pid < this.playerOrder.length; pid++) {
            const uid = this.playerOrder[pid];

            // Try to find by user_id match
            const lobbyP = lobby.find(p => p.id === uid);
            if (lobbyP) {
                this.playerNames[pid] = lobbyP.name;
                continue;
            }

            // For bots (uid === 0xFF or similar), match by position
            if (uid === 0xFF) {
                const bots = lobby.filter(p => p.isBot);
                const humanCount = this.playerOrder.filter(u => u !== 0xFF).length;
                const botIndex = pid - humanCount;
                if (botIndex >= 0 && botIndex < bots.length) {
                    this.playerNames[pid] = bots[botIndex].name;
                } else {
                    this.playerNames[pid] = `BOT ${pid + 1}`;
                }
                continue;
            }

            // Fallback
            if (!this.playerNames[pid]) {
                this.playerNames[pid] = `Player ${pid + 1}`;
            }
        }
    }

    _handleInputRelay(data) {
        const { seq, inputType, playerId, data: relayData } = data;

        // Generate log text BEFORE processing (so we can read pre-state, e.g. card identity)
        this._logRelay(inputType, playerId, relayData);

        this.engine.processRelay(inputType, playerId, relayData);
        this.lastRelaySeq = seq;

        // Check for game events
        const events = this.engine.flushEvents();
        for (const evt of events) {
            if (evt.type === 'game_over') {
                const winnerName = this.playerNames[evt.winnerId] || `Player ${evt.winnerId + 1}`;
                addGameLog(`${winnerName} wins!`);
                setTimeout(() => {
                    this.changeScreen('game-over', evt.winnerId);
                }, 2000);
            } else if (evt.type === 'eliminated') {
                const eName = this.playerNames[evt.playerId] || `Player ${evt.playerId + 1}`;
                addGameLog(`${eName} is eliminated!`);
            }
        }

        if (this.currentScreen === 'game') {
            renderGameState(this);
        }
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
                    case ACT_INCOME:
                        text = `${name} takes Income`;
                        break;
                    case ACT_FOREIGN_AID:
                        text = `${name} declares Foreign Aid`;
                        break;
                    case ACT_COUP:
                        text = `${name} launches Coup on ${targetName}`;
                        break;
                    case ACT_TAX:
                        text = `${name} declares Tax (Duke)`;
                        break;
                    case ACT_ASSASSINATE:
                        text = `${name} declares Assassinate on ${targetName}`;
                        break;
                    case ACT_STEAL:
                        text = `${name} declares Steal from ${targetName}`;
                        break;
                    case ACT_EXCHANGE:
                        text = `${name} declares Exchange (Ambassador)`;
                        break;
                }
                break;
            }
            case RELAY_RESPONSE: {
                const response = data[0];
                if (response === RESP_CHALLENGE) {
                    text = `${name} challenges!`;
                } else if (response === RESP_BLOCK) {
                    text = `${name} blocks!`;
                }
                // Don't log PASS - too noisy
                break;
            }
            case RELAY_BLOCK_CLAIM: {
                const character = data[0];
                const charName = CHARACTER_NAMES[character] || 'Unknown';
                text = `${name} claims ${charName} to block`;
                break;
            }
            case RELAY_LOSE_INFLUENCE: {
                const cardIdx = data[0];
                const player = this.engine.players[playerId];
                if (player) {
                    // Read card character before engine processes (reveals) it
                    const card = player.cards[cardIdx];
                    if (card && !card.revealed) {
                        const charName = CHARACTER_NAMES[card.character] || '??';
                        text = `${name} reveals ${charName}`;
                    } else {
                        // Card already revealed (shouldn't happen normally), try other card
                        const otherIdx = cardIdx === 0 ? 1 : 0;
                        const other = player.cards[otherIdx];
                        if (other && !other.revealed) {
                            const charName = CHARACTER_NAMES[other.character] || '??';
                            text = `${name} reveals ${charName}`;
                        } else {
                            text = `${name} loses influence`;
                        }
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

        if (text) {
            addGameLog(text);
        }
    }

    sendConnect(uuid) {
        this.connection.send(encodeConnect(uuid));
    }

    changeScreen(name, ...args) {
        this.currentScreen = name;

        // Remove existing screen (but keep rules overlay if open)
        const existingScreen = this.viewport.querySelector('.screen');
        if (existingScreen) existingScreen.remove();

        let el;
        switch (name) {
            case 'title':
                el = createTitleScreen(this);
                break;
            case 'connecting':
                el = createConnectingScreen(this);
                break;
            case 'name-entry':
                el = createNameEntryScreen(this);
                break;
            case 'lobby':
                el = createLobbyScreen(this);
                break;
            case 'game':
                el = createGameScreen(this);
                setTimeout(() => renderGameState(this), 50);
                break;
            case 'game-over':
                el = createGameOverScreen(this, ...args);
                break;
            default:
                el = document.createElement('div');
                el.className = 'screen';
                el.textContent = `Unknown screen: ${name}`;
        }

        this.viewport.appendChild(el);
    }

    showRules() {
        if (this._rulesOverlay) return; // already open
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
        // Remove any existing kick overlay
        const existing = this.viewport.querySelector('.kick-overlay');
        if (existing) existing.remove();

        const overlay = document.createElement('div');
        overlay.className = 'kick-overlay';
        overlay.style.cssText = 'position:absolute;inset:0;background:rgba(0,0,0,0.85);display:flex;align-items:center;justify-content:center;z-index:9999;';
        overlay.innerHTML = `
            <div style="background:#1a1a2e;border:2px solid #e94560;border-radius:12px;padding:32px 28px;text-align:center;max-width:320px;width:90%;">
                <div style="font-size:22px;color:#e94560;font-weight:bold;margin-bottom:12px;">KICKED</div>
                <div style="color:#ccc;font-size:14px;margin-bottom:24px;">You were removed from the server.</div>
                <button id="kick-reconnect-btn" style="background:#e94560;color:#fff;border:none;border-radius:8px;padding:12px 32px;font-size:16px;font-weight:bold;cursor:pointer;">RECONNECT</button>
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

// --- Boot ---
document.addEventListener('DOMContentLoaded', () => {
    const app = new CoupApp();
    window.coupApp = app; // debug access
    app.start();
});
