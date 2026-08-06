/**
 * connecting.js - Connection progress.
 *
 * PROTOCOL NOTE: the handshake sequence here is byte-for-byte what the live
 * client does - AUTH is sent by connection.js on open, and on AUTH_OK we send
 * MSG_CONNECT with the saved UUID (or none). Nothing about the wire changed;
 * only the panel around it did.
 */

import { BG, preload } from '../assets.js';
import { screenShell } from '../ui.js';

export function createConnectingScreen(app) {
    const el = screenShell('connecting', 'connecting');

    el.insertAdjacentHTML('beforeend', `
        <div class="panel panel-deco center-panel">
            <h2 id="conn-stage">Connecting</h2>
            <div class="sub" id="conn-detail">Establishing a link to the server</div>
            <div class="progress-track"><div class="progress-bar"></div></div>
            <div class="log conn-log" id="conn-log"></div>
            <div class="row-buttons">
                <button class="btn btn-red" id="btn-cancel">Cancel</button>
            </div>
        </div>
    `);

    const stageEl = el.querySelector('#conn-stage');
    const detailEl = el.querySelector('#conn-detail');
    const logEl = el.querySelector('#conn-log');

    function addLog(text) {
        const line = document.createElement('div');
        line.className = 'log-line';
        line.textContent = text;
        logEl.appendChild(line);
        logEl.scrollTop = logEl.scrollHeight;
    }

    el.querySelector('#btn-cancel').addEventListener('click', () => {
        app.connection.disconnect();
        app.changeScreen('title');
    });

    app.connection.autoReconnect = true;

    app.connection.onConnecting = () => {
        stageEl.textContent = 'Connecting';
        detailEl.textContent = 'Opening the WebSocket';
        addLog('Connecting to server...');
    };

    app.connection.onConnected = () => {
        stageEl.textContent = 'Authenticated';
        detailEl.textContent = 'Waiting for the server';
        addLog('Auth success');
        const savedUuid = localStorage.getItem('coup_uuid');
        if (savedUuid) {
            app.sendConnect(savedUuid);
            addLog('Sending saved identity...');
        } else {
            app.sendConnect(null);
            addLog('New connection...');
        }
    };

    app.connection.onError = () => {
        stageEl.textContent = 'Connection error';
        detailEl.textContent = 'Retrying...';
        addLog('Error - retrying');
    };

    app.connection.onDisconnected = () => {
        stageEl.textContent = 'Disconnected';
        detailEl.textContent = 'Reconnecting...';
    };

    preload([BG.lobby, BG.table]);
    app.connection.connect();
    return el;
}
