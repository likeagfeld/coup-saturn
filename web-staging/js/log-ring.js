/**
 * log-ring.js - The match action log, and the window arithmetic every view of
 * it shares.
 *
 * Mirrors examples/coup/coup_game.c coup_log() (the writer) and
 * examples/coup/coup_render.c coup_log_ring_index() (the reader) so the Saturn
 * client and this one show the same recap, in the same order, marked the same
 * way.
 *
 * ============================ THE BUG THIS PORTS ============================
 * Saturn's game-over recap shipped reading its rows like this:
 *
 *     int idx = total - shown - scroll + i;
 *
 * `total - shown - scroll + i` is a chronological ENTRY NUMBER - "the i'th row
 * of this window is the N'th thing that happened in the match". The array it
 * was indexing is a RING, whose OLDEST entry lives at `head`, not at 0. The
 * two numbers agree only while the ring has never wrapped, and with
 * COUP_LOG_LINES == 6 a finished match has always wrapped. So every recap row
 * printed somebody else's line, out of order, with one entry missing - and the
 * ">" marker that means "this is the winning action" landed on a mid-match
 * line. It was reported by a user and fixed in "the recap really was out of
 * order".
 *
 * The missing step is the last line of logRingIndex(): turn the entry NUMBER
 * into a SLOT by counting on from `head`. Everything else here exists to make
 * sure there is exactly one place that conversion can be got wrong, and that
 * scripts/qa/qa_web_log_ring.mjs is standing on it.
 * ===========================================================================
 */

/** COUP_GAMEOVER_RECAP_ROWS (examples/coup/coup.h). */
export const RECAP_ROWS = 5;

/**
 * How many entries the web log keeps.
 *
 * Saturn's COUP_LOG_LINES is 6 because the log lives in a fixed struct in 2 MB
 * of work RAM. The browser has no such constraint and the log overlay shows
 * the whole match, so this is 200 - the same number the previous plain-array
 * history used. The ARITHMETIC is capacity-agnostic; the gate exercises
 * capacity 6 as well as 200 precisely so a small, always-wrapped ring is
 * covered.
 */
export const LOG_CAPACITY = 200;

/**
 * Slot holding row `row` of a window of at most `maxRows` entries, scrolled
 * back by `scroll` entries from the newest.
 *
 * Row 0 is the OLDEST row on screen and row shown-1 the newest, which is how
 * both the log panel and the recap read top to bottom.
 *
 * Returns -1 when the row is not backed by an entry, so callers can skip
 * rather than index out of range.
 *
 * @param {number} head      slot of the oldest entry held
 * @param {number} count     entries held (saturates at capacity)
 * @param {number} capacity  ring size
 * @param {number} maxRows   rows the view can display
 * @param {number} scroll    entries scrolled back from the newest
 * @param {number} row       row within the view, 0-based from the top
 */
export function logRingIndex(head, count, capacity, maxRows, scroll, row) {
    if (count <= 0 || maxRows <= 0 || capacity <= 0) return -1;
    if (count > capacity) count = capacity;

    const shown = count < maxRows ? count : maxRows;
    if (row < 0 || row >= shown) return -1;

    if (scroll < 0) scroll = 0;
    if (scroll > count - shown) scroll = count - shown;

    /* Which entry, counting from the oldest one still held. */
    const chrono = count - shown - scroll + row;
    if (chrono < 0 || chrono >= count) return -1;

    /* THE STEP THAT WAS MISSING ON SATURN. `chrono` is an entry NUMBER; the
     * slot it lives in is that many places on from the oldest entry, which is
     * the one at `head`. Returning `chrono` itself is correct only while the
     * ring has never wrapped. */
    if (head < 0) head = 0;
    return (head + chrono) % capacity;
}

export class LogRing {
    constructor(capacity = LOG_CAPACITY) {
        this.capacity = capacity;
        this.slots = new Array(capacity).fill('');
        this.head = 0;
        this.count = 0;
    }

    /** Append one entry, evicting the oldest once full. Mirrors coup_log(). */
    push(text) {
        let dest;
        if (this.count < this.capacity) {
            dest = this.count;
            this.count++;
        } else {
            dest = this.head;
            this.head = (this.head + 1) % this.capacity;
        }
        this.slots[dest] = String(text);
    }

    /** Every entry held, oldest first. */
    toArray() {
        const out = [];
        for (let row = 0; row < this.count; row++) {
            const idx = logRingIndex(this.head, this.count, this.capacity,
                this.count, 0, row);
            if (idx >= 0) out.push(this.slots[idx]);
        }
        return out;
    }

    clear() {
        this.head = 0;
        this.count = 0;
        this.slots.fill('');
    }
}

/**
 * The game-over recap window, as rows ready to render.
 *
 * `winning` marks the newest entry of an UNSCROLLED window - the action that
 * ended the match. It is derived from the ROW, never from the slot: a slot
 * number says nothing about how recent its entry is once the ring has wrapped.
 * That is the same rule coup_render.c uses (`last = (scroll == 0 && i ==
 * shown - 1)`).
 *
 * `indexFn` is injectable only so scripts/qa/qa_web_log_ring.mjs can run its
 * negative control (the old Saturn formula) through the real renderer path.
 */
export function recapRows(ring, maxRows = RECAP_ROWS, scroll = 0,
                          indexFn = logRingIndex) {
    if (!ring || ring.count <= 0) return [];
    const shown = Math.min(ring.count, maxRows);
    const rows = [];
    for (let row = 0; row < shown; row++) {
        const idx = indexFn(ring.head, ring.count, ring.capacity,
            maxRows, scroll, row);
        if (idx < 0) continue;
        rows.push({
            text: ring.slots[idx],
            winning: scroll === 0 && row === shown - 1,
        });
    }
    return rows;
}
