/**
 * fx.js - Animation layer for the staging client.
 *
 * Every animation here is the browser-native equivalent of something the
 * Saturn client now does on VDP1/VDP2. The point is parity of EXPERIENCE, not
 * of technique - the Saturn collapses a textured quad with a distorted-sprite
 * command, we rotate a div in 3D; it steps a sprite's texture address, we step
 * a background-position. What is deliberately kept identical is the PACING,
 * because that is the part a player perceives.
 *
 *   Saturn effect                     | here
 *   ----------------------------------|--------------------------------------
 *   8-frame portrait idle, 2.67 s     | steps(8) over PORTRAIT_CYCLE_MS
 *   Y-axis trapezoid collapse + swap  | rotateY 0->90, swap art, -90->0
 *   zoom-point coin pop               | coin sprites on a bezier arc
 *   gouraud timer bar green->amber->red | width + colour ramp on rAF
 *   E1-E8 effect strips, 0.90 s       | steps(n) over EFFECT_MS
 *   slColOffsetA fade between scenes  | opacity ramp on the screen element
 *
 * PORTRAIT_CYCLE_MS is 2667 and not something rounder because the Saturn's
 * idle was retuned to exactly that after a "too fast" report (HANDOFF: "the
 * portrait idle was a bare / 8 inline ... both rates are now named constants,
 * asserted in SECONDS"). Changing it here would desync the two clients
 * visually, which is the one thing this port is trying to avoid.
 */

import { FX, PORTRAIT_FRAMES, portraitStrip, coinIcon } from './assets.js';

export const PORTRAIT_CYCLE_MS = 2667;   // 8 frames, matches the Saturn idle
export const EFFECT_MS = 900;            // Saturn effect pacing (0.30 -> 0.90 s)
export const FLIP_MS = 440;              // collapse + expand, both halves
export const TRANSITION_MS = 260;        // scene fade

/* --------------------------------------------------------------------------
 * Reduced motion
 *
 * Checked live rather than cached, so a viewer toggling the OS setting mid
 * session gets the change without a reload. Every animator below returns a
 * resolved promise in reduced mode so callers still sequence correctly - the
 * state change lands, only the motion is skipped.
 * ------------------------------------------------------------------------ */
const reduceQuery = window.matchMedia('(prefers-reduced-motion: reduce)');
export function reducedMotion() {
    return reduceQuery.matches;
}

const settled = () => Promise.resolve();

function finished(anim) {
    return anim.finished.catch(() => {});
}

/* --------------------------------------------------------------------------
 * Portrait idle - the 8-frame official animation
 * ------------------------------------------------------------------------ */

/**
 * Build an animated portrait element for a character.
 *
 * The strip is one row of 8 frames, so background-size is 800% wide and the
 * animation walks background-position-x 0% -> 100% in steps(8). The step count
 * MUST equal the frame count; steps(7) or steps(9) shows two half frames at
 * once, which reads as tearing.
 *
 * @param {number} character  CHAR_* index, or -1 for an unknown/face-down seat
 * @param {{stagger?: number, className?: string}} opts
 */
export function portrait(character, opts = {}) {
    const el = document.createElement('div');
    el.className = 'portrait' + (opts.className ? ' ' + opts.className : '');

    const strip = portraitStrip(character);
    if (!strip) {
        el.classList.add('portrait-unknown');
        return el;
    }

    el.style.backgroundImage = `url("${strip}")`;
    el.style.backgroundSize = `${PORTRAIT_FRAMES * 100}% 100%`;

    if (reducedMotion()) {
        // Hold a single expressive frame instead of cycling.
        el.style.backgroundPosition = '42.857% 0';   // frame 3 of 8
        return el;
    }

    el.style.animationDuration = PORTRAIT_CYCLE_MS + 'ms';
    // A negative delay starts the cycle part-way in, so a row of portraits
    // does not breathe in lockstep like a chorus line.
    if (opts.stagger) el.style.animationDelay = `-${opts.stagger}ms`;
    el.classList.add('portrait-idle');
    return el;
}

/* --------------------------------------------------------------------------
 * Card flip - Saturn's Y-axis collapse with a texture swap at the midpoint
 * ------------------------------------------------------------------------ */

/**
 * Flip a card element to a new face.
 *
 * Mirrors the Saturn exactly in structure: rotate to edge-on (the trapezoid
 * collapsing to the centre line), swap the art while the card has no visible
 * width, then rotate back out. Doing it as one 180deg spin would show the art
 * mirrored through the middle of the turn; two halves never do.
 *
 * @param {HTMLElement} el   element whose background-image is the card face
 * @param {string} newSrc    the face to swap in at the midpoint
 */
export function flipCard(el, newSrc) {
    if (!el) return settled();
    if (reducedMotion()) {
        el.style.backgroundImage = `url("${newSrc}")`;
        return settled();
    }

    const half = FLIP_MS / 2;
    const easing = 'cubic-bezier(0.4, 0.0, 1, 1)';
    el.classList.add('card-flipping');

    const collapse = el.animate(
        [{ transform: 'rotateY(0deg)' }, { transform: 'rotateY(90deg)' }],
        { duration: half, easing, fill: 'forwards' }
    );

    return finished(collapse).then(() => {
        el.style.backgroundImage = `url("${newSrc}")`;
        const expand = el.animate(
            [{ transform: 'rotateY(-90deg)' }, { transform: 'rotateY(0deg)' }],
            { duration: half, easing: 'cubic-bezier(0, 0, 0.2, 1)', fill: 'forwards' }
        );
        return finished(expand);
    }).then(() => {
        el.classList.remove('card-flipping');
        el.style.transform = '';
    });
}

/* --------------------------------------------------------------------------
 * Coin payout - an arc from the table to the receiving seat
 * ------------------------------------------------------------------------ */

/**
 * Fly `count` coin sprites from the treasury (or a paying seat) to a seat.
 *
 * The coins follow a quadratic bezier sampled into keyframes rather than a
 * straight translate, because a straight line between two panels reads as a
 * UI element sliding, and an arc reads as something being thrown. The arc
 * height scales with distance so short hops do not loop absurdly.
 *
 * Both endpoints are read as viewport rects and the sprites live in a fixed
 * overlay, so this works regardless of which container each end sits in.
 */
export function coinArc(fromEl, toEl, count = 1, layer = document.body) {
    if (!fromEl || !toEl || reducedMotion()) return settled();

    const a = fromEl.getBoundingClientRect();
    const b = toEl.getBoundingClientRect();
    if (!a.width || !b.width) return settled();

    const x0 = a.left + a.width / 2, y0 = a.top + a.height / 2;
    const x1 = b.left + b.width / 2, y1 = b.top + b.height / 2;
    const dist = Math.hypot(x1 - x0, y1 - y0);
    const arc = Math.min(180, Math.max(50, dist * 0.42));
    const cx = (x0 + x1) / 2, cy = Math.min(y0, y1) - arc;

    const n = Math.min(count, 5);          // more than 5 is visual noise
    const jobs = [];

    for (let i = 0; i < n; i++) {
        const coin = document.createElement('img');
        coin.src = coinIcon(1);
        coin.className = 'coin-fly';
        coin.alt = '';
        coin.setAttribute('aria-hidden', 'true');
        layer.appendChild(coin);

        const frames = [];
        const STEPS = 14;
        for (let s = 0; s <= STEPS; s++) {
            const t = s / STEPS, u = 1 - t;
            const x = u * u * x0 + 2 * u * t * cx + t * t * x1;
            const y = u * u * y0 + 2 * u * t * cy + t * t * y1;
            frames.push({
                transform: `translate3d(${x}px, ${y}px, 0) translate(-50%, -50%) `
                    + `rotate(${t * 540}deg) scale(${0.7 + 0.5 * Math.sin(Math.PI * t)})`,
                opacity: t > 0.88 ? 0 : 1,
            });
        }

        const anim = coin.animate(frames, {
            duration: 520 + i * 40,
            delay: i * 70,
            easing: 'cubic-bezier(0.3, 0, 0.4, 1)',
            fill: 'forwards',
        });
        jobs.push(finished(anim).then(() => coin.remove()));
    }

    return Promise.all(jobs);
}

/* --------------------------------------------------------------------------
 * Action effects - the E1-E7 filmstrips
 * ------------------------------------------------------------------------ */

/**
 * Play a named effect filmstrip centred on the screen (or on an anchor).
 *
 * The strip is stepped with steps(frames) over EFFECT_MS. Effects are purely
 * decorative and never block: the caller does not await this, and the element
 * removes itself. Two effects overlapping is fine and looks intentional.
 */
export function playEffect(name, layer, anchorEl) {
    const def = FX[name];
    if (!def || !layer || reducedMotion()) return settled();

    const el = document.createElement('div');
    el.className = 'fx-sprite';
    el.style.backgroundImage = `url("${def.src}")`;
    el.style.backgroundSize = `${def.frames * 100}% 100%`;
    el.style.setProperty('--fx-steps', def.frames);
    el.style.animationDuration = EFFECT_MS + 'ms';
    el.style.aspectRatio = String(def.ratio);

    if (anchorEl) {
        const r = anchorEl.getBoundingClientRect();
        const l = layer.getBoundingClientRect();
        el.style.left = (r.left - l.left + r.width / 2) + 'px';
        el.style.top = (r.top - l.top + r.height / 2) + 'px';
        el.classList.add('fx-anchored');
    }

    layer.appendChild(el);
    setTimeout(() => el.remove(), EFFECT_MS + 60);
    return settled();
}

/** Map a declared action id to its effect strip. Index = ACT_* constant. */
export const ACTION_EFFECT = [
    null,          // ACT_INCOME - a single coin, the coin arc carries it
    null,          // ACT_FOREIGN_AID - likewise
    'coup',        // ACT_COUP
    'tax',         // ACT_TAX
    'assassinate', // ACT_ASSASSINATE
    'steal',       // ACT_STEAL
    'exchange',    // ACT_EXCHANGE
];

/* --------------------------------------------------------------------------
 * Response timer bar - gouraud ramp green -> amber -> red
 * ------------------------------------------------------------------------ */

/**
 * Drive a timer bar element down from full to empty.
 *
 * The Saturn ramps a gouraud table green->amber->red across the bar as it
 * drains; here the same three stops are driven off the remaining fraction.
 *
 * The duration MUST come from the caller and not be hard-coded, because the
 * authoritative windows live on the SERVER (12 s challenge/block, 30 s
 * influence/exchange in server.py). This bar is a display of the server's
 * clock, never a second clock that could disagree with it - it never sends
 * anything when it reaches zero.
 */
export function startTimerBar(barEl, durationMs) {
    stopTimerBar(barEl);
    if (!barEl) return;

    const start = performance.now();
    barEl.style.width = '100%';
    barEl.dataset.phase = 'ok';

    if (reducedMotion()) {
        // Still communicate urgency, just without continuous motion: step the
        // bar at the two colour thresholds instead of every frame.
        const marks = [
            [durationMs * 0.5, '50%', 'warn'],
            [durationMs * 0.8, '20%', 'crit'],
            [durationMs, '0%', 'crit'],
        ];
        barEl._timers = marks.map(([at, w, p]) => setTimeout(() => {
            barEl.style.width = w;
            barEl.dataset.phase = p;
        }, at));
        return;
    }

    const tick = (now) => {
        const t = Math.min(1, (now - start) / durationMs);
        const left = 1 - t;
        barEl.style.width = (left * 100) + '%';
        barEl.dataset.phase = left > 0.5 ? 'ok' : left > 0.2 ? 'warn' : 'crit';
        if (t < 1) barEl._raf = requestAnimationFrame(tick);
    };
    barEl._raf = requestAnimationFrame(tick);
}

export function stopTimerBar(barEl) {
    if (!barEl) return;
    if (barEl._raf) cancelAnimationFrame(barEl._raf);
    barEl._raf = null;
    if (barEl._timers) barEl._timers.forEach(clearTimeout);
    barEl._timers = null;
}

/* --------------------------------------------------------------------------
 * Scene transition - the unified colour-offset fade
 * ------------------------------------------------------------------------ */

/** Fade a newly-inserted screen in. Mirrors the Saturn's slColOffsetA ramp. */
export function fadeIn(el) {
    if (!el || reducedMotion()) return settled();
    return finished(el.animate(
        [{ opacity: 0, transform: 'scale(1.012)' },
         { opacity: 1, transform: 'scale(1)' }],
        { duration: TRANSITION_MS, easing: 'ease-out' }
    ));
}

/** Fade an outgoing screen out before it is removed. */
export function fadeOut(el) {
    if (!el) return settled();
    if (reducedMotion()) { el.remove(); return settled(); }
    return finished(el.animate(
        [{ opacity: 1 }, { opacity: 0 }],
        { duration: TRANSITION_MS * 0.6, easing: 'ease-in', fill: 'forwards' }
    )).then(() => el.remove());
}

/** A white flash, for challenge results. Saturn: a +k colour-offset ramp. */
export function flash(layer, tone = 'white') {
    if (!layer || reducedMotion()) return settled();
    const el = document.createElement('div');
    el.className = 'fx-flash fx-flash-' + tone;
    layer.appendChild(el);
    return finished(el.animate(
        [{ opacity: 0 }, { opacity: 0.55, offset: 0.15 }, { opacity: 0 }],
        { duration: 420, easing: 'ease-out' }
    )).then(() => el.remove());
}

/** Attention pulse on a seat/panel, e.g. the player who just acted. */
export function pulse(el) {
    if (!el || reducedMotion()) return settled();
    return finished(el.animate(
        [{ transform: 'scale(1)' },
         { transform: 'scale(1.035)', offset: 0.4 },
         { transform: 'scale(1)' }],
        { duration: 380, easing: 'ease-out' }
    ));
}
