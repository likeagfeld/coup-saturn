/**
 * connecting.js - Connection progress with animated progress bar
 */
export function createConnectingScreen(app) {
    const el = document.createElement('div');
    el.className = 'screen screen-connecting';
    el.innerHTML = `
        <div class="connecting-panel">
            <div class="connecting-title">CONNECTING</div>
            <div class="connecting-accent"></div>
            <div class="connecting-stage" id="conn-stage">Establishing connection...</div>
            <div class="connecting-detail" id="conn-detail">Waiting for server response</div>
            <div class="connecting-progress-bg">
                <div class="connecting-progress-bar" id="conn-bar"></div>
            </div>
            <div class="connecting-log" id="conn-log"></div>
            <button class="btn btn-red" id="btn-cancel">CANCEL</button>
        </div>
    `;

    const stageEl = el.querySelector('#conn-stage');
    const detailEl = el.querySelector('#conn-detail');
    const logEl = el.querySelector('#conn-log');

    function addLog(text) {
        const line = document.createElement('div');
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
        stageEl.textContent = 'Establishing connection...';
        detailEl.textContent = 'Connecting via WebSocket';
        addLog('Connecting to server...');
    };

    app.connection.onConnected = () => {
        stageEl.textContent = 'Authenticated!';
        detailEl.textContent = 'Waiting for server response';
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

    app.connection.connect();
    return el;
}
