/**
 * name-entry.js - Username entry.
 *
 * The A-Z/0-9/SPACE restriction and the 16-char cap are NOT cosmetic: the
 * Saturn client enters names from a fixed on-screen charset, and
 * encodeSetUsername() truncates to 16 bytes. Accepting anything wider here
 * would let a web player pick a name the Saturn cannot render. Unchanged from
 * the live client on purpose.
 */

import { encodeSetUsername } from '../protocol.js';
import { screenShell } from '../ui.js';

export function createNameEntryScreen(app) {
    const el = screenShell('name-entry', 'lobby');

    el.insertAdjacentHTML('beforeend', `
        <div class="panel panel-deco center-panel">
            <h2>Enter your name</h2>
            <input type="text" class="name-input" id="name-input"
                   maxlength="16" placeholder="YOUR NAME"
                   autocomplete="off" autocapitalize="characters"
                   spellcheck="false" enterkeyhint="go" />
            <div class="fine">A-Z, 0-9 and space &bull; 16 characters max</div>
            <div class="form-error" id="name-error"></div>
            <div class="row-buttons">
                <button class="btn btn-green" id="btn-submit-name">Submit</button>
                <button class="btn btn-red" id="btn-back-name">Back</button>
            </div>
        </div>
    `);

    const input = el.querySelector('#name-input');
    const errorEl = el.querySelector('#name-error');
    const btn = el.querySelector('#btn-submit-name');

    input.addEventListener('input', () => {
        input.value = input.value.toUpperCase().replace(/[^A-Z0-9 ]/g, '');
        errorEl.textContent = '';
        errorEl.classList.remove('shake');
    });

    function submit() {
        const name = input.value.trim();
        if (!name) {
            errorEl.textContent = 'Name cannot be empty';
            errorEl.classList.add('shake');
            return;
        }
        errorEl.textContent = '';
        btn.disabled = true;
        btn.textContent = 'Submitting...';
        app.connection.send(encodeSetUsername(name));
    }

    btn.addEventListener('click', submit);
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(); });

    el.querySelector('#btn-back-name').addEventListener('click', () => {
        app.connection.disconnect();
        app.changeScreen('title');
    });

    setTimeout(() => input.focus(), 120);
    return el;
}

export function handleUsernameTaken() {
    const errorEl = document.getElementById('name-error');
    const btn = document.getElementById('btn-submit-name');
    if (errorEl) {
        errorEl.textContent = 'That name is already taken';
        errorEl.classList.remove('shake');
        void errorEl.offsetWidth;     // restart the animation
        errorEl.classList.add('shake');
    }
    if (btn) { btn.disabled = false; btn.textContent = 'Submit'; }
}
