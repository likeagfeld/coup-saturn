/**
 * name-entry.js - Username input matching Saturn charset
 */
import { encodeSetUsername } from '../protocol.js';

export function createNameEntryScreen(app) {
    const el = document.createElement('div');
    el.className = 'screen screen-name-entry';
    el.innerHTML = `
        <div class="name-panel">
            <div class="name-title">ENTER YOUR NAME</div>
            <div class="name-input-wrapper">
                <input type="text" class="name-input" id="name-input"
                       maxlength="16" placeholder="YOUR NAME"
                       autocomplete="off" spellcheck="false" />
            </div>
            <div class="name-char-hint">^  [ ]  v</div>
            <div class="name-hint">A-Z, 0-9, SPACE only &bull; 16 chars max</div>
            <div class="name-error" id="name-error"></div>
            <div style="display:flex;gap:8px">
                <button class="btn btn-green" id="btn-submit-name">SUBMIT</button>
                <button class="btn btn-red" id="btn-back-name">BACK</button>
            </div>
        </div>
    `;

    const input = el.querySelector('#name-input');
    const errorEl = el.querySelector('#name-error');
    const btn = el.querySelector('#btn-submit-name');

    input.addEventListener('input', () => {
        input.value = input.value.toUpperCase().replace(/[^A-Z0-9 ]/g, '');
        errorEl.textContent = '';
    });

    function submit() {
        const name = input.value.trim();
        if (!name) { errorEl.textContent = 'Name cannot be empty'; return; }
        errorEl.textContent = '';
        btn.disabled = true;
        btn.textContent = 'SUBMITTING...';
        app.connection.send(encodeSetUsername(name));
    }

    btn.addEventListener('click', submit);
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(); });

    el.querySelector('#btn-back-name').addEventListener('click', () => {
        app.connection.disconnect();
        app.changeScreen('title');
    });

    setTimeout(() => input.focus(), 100);
    return el;
}

export function handleUsernameTaken(app) {
    const errorEl = document.getElementById('name-error');
    const btn = document.getElementById('btn-submit-name');
    if (errorEl) { errorEl.textContent = 'Name already taken!'; errorEl.style.animation = 'shake 0.4s'; }
    if (btn) { btn.disabled = false; btn.textContent = 'SUBMIT'; }
}
