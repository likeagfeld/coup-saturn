/**
 * rules.js - Multi-page rules overlay, reachable from every screen.
 *
 * Same five pages as the live client, same R / Escape / arrow-key handling.
 * The CHARACTERS page now shows the official card face beside the animated
 * portrait, which is the one place the two clients can differ usefully: the
 * Saturn shows a 48x72 card, the web can show the 288x432 master.
 */

import { BG, cardArt, CHARACTERS } from '../assets.js';
import { applyPortrait } from '../ui.js';

const PAGES = [
    { title: 'Overview',            render: renderOverview },
    { title: 'Characters',          render: renderCharacters },
    { title: 'Actions',             render: renderActions },
    { title: 'Challenges & Blocks', render: renderChallengesBlocks },
    { title: 'Strategy',            render: renderTips },
];

const CHAR_INFO = [
    { ability: 'Tax: take 3 coins from the treasury.', blocks: 'Blocks Foreign Aid.' },
    { ability: 'Assassinate: pay 3 coins to force a player to lose influence.', blocks: 'Blocks nothing.' },
    { ability: 'Steal: take 2 coins from another player.', blocks: 'Blocks Steal.' },
    { ability: 'Exchange: draw 2 from the deck and choose what to keep.', blocks: 'Blocks Steal.' },
    { ability: 'No active ability.', blocks: 'Blocks Assassination.' },
];

export function createRulesOverlay(onClose) {
    let currentPage = 0;

    const overlay = document.createElement('div');
    overlay.className = 'overlay';

    const bg = document.createElement('div');
    bg.className = 'overlay-bg';
    bg.style.backgroundImage = `url("${BG.rules}")`;
    overlay.appendChild(bg);

    function render() {
        const page = PAGES[currentPage];

        // Rebuild everything except the backdrop.
        Array.from(overlay.children).forEach(c => {
            if (c !== bg) c.remove();
        });

        overlay.insertAdjacentHTML('beforeend', `
            <div class="overlay-head">
                <span class="overlay-title">Rules - ${page.title}</span>
                <span class="bar-meta">${currentPage + 1} / ${PAGES.length}</span>
            </div>
            <div class="overlay-body" id="rules-body"></div>
            <div class="overlay-foot">
                <button class="btn btn-dim" id="rules-prev" ${currentPage === 0 ? 'disabled' : ''}>Prev</button>
                <button class="btn btn-dim" id="rules-next" ${currentPage === PAGES.length - 1 ? 'disabled' : ''}>Next</button>
                <button class="btn btn-red" id="rules-close">Close</button>
            </div>
        `);

        page.render(overlay.querySelector('#rules-body'));

        overlay.querySelector('#rules-prev').addEventListener('click', () => {
            if (currentPage > 0) { currentPage--; render(); }
        });
        overlay.querySelector('#rules-next').addEventListener('click', () => {
            if (currentPage < PAGES.length - 1) { currentPage++; render(); }
        });
        overlay.querySelector('#rules-close').addEventListener('click', onClose);
    }

    overlay.tabIndex = 0;
    overlay.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' || e.key === 'r' || e.key === 'R') { onClose(); e.preventDefault(); }
        if (e.key === 'ArrowLeft' && currentPage > 0) { currentPage--; render(); }
        if (e.key === 'ArrowRight' && currentPage < PAGES.length - 1) { currentPage++; render(); }
    });

    render();
    setTimeout(() => overlay.focus(), 50);
    return overlay;
}

/* ---------------------------------------------------------------------- */

function renderOverview(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-h">Objective</div>
            <div class="rules-text">
                Be the <strong>last player standing</strong> with at least one
                influence (a face-down card). Force everyone else to lose both
                of theirs.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Setup</div>
            <div class="rules-text">
                Each player starts with <strong>2 coins</strong> and
                <strong>2 face-down influence cards</strong>. The deck holds
                <strong>3 copies</strong> of each of the 5 characters, 15 cards
                in total. You may look at your own cards at any time.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Turn structure</div>
            <div class="rules-text">
                On your turn take <strong>one action</strong>. Some actions can
                be <span class="hl">challenged</span> or
                <span class="hl">blocked</span>. At
                <strong>10 or more coins you must Coup</strong>.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Losing influence</div>
            <div class="rules-text">
                You <strong>choose</strong> which of your face-down cards to
                reveal. A revealed card is permanently out. Reveal both and you
                are <strong>eliminated</strong>.
            </div>
        </div>
    `;
}

function renderCharacters(body) {
    body.innerHTML = '<div class="rules-section"><div class="rules-h">The five characters</div></div>';

    CHARACTERS.forEach((c, i) => {
        const row = document.createElement('div');
        row.className = `rules-char-row ${c.key}`;
        row.innerHTML = `
            <div class="portrait-frame"><div class="portrait"></div></div>
            <div class="rules-card" style="background-image:url('${cardArt(i)}')"></div>
            <div>
                <div class="rules-char-name" style="color:${c.color}">${c.name.toUpperCase()}</div>
                <div class="rules-char-ability">${CHAR_INFO[i].ability}</div>
                <div class="rules-char-blocks">${CHAR_INFO[i].blocks}</div>
            </div>
        `;
        applyPortrait(row.querySelector('.portrait'), i, i * 333);
        body.appendChild(row);
    });
}

function renderActions(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-h">General actions</div>
            <div class="rules-text">Anyone can take these. They cannot be challenged.</div>
        </div>
        <div class="rules-char-row no-art">
            <div>
                <div class="rules-char-name">INCOME</div>
                <div class="rules-char-ability">Take <strong>1 coin</strong>. Cannot be blocked.</div>
            </div>
        </div>
        <div class="rules-char-row no-art">
            <div>
                <div class="rules-char-name">FOREIGN AID</div>
                <div class="rules-char-ability">Take <strong>2 coins</strong>. Can be blocked by
                    <span style="color:var(--char-duke)">Duke</span>.</div>
            </div>
        </div>
        <div class="rules-char-row no-art">
            <div>
                <div class="rules-char-name">COUP</div>
                <div class="rules-char-ability">Pay <strong>7 coins</strong> to force a player to lose
                    influence. Cannot be blocked or challenged.
                    <span class="hl">Mandatory at 10+ coins.</span></div>
            </div>
        </div>
        <div class="rules-section" style="margin-top:14px">
            <div class="rules-h">Character actions</div>
            <div class="rules-text">Claim a character to use its ability.
                <span class="hl">You do not need to hold the card</span> - but you can be challenged.</div>
        </div>
        <div class="rules-char-row no-art duke">
            <div>
                <div class="rules-char-name" style="color:var(--char-duke)">TAX (Duke)</div>
                <div class="rules-char-ability">Take <strong>3 coins</strong>.</div>
            </div>
        </div>
        <div class="rules-char-row no-art assassin">
            <div>
                <div class="rules-char-name" style="color:var(--char-assassin)">ASSASSINATE (Assassin)</div>
                <div class="rules-char-ability">Pay <strong>3 coins</strong>; the target loses influence.
                    Blocked by <span style="color:var(--char-contessa)">Contessa</span>.</div>
            </div>
        </div>
        <div class="rules-char-row no-art captain">
            <div>
                <div class="rules-char-name" style="color:var(--char-captain)">STEAL (Captain)</div>
                <div class="rules-char-ability">Take <strong>2 coins</strong> from a player. Blocked by
                    <span style="color:var(--char-captain)">Captain</span> or
                    <span style="color:var(--char-ambassador)">Ambassador</span>.</div>
            </div>
        </div>
        <div class="rules-char-row no-art ambassador">
            <div>
                <div class="rules-char-name" style="color:var(--char-ambassador)">EXCHANGE (Ambassador)</div>
                <div class="rules-char-ability">Draw 2 from the deck, keep 2, return the rest.</div>
            </div>
        </div>
    `;
}

function renderChallengesBlocks(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-h">Challenging</div>
            <div class="rules-text">
                When a player claims a character action, <strong>any</strong>
                other player may challenge.<br><br>
                <strong>Challenge succeeds</strong> (they do not hold the card):
                the acting player loses influence and the action is cancelled.<br><br>
                <strong>Challenge fails</strong> (they do hold it): the
                challenger loses influence, and the acting player shuffles the
                revealed card back and draws a replacement.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Blocking</div>
            <div class="rules-text">
                <strong style="color:var(--char-duke)">Duke</strong> blocks <strong>Foreign Aid</strong><br>
                <strong style="color:var(--char-contessa)">Contessa</strong> blocks <strong>Assassination</strong><br>
                <strong style="color:var(--char-captain)">Captain</strong> or
                <strong style="color:var(--char-ambassador)">Ambassador</strong> blocks <strong>Steal</strong>
                <br><br>
                A block can itself be <span class="hl">challenged</span>.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Important</div>
            <div class="rules-text">
                <strong>Income</strong> and <strong>Coup</strong> can be neither
                challenged nor blocked. You may bluff any character action. If
                nobody responds in time the server auto-passes.
            </div>
        </div>
    `;
}

function renderTips(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-h">Beginner</div>
            <div class="rules-text">
                <strong>1.</strong> Claiming <span style="color:var(--char-duke)">Duke</span>
                for Tax early is safe and fast.<br><br>
                <strong>2.</strong> Track what others claim. If someone claims
                Captain twice and you hold both Captains, call it.<br><br>
                <strong>3.</strong> Do not challenge on a hunch - a failed
                challenge costs you an influence.<br><br>
                <strong>4.</strong> Coup always works. Bank coins when you
                cannot act safely.<br><br>
                <strong>5.</strong> With <span style="color:var(--char-contessa)">Contessa</span>
                in hand, block assassinations without hesitating.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-h">Advanced</div>
            <div class="rules-text">
                <strong>1.</strong> Challenge harder late, when fewer cards are
                unaccounted for.<br><br>
                <strong>2.</strong> Down to one influence, play conservatively.<br><br>
                <strong>3.</strong> Revealed cards narrow what opponents can
                truly hold - keep count.<br><br>
                <strong>4.</strong> Bluffing is the game. A well-timed one wins it.
            </div>
        </div>
        <div class="rules-section" style="text-align:center">
            <div class="rules-text hl">Press R, or tap RULES, at any time.</div>
        </div>
    `;
}
