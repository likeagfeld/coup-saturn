/**
 * protocol.js - SNCP Binary Protocol for Coup Web Client
 *
 * Encode/decode matching server.py message types.
 * Uses DataView/Uint8Array over ArrayBuffer for binary packing.
 * Frame format: [LEN_HI:1][LEN_LO:1][PAYLOAD...]
 */

// --- Message type constants ---

// Client -> Server (auth)
export const MSG_CONNECT         = 0x01;
export const MSG_SET_USERNAME    = 0x02;
export const MSG_HEARTBEAT       = 0x04;
export const MSG_DISCONNECT      = 0x05;

// Client -> Server (game)
export const COUP_MSG_READY          = 0x10;
export const COUP_MSG_ACTION         = 0x11;
export const COUP_MSG_RESPONSE       = 0x12;
export const COUP_MSG_BLOCK_CLAIM    = 0x13;
export const COUP_MSG_LOSE_INFLUENCE = 0x14;
export const COUP_MSG_EXCHANGE_CHOICE= 0x15;
export const COUP_MSG_START_GAME_REQ = 0x16;
export const COUP_MSG_ADD_BOT        = 0x17;
export const COUP_MSG_REMOVE_BOT     = 0x18;
export const COUP_MSG_SET_BOT_DIFFICULTY = 0x19;
export const COUP_MSG_RESYNC_REQ     = 0x1A;

// Server -> Client (auth)
export const MSG_USERNAME_REQUIRED = 0x81;
export const MSG_WELCOME           = 0x82;
export const MSG_WELCOME_BACK      = 0x83;
export const MSG_USERNAME_TAKEN    = 0x84;

// Server -> Client (game)
export const COUP_MSG_LOBBY_STATE    = 0xA0;
export const COUP_MSG_GAME_START     = 0xA1;
export const COUP_MSG_LOG            = 0xAE;
export const COUP_MSG_INPUT_RELAY    = 0xB2;
export const COUP_MSG_RESYNC         = 0xB3;
export const COUP_MSG_RESYNC_FULL    = 0xB4;
export const COUP_MSG_ACTION_REJECTED= 0xB5;

// INPUT_RELAY input type codes
export const RELAY_START_GAME      = 0;
export const RELAY_ACTION          = 1;
export const RELAY_RESPONSE        = 2;
export const RELAY_BLOCK_CLAIM     = 3;
export const RELAY_LOSE_INFLUENCE  = 4;
export const RELAY_EXCHANGE_CHOICE = 5;
export const RELAY_TIMEOUT         = 6;

// Response types
export const RESP_PASS      = 0;
export const RESP_CHALLENGE = 1;
export const RESP_BLOCK     = 2;

// Character IDs
export const CHAR_DUKE       = 0;
export const CHAR_ASSASSIN   = 1;
export const CHAR_CAPTAIN    = 2;
export const CHAR_AMBASSADOR = 3;
export const CHAR_CONTESSA   = 4;
export const CHAR_FACEDOWN   = 5;
export const CHAR_NONE       = 6;

// Action IDs
export const ACT_INCOME      = 0;
export const ACT_FOREIGN_AID = 1;
export const ACT_COUP        = 2;
export const ACT_TAX         = 3;
export const ACT_ASSASSINATE = 4;
export const ACT_STEAL       = 5;
export const ACT_EXCHANGE    = 6;

export const UUID_LEN = 36;

// --- Frame helper ---

function encodeFrame(payload) {
    const frame = new Uint8Array(2 + payload.length);
    frame[0] = (payload.length >> 8) & 0xFF;
    frame[1] = payload.length & 0xFF;
    frame.set(payload, 2);
    return frame;
}

// --- Encoder functions ---

export function encodeAuth(secret) {
    // AUTH handshake: "AUTH" + secret_len + secret
    const secretBytes = typeof secret === 'string'
        ? new TextEncoder().encode(secret) : secret;
    const buf = new Uint8Array(4 + 1 + secretBytes.length);
    buf[0] = 0x41; // 'A'
    buf[1] = 0x55; // 'U'
    buf[2] = 0x54; // 'T'
    buf[3] = 0x48; // 'H'
    buf[4] = secretBytes.length;
    buf.set(secretBytes, 5);
    return buf;
}

export function encodeConnect(uuid) {
    if (uuid) {
        const payload = new Uint8Array(1 + UUID_LEN);
        payload[0] = MSG_CONNECT;
        const uuidBytes = new TextEncoder().encode(uuid);
        payload.set(uuidBytes.slice(0, UUID_LEN), 1);
        return encodeFrame(payload);
    }
    return encodeFrame(new Uint8Array([MSG_CONNECT]));
}

export function encodeSetUsername(name) {
    const nameBytes = new TextEncoder().encode(name.slice(0, 16));
    const payload = new Uint8Array(2 + nameBytes.length);
    payload[0] = MSG_SET_USERNAME;
    payload[1] = nameBytes.length;
    payload.set(nameBytes, 2);
    return encodeFrame(payload);
}

export function encodeHeartbeat() {
    return encodeFrame(new Uint8Array([MSG_HEARTBEAT]));
}

export function encodeDisconnect() {
    return encodeFrame(new Uint8Array([MSG_DISCONNECT]));
}

export function encodeReady(ready) {
    if (ready !== undefined) {
        return encodeFrame(new Uint8Array([COUP_MSG_READY, ready ? 1 : 0]));
    }
    return encodeFrame(new Uint8Array([COUP_MSG_READY]));
}

export function encodeAction(action, target) {
    return encodeFrame(new Uint8Array([COUP_MSG_ACTION, action, target]));
}

export function encodeResponse(response) {
    return encodeFrame(new Uint8Array([COUP_MSG_RESPONSE, response]));
}

export function encodeBlockClaim(character) {
    return encodeFrame(new Uint8Array([COUP_MSG_BLOCK_CLAIM, character]));
}

export function encodeLoseInfluence(cardIndex) {
    return encodeFrame(new Uint8Array([COUP_MSG_LOSE_INFLUENCE, cardIndex]));
}

export function encodeExchangeChoice(keep0, keep1) {
    return encodeFrame(new Uint8Array([COUP_MSG_EXCHANGE_CHOICE, keep0, keep1]));
}

export function encodeStartGame() {
    return encodeFrame(new Uint8Array([COUP_MSG_START_GAME_REQ]));
}

export function encodeAddBot(difficulty) {
    return encodeFrame(new Uint8Array([COUP_MSG_ADD_BOT, difficulty || 1]));
}

export function encodeRemoveBot() {
    return encodeFrame(new Uint8Array([COUP_MSG_REMOVE_BOT]));
}

export function encodeSetBotDifficulty(botIndex, difficulty) {
    return encodeFrame(new Uint8Array([COUP_MSG_SET_BOT_DIFFICULTY, botIndex, difficulty]));
}

export function encodeResyncReq(lastSeenSeq) {
    return encodeFrame(new Uint8Array([
        COUP_MSG_RESYNC_REQ,
        (lastSeenSeq >> 8) & 0xFF,
        lastSeenSeq & 0xFF
    ]));
}

// --- Frame reassembly state machine ---

export class FrameDecoder {
    constructor() {
        this.buffer = new Uint8Array(0);
    }

    /** Feed raw bytes (Uint8Array or ArrayBuffer), returns array of decoded payloads */
    feed(data) {
        const incoming = data instanceof ArrayBuffer ? new Uint8Array(data) : data;
        // Append to buffer
        const merged = new Uint8Array(this.buffer.length + incoming.length);
        merged.set(this.buffer);
        merged.set(incoming, this.buffer.length);
        this.buffer = merged;

        const frames = [];
        while (this.buffer.length >= 2) {
            const payloadLen = (this.buffer[0] << 8) | this.buffer[1];
            const total = 2 + payloadLen;
            if (this.buffer.length < total) break;
            if (payloadLen > 0) {
                frames.push(this.buffer.slice(2, total));
            }
            this.buffer = this.buffer.slice(total);
        }
        return frames;
    }
}

// --- Decoder / dispatcher ---

function readLPString(data, offset) {
    if (offset >= data.length) return { str: '', bytesRead: 0 };
    const len = data[offset];
    const str = new TextDecoder().decode(data.slice(offset + 1, offset + 1 + len));
    return { str, bytesRead: 1 + len };
}

export class MessageDispatcher {
    constructor() {
        this.handlers = {};
    }

    on(msgType, handler) {
        this.handlers[msgType] = handler;
    }

    dispatch(payload) {
        if (payload.length < 1) return;
        const msgType = payload[0];
        const handler = this.handlers[msgType];
        if (!handler) return;

        switch (msgType) {
            case MSG_USERNAME_REQUIRED:
                handler();
                break;

            case MSG_WELCOME:
            case MSG_WELCOME_BACK: {
                const userId = payload[1];
                const uuid = new TextDecoder().decode(payload.slice(2, 2 + UUID_LEN));
                const { str: username } = readLPString(payload, 2 + UUID_LEN);
                handler({ userId, uuid, username, isReturning: msgType === MSG_WELCOME_BACK });
                break;
            }

            case MSG_USERNAME_TAKEN:
                handler();
                break;

            case COUP_MSG_LOBBY_STATE: {
                const count = payload[1];
                const players = [];
                let offset = 2;
                for (let i = 0; i < count && offset < payload.length; i++) {
                    const id = payload[offset++];
                    const { str: name, bytesRead } = readLPString(payload, offset);
                    offset += bytesRead;
                    const ready = payload[offset++] === 1;
                    const isBot = payload[offset++] === 1;
                    const difficulty = payload[offset++];
                    players.push({ id, name, ready, isBot, difficulty });
                }
                handler(players);
                break;
            }

            case COUP_MSG_GAME_START: {
                const seed = (payload[1] << 24) | (payload[2] << 16) |
                             (payload[3] << 8) | payload[4];
                const myPid = payload[5];
                const playerCount = payload[6];
                const playerOrder = [];
                for (let i = 0; i < playerCount; i++) {
                    playerOrder.push(payload[7 + i]);
                }
                handler({ seed: seed >>> 0, myPid, playerOrder });
                break;
            }

            case COUP_MSG_LOG: {
                const len = payload[1];
                const text = new TextDecoder().decode(payload.slice(2, 2 + len));
                handler(text);
                break;
            }

            case COUP_MSG_INPUT_RELAY: {
                const seq = (payload[1] << 8) | payload[2];
                const inputType = payload[3];
                const playerId = payload[4];
                const data = payload.slice(5);
                handler({ seq, inputType, playerId, data });
                break;
            }

            case COUP_MSG_RESYNC: {
                const count = payload[1];
                const entries = [];
                let offset = 2;
                for (let i = 0; i < count && offset < payload.length; i++) {
                    const entryLen = payload[offset++];
                    const seq = (payload[offset] << 8) | payload[offset + 1];
                    const inputType = payload[offset + 2];
                    const pid = payload[offset + 3];
                    const data = payload.slice(offset + 4, offset + entryLen);
                    entries.push({ seq, inputType, pid, data });
                    offset += entryLen;
                }
                handler(entries);
                break;
            }

            case COUP_MSG_RESYNC_FULL: {
                const seed = ((payload[1] << 24) | (payload[2] << 16) |
                              (payload[3] << 8) | payload[4]) >>> 0;
                const myPid = payload[5];
                const totalRelays = (payload[6] << 8) | payload[7];
                handler({ seed, myPid, totalRelays });
                break;
            }

            case COUP_MSG_ACTION_REJECTED: {
                const currentSeq = (payload[1] << 8) | payload[2];
                const phase = payload[3];
                handler({ currentSeq, phase });
                break;
            }

            default:
                handler(payload);
        }
    }
}
