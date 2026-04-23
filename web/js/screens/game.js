/**
 * game.js - Main Game Screen (replicates Saturn layout)
 *
 * Layout: Log (top) | Left seats | Center phase | Right seats | Hand (bottom)
 * All phases: idle, select action, select target, challenge, block,
 *             block-challenge, lose influence, exchange
 */
import {
    encodeAction, encodeResponse, encodeBlockClaim,
    encodeLoseInfluence, encodeExchangeChoice,
    ACT_INCOME, ACT_FOREIGN_AID, ACT_COUP, ACT_TAX,
    ACT_ASSASSINATE, ACT_STEAL, ACT_EXCHANGE,
    RESP_PASS, RESP_CHALLENGE, RESP_BLOCK,
    CHAR_DUKE, CHAR_ASSASSIN, CHAR_CAPTAIN, CHAR_AMBASSADOR, CHAR_CONTESSA,
    CHAR_FACEDOWN, CHAR_NONE
} from '../protocol.js';
import { PHASE, CHARACTER_NAMES, ACTION_NAMES } from '../game-engine.js';
import { audio } from '../audio.js';

const CHAR_ABBREV = ['Du', 'As', 'Ca', 'Am', 'Co', '??', '--'];
const CHAR_CSS = ['duke', 'assassin', 'captain', 'ambassador', 'contessa', 'facedown', 'none'];
const CHAR_COLORS = {
    [CHAR_DUKE]: 'var(--char-duke)', [CHAR_ASSASSIN]: 'var(--char-assassin)',
    [CHAR_CAPTAIN]: 'var(--char-captain)', [CHAR_AMBASSADOR]: 'var(--char-ambassador)',
    [CHAR_CONTESSA]: 'var(--char-contessa)', [CHAR_FACEDOWN]: 'var(--char-facedown)',
};
const CHAR_PORTRAITS = {
    [CHAR_DUKE]: 'assets/DukePortrait.png', [CHAR_ASSASSIN]: 'assets/AssassinPortrait.png',
    [CHAR_CAPTAIN]: 'assets/CaptainPortrait.png', [CHAR_AMBASSADOR]: 'assets/AmbassadorPortrait.png',
    [CHAR_CONTESSA]: 'assets/ContessaPortrait.png',
};
const CHAR_VIDEOS = {
    [CHAR_DUKE]: 'assets/Duke.mp4', [CHAR_ASSASSIN]: 'assets/Assassin.mp4',
    [CHAR_CAPTAIN]: 'assets/Captain.mp4', [CHAR_AMBASSADOR]: 'assets/Ambassador.mp4',
    [CHAR_CONTESSA]: 'assets/Contessa.mp4',
};

// Blockable actions -> characters that can block
const BLOCKABLE_BY = {
    [ACT_FOREIGN_AID]: [CHAR_DUKE],
    [ACT_ASSASSINATE]: [CHAR_CONTESSA],
    [ACT_STEAL]: [CHAR_CAPTAIN, CHAR_AMBASSADOR],
};

const ACTIONS = [
    { id: ACT_INCOME,      label: 'Income',      detail: '+1 coin',            cost: '' },
    { id: ACT_FOREIGN_AID, label: 'Foreign Aid',  detail: '+2 coins',           cost: '' },
    { id: ACT_COUP,        label: 'Coup',         detail: 'Eliminate target',   cost: '-7' },
    { id: ACT_TAX,         label: 'Tax',          detail: '+3 coins',           cost: '', char: 'duke' },
    { id: ACT_ASSASSINATE, label: 'Assassinate',  detail: 'Kill target',        cost: '-3', char: 'assassin' },
    { id: ACT_STEAL,       label: 'Steal',        detail: 'Take 2 coins',       cost: '', char: 'captain' },
    { id: ACT_EXCHANGE,    label: 'Exchange',     detail: 'Swap cards',          cost: '', char: 'ambassador' },
];

export function createGameScreen(app) {
    const el = document.createElement('div');
    el.className = 'screen screen-game';

    el.innerHTML = `
        <div class="game-header-bar">
            <div class="game-header-buttons">
                <button class="btn btn-dim btn-sm" id="btn-rules-game">RULES</button>
                <button class="btn btn-green btn-sm" id="btn-log-game">LOG</button>
                <button class="btn btn-dim btn-sm" id="btn-mute-game">${audio.muted ? 'UNMUTE' : 'MUTE'}</button>
            </div>
            <span class="game-turn-label" id="game-turn-label"></span>
        </div>
        <div class="game-layout">
            <div class="game-seats-col" id="seats-left"></div>
            <div class="game-center-col">
                <div class="game-phase-panel" id="phase-panel">
                    <div class="phase-title-bar" id="phase-title">Waiting...</div>
                    <div class="phase-body" id="phase-body"></div>
                    <div class="phase-timer" id="phase-timer"><div class="phase-timer-bar" id="phase-timer-bar"></div></div>
                </div>
                <div class="game-hand-panel" id="hand-panel"></div>
                <div class="game-status-bar" id="game-status"></div>
            </div>
            <div class="game-seats-col" id="seats-right"></div>
            <div class="game-log-panel" id="game-log"></div>
        </div>
    `;

    // Rules button - click and keyboard
    el.querySelector('#btn-rules-game').addEventListener('click', () => app.showRules());
    el.querySelector('#btn-log-game').addEventListener('click', () => toggleLogOverlay(app));
    el.querySelector('#btn-mute-game').addEventListener('click', (e) => {
        const muted = audio.toggleMute();
        e.target.textContent = muted ? 'UNMUTE' : 'MUTE';
    });
    el.addEventListener('keydown', (e) => {
        if (e.key === 'r' || e.key === 'R') app.showRules();
        if (e.key === 'l' || e.key === 'L') toggleLogOverlay(app);
    });

    return el;
}

/* ============================================================
   FULL RENDER
   ============================================================ */

export function renderGameState(app) {
    const engine = app.engine;
    if (!engine || !engine.gameActive) return;
    renderSeats(app);
    renderHand(app);
    renderPhasePanel(app);
    renderStatus(app);
}

/* ============================================================
   OPPONENT SEATS (Saturn: 3 left, 3 right, clockwise from self+1)
   ============================================================ */

function renderSeats(app) {
    const engine = app.engine;
    const leftEl = document.getElementById('seats-left');
    const rightEl = document.getElementById('seats-right');
    if (!leftEl || !rightEl) return;

    // Collect opponents in clockwise order
    const opponents = [];
    for (let i = 0; i < engine.playerCount; i++) {
        if (i === engine.myPid) continue;
        opponents.push(i);
    }

    // Saturn arrangement: left (bottom→mid→top), right (top→mid→bottom)
    const leftPids = opponents.slice(0, 3);
    const rightPids = opponents.slice(3, 6);

    leftEl.innerHTML = '';
    rightEl.innerHTML = '';

    for (const pid of leftPids) leftEl.appendChild(createSeat(app, pid));
    // Pad empty seats
    for (let i = leftPids.length; i < 3; i++) leftEl.appendChild(createEmptySeat());

    for (const pid of rightPids) rightEl.appendChild(createSeat(app, pid));
    for (let i = rightPids.length; i < 3; i++) rightEl.appendChild(createEmptySeat());
}

function createSeat(app, pid) {
    const engine = app.engine;
    const view = engine.getPlayerView(pid);
    const name = app.playerNames[pid] || `P${pid}`;
    const isCurrent = engine.currentPlayer() === pid;

    const seat = document.createElement('div');
    seat.className = 'seat' + (view.alive ? '' : ' dead') + (isCurrent ? ' active-turn' : '');

    let cardsHtml = '';
    for (const card of view.cards) {
        const ch = card.character;
        const abbr = ch <= 4 ? CHAR_ABBREV[ch] : '??';
        const color = CHAR_COLORS[ch] || 'var(--char-facedown)';
        const revClass = card.revealed ? 'revealed' : '';
        cardsHtml += `<div class="seat-card ${revClass}" style="border-color:${color}">
            <span class="card-abbrev ${card.revealed ? 'revealed-abbrev' : ''}">${card.revealed ? abbr : '??'}</span>
        </div>`;
    }

    seat.innerHTML = `
        <div class="seat-header">
            <span class="seat-name">${esc(name)}</span>
            <span class="seat-coins">$${view.coins}</span>
            <span class="seat-dead-label">DEAD</span>
        </div>
        <div class="seat-cards">${cardsHtml}</div>
    `;

    // Click to target OR zoom
    seat.style.cursor = 'pointer';
    seat.addEventListener('click', () => {
        if (app._pendingTargetAction !== undefined && view.alive) {
            app.connection.send(encodeAction(app._pendingTargetAction, pid));
            app._pendingTargetAction = undefined;
            renderPhasePanel(app);
        } else {
            showSeatFullscreen(app, pid);
        }
    });

    return seat;
}

function createEmptySeat() {
    const seat = document.createElement('div');
    seat.className = 'seat';
    seat.style.opacity = '0.15';
    seat.innerHTML = '<div class="seat-header"><span class="seat-name text-gray">---</span></div>';
    return seat;
}

/* ============================================================
   PLAYER HAND (bottom) - Large animated portraits
   ============================================================ */

function renderHand(app) {
    const engine = app.engine;
    const panel = document.getElementById('hand-panel');
    if (!panel) return;

    const isMyTurn = engine.currentPlayer() === engine.myPid;
    panel.className = 'game-hand-panel' + (isMyTurn ? ' my-turn' : '');

    if (engine.myPid === 0xFF || engine.myPid < 0) {
        panel.innerHTML = '<div class="hand-spectator">SPECTATING</div>';
        return;
    }

    const me = engine.getPlayerView(engine.myPid);
    if (!me.alive) {
        panel.innerHTML = `
            <div class="hand-info">
                <div class="hand-player-name text-gray">${esc(app.playerNames[engine.myPid] || 'You')}</div>
                <div class="hand-dead-label">ELIMINATED</div>
            </div>
        `;
        return;
    }

    const hand = engine.getMyHand();
    let cardsHtml = '';
    for (let i = 0; i < hand.length; i++) {
        const card = hand[i];
        const ch = card.character;
        const charCss = ch <= 4 ? `char-${CHAR_CSS[ch]}` : '';
        const portrait = (!card.revealed && CHAR_PORTRAITS[ch]) || '';
        const name = CHARACTER_NAMES[ch] || '?';
        const revClass = card.revealed ? 'revealed' : '';

        let inner = '';
        const video = (!card.revealed && CHAR_VIDEOS[ch]) || '';
        if (video && !card.revealed) {
            inner = `<video src="${video}" class="card-portrait" autoplay loop muted playsinline webkit-playsinline disablepictureinpicture></video>
                     <span class="card-char-name" style="color:${CHAR_COLORS[ch] || '#fff'}">${name}</span>`;
        } else if (portrait && !card.revealed) {
            inner = `<img src="${portrait}" class="card-portrait" alt="${name}" draggable="false" />
                     <span class="card-char-name" style="color:${CHAR_COLORS[ch] || '#fff'}">${name}</span>`;
        } else {
            const color = card.revealed ? 'var(--text-gray)' : (CHAR_COLORS[ch] || '#fff');
            inner = `<span class="card-char-name" style="color:${color}">${name}</span>`;
        }

        cardsHtml += `<div class="hand-card ${revClass} ${charCss}" data-idx="${i}"
                           style="border-color:${CHAR_COLORS[ch] || 'var(--accent-dim)'}">${inner}</div>`;
    }

    panel.innerHTML = `
        <div class="hand-info-bar">
            <span class="hand-player-name">${esc(app.playerNames[engine.myPid] || 'You')}</span>
            <span class="hand-coins"><span class="coin-symbol"></span><span>${me.coins}</span></span>
        </div>
        <div class="hand-cards">${cardsHtml}</div>
    `;

    // Card click: influence loss OR fullscreen view (triple-tap Contessa = easter egg)
    panel.querySelectorAll('.hand-card').forEach(cardEl => {
        let tapCount = 0;
        let tapTimer = null;
        cardEl.addEventListener('click', () => {
            const idx = parseInt(cardEl.dataset.idx);
            if (engine.phase === PHASE.WAITING_FOR_INFLUENCE_LOSS &&
                engine.influenceLoser === engine.myPid &&
                !hand[idx].revealed) {
                app.connection.send(encodeLoseInfluence(idx));
                return;
            }
            if (hand[idx].revealed) return;

            tapCount++;
            if (tapTimer) clearTimeout(tapTimer);
            tapTimer = setTimeout(() => {
                if (tapCount >= 3 && hand[idx].character === CHAR_CONTESSA) {
                    showCardFullscreen(CHAR_CONTESSA, true);
                } else {
                    showCardFullscreen(hand[idx].character);
                }
                tapCount = 0;
            }, 350);
        });
    });
}

/* ============================================================
   PHASE PANEL (center) - Action select, challenges, blocks, etc.
   ============================================================ */

function renderPhasePanel(app) {
    const engine = app.engine;
    const titleEl = document.getElementById('phase-title');
    const bodyEl = document.getElementById('phase-body');
    const timerBar = document.getElementById('phase-timer-bar');
    if (!titleEl || !bodyEl) return;

    bodyEl.innerHTML = '';
    titleEl.className = 'phase-title-bar';

    // Timer
    if (engine.phase >= PHASE.CHALLENGE_WINDOW && engine.phase <= PHASE.BLOCK_CHALLENGE_WINDOW) {
        timerBar.style.width = '100%';
        timerBar.className = 'phase-timer-bar' +
            (engine.phase === PHASE.BLOCK_CHALLENGE_WINDOW ? ' purple' :
             engine.phase === PHASE.BLOCK_WINDOW ? ' blue' : '');
        startTimerAnimation(timerBar);
    } else {
        timerBar.style.width = '0%';
    }

    // Waiting for target selection
    if (app._pendingTargetAction !== undefined) {
        titleEl.textContent = `${ACTION_NAMES[app._pendingTargetAction]} - Select Target:`;
        titleEl.classList.add('accent-blue');
        renderTargetSelection(app, bodyEl);
        return;
    }

    switch (engine.phase) {
        case PHASE.WAITING_FOR_ACTION:
            if (engine.currentPlayer() === engine.myPid) {
                renderActionSelection(app, titleEl, bodyEl);
            } else {
                renderWaiting(app, titleEl, bodyEl);
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
                titleEl.textContent = 'Resolving...';
                bodyEl.innerHTML = '<div class="text-gray" style="padding:8px">Waiting for block claim...</div>';
            }
            break;
        case PHASE.WAITING_FOR_INFLUENCE_LOSS:
            renderInfluenceLoss(app, titleEl, bodyEl);
            break;
        case PHASE.WAITING_FOR_EXCHANGE:
            renderExchange(app, titleEl, bodyEl);
            break;
        default:
            titleEl.textContent = 'Waiting...';
    }
}

function renderWaiting(app, titleEl, bodyEl) {
    const engine = app.engine;
    const cp = engine.currentPlayer();
    const name = app.playerNames[cp] || `Player ${cp}`;
    titleEl.textContent = `Waiting for ${name}...`;
    bodyEl.innerHTML = `<div class="text-gray" style="padding:8px;text-align:center">
        ${esc(name)} is choosing an action</div>`;
}

function renderActionSelection(app, titleEl, bodyEl) {
    const engine = app.engine;
    titleEl.textContent = 'Select Action:';
    titleEl.classList.add('accent-green');

    const validMask = engine.validActions();

    for (const act of ACTIONS) {
        const enabled = !!(validMask & (1 << act.id));
        const item = document.createElement('div');
        item.className = 'phase-menu-item' + (enabled ? '' : ' disabled') +
            (act.char ? ` char-${act.char}` : '');

        item.innerHTML = `
            <span class="item-label">${act.label}</span>
            <span class="item-detail">${act.detail}</span>
            ${act.cost ? `<span class="item-cost">${act.cost}</span>` : ''}
        `;

        if (enabled) {
            item.addEventListener('click', () => {
                const needsTarget = [ACT_COUP, ACT_ASSASSINATE, ACT_STEAL].includes(act.id);
                if (needsTarget) {
                    app._pendingTargetAction = act.id;
                    renderPhasePanel(app);
                } else {
                    app.connection.send(encodeAction(act.id, 0xFF));
                }
            });
        }
        bodyEl.appendChild(item);
    }
}

function renderTargetSelection(app, bodyEl) {
    const engine = app.engine;
    for (let i = 0; i < engine.playerCount; i++) {
        if (i === engine.myPid || !engine._playerAlive(i)) continue;
        const name = app.playerNames[i] || `Player ${i}`;
        const view = engine.getPlayerView(i);

        const item = document.createElement('div');
        item.className = 'phase-menu-item';
        item.innerHTML = `
            <span class="item-label">${esc(name)}</span>
            <span class="item-cost">$${view.coins}</span>
        `;
        item.addEventListener('click', () => {
            app.connection.send(encodeAction(app._pendingTargetAction, i));
            app._pendingTargetAction = undefined;
            renderPhasePanel(app);
        });
        bodyEl.appendChild(item);
    }

    const cancelItem = document.createElement('div');
    cancelItem.className = 'phase-menu-item';
    cancelItem.innerHTML = '<span class="item-label text-red">Cancel</span>';
    cancelItem.addEventListener('click', () => {
        app._pendingTargetAction = undefined;
        renderPhasePanel(app);
    });
    bodyEl.appendChild(cancelItem);
}

function renderChallengePrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const actor = app.playerNames[engine.actionPlayer] || `Player ${engine.actionPlayer}`;
    const claimed = engine.actionClaim >= 0 ? CHARACTER_NAMES[engine.actionClaim] : '?';

    titleEl.textContent = `${actor} claims ${claimed}`;
    titleEl.classList.add('accent-red');

    if (engine.canRespond()) {
        addResponseItem(bodyEl, 'Allow', 'text-white', () => {
            app.connection.send(encodeResponse(RESP_PASS));
        });
        addResponseItem(bodyEl, 'CHALLENGE', 'text-red', () => {
            app.connection.send(encodeResponse(RESP_CHALLENGE));
        });
    } else {
        bodyEl.innerHTML = '<div class="text-gray" style="padding:8px">Waiting for responses...</div>';
    }
}

function renderBlockPrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const actor = app.playerNames[engine.actionPlayer] || `Player ${engine.actionPlayer}`;
    const actName = ACTION_NAMES[engine.currentAction] || '?';

    titleEl.textContent = `${actor}: ${actName}`;
    titleEl.classList.add('accent-red');

    if (engine.canRespond()) {
        addResponseItem(bodyEl, 'Allow', 'text-white', () => {
            app.connection.send(encodeResponse(RESP_PASS));
        });

        // Block options with character claims
        const blockChars = BLOCKABLE_BY[engine.currentAction] || [];
        for (const ch of blockChars) {
            const charName = CHARACTER_NAMES[ch];
            const cssClass = `char-${CHAR_CSS[ch]}`;
            const item = document.createElement('div');
            item.className = `phase-menu-item ${cssClass}`;
            item.innerHTML = `<span class="item-label">Block as ${charName}</span>`;
            item.addEventListener('click', () => {
                app.connection.send(encodeResponse(RESP_BLOCK));
            });
            bodyEl.appendChild(item);
        }
    } else {
        bodyEl.innerHTML = '<div class="text-gray" style="padding:8px">Waiting for responses...</div>';
    }
}

function renderBlockChallengePrompt(app, titleEl, bodyEl) {
    const engine = app.engine;
    const blocker = app.playerNames[engine.blockerId] || `Player ${engine.blockerId}`;
    const claimed = engine.blockerClaim >= 0 ? CHARACTER_NAMES[engine.blockerClaim] : '?';

    titleEl.textContent = `${blocker} blocks w/ ${claimed}`;
    titleEl.classList.add('accent-purple');

    if (engine.canRespond()) {
        addResponseItem(bodyEl, 'Allow', 'text-white', () => {
            app.connection.send(encodeResponse(RESP_PASS));
        });
        addResponseItem(bodyEl, 'CHALLENGE', 'text-red', () => {
            app.connection.send(encodeResponse(RESP_CHALLENGE));
        });
    } else {
        bodyEl.innerHTML = '<div class="text-gray" style="padding:8px">Waiting for responses...</div>';
    }
}

function renderBlockClaimSelection(app, titleEl, bodyEl) {
    const engine = app.engine;
    titleEl.textContent = 'Block with which character?';
    titleEl.classList.add('accent-purple');

    const blockableBy = engine.currentBlockableBy;
    for (let ch = 0; ch < 5; ch++) {
        if (!(blockableBy & (1 << ch))) continue;
        const item = document.createElement('div');
        item.className = `phase-menu-item char-${CHAR_CSS[ch]}`;
        item.innerHTML = `<span class="item-label">${CHARACTER_NAMES[ch]}</span>`;
        item.addEventListener('click', () => {
            app.connection.send(encodeBlockClaim(ch));
        });
        bodyEl.appendChild(item);
    }
}

function renderInfluenceLoss(app, titleEl, bodyEl) {
    const engine = app.engine;
    const loserId = engine.influenceLoser;
    const isMe = loserId === engine.myPid;

    titleEl.textContent = 'Lose Influence:';
    titleEl.classList.add('accent-red');

    if (isMe) {
        const hand = engine.getMyHand();
        for (let i = 0; i < hand.length; i++) {
            if (hand[i].revealed) continue;
            const ch = hand[i].character;
            const item = document.createElement('div');
            item.className = `phase-menu-item char-${CHAR_CSS[ch]}`;
            item.innerHTML = `<span class="item-label">${CHARACTER_NAMES[ch]}</span>`;
            item.addEventListener('click', () => {
                app.connection.send(encodeLoseInfluence(i));
            });
            bodyEl.appendChild(item);
        }
        bodyEl.insertAdjacentHTML('beforeend',
            '<div class="text-gray" style="padding:4px;font-size:11px">Select a card to lose or click it in your hand</div>');
    } else {
        const name = app.playerNames[loserId] || `Player ${loserId}`;
        bodyEl.innerHTML = `<div class="text-gray" style="padding:8px">${esc(name)} must lose influence...</div>`;
    }
}

function renderExchange(app, titleEl, bodyEl) {
    const engine = app.engine;
    const isMe = engine.exchangePlayer === engine.myPid;

    titleEl.textContent = 'Exchange: Keep 2 cards';
    titleEl.classList.add('accent-blue');

    if (!isMe) {
        const name = app.playerNames[engine.exchangePlayer] || `Player ${engine.exchangePlayer}`;
        bodyEl.innerHTML = `<div class="text-gray" style="padding:8px">${esc(name)} is exchanging cards...</div>`;
        return;
    }

    const cards = engine.getExchangeCards();
    const selected = new Set();

    for (let i = 0; i < cards.length; i++) {
        const ch = cards[i];
        const item = document.createElement('div');
        item.className = `phase-menu-item char-${CHAR_CSS[ch]}`;
        item.innerHTML = `<span class="item-label">${CHARACTER_NAMES[ch]}</span>
                          <span class="item-detail" id="exch-check-${i}"></span>`;
        item.addEventListener('click', () => {
            if (selected.has(i)) {
                selected.delete(i);
                item.classList.remove('selected');
                document.getElementById(`exch-check-${i}`).textContent = '';
            } else if (selected.size < 2) {
                selected.add(i);
                item.classList.add('selected');
                document.getElementById(`exch-check-${i}`).textContent = '[KEEP]';
            }
            if (selected.size === 2) {
                const picks = Array.from(selected);
                app.connection.send(encodeExchangeChoice(picks[0], picks[1]));
            }
        });
        bodyEl.appendChild(item);
    }

    bodyEl.insertAdjacentHTML('beforeend',
        `<div class="text-gray" style="padding:4px;font-size:11px">Select 2 cards to keep (${cards.length} available)</div>`);
}

function addResponseItem(bodyEl, label, colorClass, onClick) {
    const item = document.createElement('div');
    item.className = 'phase-menu-item';
    item.innerHTML = `<span class="item-label ${colorClass}">${label}</span>`;
    item.addEventListener('click', onClick);
    bodyEl.appendChild(item);
}

/* ============================================================
   STATUS BAR
   ============================================================ */

function renderStatus(app) {
    const engine = app.engine;
    const el = document.getElementById('game-status');
    if (!el) return;

    const cp = engine.currentPlayer();
    const name = app.playerNames[cp] || `Player ${cp}`;
    const isMyTurn = cp === engine.myPid;

    let text = '';
    switch (engine.phase) {
        case PHASE.WAITING_FOR_ACTION: text = isMyTurn ? 'YOUR TURN' : `${name}'s turn`; break;
        case PHASE.CHALLENGE_WINDOW: text = 'Challenge window'; break;
        case PHASE.BLOCK_WINDOW: text = 'Block window'; break;
        case PHASE.BLOCK_CHALLENGE_WINDOW: text = 'Block challenge window'; break;
        case PHASE.WAITING_FOR_INFLUENCE_LOSS: text = 'Losing influence'; break;
        case PHASE.WAITING_FOR_EXCHANGE: text = 'Exchange in progress'; break;
        case PHASE.RESOLVING: text = 'Resolving...'; break;
        default: text = 'Waiting...';
    }

    el.textContent = text;
    el.className = 'game-status-bar' + (isMyTurn ? ' my-turn' : '');

    // Update header turn label
    const turnLabel = document.getElementById('game-turn-label');
    if (turnLabel) {
        turnLabel.textContent = text;
        turnLabel.className = 'game-turn-label' + (isMyTurn ? ' my-turn' : '');
    }
}

/* ============================================================
   GAME LOG
   ============================================================ */

let _logOverlay = null;
const _logHistory = [];

export function addGameLog(text) {
    // Store in history array (reliable source for overlay)
    _logHistory.push(text);
    if (_logHistory.length > 200) _logHistory.shift();

    const logEl = document.getElementById('game-log');
    if (logEl) {
        const line = _makeLogLine(text);
        logEl.appendChild(line);
        logEl.scrollTop = logEl.scrollHeight;
        while (logEl.children.length > 40) logEl.removeChild(logEl.firstChild);
    }

    // Also update log overlay if open
    if (_logOverlay) {
        const overlayBody = _logOverlay.querySelector('#log-overlay-body');
        if (overlayBody) {
            overlayBody.appendChild(_makeLogLine(text));
            overlayBody.scrollTop = overlayBody.scrollHeight;
        }
    }
}

function _makeLogLine(text) {
    const line = document.createElement('div');
    line.className = 'log-line';
    const lower = text.toLowerCase();
    if (lower.includes('challenge')) line.classList.add('event-challenge');
    else if (lower.includes('block')) line.classList.add('event-block');
    else if (lower.includes('declares') || lower.includes('action')) line.classList.add('event-action');
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
    _logOverlay.className = 'log-overlay';
    _logOverlay.innerHTML = `
        <div class="log-overlay-header">
            <span class="log-overlay-title">GAME LOG</span>
            <button class="btn btn-red btn-sm" id="btn-close-log">CLOSE</button>
        </div>
        <div class="log-overlay-body" id="log-overlay-body"></div>
    `;

    const logBody = _logOverlay.querySelector('#log-overlay-body');

    // Build from history array (always reliable)
    if (_logHistory.length === 0) {
        logBody.innerHTML = '<div class="log-line" style="color:var(--text-gray);font-style:italic;padding:12px;">No log entries yet. Events will appear here as the game progresses.</div>';
    } else {
        for (const text of _logHistory) {
            logBody.appendChild(_makeLogLine(text));
        }
        setTimeout(() => { logBody.scrollTop = logBody.scrollHeight; }, 50);
    }

    _logOverlay.querySelector('#btn-close-log').addEventListener('click', () => {
        _logOverlay.remove();
        _logOverlay = null;
    });

    app.viewport.appendChild(_logOverlay);
}

/* ============================================================
   TIMER ANIMATION
   ============================================================ */

let _timerInterval = null;
function startTimerAnimation(timerBar) {
    if (_timerInterval) clearInterval(_timerInterval);
    let width = 100;
    // Approximate: challenge/block ~12s, shrink smoothly
    const step = 100 / (12 * 10); // 12 seconds at 100ms intervals
    _timerInterval = setInterval(() => {
        width -= step;
        if (width <= 0) { width = 0; clearInterval(_timerInterval); }
        timerBar.style.width = width + '%';
    }, 100);
}

export function stopTimer() {
    if (_timerInterval) { clearInterval(_timerInterval); _timerInterval = null; }
}

/* ============================================================
   FULLSCREEN CARD VIEW
   ============================================================ */

function showSeatFullscreen(app, pid) {
    const engine = app.engine;
    const view = engine.getPlayerView(pid);
    const name = app.playerNames[pid] || `Player ${pid}`;
    const isCurrent = engine.currentPlayer() === pid;

    let cardsHtml = '';
    for (const card of view.cards) {
        const ch = card.character;
        const charName = ch <= 4 ? CHARACTER_NAMES[ch] : '??';
        const abbr = ch <= 4 ? CHAR_ABBREV[ch] : '??';
        const color = CHAR_COLORS[ch] || 'var(--char-facedown)';
        const video = card.revealed ? CHAR_VIDEOS[ch] : null;

        if (card.revealed && video) {
            cardsHtml += `<div class="seat-zoom-card" style="border-color:${color}">
                <video src="${video}" autoplay loop muted playsinline webkit-playsinline disablepictureinpicture
                       class="seat-zoom-portrait"></video>
                <span class="seat-zoom-label" style="color:${color}">${charName}</span>
                <span class="seat-zoom-status">REVEALED</span>
            </div>`;
        } else {
            cardsHtml += `<div class="seat-zoom-card" style="border-color:${card.revealed ? 'var(--text-gray)' : color}">
                <span class="seat-zoom-abbrev">${card.revealed ? abbr : '??'}</span>
                ${card.revealed ? `<span class="seat-zoom-label" style="color:var(--text-gray)">${charName}</span>` : ''}
                ${card.revealed ? '<span class="seat-zoom-status">REVEALED</span>' : ''}
            </div>`;
        }
    }

    const overlay = document.createElement('div');
    overlay.className = 'card-fullscreen-overlay';
    overlay.innerHTML = `
        <div class="seat-zoom-name ${isCurrent ? 'active' : ''}" ${view.alive ? '' : 'style="color:var(--text-red)"'}>
            ${esc(name)} ${!view.alive ? '(DEAD)' : ''}
        </div>
        <div class="seat-zoom-coins">${view.coins} coins</div>
        <div class="seat-zoom-cards">${cardsHtml}</div>
        <div class="card-fullscreen-hint">Tap anywhere to close</div>
    `;

    overlay.addEventListener('click', () => overlay.remove());
    document.body.appendChild(overlay);
}

function showCardFullscreen(character, altVideo) {
    const video = altVideo && character === CHAR_CONTESSA
        ? 'assets/ContessaBoobs.mp4' : CHAR_VIDEOS[character];
    if (!video) return;

    const name = CHARACTER_NAMES[character] || '?';
    const color = CHAR_COLORS[character] || '#fff';

    const overlay = document.createElement('div');
    overlay.className = 'card-fullscreen-overlay';
    overlay.innerHTML = `
        <video src="${video}" autoplay loop muted playsinline webkit-playsinline disablepictureinpicture
               style="border-color:${color}"></video>
        <div class="card-fullscreen-name" style="color:${color}">${name}</div>
        <div class="card-fullscreen-hint">Tap anywhere to close</div>
    `;

    overlay.addEventListener('click', () => overlay.remove());
    document.body.appendChild(overlay);

    // Ensure video plays (mobile autoplay workaround)
    const vid = overlay.querySelector('video');
    vid.play().catch(() => {});
}

function esc(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
