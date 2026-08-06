/**
 * sfx-map.js - Which sound fires on which event.
 *
 * This is the mirror of the coup_audio_play_sfx() / coup_audio_play_sfx_as()
 * call sites in examples/coup/coup_game.c. It is a separate file from sfx.js
 * on purpose: sfx.js is "how to make a noise", this is "when", and only the
 * second one has to be argued against the Saturn line by line.
 * scripts/qa/qa_web_sfx.py asserts that the set of effects fired here is
 * exactly the set fired in coup_game.c.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE TWO CLIENTS DIFFER, AND WHY THEY STILL AGREE
 *
 * Saturn drives its audio from coup_event_t values produced by the rule
 * engine. This client has a rule engine too (game-engine.js) but it emits a
 * SMALLER set of events - there is no TURN_STARTED, no COINS_CHANGED and no
 * CARD_REPLACED. game-engine.js is a byte-identical copy of the live client's
 * and must stay wire-compatible, so it is not edited to add them.
 *
 * So the cues come from three places instead of one, chosen per cue to fire at
 * the same MOMENT the Saturn's does:
 *
 *   onRelay()    before engine.processRelay(), so pre-state is readable. This
 *                is where a claimed character still exists - once the relay is
 *                processed, "who was being challenged" is gone. It is the same
 *                reason _logRelay() and playRelayFx() already run here.
 *   onEvents()   after, from engine.flushEvents(), for facts only the engine
 *                knows: which card was actually revealed, who was eliminated,
 *                who won.
 *   onDeltas()   from the render snapshot game.js already computes, for the
 *                two things the engine does not announce: coins moving, and
 *                the turn passing.
 *
 * Nothing here sends, receives, or inspects anything on the wire.
 * ---------------------------------------------------------------------------
 */

import { sfx, SFX, seatVoice } from './sfx.js';
import {
    RELAY_ACTION, RELAY_RESPONSE, RELAY_BLOCK_CLAIM, RELAY_LOSE_INFLUENCE,
    RELAY_EXCHANGE_CHOICE,
    RESP_CHALLENGE, RESP_BLOCK,
    ACT_TAX, ACT_COUP, ACT_ASSASSINATE, ACT_STEAL, ACT_EXCHANGE,
    CHAR_DUKE, CHAR_ASSASSIN, CHAR_CAPTAIN, CHAR_AMBASSADOR,
} from './protocol.js';
import { PHASE } from './game-engine.js';

/**
 * The character an action CLAIMS - coup_game.c action_voice().
 *
 * Income, Foreign Aid and Coup claim nobody; they are the moves anyone can
 * make, and they get the unpitched sample, which is exactly how they should
 * read against the four that name a card.
 */
export function actionVoice(action) {
    switch (action) {
        case ACT_TAX:         return CHAR_DUKE;
        case ACT_ASSASSINATE: return CHAR_ASSASSIN;
        case ACT_STEAL:       return CHAR_CAPTAIN;
        case ACT_EXCHANGE:    return CHAR_AMBASSADOR;
        default:              return -1;
    }
}

/* ------------------------------------------------------------------------ */
/* 1. Relay-time cues (pre-state)                                            */
/* ------------------------------------------------------------------------ */

/**
 * Called from _handleInputRelay BEFORE engine.processRelay(), alongside
 * _logRelay() and playRelayFx().
 */
export function onRelay(app, inputType, playerId, data) {
    const engine = app.engine;

    switch (inputType) {
        case RELAY_ACTION: {
            /* coup_game.c COUP_EVT_ACTION_DECLARED: the declared act, in the
             * claimed character's voice. Income, Foreign Aid and Tax are
             * silent HERE on purpose - the coin movement that follows is
             * their sound (see onDeltas). */
            const action = data[0];
            switch (action) {
                case ACT_COUP:
                    sfx.play(SFX.COUP_STRIKE);
                    break;
                case ACT_ASSASSINATE:
                    sfx.playAs(SFX.ASSASSINATE, actionVoice(action));
                    break;
                case ACT_STEAL:
                    sfx.playAs(SFX.STEAL, actionVoice(action));
                    break;
                case ACT_EXCHANGE:
                    sfx.playAs(SFX.CARD_SHUFFLE, actionVoice(action));
                    break;
                default:
                    break;
            }
            break;
        }

        case RELAY_RESPONSE: {
            if (data[0] === RESP_CHALLENGE) {
                /* coup_game.c COUP_EVT_CHALLENGE_OPENED and
                 * COUP_EVT_BLOCK_CHALLENGE_OPENED: someone's claim is being
                 * called, in the voice of the card they claimed. Which claim
                 * that is depends on the window we are in, and BOTH are gone
                 * from the engine a moment later - which is why this cue lives
                 * pre-relay and not in onEvents(). */
                const claimed = engine.phase === PHASE.BLOCK_CHALLENGE_WINDOW
                    ? engine.blockerClaim
                    : engine.actionClaim;
                sfx.playAs(SFX.CHALLENGE, claimed);
            }
            /* RESP_BLOCK is deliberately silent here. Saturn's
             * COUP_EVT_BLOCK_DECLARED carries the blocking character and
             * plays the counter in that voice; on the wire the block and the
             * character it claims are two separate relays, and a counter with
             * no voice is the one thing this cue must not be. It fires on
             * RELAY_BLOCK_CLAIM below instead - one relay later, with the
             * voice. */
            break;
        }

        case RELAY_BLOCK_CLAIM:
            /* coup_game.c COUP_EVT_BLOCK_DECLARED: a block is a door closing,
             * in the blocker's voice - the Duke on foreign aid, the Contessa
             * on an assassination, Captain or Ambassador on a steal. */
            sfx.playAs(SFX.COUNTER, data[0]);
            break;

        case RELAY_LOSE_INFLUENCE:
        case RELAY_EXCHANGE_CHOICE:
            /* Both are announced by the engine with more information than the
             * relay carries; see onEvents(). */
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------ */
/* 2. Engine events (post-state)                                             */
/* ------------------------------------------------------------------------ */

/** Called with the array from engine.flushEvents(). */
export function onEvents(app, events) {
    for (const e of events) {
        switch (e.type) {
            case 'game_started':
                /* coup_game.c COUP_EVT_GAME_STARTED - the court deck going
                 * down on the table. */
                sfx.play(SFX.DECK_PLACE);
                break;

            case 'challenge_result': {
                /* coup_game.c COUP_EVT_CHALLENGE_RESULT and
                 * COUP_EVT_BLOCK_CHALLENGE_RESULT: resolved or deflated, in
                 * the voice of the card that settled it.
                 *
                 * Saturn keys on `defender_had_card`; this engine reports the
                 * same fact from the challenger's side, as `success`. They are
                 * exact opposites - a challenge succeeds precisely when the
                 * defender did NOT have the card - so the sounds swap. */
                sfx.playAs(e.success ? SFX.ACT_FAIL : SFX.ACT_SUCCESS,
                    e.character);
                break;
            }

            case 'influence_lost':
                /* coup_game.c COUP_EVT_INFLUENCE_LOST. Two sounds would
                 * collide here - the card is turned face up AND its owner is
                 * one influence poorer - so the loss is what plays, in the
                 * dead card's voice. That is why SFX.CARD_REVEAL ships but is
                 * never fired, on either client. */
                sfx.playAs(SFX.INFLUENCE_LOST, e.character);
                break;

            case 'eliminated':
                /* coup_game.c COUP_EVT_PLAYER_ELIMINATED. */
                sfx.playAs(SFX.EXILED, seatVoice(e.playerId));
                break;

            case 'game_over':
                /* coup_game.c's game-over branch: victory for us, the exile
                 * toll for everyone else. Unpitched on both clients. */
                if (e.winnerId === app.engine.myPid) sfx.play(SFX.VICTORY);
                else sfx.play(SFX.EXILED);
                break;

            default:
                break;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* 3. State deltas                                                           */
/* ------------------------------------------------------------------------ */

let _lastTurnPid = -1;
let _lastLocalCue = '';

/** Forget the per-match cue state, so a new game does not inherit one. */
export function resetSfxState() {
    _lastTurnPid = -1;
    _lastLocalCue = '';
}

/**
 * Which prompt, if any, is currently the LOCAL player's.
 *
 * renderGameState() rebuilds the phase panel on every relay, so a cue tied to
 * "the panel is showing X" would fire several times for one prompt. This
 * reduces the phase to a single string and the caller fires only on a change -
 * the same shape as Saturn's `if (g_state.phase != COUP_PHASE_...)` guards.
 */
function localCue(engine) {
    if (engine.phase === PHASE.WAITING_FOR_EXCHANGE
        && engine.exchangePlayer === engine.myPid) {
        return 'exchange';
    }
    if (!engine.canRespond || !engine.canRespond()) return '';
    if (engine.phase === PHASE.CHALLENGE_WINDOW
        || engine.phase === PHASE.BLOCK_CHALLENGE_WINDOW) {
        return 'challenge';
    }
    return '';
}

/**
 * Called once per render from game.js, with the coin deltas it already
 * computes for the coin-arc animation.
 *
 * coup_game.c COUP_EVT_COINS_CHANGED: gained is bright and upward, spent is
 * duller and downward. Paying out used to be silent on Saturn too, so a Coup
 * or an assassination cost seven or three coins with no sound at all.
 */
export function onDeltas(app, coinDeltas) {
    if (coinDeltas) {
        // One cue per direction per render, not one per player: a Steal is a
        // single event that moves two purses.
        if (coinDeltas.some(d => d.delta > 0)) sfx.play(SFX.COIN_GAIN);
        if (coinDeltas.some(d => d.delta < 0)) sfx.play(SFX.COIN_SPEND);
    }

    const engine = app.engine;
    if (!engine || !engine.gameActive) return;

    /* coup_game.c COUP_EVT_TURN_STARTED, plus the sync_ui_phase() cue for the
     * local player's own turn. Both clients play it for EVERY turn, not just
     * the local one - a remote player's turn used to begin in silence. */
    const pid = engine.currentPlayer();
    if (typeof pid === 'number' && pid !== _lastTurnPid) {
        if (_lastTurnPid !== -1) sfx.playAs(SFX.TURN_START, seatVoice(pid));
        _lastTurnPid = pid;
    }

    /* The prompts that are OURS. */
    const cue = localCue(engine);
    if (cue !== _lastLocalCue) {
        if (cue === 'challenge') {
            /* coup_game.c COUP_TURN_CHALLENGE_WINDOW / BLOCK_CHALLENGE_WINDOW:
             * the menu cue, not the game one. The challenge itself already
             * sounded on CHALLENGE_OPENED, for everyone; this is the narrower
             * fact that the prompt is yours. */
            sfx.play(SFX.UI_CHALLENGE);
        } else if (cue === 'exchange') {
            /* coup_game.c enter_human_exchange_phase(): the exchange cards
             * being put in front of you. The riffle already played when the
             * Ambassador was declared. */
            sfx.play(SFX.CARD_DEAL);
        }
        _lastLocalCue = cue;
    }
}
