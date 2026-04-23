/**
 * rules.js - Multi-page Rules Overlay
 *
 * Accessible from ANY screen via app.showRules().
 * Pages: Overview, Characters, Actions, Challenges & Blocks, Tips
 */

const PAGES = [
    { title: 'GAME OVERVIEW', render: renderOverview },
    { title: 'CHARACTERS', render: renderCharacters },
    { title: 'ACTIONS', render: renderActions },
    { title: 'CHALLENGES & BLOCKS', render: renderChallengesBlocks },
    { title: 'STRATEGY TIPS', render: renderTips },
];

const CHARACTERS = [
    {
        name: 'Duke', css: 'duke', img: 'assets/DukePortrait.png', video: 'assets/Duke.mp4',
        ability: 'Tax: Take 3 coins from the treasury.',
        blocks: 'Blocks Foreign Aid.',
    },
    {
        name: 'Assassin', css: 'assassin', img: 'assets/AssassinPortrait.png', video: 'assets/Assassin.mp4',
        ability: 'Assassinate: Pay 3 coins to force a player to lose influence.',
        blocks: 'Cannot block any action.',
    },
    {
        name: 'Captain', css: 'captain', img: 'assets/CaptainPortrait.png', video: 'assets/Captain.mp4',
        ability: 'Steal: Take 2 coins from another player.',
        blocks: 'Blocks Steal.',
    },
    {
        name: 'Ambassador', css: 'ambassador', img: 'assets/AmbassadorPortrait.png', video: 'assets/Ambassador.mp4',
        ability: 'Exchange: Draw 2 cards from the deck, choose which to keep.',
        blocks: 'Blocks Steal.',
    },
    {
        name: 'Contessa', css: 'contessa', img: 'assets/ContessaPortrait.png', video: 'assets/Contessa.mp4',
        ability: 'No active ability.',
        blocks: 'Blocks Assassination.',
    },
];

export function createRulesOverlay(onClose) {
    let currentPage = 0;

    const overlay = document.createElement('div');
    overlay.className = 'rules-overlay';

    function render() {
        const page = PAGES[currentPage];
        overlay.innerHTML = `
            <div class="rules-header">
                <span class="rules-title">RULES - ${page.title}</span>
                <span class="rules-page-indicator">${currentPage + 1} / ${PAGES.length}</span>
            </div>
            <div class="rules-body" id="rules-body"></div>
            <div class="rules-nav">
                <button class="btn btn-dim" id="rules-prev" ${currentPage === 0 ? 'disabled' : ''}>PREV</button>
                <span class="text-gray" style="font-size:clamp(9px,1.2vmin,12px)">${currentPage + 1} / ${PAGES.length}</span>
                <button class="btn btn-dim" id="rules-next" ${currentPage === PAGES.length - 1 ? 'disabled' : ''}>NEXT</button>
                <button class="btn btn-red" id="rules-close">CLOSE</button>
            </div>
        `;

        const body = overlay.querySelector('#rules-body');
        page.render(body);

        overlay.querySelector('#rules-prev').addEventListener('click', () => {
            if (currentPage > 0) { currentPage--; render(); }
        });
        overlay.querySelector('#rules-next').addEventListener('click', () => {
            if (currentPage < PAGES.length - 1) { currentPage++; render(); }
        });
        overlay.querySelector('#rules-close').addEventListener('click', onClose);
    }

    // Keyboard navigation
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

/* ============================================================
   PAGE RENDERERS
   ============================================================ */

function renderOverview(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-section-title text-gold">OBJECTIVE</div>
            <div class="rules-text">
                Be the <strong>last player standing</strong> with at least one influence (face-down card).
                Eliminate all other players by forcing them to lose both their cards.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">SETUP</div>
            <div class="rules-text">
                Each player starts with <strong>2 coins</strong> and <strong>2 face-down influence cards</strong>.
                The deck has <strong>3 copies</strong> of each of the 5 characters (15 cards total).
                You may look at your own cards at any time.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">TURN STRUCTURE</div>
            <div class="rules-text">
                On your turn, choose <strong>one action</strong>. Some actions can be
                <span class="highlight">challenged</span> or <span class="highlight">blocked</span> by other players.
                If you have <strong>10+ coins</strong>, you <strong>must Coup</strong>.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">LOSING INFLUENCE</div>
            <div class="rules-text">
                When you lose influence, you <strong>choose</strong> one of your face-down cards to
                <strong>reveal</strong> (turn face-up). That card is permanently out.
                If both your cards are revealed, you are <strong>eliminated</strong>.
            </div>
        </div>
    `;
}

function renderCharacters(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-section-title text-gold">THE 5 CHARACTERS</div>
        </div>
    `;
    for (const ch of CHARACTERS) {
        const row = document.createElement('div');
        row.className = `rules-char-row ${ch.css}`;
        row.innerHTML = `
            <video class="rules-char-portrait" src="${ch.video}" autoplay loop muted playsinline webkit-playsinline disablepictureinpicture></video>
            <div class="rules-char-info">
                <div class="rules-char-name" style="color:var(--char-${ch.css})">${ch.name.toUpperCase()}</div>
                <div class="rules-char-ability">${ch.ability}</div>
                <div class="rules-char-blocks">${ch.blocks}</div>
            </div>
        `;
        body.appendChild(row);
    }
}

function renderActions(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-section-title text-gold">GENERAL ACTIONS</div>
            <div class="rules-text">Anyone can take these. They cannot be challenged.</div>
        </div>
        <div class="rules-char-row">
            <div class="rules-char-info">
                <div class="rules-char-name">INCOME</div>
                <div class="rules-char-ability">Take <strong>1 coin</strong> from the treasury. Cannot be blocked.</div>
            </div>
        </div>
        <div class="rules-char-row">
            <div class="rules-char-info">
                <div class="rules-char-name">FOREIGN AID</div>
                <div class="rules-char-ability">Take <strong>2 coins</strong> from the treasury. Can be blocked by <span style="color:var(--char-duke)">Duke</span>.</div>
            </div>
        </div>
        <div class="rules-char-row">
            <div class="rules-char-info">
                <div class="rules-char-name">COUP</div>
                <div class="rules-char-ability">Pay <strong>7 coins</strong> to force a player to lose influence. Cannot be blocked or challenged. <span class="highlight">Mandatory at 10+ coins.</span></div>
            </div>
        </div>
        <div class="rules-section" style="margin-top:12px">
            <div class="rules-section-title text-gold">CHARACTER ACTIONS</div>
            <div class="rules-text">Claim a character to use its ability. <span class="highlight">You don't need to actually have the card!</span> But you can be challenged.</div>
        </div>
        <div class="rules-char-row duke">
            <div class="rules-char-info">
                <div class="rules-char-name" style="color:var(--char-duke)">TAX (Duke)</div>
                <div class="rules-char-ability">Take <strong>3 coins</strong> from the treasury.</div>
            </div>
        </div>
        <div class="rules-char-row assassin">
            <div class="rules-char-info">
                <div class="rules-char-name" style="color:var(--char-assassin)">ASSASSINATE (Assassin)</div>
                <div class="rules-char-ability">Pay <strong>3 coins</strong>. Target must lose influence. Can be blocked by <span style="color:var(--char-contessa)">Contessa</span>.</div>
            </div>
        </div>
        <div class="rules-char-row captain">
            <div class="rules-char-info">
                <div class="rules-char-name" style="color:var(--char-captain)">STEAL (Captain)</div>
                <div class="rules-char-ability">Take <strong>2 coins</strong> from another player. Can be blocked by <span style="color:var(--char-captain)">Captain</span> or <span style="color:var(--char-ambassador)">Ambassador</span>.</div>
            </div>
        </div>
        <div class="rules-char-row ambassador">
            <div class="rules-char-info">
                <div class="rules-char-name" style="color:var(--char-ambassador)">EXCHANGE (Ambassador)</div>
                <div class="rules-char-ability">Draw 2 cards from the deck. Choose 2 to keep, return the rest.</div>
            </div>
        </div>
    `;
}

function renderChallengesBlocks(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-section-title text-gold">CHALLENGING</div>
            <div class="rules-text">
                When a player claims a character action, <strong>any</strong> other player may challenge.
                <br><br>
                <strong>If the challenge succeeds</strong> (they don't have the card): the acting player
                loses influence, and their action is cancelled.
                <br><br>
                <strong>If the challenge fails</strong> (they DO have the card): the challenger loses influence.
                The acting player shuffles the revealed card back into the deck and draws a new one.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">BLOCKING</div>
            <div class="rules-text">
                Certain actions can be blocked by claiming a specific character:
                <br><br>
                <strong style="color:var(--char-duke)">Duke</strong> blocks <strong>Foreign Aid</strong><br>
                <strong style="color:var(--char-contessa)">Contessa</strong> blocks <strong>Assassination</strong><br>
                <strong style="color:var(--char-captain)">Captain</strong> or <strong style="color:var(--char-ambassador)">Ambassador</strong> blocks <strong>Steal</strong>
                <br><br>
                A block can also be <span class="highlight">challenged</span>! If someone blocks and you think they're bluffing,
                challenge the block.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">IMPORTANT</div>
            <div class="rules-text">
                <strong>Income</strong> and <strong>Coup</strong> cannot be challenged or blocked.<br>
                You can bluff any character action &mdash; you don't need the card!<br>
                The timer auto-passes if no one responds in time.
            </div>
        </div>
    `;
}

function renderTips(body) {
    body.innerHTML = `
        <div class="rules-section">
            <div class="rules-section-title text-gold">BEGINNER TIPS</div>
            <div class="rules-text">
                <strong>1.</strong> Claiming <span style="color:var(--char-duke)">Duke</span> for Tax early is safe &mdash; it's hard to disprove and gets you coins fast.<br><br>
                <strong>2.</strong> Track what characters others claim. If someone claims Captain twice and you have both Captains, call the bluff!<br><br>
                <strong>3.</strong> Don't challenge randomly. The penalty for a failed challenge (losing influence) is severe.<br><br>
                <strong>4.</strong> Coup is guaranteed &mdash; save up when you can't safely use character actions.<br><br>
                <strong>5.</strong> If you have <span style="color:var(--char-contessa)">Contessa</span>, block assassinations confidently. If you don't, bluffing Contessa is risky but can save your life.
            </div>
        </div>
        <div class="rules-section">
            <div class="rules-section-title text-gold">ADVANCED STRATEGY</div>
            <div class="rules-text">
                <strong>1.</strong> Challenge aggressively in late game when there are fewer cards in play.<br><br>
                <strong>2.</strong> If you lose one influence, play more conservatively with your last card.<br><br>
                <strong>3.</strong> Pay attention to which cards have been revealed &mdash; this narrows down what opponents can truly have.<br><br>
                <strong>4.</strong> Bluffing is the heart of Coup. A well-timed bluff wins games.
            </div>
        </div>
        <div class="rules-section" style="text-align:center;padding-top:12px">
            <div class="rules-text highlight" style="font-size:clamp(11px,1.6vmin,15px)">
                Press <strong>R</strong> or tap RULES at any time to view these rules.
            </div>
        </div>
    `;
}
