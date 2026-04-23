/**
 * title.js - Title Screen with scrolling animated character parade
 */
import { audio } from '../audio.js';

const CHARACTERS = [
    { name: 'DUKE',       img: 'assets/DukePortrait.png', video: 'assets/Duke.mp4' },
    { name: 'ASSASSIN',   img: 'assets/AssassinPortrait.png', video: 'assets/Assassin.mp4' },
    { name: 'CAPTAIN',    img: 'assets/CaptainPortrait.png', video: 'assets/Captain.mp4' },
    { name: 'AMBASSADOR', img: 'assets/AmbassadorPortrait.png', video: 'assets/Ambassador.mp4' },
    { name: 'CONTESSA',   img: 'assets/ContessaPortrait.png', video: 'assets/Contessa.mp4' },
];

export function createTitleScreen(app) {
    const el = document.createElement('div');
    el.className = 'screen screen-title';

    // Build doubled character parade for seamless loop
    const chars = [...CHARACTERS, ...CHARACTERS];
    const paradeCards = chars.map(c =>
        `<div class="title-char-card">
            <video src="${c.video}" autoplay loop muted playsinline disablepictureinpicture></video>
            <span class="char-name-tag">${c.name}</span>
        </div>`
    ).join('');

    el.innerHTML = `
        <div class="title-character-parade">
            <div class="title-character-track">${paradeCards}</div>
        </div>
        <div class="title-overlay">
            <h1 class="title-logo">COUP</h1>
            <p class="title-subtitle">SATURN NETLINK EDITION</p>
            <div class="title-buttons">
                <button class="btn btn-lg btn-play" id="btn-play">PLAY</button>
                <button class="btn btn-dim" id="btn-rules-title">RULES</button>
            </div>
        </div>
        <div class="title-footer">Web Client v1.0 &bull; Cross-play with Sega Saturn</div>
    `;

    el.querySelector('#btn-play').addEventListener('click', () => {
        audio.startBGM('assets/rebellion.mp3');
        audio.resumeFromUserGesture();
        app.changeScreen('connecting');
    });

    el.querySelector('#btn-rules-title').addEventListener('click', () => {
        app.showRules();
    });

    return el;
}
