/**
 * ui.js - Small shared DOM helpers for the staging client.
 *
 * Deliberately tiny and dependency-free. Every screen builds its markup with
 * template strings like the live client does, so the diff between the two
 * clients stays readable; this file only holds the pieces every screen needs.
 */

import { BG, portraitStrip, PORTRAIT_FRAMES } from './assets.js';
import { portrait, reducedMotion } from './fx.js';

/** HTML-escape untrusted text. Player names come off the wire. */
export function esc(str) {
    return String(str == null ? '' : str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

/**
 * Build a screen root with its painted backdrop already installed.
 *
 * @param {string} name   screen modifier, e.g. "title" -> .screen-title
 * @param {string} bgKey  key into the bg-* CSS classes ("title", "table", ...)
 */
export function screenShell(name, bgKey) {
    const el = document.createElement('div');
    el.className = `screen screen-${name}`;
    if (bgKey) {
        const bg = document.createElement('div');
        bg.className = `screen-bg bg-${bgKey}`;
        el.appendChild(bg);
    }
    return el;
}

/** An effect layer, scoped to a screen, for filmstrips and flashes. */
export function fxLayer(screenEl) {
    const layer = document.createElement('div');
    layer.className = 'fx-layer';
    screenEl.appendChild(layer);
    return layer;
}

/**
 * A framed, idling character portrait.
 * @param {number} character CHAR_* index
 * @param {{stagger?: number, tag?: string}} opts
 */
export function portraitMedallion(character, opts = {}) {
    const frame = document.createElement('div');
    frame.className = 'portrait-frame' + (opts.className ? ' ' + opts.className : '');
    frame.appendChild(portrait(character, { stagger: opts.stagger }));
    if (opts.tag) {
        const tag = document.createElement('span');
        tag.className = 'title-char-tag';
        tag.textContent = opts.tag;
        frame.appendChild(tag);
    }
    return frame;
}

/**
 * Apply a portrait strip to an existing element (used where the element is
 * built from an HTML string and only needs the animation wired afterwards).
 */
export function applyPortrait(el, character, stagger = 0) {
    const strip = portraitStrip(character);
    if (!el) return;
    if (!strip) { el.classList.add('portrait-unknown'); return; }
    el.style.backgroundImage = `url("${strip}")`;
    el.style.backgroundSize = `${PORTRAIT_FRAMES * 100}% 100%`;
    if (reducedMotion()) {
        el.style.backgroundPosition = '42.857% 0';
        return;
    }
    el.style.animationDuration = '2667ms';
    if (stagger) el.style.animationDelay = `-${stagger}ms`;
    el.classList.add('portrait-idle');
}

/** The backdrop URL for an overlay that wants the painted rules scene. */
export const OVERLAY_BG = BG.rules;
