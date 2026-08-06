/**
 * title.js - Title screen.
 *
 * The Saturn title is B1_title on NBG1, the wordmark sprite with an animated
 * gouraud sheen, and a portrait parade with rim lighting. This is the same
 * composition: painted backdrop, the real 2172x724 wordmark, and the five
 * official 8-frame idles parading below it.
 */

import { LOGO, BGM, CHARACTERS, BG, preload } from '../assets.js';
import { portraitMedallion, screenShell } from '../ui.js';
import { audio } from '../audio.js';
import { sfx, SFX } from '../sfx.js';

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
            <div class="title-parade" id="title-parade"></div>
            <div class="title-buttons">
                <button class="btn btn-gold btn-lg" id="btn-play">Play</button>
                <button class="btn btn-dim" id="btn-rules-title">Rules</button>
            </div>
            <div class="title-footer">
                Web client &bull; cross-play with Sega Saturn over NetLink
            </div>
        </div>
    `);

    // The parade: one medallion per character, each idle offset so the five
    // do not breathe in lockstep. 333 ms is one frame of the 2.67 s cycle.
    const parade = el.querySelector('#title-parade');
    CHARACTERS.forEach((c, i) => {
        parade.appendChild(portraitMedallion(i, {
            stagger: i * 333,
            tag: c.name.toUpperCase(),
        }));
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
