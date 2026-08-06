/**
 * title.js - Title screen.
 *
 * The Saturn title is B1_title on NBG1, the wordmark sprite with an animated
 * gouraud sheen, and the cast shown below it. This is the same composition:
 * painted backdrop, the real 2172x724 wordmark, and the cast presented as a
 * slowly turning 3D ring of cards.
 *
 * WHY THE CARD RING REPLACED THE PORTRAIT PARADE
 *   The parade was five 8-frame portrait medallions in a flat row. The ring
 *   carries the same information - here are the roles - but at the size the
 *   art was drawn for: each card's name and illustration are legible when it
 *   reaches the front, which the 64-132 px medallions never were.
 *
 *   Keeping both was measured and rejected. On a short viewport (a phone held
 *   in landscape, ~390 px tall) the existing stack - wordmark, subtitle,
 *   buttons, footer and their gaps - already spends ~180 px of the ~374 px
 *   of content box. The ring needs the rest. Two looping animations stacked
 *   on top of each other would also compete: the parade idles on a 2.67 s
 *   cycle and the ring turns on a 24 s one, and neither would settle.
 *
 * WHY CSS ANIMATION AND NOT A rAF LOOP
 *   main.js's changeScreen() disposes of a screen with a bare
 *   `existing.remove()` - there is no teardown hook, and this module may not
 *   add one. A requestAnimationFrame loop started here would therefore run
 *   for the rest of the session, driving a detached DOM tree, every time the
 *   player leaves the title. CSS animations die with the element that owns
 *   them, so the leak cannot exist. They also run off the main thread, and a
 *   backgrounded tab resumes them in phase instead of jumping.
 */

import { LOGO, BGM, CHARACTERS, BG, cardArt, CARD_BACK, preload } from '../assets.js';
import { screenShell } from '../ui.js';
import { reducedMotion } from '../fx.js';
import { audio } from '../audio.js';
import { sfx, SFX } from '../sfx.js';

/**
 * The ring, in order: the five role faces, then the face-down back as a
 * sixth. Six is not decoration - it is what makes the ring read as a circle.
 * At 60 deg spacing the two cards a third of the way round (120 deg and
 * 240 deg) stay about half visible past the edges of the enlarged front card,
 * so you see the ring close behind itself. At 72 deg (five cards) those two
 * positions fall entirely behind the front card and the ring collapses into
 * a three-card fan.
 *
 * Every path comes from assets.js, which derives them from import.meta.url.
 * A literal "/assets/..." here would resolve against the LIVE site root and
 * silently serve the old art to the staging client.
 */
const CAROUSEL_FACES = [
    ...CHARACTERS.map((_, i) => cardArt(i)),   // duke .. contessa
    CARD_BACK,                                 // the face-down sixth
];

export function createTitleScreen(app) {
    const el = screenShell('title', 'title');

    el.insertAdjacentHTML('beforeend', `
        <img class="title-boxart" src="${LOGO.boxart}" alt="" draggable="false" />
        <div class="title-inner">
            <span class="title-wordmark-wrap">
                <img class="title-wordmark" src="${LOGO.wordmark}"
                     alt="COUP" draggable="false" />
            </span>
            <p class="title-subtitle">Saturn NetLink Edition</p>
            <div class="title-carousel" id="title-carousel" aria-hidden="true">
                <div class="tc-stage">
                    <div class="tc-ring" id="tc-ring"></div>
                </div>
            </div>
            <div class="title-buttons">
                <button class="btn btn-gold btn-lg" id="btn-play">Play</button>
                <button class="btn btn-dim" id="btn-rules-title">Rules</button>
            </div>
            <div class="title-footer">
                Web client &bull; cross-play with Sega Saturn over NetLink
            </div>
        </div>
    `);

    // The card ring.
    //
    // Every card is double-sided: its own face, and the shared back on the
    // reverse, both with backface-visibility: hidden. So the half of the ring
    // that has turned past edge-on shows card BACKS rather than the role art
    // mirrored, which is what a single-faced ring would show.
    //
    // Only --i is set from here. The whole animation - the orbit, the
    // enlargement of the front card and the fade of the far ones - is CSS
    // driven off that one index; see the "TITLE CARD CAROUSEL" block in
    // style.css. Nothing in this module runs per frame.
    const carousel = el.querySelector('#title-carousel');
    const ring = el.querySelector('#tc-ring');

    // Two independent guards on motion. This one settles it at build time;
    // the CSS gates the same block on @media (prefers-reduced-motion:
    // no-preference), so a viewer who turns the OS setting on mid-session
    // drops to the static layout without a reload. The static form is the
    // BASE stylesheet, not an override, so neither guard can leave a
    // half-applied ring behind.
    if (!reducedMotion()) carousel.classList.add('tc-motion');

    CAROUSEL_FACES.forEach((src, i) => {
        const card = document.createElement('div');
        card.className = 'tc-card';
        card.style.setProperty('--i', String(i));
        card.innerHTML =
            '<div class="tc-inner">'
            + '<div class="tc-face tc-face-front"></div>'
            + '<div class="tc-face tc-face-back"></div>'
            + '</div>';
        card.querySelector('.tc-face-front').style.backgroundImage =
            `url("${src}")`;
        // The reverse carries the SAME role art, not the card back.
        // Backs on the far half meant three of the six cards showed no role
        // at all at any moment, dimmed to 0.38 - reported as "occasional
        // blank cards". The point of the ring is to show the roles, so both
        // sides show one. CSS counter-mirrors it (rotateY(180deg) alone
        // would paint the art reversed).
        card.querySelector('.tc-face-back').style.backgroundImage =
            `url("${src}")`;
        ring.appendChild(card);
    });

    el.querySelector('#btn-play').addEventListener('click', () => {
        audio.startBGM(BGM);
        // Unlocks the AudioContext as well as the BGM element - this is the
        // gesture that makes every later effect audible.
        audio.resumeFromUserGesture();
        sfx.play(SFX.UI_CONFIRM);
        app.changeScreen('connecting');
    });

    el.querySelector('#btn-rules-title').addEventListener('click', () => {
        audio.resumeFromUserGesture();
        sfx.play(SFX.UI_CONFIRM);
        app.showRules();
    });

    // The next screens' backdrops, fetched while the player reads the title.
    preload([BG.connecting, BG.lobby]);

    return el;
}
