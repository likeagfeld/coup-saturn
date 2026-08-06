/**
 * game-engine.js - Coup Game State Engine (JavaScript port)
 *
 * Ports the xorshift32 PRNG, deck init, Fisher-Yates shuffle, and state
 * tracking from coup_rules.c. Processes INPUT_RELAY messages to maintain
 * identical game state to the C engine on Saturn clients.
 */

import {
    RELAY_START_GAME, RELAY_ACTION, RELAY_RESPONSE,
    RELAY_BLOCK_CLAIM, RELAY_LOSE_INFLUENCE, RELAY_EXCHANGE_CHOICE,
    RELAY_TIMEOUT,
    CHAR_DUKE, CHAR_ASSASSIN, CHAR_CAPTAIN, CHAR_AMBASSADOR, CHAR_CONTESSA,
    CHAR_FACEDOWN, CHAR_NONE,
    ACT_INCOME, ACT_FOREIGN_AID, ACT_COUP, ACT_TAX,
    ACT_ASSASSINATE, ACT_STEAL, ACT_EXCHANGE,
    RESP_PASS, RESP_CHALLENGE, RESP_BLOCK
} from './protocol.js';

// Constants matching coup_rules.h
const MAX_PLAYERS = 7;
const CARDS_PER_PLAYER = 2;
const DECK_SIZE = 15;
const NUM_CHARACTERS = 5;
const INITIAL_COINS = 2;
const COUP_COST = 7;
const ASSASSINATE_COST = 3;
const FORCE_COUP_COINS = 10;

// Phase enum matching coup_rules.h
export const PHASE = {
    LOBBY: 0,
    WAITING_FOR_ACTION: 1,
    CHALLENGE_WINDOW: 2,
    BLOCK_WINDOW: 3,
    BLOCK_CHALLENGE_WINDOW: 4,
    WAITING_FOR_INFLUENCE_LOSS: 5,
    WAITING_FOR_EXCHANGE: 6,
    RESOLVING: 7
};

// Action -> claimed character (-1 = none)
const ACTION_CLAIM = [-1, -1, -1, CHAR_DUKE, CHAR_ASSASSIN, CHAR_CAPTAIN, CHAR_AMBASSADOR];
const ACTION_NEEDS_TARGET = [false, false, true, false, true, true, false];

// Which characters can block which actions
// Foreign Aid can be blocked by Duke
// Assassinate can be blocked by Contessa
// Steal can be blocked by Captain or Ambassador
const BLOCKABLE_BY = {
    [ACT_FOREIGN_AID]: [CHAR_DUKE],
    [ACT_ASSASSINATE]: [CHAR_CONTESSA],
    [ACT_STEAL]: [CHAR_CAPTAIN, CHAR_AMBASSADOR]
};

// Character names
export const CHARACTER_NAMES = ['Duke', 'Assassin', 'Captain', 'Ambassador', 'Contessa'];
export const ACTION_NAMES = ['Income', 'Foreign Aid', 'Coup', 'Tax', 'Assassinate', 'Steal', 'Exchange'];

// --- xorshift32 PRNG (must match coup_rules.c exactly) ---

export function xorshift32(state) {
    let x = state >>> 0;
    x ^= (x << 13) >>> 0;
    x ^= x >>> 17;
    x ^= (x << 5) >>> 0;
    return x >>> 0;
}

// --- Game State Engine ---

export class GameEngine {
    constructor() {
        this.reset();
    }

    reset() {
        this.seed = 0;
        this.rngState = 0;
        this.playerCount = 0;
        this.myPid = -1;
        this.gameActive = false;
        this.phase = PHASE.LOBBY;

        // Player state
        this.players = [];
        this.deck = [];
        this.deckTop = 0;

        // Turn state
        this.turnOrderCount = 0;
        this.turnOrder = [];
        this.currentTurnIdx = 0;
        this.roundNumber = 0;

        // Action state
        this.currentAction = -1;
        this.actionPlayer = -1;
        this.actionTarget = 0xFF;
        this.actionClaim = -1;
        this.blockerId = 0xFF;
        this.blockerClaim = -1;
        this.influenceLoser = 0xFF;
        this.challengerId = -1;
        this.pendingResponses = new Uint8Array(MAX_PLAYERS);
        this.currentBlockableBy = 0;

        // Exchange state
        this.exchangePlayer = 0xFF;
        this.exchangeCards = [CHAR_NONE, CHAR_NONE, CHAR_NONE, CHAR_NONE];

        // Sequence tracking
        this.lastSeq = -1;

        // Event log for UI
        this.events = [];
    }

    _rand() {
        this.rngState = xorshift32(this.rngState);
        return this.rngState;
    }

    _initDeck() {
        this.deck = new Array(DECK_SIZE);
        // Build deck same order as C: 3 copies of each character
        this.deckTop = 0; // deckTop = number of cards remaining (matches C deck_count)
        for (let c = 0; c < NUM_CHARACTERS; c++) {
            for (let i = 0; i < 3; i++) {
                this.deck[this.deckTop++] = c;
            }
        }
        // deckTop = DECK_SIZE (15) after init
    }

    _shuffleDeck() {
        // Must match C: Fisher-Yates over deckTop (remaining) cards only
        for (let i = this.deckTop - 1; i > 0; i--) {
            const j = this._rand() % (i + 1);
            const tmp = this.deck[i];
            this.deck[i] = this.deck[j];
            this.deck[j] = tmp;
        }
    }

    _drawCard() {
        // Draw from end of deck (matches C: deck[--deck_count])
        if (this.deckTop <= 0) return CHAR_NONE;
        return this.deck[--this.deckTop];
    }

    _returnCard(card) {
        // Add to end of deck (matches C: deck[deck_count++])
        if (this.deckTop < DECK_SIZE) {
            this.deck[this.deckTop++] = card;
        }
    }

    _clearPending() {
        this.pendingResponses.fill(0);
    }

    _playerAlive(pid) {
        const p = this.players[pid];
        return p && p.alive;
    }

    _countAlive() {
        let count = 0;
        for (let i = 0; i < this.playerCount; i++) {
            if (this.players[i].alive) count++;
        }
        return count;
    }

    _nextAlivePlayer() {
        for (let step = 0; step < this.playerCount; step++) {
            this.currentTurnIdx = (this.currentTurnIdx + 1) % this.turnOrderCount;
            const pid = this.turnOrder[this.currentTurnIdx];
            if (this._playerAlive(pid)) return pid;
        }
        return this.turnOrder[this.currentTurnIdx];
    }

    _advanceTurn() {
        const prevIdx = this.currentTurnIdx;
        const nextPid = this._nextAlivePlayer();

        // Check if we wrapped around for a new round
        if (this.currentTurnIdx <= prevIdx && this.gameActive) {
            this.roundNumber++;
        }

        this.phase = PHASE.WAITING_FOR_ACTION;
        this.currentAction = -1;
        this.actionPlayer = -1;
        this.actionTarget = 0xFF;
        this.actionClaim = -1;
        this.blockerId = 0xFF;
        this.blockerClaim = -1;
        this.influenceLoser = 0xFF;
        this.challengerId = -1;
        this.currentBlockableBy = 0;
        this._clearPending();
    }

    _eliminatePlayer(pid) {
        const p = this.players[pid];
        p.alive = false;
        this._emitEvent('eliminated', { playerId: pid });

        if (this._countAlive() <= 1) {
            this.gameActive = false;
            for (let i = 0; i < this.playerCount; i++) {
                if (this.players[i].alive) {
                    this._emitEvent('game_over', { winnerId: i });
                    break;
                }
            }
        }
    }

    _loseInfluence(pid, cardIdx) {
        const p = this.players[pid];
        if (cardIdx < 0 || cardIdx >= CARDS_PER_PLAYER) cardIdx = 0;
        if (p.cards[cardIdx].revealed) {
            // Pick the other card
            cardIdx = (cardIdx === 0) ? 1 : 0;
        }
        p.cards[cardIdx].revealed = true;
        this._emitEvent('influence_lost', {
            playerId: pid,
            cardIdx,
            character: p.cards[cardIdx].character
        });

        // Check if player is eliminated (both cards revealed)
        const alive = p.cards.some(c => !c.revealed);
        if (!alive) {
            this._eliminatePlayer(pid);
        }
    }

    _playerHasCharacter(pid, character) {
        const p = this.players[pid];
        return p.cards.some(c => !c.revealed && c.character === character);
    }

    _replaceCard(pid, character) {
        // Return the card to deck, shuffle, draw new
        const p = this.players[pid];
        for (let i = 0; i < CARDS_PER_PLAYER; i++) {
            if (!p.cards[i].revealed && p.cards[i].character === character) {
                this._returnCard(character);
                this._shuffleDeck();
                p.cards[i].character = this._drawCard();
                break;
            }
        }
    }

    _emitEvent(type, data) {
        this.events.push({ type, ...data });
    }

    // --- Initialize from GAME_START ---

    initGame(seed, myPid, playerCount) {
        this.reset();
        this.seed = seed;
        this.rngState = seed || 1;
        this.myPid = myPid;
        this.playerCount = playerCount;

        // Create player state
        for (let i = 0; i < playerCount; i++) {
            this.players.push({
                coins: INITIAL_COINS,
                alive: true,
                cards: [
                    { character: CHAR_NONE, revealed: false },
                    { character: CHAR_NONE, revealed: false }
                ]
            });
        }

        // Init deck
        this._initDeck();
    }

    // --- Process INPUT_RELAY ---

    processRelay(inputType, playerId, data) {
        switch (inputType) {
            case RELAY_START_GAME:
                return this._processStartGame();
            case RELAY_ACTION:
                return this._processAction(playerId, data[0], data[1]);
            case RELAY_RESPONSE:
                return this._processResponse(playerId, data[0]);
            case RELAY_BLOCK_CLAIM:
                return this._processBlockClaim(playerId, data[0]);
            case RELAY_LOSE_INFLUENCE:
                return this._processLoseInfluence(playerId, data[0]);
            case RELAY_EXCHANGE_CHOICE:
                return this._processExchangeChoice(playerId, data[0], data[1]);
            case RELAY_TIMEOUT:
                return this._processTimeout();
        }
    }

    _processStartGame() {
        // Shuffle and deal
        this._shuffleDeck();

        for (let i = 0; i < this.playerCount; i++) {
            this.players[i].cards[0].character = this._drawCard();
            this.players[i].cards[1].character = this._drawCard();
            this.players[i].coins = INITIAL_COINS;
        }

        // Build turn order (0, 1, 2, ... playerCount-1)
        this.turnOrder = [];
        for (let i = 0; i < this.playerCount; i++) {
            this.turnOrder.push(i);
        }
        this.turnOrderCount = this.playerCount;
        this.currentTurnIdx = this.playerCount - 1; // will advance to 0

        this.gameActive = true;
        this._advanceTurn();

        this._emitEvent('game_started', { playerCount: this.playerCount });
    }

    _processAction(pid, action, target) {
        if (this.phase !== PHASE.WAITING_FOR_ACTION) return;

        this.actionPlayer = pid;
        this.currentAction = action;
        this.actionTarget = target;
        this.actionClaim = ACTION_CLAIM[action] !== undefined ? ACTION_CLAIM[action] : -1;

        this._emitEvent('action_declared', { playerId: pid, action, target });

        // Immediate actions: Income, Coup
        if (action === ACT_INCOME) {
            this.players[pid].coins++;
            this._advanceTurn();
            return;
        }

        if (action === ACT_COUP) {
            if (this.players[pid].coins < COUP_COST) return;
            this.players[pid].coins -= COUP_COST;
            this.influenceLoser = target;
            this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
            return;
        }

        // Deduct Assassinate cost upfront (matches C engine — no refund on block/challenge)
        if (action === ACT_ASSASSINATE) {
            if (this.players[pid].coins < ASSASSINATE_COST) return;
            this.players[pid].coins -= ASSASSINATE_COST;
        }

        // Challengeable actions (Tax, Assassinate, Steal, Exchange)
        if (this.actionClaim >= 0) {
            // Open challenge window
            this.phase = PHASE.CHALLENGE_WINDOW;
            this._clearPending();
            for (let i = 0; i < this.playerCount; i++) {
                if (i !== pid && this._playerAlive(i)) {
                    this.pendingResponses[i] = 1;
                }
            }
            return;
        }

        // Foreign Aid - not challengeable but blockable
        if (action === ACT_FOREIGN_AID) {
            this.phase = PHASE.BLOCK_WINDOW;
            this._clearPending();
            for (let i = 0; i < this.playerCount; i++) {
                if (i !== pid && this._playerAlive(i)) {
                    this.pendingResponses[i] = 1;
                }
            }
            this.currentBlockableBy = (1 << CHAR_DUKE);
            return;
        }
    }

    _processResponse(pid, response) {
        if (this.phase !== PHASE.CHALLENGE_WINDOW &&
            this.phase !== PHASE.BLOCK_WINDOW &&
            this.phase !== PHASE.BLOCK_CHALLENGE_WINDOW) return;

        this.pendingResponses[pid] = 0;

        if (response === RESP_CHALLENGE) {
            this.challengerId = pid;
            this._emitEvent('challenge', { challengerId: pid });

            if (this.phase === PHASE.BLOCK_CHALLENGE_WINDOW) {
                // Challenging the blocker
                return this._resolveBlockChallenge(pid);
            }

            // Challenging the action
            return this._resolveActionChallenge(pid);
        }

        if (response === RESP_BLOCK) {
            this._emitEvent('block_declared', { blockerId: pid });
            this.blockerId = pid;
            this.phase = PHASE.RESOLVING;
            // Need to know what character the blocker claims
            return;
        }

        // PASS - check if all have responded
        if (response === RESP_PASS) {
            this._checkAllResponded();
        }
    }

    _checkAllResponded() {
        for (let i = 0; i < this.playerCount; i++) {
            if (this.pendingResponses[i]) return; // still waiting
        }

        // All passed
        if (this.phase === PHASE.CHALLENGE_WINDOW) {
            // Action not challenged - resolve or open block window
            const blockable = BLOCKABLE_BY[this.currentAction];
            if (blockable && blockable.length > 0) {
                this.phase = PHASE.BLOCK_WINDOW;
                this._clearPending();
                // Only target can block targeted actions, anyone for untargeted
                if (ACTION_NEEDS_TARGET[this.currentAction] && this.actionTarget !== 0xFF) {
                    if (this._playerAlive(this.actionTarget)) {
                        this.pendingResponses[this.actionTarget] = 1;
                    }
                } else {
                    for (let i = 0; i < this.playerCount; i++) {
                        if (i !== this.actionPlayer && this._playerAlive(i)) {
                            this.pendingResponses[i] = 1;
                        }
                    }
                }
                this.currentBlockableBy = 0;
                for (const ch of blockable) {
                    this.currentBlockableBy |= (1 << ch);
                }
                return;
            }
            this._resolveAction();
        } else if (this.phase === PHASE.BLOCK_WINDOW) {
            // Not blocked - resolve action
            this._resolveAction();
        } else if (this.phase === PHASE.BLOCK_CHALLENGE_WINDOW) {
            // Block not challenged - block succeeds, action fails
            this._advanceTurn();
        }
    }

    _resolveActionChallenge(challengerId) {
        const defender = this.actionPlayer;
        const claimed = this.actionClaim;

        if (this._playerHasCharacter(defender, claimed)) {
            // Challenge fails - challenger loses influence
            this._emitEvent('challenge_result', {
                success: false, challengerId, defenderId: defender, character: claimed
            });
            this._replaceCard(defender, claimed);
            this.influenceLoser = challengerId;
            this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
            // After challenger loses influence, check if action still resolves
            this._afterChallengeAction = 'resolve';
        } else {
            // Challenge succeeds - action player loses influence
            this._emitEvent('challenge_result', {
                success: true, challengerId, defenderId: defender, character: claimed
            });
            this.influenceLoser = defender;
            this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
            this._afterChallengeAction = 'cancel';
        }
    }

    _resolveBlockChallenge(challengerId) {
        const blocker = this.blockerId;
        const claimed = this.blockerClaim;

        if (this._playerHasCharacter(blocker, claimed)) {
            // Challenge fails - challenger loses influence, block stands
            this._emitEvent('challenge_result', {
                success: false, challengerId, defenderId: blocker, character: claimed
            });
            this._replaceCard(blocker, claimed);
            this.influenceLoser = challengerId;
            this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
            this._afterChallengeAction = 'cancel'; // block stands = action cancelled
        } else {
            // Challenge succeeds - blocker loses influence, action resolves
            this._emitEvent('challenge_result', {
                success: true, challengerId, defenderId: blocker, character: claimed
            });
            this.influenceLoser = blocker;
            this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
            this._afterChallengeAction = 'resolve';
        }
    }

    _processBlockClaim(pid, character) {
        if (this.phase !== PHASE.RESOLVING) return;
        if (pid !== this.blockerId) return;

        this.blockerClaim = character;
        this._emitEvent('block_claim', { blockerId: pid, character });

        // Open block challenge window
        this.phase = PHASE.BLOCK_CHALLENGE_WINDOW;
        this._clearPending();
        for (let i = 0; i < this.playerCount; i++) {
            if (i !== pid && this._playerAlive(i)) {
                this.pendingResponses[i] = 1;
            }
        }
    }

    _processLoseInfluence(pid, cardIdx) {
        if (this.phase !== PHASE.WAITING_FOR_INFLUENCE_LOSS) return;
        if (pid !== this.influenceLoser) return;

        this._loseInfluence(pid, cardIdx);

        if (!this.gameActive) return;

        // What happens after losing influence depends on context
        const afterAction = this._afterChallengeAction;
        this._afterChallengeAction = null;

        if (afterAction === 'resolve') {
            // Successful defense or block-challenge success
            const blockable = BLOCKABLE_BY[this.currentAction];
            if (blockable && blockable.length > 0 && this.blockerId === 0xFF) {
                // Still need block window after successful challenge defense
                this.phase = PHASE.BLOCK_WINDOW;
                this._clearPending();
                if (ACTION_NEEDS_TARGET[this.currentAction] && this.actionTarget !== 0xFF) {
                    if (this._playerAlive(this.actionTarget)) {
                        this.pendingResponses[this.actionTarget] = 1;
                    }
                } else {
                    for (let i = 0; i < this.playerCount; i++) {
                        if (i !== this.actionPlayer && this._playerAlive(i)) {
                            this.pendingResponses[i] = 1;
                        }
                    }
                }
                this.currentBlockableBy = 0;
                for (const ch of blockable) {
                    this.currentBlockableBy |= (1 << ch);
                }
                return;
            }
            this._resolveAction();
        } else if (afterAction === 'cancel') {
            this._advanceTurn();
        } else {
            // Normal influence loss (e.g. from Coup) - advance turn
            this._advanceTurn();
        }
    }

    _processExchangeChoice(pid, keep0, keep1) {
        if (this.phase !== PHASE.WAITING_FOR_EXCHANGE) return;

        const p = this.players[pid];
        const held = this.exchangeCards.filter(c => c !== CHAR_NONE);

        // Count unrevealed slots (matches C logic)
        let unrevealedSlots = 0;
        for (let i = 0; i < CARDS_PER_PLAYER; i++) {
            if (!p.cards[i].revealed) unrevealedSlots++;
        }

        if (unrevealedSlots === 1) {
            // 1-slot case: keep only keep0, return all others (matches C)
            for (let i = 0; i < CARDS_PER_PLAYER; i++) {
                if (!p.cards[i].revealed) {
                    p.cards[i].character = held[keep0];
                    break;
                }
            }
            for (let i = 0; i < held.length; i++) {
                if (i !== keep0) {
                    this._returnCard(held[i]);
                }
            }
        } else {
            // Normal 2-slot case: keep keep0 and keep1
            let slot = 0;
            for (let i = 0; i < CARDS_PER_PLAYER; i++) {
                if (!p.cards[i].revealed) {
                    p.cards[i].character = (slot === 0) ? held[keep0] : held[keep1];
                    slot++;
                }
            }
            for (let i = 0; i < held.length; i++) {
                if (i !== keep0 && i !== keep1) {
                    this._returnCard(held[i]);
                }
            }
        }
        this._shuffleDeck();

        this._advanceTurn();
    }

    _processTimeout() {
        // Timeout all pending responses to PASS
        this._clearPending();

        if (this.phase === PHASE.CHALLENGE_WINDOW ||
            this.phase === PHASE.BLOCK_WINDOW ||
            this.phase === PHASE.BLOCK_CHALLENGE_WINDOW) {
            this._checkAllResponded();
        } else if (this.phase === PHASE.RESOLVING) {
            // Blocker timed out - action resolves
            this.blockerId = 0xFF;
            this._resolveAction();
        }
    }

    _resolveAction() {
        const action = this.currentAction;
        const pid = this.actionPlayer;
        const target = this.actionTarget;

        switch (action) {
            case ACT_FOREIGN_AID:
                this.players[pid].coins += 2;
                this._advanceTurn();
                break;

            case ACT_TAX:
                this.players[pid].coins += 3;
                this._advanceTurn();
                break;

            case ACT_ASSASSINATE:
                // Coins already deducted upfront in _processAction
                if (target !== 0xFF && this._playerAlive(target)) {
                    this.influenceLoser = target;
                    this.phase = PHASE.WAITING_FOR_INFLUENCE_LOSS;
                    this._afterChallengeAction = null;
                } else {
                    this._advanceTurn();
                }
                break;

            case ACT_STEAL:
                if (target !== 0xFF && this._playerAlive(target)) {
                    const stolen = Math.min(2, this.players[target].coins);
                    this.players[target].coins -= stolen;
                    this.players[pid].coins += stolen;
                }
                this._advanceTurn();
                break;

            case ACT_EXCHANGE: {
                // Draw 2 cards from deck
                const p = this.players[pid];
                this.exchangePlayer = pid;
                const drawn = [this._drawCard(), this._drawCard()];

                // Build exchange options: player's unrevealed cards + drawn cards
                let ci = 0;
                this.exchangeCards = [CHAR_NONE, CHAR_NONE, CHAR_NONE, CHAR_NONE];
                for (let i = 0; i < CARDS_PER_PLAYER; i++) {
                    if (!p.cards[i].revealed) {
                        this.exchangeCards[ci++] = p.cards[i].character;
                    }
                }
                for (const d of drawn) {
                    if (ci < 4) this.exchangeCards[ci++] = d;
                }

                this.phase = PHASE.WAITING_FOR_EXCHANGE;
                break;
            }

            default:
                this._advanceTurn();
        }
    }

    // --- Public API ---

    currentPlayer() {
        return this.turnOrder[this.currentTurnIdx];
    }

    getMyHand() {
        if (this.myPid < 0 || this.myPid >= this.playerCount) return [];
        if (this.myPid === 0xFF) return []; // spectator
        return this.players[this.myPid].cards.map(c => ({
            character: c.revealed ? c.character : c.character,
            revealed: c.revealed
        }));
    }

    getPlayerView(pid) {
        if (pid >= this.playerCount) return null;
        const p = this.players[pid];
        return {
            coins: p.coins,
            alive: p.alive,
            cards: p.cards.map((c, i) => ({
                character: (pid === this.myPid || c.revealed) ? c.character : CHAR_FACEDOWN,
                revealed: c.revealed
            }))
        };
    }

    getExchangeCards() {
        if (this.myPid === this.exchangePlayer) {
            return this.exchangeCards.filter(c => c !== CHAR_NONE);
        }
        return [];
    }

    validActions() {
        if (this.phase !== PHASE.WAITING_FOR_ACTION) return 0;
        if (this.currentPlayer() !== this.myPid) return 0;

        const coins = this.players[this.myPid].coins;
        let mask = 0;

        // Must coup if >= 10 coins
        if (coins >= FORCE_COUP_COINS) {
            return (1 << ACT_COUP);
        }

        mask |= (1 << ACT_INCOME);
        mask |= (1 << ACT_FOREIGN_AID);

        if (coins >= COUP_COST) mask |= (1 << ACT_COUP);
        mask |= (1 << ACT_TAX);
        if (coins >= ASSASSINATE_COST) mask |= (1 << ACT_ASSASSINATE);
        mask |= (1 << ACT_STEAL);
        mask |= (1 << ACT_EXCHANGE);

        return mask;
    }

    canRespond() {
        if (this.myPid < 0 || this.myPid >= this.playerCount) return false;
        if (this.myPid === 0xFF) return false;
        return this.pendingResponses[this.myPid] === 1;
    }

    /** Flush accumulated events and return them */
    flushEvents() {
        const events = this.events;
        this.events = [];
        return events;
    }
}
