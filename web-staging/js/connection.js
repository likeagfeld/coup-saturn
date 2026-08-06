/**
 * connection.js - WebSocket Connection Manager
 *
 * Handles WSS connection, AUTH handshake, heartbeat, and reconnect.
 */

import { encodeAuth, encodeHeartbeat, FrameDecoder, MessageDispatcher } from './protocol.js';

const SHARED_SECRET = 'SaturnCoup2025!NetLink#SecretKey';
const HEARTBEAT_INTERVAL = 20000; // 20 seconds
const RECONNECT_DELAYS = [1000, 2000, 4000, 8000, 15000];
const AUTH_OK = 0x01;

export class Connection {
    constructor(wsUrl) {
        this.wsUrl = wsUrl;
        this.ws = null;
        this.decoder = new FrameDecoder();
        this.dispatcher = new MessageDispatcher();
        this.heartbeatTimer = null;
        this.reconnectAttempt = 0;
        this.autoReconnect = true;
        this._authenticated = false;
        this._authPhase = true;
        this.kicked = false;

        // Callbacks
        this.onConnected = null;
        this.onDisconnected = null;
        this.onKicked = null;
        this.onError = null;
        this.onConnecting = null;
    }

    connect() {
        if (this.ws) {
            try { this.ws.close(); } catch (e) {}
        }

        this._authenticated = false;
        this._authPhase = true;
        this.decoder = new FrameDecoder();

        if (this.onConnecting) this.onConnecting();

        this.ws = new WebSocket(this.wsUrl);
        this.ws.binaryType = 'arraybuffer';

        this.ws.onopen = () => {
            this.ws.send(encodeAuth(SHARED_SECRET));
        };

        this.ws.onmessage = (event) => {
            const data = new Uint8Array(event.data);

            if (this._authPhase) {
                if (data.length >= 1 && data[0] === AUTH_OK) {
                    this._authPhase = false;
                    this._authenticated = true;
                    this.reconnectAttempt = 0;
                    this._startHeartbeat();
                    if (this.onConnected) this.onConnected();
                } else {
                    console.error('Auth failed');
                    this.ws.close();
                }
                return;
            }

            const frames = this.decoder.feed(data);
            for (const payload of frames) {
                this.dispatcher.dispatch(payload);
            }
        };

        this.ws.onclose = (event) => {
            this._stopHeartbeat();
            this._authenticated = false;
            if (event.code === 4001) {
                this.kicked = true;
                this.autoReconnect = false;
                if (this.onKicked) this.onKicked();
                return;
            }
            if (this.onDisconnected) this.onDisconnected();
            if (this.autoReconnect) this._scheduleReconnect();
        };

        this.ws.onerror = (err) => {
            if (this.onError) this.onError(err);
        };
    }

    send(frame) {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN || !this._authenticated) return;
        this.ws.send(frame);
    }

    disconnect() {
        this.autoReconnect = false;
        this._stopHeartbeat();
        if (this.ws) {
            try { this.ws.close(); } catch (e) {}
            this.ws = null;
        }
    }

    get connected() {
        return this._authenticated && this.ws && this.ws.readyState === WebSocket.OPEN;
    }

    on(msgType, handler) {
        this.dispatcher.on(msgType, handler);
    }

    _startHeartbeat() {
        this._stopHeartbeat();
        this.heartbeatTimer = setInterval(() => {
            this.send(encodeHeartbeat());
        }, HEARTBEAT_INTERVAL);
    }

    _stopHeartbeat() {
        if (this.heartbeatTimer) {
            clearInterval(this.heartbeatTimer);
            this.heartbeatTimer = null;
        }
    }

    _scheduleReconnect() {
        const delay = RECONNECT_DELAYS[
            Math.min(this.reconnectAttempt, RECONNECT_DELAYS.length - 1)
        ];
        this.reconnectAttempt++;
        setTimeout(() => {
            if (this.autoReconnect) this.connect();
        }, delay);
    }
}
