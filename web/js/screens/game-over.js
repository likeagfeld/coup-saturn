/**
 * game-over.js - Victory/Defeat Screen
 *
 * Dramatic reveal with winner portrait, stats, and return-to-lobby.
 */

const CHAR_PORTRAITS = {
    0: 'assets/DukePortrait.png',
    1: 'assets/AssassinPortrait.png',
    2: 'assets/CaptainPortrait.png',
    3: 'assets/AmbassadorPortrait.png',
    4: 'assets/ContessaPortrait.png',
};

export function createGameOverScreen(app, winnerId) {
    const el = document.createElement('div');
    el.className = 'screen screen-game-over';

    const winnerName = app.playerNames[winnerId] || `Player ${winnerId}`;
    const isMe = winnerId === app.engine.myPid;

    // Try to get winner's surviving card for portrait
    let portraitHtml = '';
    try {
        const view = app.engine.getPlayerView(winnerId);
        if (view && view.cards) {
            for (const card of view.cards) {
                if (!card.revealed && CHAR_PORTRAITS[card.character]) {
                    portraitHtml = `<img src="${CHAR_PORTRAITS[card.character]}"
                                         class="gameover-portrait" alt="Winner"
                                         draggable="false" />`;
                    break;
                }
            }
        }
    } catch (_) { /* no engine state available */ }

    el.innerHTML = `
        <div class="gameover-bg ${isMe ? 'victory' : 'defeat'}"></div>
        <div class="gameover-content">
            ${portraitHtml}
            <h1 class="gameover-title">${isMe ? 'VICTORY!' : 'GAME OVER'}</h1>
            <p class="gameover-winner">${esc(winnerName)} WINS!</p>
            <p class="gameover-subtitle">${isMe ? 'You are the last one standing!' : 'Better luck next time...'}</p>
            <div style="display:flex; gap:clamp(8px,1.5vmin,16px); margin-top:clamp(12px,2vmin,24px)">
                <button class="btn btn-gold btn-lg" id="btn-return-lobby">RETURN TO LOBBY</button>
                <button class="btn btn-dim" id="btn-rules-gameover">RULES</button>
            </div>
        </div>
    `;

    el.querySelector('#btn-return-lobby').addEventListener('click', () => {
        app.engine.reset();
        app.changeScreen('lobby');
    });

    el.querySelector('#btn-rules-gameover').addEventListener('click', () => {
        app.showRules();
    });

    return el;
}

function esc(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}
