/**
 * qa_web_log_ring.mjs - Prove the web action log reads back in TRUE
 * chronological order, including after the ring has wrapped.
 *
 * WHY THIS GATE EXISTS
 *   Saturn's game-over recap shipped with a real, user-reported ordering bug
 *   (fixed in "the recap really was out of order", examples/coup/coup_render.c
 *   coup_log_ring_index()). The recap did this:
 *
 *       int idx = total - shown - scroll + i;      // WRONG
 *
 *   `total - shown - scroll + i` is a chronological ENTRY NUMBER: "the i'th
 *   row of the window is the N'th thing that happened". The array it indexes
 *   is a RING, whose oldest entry sits at `log_head`, not at 0. The two agree
 *   only while the ring has never wrapped. With COUP_LOG_LINES == 6 a finished
 *   match has ALWAYS wrapped, so every recap row printed somebody else's line,
 *   in the wrong order, with one entry missing - and the ">" marker that means
 *   "this is the winning action" sat on a mid-match line.
 *
 *   The web client is now growing the same recap. This gate exists so it
 *   cannot grow the same bug, and so a future refactor of the log storage
 *   cannot silently reintroduce it.
 *
 * WHY IT IS PLAIN NODE WITH NO DEPENDENCIES
 *   scripts/smoke_web_staging.mjs imports jsdom, which is not vendored, has no
 *   package.json and is not installed anywhere in this repo - it cannot
 *   currently run, and a gate that cannot run is not a gate. The log ring is
 *   pure arithmetic over an array, so it needs no DOM at all: this file
 *   imports the SHIPPED module (web-staging/js/log-ring.js) directly and
 *   exercises it. No install step, no lockfile, no network.
 *
 * NEGATIVE CONTROL
 *   --selftest replays the exact Saturn formula that was wrong and REQUIRES it
 *   to fail these assertions. If the broken formula passes, the assertions are
 *   not measuring anything and a GREEN from this file is worthless.
 *
 * Run:  node scripts/qa/qa_web_log_ring.mjs [--selftest]
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.dirname(path.dirname(HERE));
const MODULE = path.join(REPO, 'web-staging', 'js', 'log-ring.js');

const selftest = process.argv.includes('--selftest');

let checks = 0;
let fails = 0;

function ok(cond, label) {
    checks++;
    if (cond) {
        console.log(`  PASS  ${label}`);
    } else {
        fails++;
        console.log(`  FAIL  ${label}`);
    }
}

function eq(actual, expected, label) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    // Detail only on failure: a passing gate that prints 200-element arrays is
    // a gate nobody reads.
    ok(a === e, a === e ? label
        : `${label}\n          expected ${e}\n          actual   ${a}`);
}

function section(name) {
    console.log(`\n${name}`);
}

/* ------------------------------------------------------------------------ */
/* An INDEPENDENT model of what the answer must be.                          */
/*                                                                           */
/* Deliberately written the dumb way - a plain array, pushed and sliced -     */
/* so it cannot share a bug with the ring implementation under test. The      */
/* ring has to agree with this for every case below.                         */
/* ------------------------------------------------------------------------ */

function modelPush(all, text) {
    all.push(text);
    return all;
}

function modelWindow(all, capacity, maxRows, scroll) {
    const held = all.slice(Math.max(0, all.length - capacity));   // what survives
    const shown = Math.min(held.length, maxRows);
    let s = Math.max(0, Math.min(scroll, held.length - shown));
    const start = held.length - shown - s;
    return held.slice(start, start + shown);
}

/* ------------------------------------------------------------------------ */

const mod = await import(pathToFileURL(MODULE).href);
const { LogRing, logRingIndex, recapRows, RECAP_ROWS } = mod;

if (typeof LogRing !== 'function' || typeof logRingIndex !== 'function'
    || typeof recapRows !== 'function') {
    console.log('GATE WEB LOG RING: INCONCLUSIVE - web-staging/js/log-ring.js '
        + 'does not export LogRing, logRingIndex and recapRows');
    process.exit(2);
}

/* Under --selftest the ring keeps its (correct) storage but the WINDOW
 * arithmetic is swapped for the Saturn bug: a chronological entry number used
 * straight as a slot number. Everything below then runs against it. */
const ringIndexUnderTest = selftest
    ? function saturnBug(head, count, capacity, maxRows, scroll, row) {
        if (count <= 0 || maxRows <= 0) return -1;
        const n = Math.min(count, capacity);
        const shown = Math.min(n, maxRows);
        if (row < 0 || row >= shown) return -1;
        let s = Math.max(0, Math.min(scroll, n - shown));
        return n - shown - s + row;          // <-- the bug, verbatim
    }
    : logRingIndex;

function windowOf(ring, maxRows, scroll) {
    const out = [];
    const shown = Math.min(ring.count, maxRows);
    for (let row = 0; row < shown; row++) {
        const idx = ringIndexUnderTest(ring.head, ring.count, ring.capacity,
            maxRows, scroll, row);
        if (idx < 0) continue;
        out.push(ring.slots[idx]);
    }
    return out;
}

/* ======================================================================== */
console.log(selftest
    ? 'GATE WEB LOG RING  (--selftest: running the OLD Saturn formula, which MUST fail)'
    : 'GATE WEB LOG RING');

/* ---------------------------------------------------------------------- */
section('1. the case that broke on Saturn: more entries than the ring holds');
/* ---------------------------------------------------------------------- */
{
    const CAP = 6;                       // COUP_LOG_LINES
    const N = 20;                        // >>> CAP: the ring wraps three times
    const ring = new LogRing(CAP);
    const all = [];
    for (let i = 1; i <= N; i++) {
        ring.push(`entry ${i}`);
        modelPush(all, `entry ${i}`);
    }

    console.log(`  appended ${N} entries into a ${CAP}-slot ring `
        + `(wrapped ${Math.floor(N / CAP)}x); head=${ring.head} count=${ring.count}`);

    eq(ring.count, CAP, 'count saturates at capacity');

    const readBack = [];
    const shown = Math.min(ring.count, CAP);
    for (let row = 0; row < shown; row++) {
        const idx = ringIndexUnderTest(ring.head, ring.count, ring.capacity,
            CAP, 0, row);
        readBack.push(idx < 0 ? null : ring.slots[idx]);
    }
    eq(readBack, modelWindow(all, CAP, CAP, 0),
        'full read-back after wrap is the newest CAP entries, oldest first');

    // The distinctive shape of the bug: entries out of order AND one missing.
    const uniq = new Set(readBack);
    ok(uniq.size === readBack.length,
        `read-back has no duplicated/missing entry (${uniq.size} unique of ${readBack.length})`);

    const nums = readBack.map(t => t && Number(String(t).split(' ')[1]));
    const ascending = nums.every((v, i) => i === 0 || (v !== null && v > nums[i - 1]));
    ok(ascending, `read-back is strictly ascending in time: [${nums.join(', ')}]`);
}

/* ---------------------------------------------------------------------- */
section('2. toArray() is chronological for every fill level');
/* ---------------------------------------------------------------------- */
{
    for (const [cap, n] of [[6, 0], [6, 1], [6, 5], [6, 6], [6, 7], [6, 61], [200, 517]]) {
        const ring = new LogRing(cap);
        const all = [];
        for (let i = 1; i <= n; i++) { ring.push(`e${i}`); modelPush(all, `e${i}`); }
        eq(ring.toArray(), modelWindow(all, cap, cap, 0),
            `cap=${cap} pushes=${n}: toArray() matches the plain-array model`);
    }
}

/* ---------------------------------------------------------------------- */
section('3. every (count, maxRows, scroll, row) agrees with the model');
/* ---------------------------------------------------------------------- */
{
    const CAP = 6;
    let cases = 0;
    let bad = 0;
    for (let pushes = 0; pushes <= 25; pushes++) {
        const ring = new LogRing(CAP);
        const all = [];
        for (let i = 1; i <= pushes; i++) { ring.push(`e${i}`); modelPush(all, `e${i}`); }
        for (let maxRows = 1; maxRows <= 8; maxRows++) {
            for (let scroll = -2; scroll <= 8; scroll++) {
                cases++;
                const got = windowOf(ring, maxRows, scroll);
                const want = modelWindow(all, CAP, maxRows, scroll);
                if (JSON.stringify(got) !== JSON.stringify(want)) {
                    if (bad < 4) {
                        console.log(`        pushes=${pushes} maxRows=${maxRows} `
                            + `scroll=${scroll}\n          want ${JSON.stringify(want)}`
                            + `\n          got  ${JSON.stringify(got)}`);
                    }
                    bad++;
                }
            }
        }
    }
    ok(bad === 0, `${cases - bad}/${cases} window cases match the model `
        + `(${bad} mismatched)`);
}

/* ---------------------------------------------------------------------- */
section('4. the recap itself: newest last, and the winning row is the newest');
/* ---------------------------------------------------------------------- */
{
    const CAP = 6;
    const ring = new LogRing(CAP);
    const script = [
        'Alice takes Income',
        'Bob declares Tax (Duke)',
        'Alice challenges!',
        'Bob reveals Duke',
        'Alice reveals Captain',
        'Alice is eliminated!',
        'Bob launches Coup on Carol',
        'Carol reveals Contessa',
        'Carol is eliminated!',
        'Bob wins!',
    ];
    script.forEach(t => ring.push(t));

    const rows = recapRows(ring, RECAP_ROWS, 0, ringIndexUnderTest);
    console.log(`  RECAP_ROWS=${RECAP_ROWS}; ${script.length} entries into a ${CAP}-slot ring`);
    rows.forEach(r => console.log(`    ${r.winning ? '>' : ' '} ${r.text}`));

    eq(rows.map(r => r.text), script.slice(script.length - RECAP_ROWS),
        'the recap shows the last RECAP_ROWS entries, oldest at the top');

    ok(rows.length > 0 && rows[rows.length - 1].winning === true,
        'the LAST row is flagged as the winning action');
    ok(rows.slice(0, -1).every(r => r.winning === false),
        'no earlier row is flagged as the winning action');
    ok(rows[rows.length - 1].text === 'Bob wins!',
        `the winning row is the real winning line (got "${rows[rows.length - 1].text}")`);

    // Scrolled back: nothing is the winning action any more (matches Saturn,
    // where `last` is (scroll == 0 && i == shown - 1)).
    const scrolled = recapRows(ring, RECAP_ROWS, 1, ringIndexUnderTest);
    ok(scrolled.every(r => r.winning === false),
        'scrolled off the bottom, no row claims to be the winning action');
    eq(scrolled.map(r => r.text),
        script.slice(script.length - RECAP_ROWS - 1, script.length - 1),
        'scrolling back by one steps exactly one entry into the past');
}

/* ---------------------------------------------------------------------- */
section('5. degenerate inputs return -1 rather than an out-of-range slot');
/* ---------------------------------------------------------------------- */
{
    const CAP = 6;
    ok(logRingIndex(0, 0, CAP, 5, 0, 0) === -1, 'empty log -> -1');
    ok(logRingIndex(0, 3, CAP, 0, 0, 0) === -1, 'maxRows 0 -> -1');
    ok(logRingIndex(0, 3, CAP, 5, 0, -1) === -1, 'negative row -> -1');
    ok(logRingIndex(0, 3, CAP, 5, 0, 3) === -1, 'row past the shown count -> -1');

    let inRange = true;
    for (let head = 0; head < CAP; head++) {
        for (let count = 0; count <= CAP + 4; count++) {
            for (let row = -1; row < 8; row++) {
                const i = logRingIndex(head, count, CAP, 5, 0, row);
                if (i !== -1 && (i < 0 || i >= CAP)) inRange = false;
            }
        }
    }
    ok(inRange, 'no input produces a slot outside [0, capacity)');
}

/* ---------------------------------------------------------------------- */
section('6. the recap is actually wired to the log the match writes');
/* ----------------------------------------------------------------------
 * Sections 1-5 prove the arithmetic. They would all still pass if
 * game-over.js rendered nothing, or read some other array - which is exactly
 * the state the web client was in before this work: the data existed and the
 * game-over screen showed none of it. These are the joins.
 * ---------------------------------------------------------------------- */
{
    const { readFileSync } = await import('node:fs');
    const src = (rel) => {
        try { return readFileSync(path.join(REPO, rel), 'utf8'); }
        catch { return ''; }
    };

    const gameJs = src('web-staging/js/screens/game.js');
    const overJs = src('web-staging/js/screens/game-over.js');
    const mainJs = src('web-staging/js/main.js');

    ok(/new\s+LogRing\s*\(/.test(gameJs),
        'game.js stores the match log in a LogRing');
    ok(/_logRing\.push\s*\(/.test(gameJs),
        'addGameLog() writes into that ring');
    ok(!/_logHistory/.test(gameJs),
        'the old unbounded _logHistory array is gone (one log, one reader)');
    ok(/export\s+function\s+getLogRing/.test(gameJs),
        'game.js exposes the ring for the recap');

    ok(/getLogRing/.test(overJs),
        'game-over.js reads the match log');
    ok(/recapRows\s*\(/.test(overJs),
        'game-over.js builds its rows through recapRows() - not by indexing '
        + 'the ring itself, which is how the Saturn bug happened');
    ok(/RECAP_ROWS/.test(overJs),
        'game-over.js uses the shared RECAP_ROWS count');
    ok(/winning/.test(overJs),
        'game-over.js marks the winning row');
    ok(!/\.slots\s*\[/.test(overJs),
        'game-over.js never indexes ring.slots directly');

    ok(/resetGameLog/.test(mainJs),
        'main.js clears the log when a new match starts, so a recap cannot '
        + 'show the previous match');
}

/* ======================================================================== */
console.log();
if (selftest) {
    if (fails === 0) {
        console.log(`GATE WEB LOG RING: RED - the OLD Saturn formula passed all `
            + `${checks} assertions, so this gate cannot tell the broken ordering `
            + `from the correct one and a GREEN from it means nothing`);
        process.exit(1);
    }
    console.log(`GATE WEB LOG RING: negative control OK - the old formula fails `
        + `${fails} of ${checks} assertions, so the assertions bite`);
    process.exit(0);
}

if (fails) {
    console.log(`GATE WEB LOG RING: RED - ${fails} of ${checks} assertions failed`);
    process.exit(1);
}
console.log(`GATE WEB LOG RING: GREEN - ${checks}/${checks} assertions; the log `
    + `reads back in true chronological order across ring wraps`);
process.exit(0);
