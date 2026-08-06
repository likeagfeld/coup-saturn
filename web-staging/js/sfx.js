/**
 * sfx.js - Sound effects, mirroring the Saturn build's.
 *
 * The live web client has never had any: audio.js implements BGM and nothing
 * else. The Saturn build has 21 CC0 effects and a per-character pitch scheme,
 * and the requirement is not "add some sounds" but "make the two clients sound
 * like the same game". So everything identifying below is a mirror of a
 * specific Saturn file, and scripts/qa/qa_web_sfx.py reads those files as the
 * source of truth and fails if this one drifts from them:
 *
 *   SFX             <- examples/coup/coup_sfx_ids.h   (names AND numbers)
 *   CHAR_PITCH_Q10  <- examples/coup/coup_sfx_pitch.h (ratio_q10[5])
 *   sfx-map.js      <- the coup_audio_play_sfx*() call sites in coup_game.c
 *
 * ---------------------------------------------------------------------------
 * PITCH: how a Saturn hardware register becomes a Web Audio float
 *
 * The Saturn cannot afford five recordings of each per-character cue - forty
 * samples do not fit in the 81,920 bytes Sound RAM leaves for effects. It does
 * not need to: the SCSP retunes a slot in hardware through its OCT/FNS pitch
 * register, so ONE sample serves all five characters at five pitches and the
 * player hears WHO is acting without reading the screen. Duke lowest,
 * Contessa highest, four semitones apart.
 *
 * A browser has no such storage problem, and shipping five recordings would
 * have been possible. It would also have been wrong: the pitch relationship IS
 * the design - it is what makes the Duke's block and the Contessa's block the
 * same gesture in two voices. AudioBufferSourceNode.playbackRate is the same
 * knob as OCT/FNS, so the SAME Q10 ratios are used here, unquantised.
 *
 * The Saturn's OCT/FNS encoding rounds those ratios to about a cent; that
 * difference is far below what a listener could match across two rooms, and
 * the alternative - reproducing the SCSP's integer quantisation in JS - would
 * bind the web client to a hardware register format for no audible gain.
 * ---------------------------------------------------------------------------
 */

import { asset } from './assets.js';

/* --- Effect ids: examples/coup/coup_sfx_ids.h ---------------------------
 * The numbers are load-bearing, not decorative: qa_web_sfx.py checks each one
 * against its #define, so a reordered C header cannot leave this table
 * pointing at the wrong sound.
 *
 * The C header's four compatibility aliases (CONFIRM, CANCEL, COINS,
 * ELIMINATED) are deliberately NOT mirrored. They exist so old Saturn call
 * sites cannot silently pick up a different sound; this file has no old call
 * sites, and two names for one effect is how a mapping drifts.
 */
export const SFX = Object.freeze({
    UI_MOVE: 0,          // short dry tick, no tail
    UI_CONFIRM: 1,       // rising two-tone, affirmative
    UI_CANCEL: 2,        // falling, the confirm inverted
    UI_CHALLENGE: 3,     // sharper and more urgent than confirm
    CARD_DEAL: 4,        // one paper slide - a card dealt
    DECK_PLACE: 5,       // heavier, a stack landing - the court deck
    CARD_REVEAL: 6,      // a card turned face up, with weight
    CARD_SHUFFLE: 7,     // cards moving, riffled - Ambassador exchange
    COIN_GAIN: 8,        // bright metal, upward
    COIN_SPEND: 9,       // duller, downward
    COUP_STRIKE: 10,     // heavy and final
    TURN_START: 11,      // short low chime, expectant
    INFLUENCE_LOST: 12,  // descending, damaged
    EXILED: 13,          // terminal, lower and longer
    CHALLENGE: 14,       // sharp, confrontational, rising
    COUNTER: 15,         // firm, closing - a door shutting
    ACT_SUCCESS: 16,     // resolved, upward
    ACT_FAIL: 17,        // deflating, downward
    ASSASSINATE: 18,     // a blade - quiet, then sharp
    STEAL: 19,           // a snatch
    VICTORY: 20,         // a three-note figure - the game is won
});

/** id -> asset base name. Index order IS the id order above. */
export const SFX_NAMES = Object.freeze([
    'ui_move', 'ui_confirm', 'ui_cancel', 'ui_challenge',
    'card_deal', 'deck_place', 'card_reveal', 'card_shuffle',
    'coin_gain', 'coin_spend', 'coup_strike', 'turn_start',
    'influence_lost', 'exiled', 'challenge', 'counter',
    'act_success', 'act_fail', 'assassinate', 'steal', 'victory',
]);

/* --- Per-character pitch: examples/coup/coup_sfx_pitch.h ----------------
 * Q10 playback ratios, four semitones apart, spanning a major third either
 * side of centre. Copied verbatim from ratio_q10[5]; qa_web_sfx.py asserts
 * the two are identical.
 *
 *   Duke        2^(-4/12) * 1024 =  812.7 -> 813
 *   Assassin    2^(-2/12) * 1024 =  912.4 -> 912
 *   Captain     2^( 0/12) * 1024 = 1024.0 -> 1024
 *   Ambassador  2^( 2/12) * 1024 = 1149.4 -> 1149
 *   Contessa    2^( 4/12) * 1024 = 1290.2 -> 1290
 */
export const CHAR_PITCH_Q10 = Object.freeze([813, 912, 1024, 1149, 1290]);

/** COUP_SCSP_PITCH_UNITY: "play the sample at the rate it was authored". */
export const PITCH_UNITY = 1024;

/** How many characters have a voice. Anything else plays unpitched. */
const NUM_VOICES = 5;

/**
 * playbackRate for a character's voice.
 *
 * Mirrors coup_sfx_pitch_word()'s contract, including its escape hatch:
 * CHAR_FACEDOWN, CHAR_NONE and "a bot with no claimed card" are all outside
 * 0..4 and play at the authored pitch, so no caller has to special-case them.
 */
export function pitchRate(character) {
    if (typeof character !== 'number' || !(character >= 0) || character >= NUM_VOICES) {
        return 1;
    }
    return CHAR_PITCH_Q10[character | 0] / PITCH_UNITY;
}

/**
 * A voice for a SEAT rather than a card - coup_game.c seat_voice().
 *
 * "Turn begins" and "player exiled" have no character behind them, but they
 * are the events where knowing WHOSE matters most, so the seat index is folded
 * onto the same five pitches. A player hears their own turn start on the same
 * note every time.
 */
export function seatVoice(playerId) {
    return ((playerId | 0) % NUM_VOICES + NUM_VOICES) % NUM_VOICES;
}

/* ------------------------------------------------------------------------ */

/* Two of the same effect inside this window is a double-trigger, not a
 * rhythm - e.g. a Steal moves coins on two seats and would fire coin_gain
 * twice in the same frame. Saturn is bounded by its eight SCSP slots; this is
 * the browser's equivalent. */
const RETRIGGER_GUARD_MS = 30;

const DEFAULT_VOLUME = 0.5;

/** The container this browser can actually decode. */
function pickExtension() {
    if (typeof document === 'undefined') return '.m4a';
    try {
        const probe = document.createElement('audio');
        if (probe.canPlayType && probe.canPlayType('audio/webm; codecs="opus"')) {
            return '.webm';
        }
    } catch (_) { /* exotic environment; fall through to the safe format */ }
    // Safari's Opus-in-WebM support arrived late and is uneven across the iOS
    // versions people actually run. AAC-in-MP4 is decodable everywhere this
    // client's ES modules are.
    return '.m4a';
}

class SfxPlayer {
    constructor() {
        this.ctx = null;
        this.gain = null;
        this.ext = pickExtension();
        this.buffers = new Map();     // id -> AudioBuffer
        this.loading = new Map();     // id -> Promise (dedupes concurrent loads)
        this.lastPlayed = new Map();  // id -> timestamp
        this.muted = false;
        this.volume = DEFAULT_VOLUME;
        this.unlocked = false;
        this.failed = false;          // Web Audio unavailable; stay silent
    }

    /** URL of one effect, resolved relative to this module - never absolute. */
    url(id) {
        const name = SFX_NAMES[id];
        return name ? asset('sfx/' + name + this.ext) : null;
    }

    _ensureContext() {
        if (this.ctx || this.failed) return this.ctx;
        try {
            const Ctor = window.AudioContext || window.webkitAudioContext;
            if (!Ctor) { this.failed = true; return null; }
            this.ctx = new Ctor();
            this.gain = this.ctx.createGain();
            this.gain.gain.value = this.muted ? 0 : this.volume;
            this.gain.connect(this.ctx.destination);
        } catch (_) {
            // No Web Audio, or the context limit was hit. The game must still
            // be playable, so this is a permanent, silent downgrade.
            this.failed = true;
            this.ctx = null;
        }
        return this.ctx;
    }

    /**
     * Satisfy the browser autoplay policy. MUST be called from inside a real
     * user gesture (click / keydown / touch).
     *
     * A suspended AudioContext does not error when you start a source on it -
     * it just never makes a sound, and then resumes later and plays nothing.
     * So the unlock is what makes every later play() work, and it is wired to
     * the first gesture of any kind rather than to one particular button.
     */
    unlock() {
        const ctx = this._ensureContext();
        if (!ctx) return;
        if (ctx.state === 'suspended') {
            // Never throws: a rejected resume() just means still-locked.
            Promise.resolve(ctx.resume()).catch(() => {});
        }
        if (!this.unlocked) {
            this.unlocked = true;
            this.preload();
        }
    }

    /** Warm every effect. ~45 KB total, so this is one small burst, once. */
    preload() {
        for (let id = 0; id < SFX_NAMES.length; id++) {
            this._load(id).catch(() => {});
        }
    }

    _load(id) {
        if (this.buffers.has(id)) return Promise.resolve(this.buffers.get(id));
        if (this.loading.has(id)) return this.loading.get(id);

        const ctx = this._ensureContext();
        const url = this.url(id);
        if (!ctx || !url) return Promise.resolve(null);

        const p = fetch(url)
            .then(r => (r.ok ? r.arrayBuffer()
                : Promise.reject(new Error(`${r.status} ${url}`))))
            // The callback form as well as the promise form: Safari's older
            // decodeAudioData resolves through the callback only.
            .then(buf => new Promise((res, rej) =>
                ctx.decodeAudioData(buf, res, rej)))
            .then(decoded => { this.buffers.set(id, decoded); return decoded; })
            .catch(() => null)          // a missing or undecodable effect is
            .finally(() => this.loading.delete(id));   // silence, never a throw

        this.loading.set(id, p);
        return p;
    }

    setMuted(muted) {
        this.muted = !!muted;
        if (this.gain && this.ctx) {
            // A short ramp rather than a step: a gain jump mid-sample is an
            // audible click.
            try {
                this.gain.gain.setTargetAtTime(
                    this.muted ? 0 : this.volume, this.ctx.currentTime, 0.01);
            } catch (_) {
                this.gain.gain.value = this.muted ? 0 : this.volume;
            }
        }
    }

    /** Play an effect at its authored pitch. */
    play(id) {
        this.playAs(id, -1);
    }

    /**
     * Play an effect in a character's voice.
     * @param {number} id         SFX.*
     * @param {number} character  CHAR_* 0..4, or anything else for no transpose
     */
    playAs(id, character) {
        if (this.muted || this.failed) return;
        if (typeof id !== 'number' || !SFX_NAMES[id]) return;

        const now = (typeof performance !== 'undefined' && performance.now)
            ? performance.now() : Date.now();
        const last = this.lastPlayed.get(id);
        if (last !== undefined && now - last < RETRIGGER_GUARD_MS) return;
        this.lastPlayed.set(id, now);

        const ctx = this._ensureContext();
        if (!ctx) return;

        const buf = this.buffers.get(id);
        if (!buf) {
            // Not decoded yet (first play of the session, or the unlock only
            // just happened). Fetch it and play when it lands - late is better
            // than never, and by design nothing here can reject.
            this._load(id).then(b => {
                if (b && !this.muted) this._start(b, character);
            }).catch(() => {});
            return;
        }
        this._start(buf, character);
    }

    _start(buffer, character) {
        const ctx = this.ctx;
        if (!ctx || !this.gain) return;
        try {
            const src = ctx.createBufferSource();
            src.buffer = buffer;
            // The whole per-character scheme, in one assignment. Derived from
            // CHAR_PITCH_Q10 so the Saturn and this cannot disagree.
            src.playbackRate.value = pitchRate(character);
            src.connect(this.gain);
            src.start(0);
        } catch (_) {
            // A blocked or over-subscribed context must never take the game
            // down with it.
        }
    }
}

export const sfx = new SfxPlayer();
