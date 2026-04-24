"""
coup_engine.py - Python ctypes wrapper for libcoup_rules

Loads the shared library built by `make coup-lib` and provides a clean
CoupEngine class that hides all ctypes details.
"""

import ctypes
import os
import platform
import shutil
import tempfile

# Event type constants (mirrors coup_rules.h)
EVT_GAME_STARTED = 0
EVT_TURN_STARTED = 1
EVT_ACTION_DECLARED = 2
EVT_CHALLENGE_OPENED = 3
EVT_CHALLENGE_RESULT = 4
EVT_BLOCK_OPENED = 5
EVT_BLOCK_DECLARED = 6
EVT_BLOCK_CHALLENGE_OPENED = 7
EVT_BLOCK_CHALLENGE_RESULT = 8
EVT_INFLUENCE_LOSS_REQUESTED = 9
EVT_INFLUENCE_LOST = 10
EVT_EXCHANGE_OFFERED = 11
EVT_EXCHANGE_RESOLVED = 12
EVT_COINS_CHANGED = 13
EVT_PLAYER_ELIMINATED = 14
EVT_ACTION_RESOLVED = 15
EVT_ACTION_CANCELLED = 16
EVT_CARD_REPLACED = 17
EVT_ROUND_ADVANCED = 18
EVT_GAME_OVER = 19
EVT_PLAYER_JOINED = 20
EVT_PLAYER_LEFT = 21
EVT_READY_CHANGED = 22

# Phase constants (mirrors coup_rules.h)
PHASE_LOBBY = 0
PHASE_WAITING_FOR_ACTION = 1
PHASE_CHALLENGE_WINDOW = 2
PHASE_BLOCK_WINDOW = 3
PHASE_BLOCK_CHALLENGE_WINDOW = 4
PHASE_WAITING_FOR_INFLUENCE_LOSS = 5
PHASE_WAITING_FOR_EXCHANGE = 6
PHASE_RESOLVING = 7

# Character constants
CHAR_DUKE = 0
CHAR_ASSASSIN = 1
CHAR_CAPTAIN = 2
CHAR_AMBASSADOR = 3
CHAR_CONTESSA = 4

# Action constants
ACT_INCOME = 0
ACT_FOREIGN_AID = 1
ACT_COUP = 2
ACT_TAX = 3
ACT_ASSASSINATE = 4
ACT_STEAL = 5
ACT_EXCHANGE = 6

# Response constants
RESP_PASS = 0
RESP_CHALLENGE = 1
RESP_BLOCK = 2

u8 = ctypes.c_uint8
c_int = ctypes.c_int
u32 = ctypes.c_uint32


def _find_library_path():
    """Find the path to libcoup_rules shared library."""
    lib_path = os.environ.get("COUP_RULES_LIB")
    if lib_path and os.path.exists(lib_path):
        return lib_path

    if platform.system() == "Darwin":
        lib_name = "libcoup_rules.dylib"
    else:
        lib_name = "libcoup_rules.so"

    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Look next to this script first (self-contained server deployment)
    lib_path = os.path.join(script_dir, lib_name)
    if os.path.exists(lib_path):
        return lib_path

    # Fall back to project build directory (development)
    project_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    lib_path = os.path.join(project_root, "build", lib_name)
    if os.path.exists(lib_path):
        return lib_path

    raise FileNotFoundError(
        f"Shared library '{lib_name}' not found. "
        f"Build it with: make coup-lib (from project root)"
    )


def _load_library():
    """Load libcoup_rules shared library (shared singleton)."""
    return ctypes.CDLL(_find_library_path())


def _load_library_isolated():
    """Load an isolated copy of libcoup_rules with independent static state.

    The C bridge uses a single static g_rules instance, so dlopen() with the
    same path returns the same handle.  Copying the .dylib/.so to a unique
    temp path forces dlopen() to create a fresh mapping with independent BSS.
    """
    src = _find_library_path()
    suffix = ".dylib" if platform.system() == "Darwin" else ".so"
    fd, tmp = tempfile.mkstemp(suffix=suffix, prefix="coup_rules_")
    os.close(fd)
    shutil.copy2(src, tmp)
    lib = ctypes.CDLL(tmp)
    os.unlink(tmp)  # safe on Unix — mapped pages stay until handle is closed
    return lib


def _setup_bindings(lib):
    """Set argtypes and restype for all bridge functions."""
    # Lifecycle
    lib.bridge_init.argtypes = [u32]
    lib.bridge_init.restype = None

    # Submit functions
    lib.bridge_submit_start.argtypes = []
    lib.bridge_submit_start.restype = c_int

    lib.bridge_submit_action.argtypes = [u8, u8, u8]
    lib.bridge_submit_action.restype = c_int

    lib.bridge_submit_response.argtypes = [u8, u8]
    lib.bridge_submit_response.restype = c_int

    lib.bridge_submit_block_claim.argtypes = [u8, u8]
    lib.bridge_submit_block_claim.restype = c_int

    lib.bridge_submit_lose_influence.argtypes = [u8, u8]
    lib.bridge_submit_lose_influence.restype = c_int

    lib.bridge_submit_exchange.argtypes = [u8, u8, u8]
    lib.bridge_submit_exchange.restype = c_int

    lib.bridge_submit_timeout.argtypes = []
    lib.bridge_submit_timeout.restype = c_int

    # Lobby submit functions
    lib.bridge_submit_add_player.argtypes = []
    lib.bridge_submit_add_player.restype = c_int

    lib.bridge_submit_add_bot.argtypes = []
    lib.bridge_submit_add_bot.restype = c_int

    lib.bridge_submit_remove_player.argtypes = [u8]
    lib.bridge_submit_remove_player.restype = c_int

    lib.bridge_submit_set_ready.argtypes = [u8, u8]
    lib.bridge_submit_set_ready.restype = c_int

    # Event access
    lib.bridge_event_count.argtypes = []
    lib.bridge_event_count.restype = c_int

    lib.bridge_event_type.argtypes = [c_int]
    lib.bridge_event_type.restype = c_int

    # Event field accessors — all follow pattern: (int idx) -> uint8_t or int
    for name in [
        "bridge_evt_game_started_count",
        "bridge_evt_turn_started_player",
        "bridge_evt_turn_started_actions",
        "bridge_evt_action_declared_actor",
        "bridge_evt_action_declared_action",
        "bridge_evt_action_declared_target",
        "bridge_evt_challenge_opened_defender",
        "bridge_evt_challenge_opened_char",
        "bridge_evt_challenge_result_challenger",
        "bridge_evt_challenge_result_defender",
        "bridge_evt_challenge_result_revealed",
        "bridge_evt_block_opened_blockable_by",
        "bridge_evt_block_opened_target_only",
        "bridge_evt_block_declared_blocker",
        "bridge_evt_block_declared_char",
        "bridge_evt_block_challenge_opened_blocker",
        "bridge_evt_block_challenge_opened_char",
        "bridge_evt_block_challenge_result_challenger",
        "bridge_evt_block_challenge_result_blocker",
        "bridge_evt_block_challenge_result_revealed",
        "bridge_evt_influence_loss_requested_player",
        "bridge_evt_influence_lost_player",
        "bridge_evt_influence_lost_card_idx",
        "bridge_evt_influence_lost_char",
        "bridge_evt_exchange_offered_player",
        "bridge_evt_exchange_offered_count",
        "bridge_evt_exchange_resolved_player",
        "bridge_evt_coins_changed_player",
        "bridge_evt_coins_changed_old",
        "bridge_evt_coins_changed_new",
        "bridge_evt_player_eliminated_player",
        "bridge_evt_action_resolved_action",
        "bridge_evt_action_resolved_actor",
        "bridge_evt_action_resolved_target",
        "bridge_evt_action_cancelled_action",
        "bridge_evt_action_cancelled_actor",
        "bridge_evt_action_cancelled_reason",
        "bridge_evt_card_replaced_player",
        "bridge_evt_card_replaced_card_idx",
        "bridge_evt_card_replaced_new_char",
        "bridge_evt_round_advanced_number",
        "bridge_evt_game_over_winner",
        "bridge_evt_player_joined_id",
        "bridge_evt_player_joined_is_bot",
        "bridge_evt_player_left_id",
        "bridge_evt_ready_changed_player",
        "bridge_evt_ready_changed_ready",
    ]:
        fn = getattr(lib, name)
        fn.argtypes = [c_int]
        fn.restype = u8

    # Bool-returning event accessors
    for name in [
        "bridge_evt_challenge_result_had_card",
        "bridge_evt_block_challenge_result_had_card",
    ]:
        fn = getattr(lib, name)
        fn.argtypes = [c_int]
        fn.restype = c_int

    # Special: exchange_offered_card takes (idx, card_idx)
    lib.bridge_evt_exchange_offered_card.argtypes = [c_int, c_int]
    lib.bridge_evt_exchange_offered_card.restype = u8

    # State queries
    lib.bridge_phase.argtypes = []
    lib.bridge_phase.restype = u8

    lib.bridge_current_player.argtypes = []
    lib.bridge_current_player.restype = u8

    lib.bridge_valid_actions.argtypes = []
    lib.bridge_valid_actions.restype = u8

    lib.bridge_game_active.argtypes = []
    lib.bridge_game_active.restype = c_int

    # Player state
    lib.bridge_player_card.argtypes = [c_int, c_int]
    lib.bridge_player_card.restype = u8

    lib.bridge_player_revealed.argtypes = [c_int, c_int]
    lib.bridge_player_revealed.restype = c_int

    lib.bridge_player_coins.argtypes = [c_int]
    lib.bridge_player_coins.restype = u8

    lib.bridge_player_alive.argtypes = [c_int]
    lib.bridge_player_alive.restype = c_int

    lib.bridge_player_ready.argtypes = [c_int]
    lib.bridge_player_ready.restype = c_int

    lib.bridge_player_is_bot.argtypes = [c_int]
    lib.bridge_player_is_bot.restype = c_int

    lib.bridge_player_count.argtypes = []
    lib.bridge_player_count.restype = c_int

    # Timeout helpers
    lib.bridge_influence_loser.argtypes = []
    lib.bridge_influence_loser.restype = u8

    lib.bridge_exchange_player.argtypes = []
    lib.bridge_exchange_player.restype = u8

    lib.bridge_blocker_id.argtypes = []
    lib.bridge_blocker_id.restype = u8

    # Exchange cards
    lib.bridge_exchange_card.argtypes = [c_int]
    lib.bridge_exchange_card.restype = u8

    lib.bridge_exchange_count.argtypes = []
    lib.bridge_exchange_count.restype = c_int

    # Pending response
    lib.bridge_pending_response.argtypes = [c_int]
    lib.bridge_pending_response.restype = c_int

    lib.bridge_pending_count.argtypes = []
    lib.bridge_pending_count.restype = c_int

    # Bot bridge bindings
    lib.bot_bridge_decide.argtypes = [u8, u8, u32]
    lib.bot_bridge_decide.restype = u32

    lib.bot_bridge_result_valid.argtypes = []
    lib.bot_bridge_result_valid.restype = c_int

    for name in [
        "bot_bridge_result_input_type",
        "bot_bridge_result_player_id",
        "bot_bridge_result_action",
        "bot_bridge_result_target",
        "bot_bridge_result_response",
        "bot_bridge_result_card_idx",
        "bot_bridge_result_keep0",
        "bot_bridge_result_keep1",
        "bot_bridge_result_block_claim_char",
    ]:
        fn = getattr(lib, name)
        fn.argtypes = []
        fn.restype = u8

    lib.bot_bridge_result_has_block_claim.argtypes = []
    lib.bot_bridge_result_has_block_claim.restype = c_int


class CoupEngine:
    """High-level wrapper around the C coup rules engine.

    Args:
        isolated: If True, load an independent copy of the shared library
            so this instance has its own static state.  Required when
            multiple engines coexist in the same process (e.g. tests).
    """

    def __init__(self, isolated=False):
        self._lib = _load_library_isolated() if isolated else _load_library()
        _setup_bindings(self._lib)

    def init(self, seed=0):
        self._lib.bridge_init(seed)

    # --- Submit ---

    def submit_start(self):
        return self._lib.bridge_submit_start()

    def submit_add_player(self):
        return self._lib.bridge_submit_add_player()

    def submit_add_bot(self):
        return self._lib.bridge_submit_add_bot()

    def submit_remove_player(self, player_id):
        return self._lib.bridge_submit_remove_player(player_id)

    def submit_set_ready(self, player_id, ready):
        return self._lib.bridge_submit_set_ready(player_id, ready)

    def submit_action(self, player_id, action, target=0xFF):
        return self._lib.bridge_submit_action(player_id, action, target)

    def submit_response(self, player_id, response):
        return self._lib.bridge_submit_response(player_id, response)

    def submit_block_claim(self, player_id, character):
        return self._lib.bridge_submit_block_claim(player_id, character)

    def submit_lose_influence(self, player_id, card_idx):
        return self._lib.bridge_submit_lose_influence(player_id, card_idx)

    def submit_exchange(self, player_id, keep0, keep1):
        return self._lib.bridge_submit_exchange(player_id, keep0, keep1)

    def submit_timeout(self):
        return self._lib.bridge_submit_timeout()

    # --- Events ---

    def event_count(self):
        return self._lib.bridge_event_count()

    def event_type(self, idx):
        return self._lib.bridge_event_type(idx)

    def get_events(self):
        """Return list of (type, idx) tuples for all events from last submit."""
        count = self.event_count()
        return [(self.event_type(i), i) for i in range(count)]

    # --- Event field accessors (delegate to lib) ---

    def evt_game_started_count(self, idx):
        return self._lib.bridge_evt_game_started_count(idx)

    def evt_turn_started_player(self, idx):
        return self._lib.bridge_evt_turn_started_player(idx)

    def evt_turn_started_actions(self, idx):
        return self._lib.bridge_evt_turn_started_actions(idx)

    def evt_action_declared_actor(self, idx):
        return self._lib.bridge_evt_action_declared_actor(idx)

    def evt_action_declared_action(self, idx):
        return self._lib.bridge_evt_action_declared_action(idx)

    def evt_action_declared_target(self, idx):
        return self._lib.bridge_evt_action_declared_target(idx)

    def evt_challenge_opened_defender(self, idx):
        return self._lib.bridge_evt_challenge_opened_defender(idx)

    def evt_challenge_opened_char(self, idx):
        return self._lib.bridge_evt_challenge_opened_char(idx)

    def evt_challenge_result_challenger(self, idx):
        return self._lib.bridge_evt_challenge_result_challenger(idx)

    def evt_challenge_result_defender(self, idx):
        return self._lib.bridge_evt_challenge_result_defender(idx)

    def evt_challenge_result_had_card(self, idx):
        return bool(self._lib.bridge_evt_challenge_result_had_card(idx))

    def evt_challenge_result_revealed(self, idx):
        return self._lib.bridge_evt_challenge_result_revealed(idx)

    def evt_block_opened_blockable_by(self, idx):
        return self._lib.bridge_evt_block_opened_blockable_by(idx)

    def evt_block_opened_target_only(self, idx):
        return self._lib.bridge_evt_block_opened_target_only(idx)

    def evt_block_declared_blocker(self, idx):
        return self._lib.bridge_evt_block_declared_blocker(idx)

    def evt_block_declared_char(self, idx):
        return self._lib.bridge_evt_block_declared_char(idx)

    def evt_block_challenge_opened_blocker(self, idx):
        return self._lib.bridge_evt_block_challenge_opened_blocker(idx)

    def evt_block_challenge_opened_char(self, idx):
        return self._lib.bridge_evt_block_challenge_opened_char(idx)

    def evt_block_challenge_result_challenger(self, idx):
        return self._lib.bridge_evt_block_challenge_result_challenger(idx)

    def evt_block_challenge_result_blocker(self, idx):
        return self._lib.bridge_evt_block_challenge_result_blocker(idx)

    def evt_block_challenge_result_had_card(self, idx):
        return bool(self._lib.bridge_evt_block_challenge_result_had_card(idx))

    def evt_block_challenge_result_revealed(self, idx):
        return self._lib.bridge_evt_block_challenge_result_revealed(idx)

    def evt_influence_loss_requested_player(self, idx):
        return self._lib.bridge_evt_influence_loss_requested_player(idx)

    def evt_influence_lost_player(self, idx):
        return self._lib.bridge_evt_influence_lost_player(idx)

    def evt_influence_lost_card_idx(self, idx):
        return self._lib.bridge_evt_influence_lost_card_idx(idx)

    def evt_influence_lost_char(self, idx):
        return self._lib.bridge_evt_influence_lost_char(idx)

    def evt_exchange_offered_player(self, idx):
        return self._lib.bridge_evt_exchange_offered_player(idx)

    def evt_exchange_offered_card(self, idx, card_idx):
        return self._lib.bridge_evt_exchange_offered_card(idx, card_idx)

    def evt_exchange_offered_count(self, idx):
        return self._lib.bridge_evt_exchange_offered_count(idx)

    def evt_exchange_resolved_player(self, idx):
        return self._lib.bridge_evt_exchange_resolved_player(idx)

    def evt_coins_changed_player(self, idx):
        return self._lib.bridge_evt_coins_changed_player(idx)

    def evt_coins_changed_old(self, idx):
        return self._lib.bridge_evt_coins_changed_old(idx)

    def evt_coins_changed_new(self, idx):
        return self._lib.bridge_evt_coins_changed_new(idx)

    def evt_player_eliminated_player(self, idx):
        return self._lib.bridge_evt_player_eliminated_player(idx)

    def evt_action_resolved_action(self, idx):
        return self._lib.bridge_evt_action_resolved_action(idx)

    def evt_action_resolved_actor(self, idx):
        return self._lib.bridge_evt_action_resolved_actor(idx)

    def evt_action_resolved_target(self, idx):
        return self._lib.bridge_evt_action_resolved_target(idx)

    def evt_action_cancelled_action(self, idx):
        return self._lib.bridge_evt_action_cancelled_action(idx)

    def evt_action_cancelled_actor(self, idx):
        return self._lib.bridge_evt_action_cancelled_actor(idx)

    def evt_action_cancelled_reason(self, idx):
        return self._lib.bridge_evt_action_cancelled_reason(idx)

    def evt_card_replaced_player(self, idx):
        return self._lib.bridge_evt_card_replaced_player(idx)

    def evt_card_replaced_card_idx(self, idx):
        return self._lib.bridge_evt_card_replaced_card_idx(idx)

    def evt_card_replaced_new_char(self, idx):
        return self._lib.bridge_evt_card_replaced_new_char(idx)

    def evt_round_advanced_number(self, idx):
        return self._lib.bridge_evt_round_advanced_number(idx)

    def evt_game_over_winner(self, idx):
        return self._lib.bridge_evt_game_over_winner(idx)

    def evt_player_joined_id(self, idx):
        return self._lib.bridge_evt_player_joined_id(idx)

    def evt_player_joined_is_bot(self, idx):
        return self._lib.bridge_evt_player_joined_is_bot(idx)

    def evt_player_left_id(self, idx):
        return self._lib.bridge_evt_player_left_id(idx)

    def evt_ready_changed_player(self, idx):
        return self._lib.bridge_evt_ready_changed_player(idx)

    def evt_ready_changed_ready(self, idx):
        return self._lib.bridge_evt_ready_changed_ready(idx)

    # --- State queries ---

    def phase(self):
        return self._lib.bridge_phase()

    def current_player(self):
        return self._lib.bridge_current_player()

    def valid_actions(self):
        return self._lib.bridge_valid_actions()

    def game_active(self):
        return bool(self._lib.bridge_game_active())

    # --- Player state ---

    def player_card(self, pid, slot):
        return self._lib.bridge_player_card(pid, slot)

    def player_revealed(self, pid, slot):
        return bool(self._lib.bridge_player_revealed(pid, slot))

    def player_coins(self, pid):
        return self._lib.bridge_player_coins(pid)

    def player_alive(self, pid):
        return bool(self._lib.bridge_player_alive(pid))

    def player_ready(self, pid):
        return bool(self._lib.bridge_player_ready(pid))

    def player_is_bot(self, pid):
        return bool(self._lib.bridge_player_is_bot(pid))

    def player_count(self):
        return self._lib.bridge_player_count()

    # --- Timeout helpers ---

    def influence_loser(self):
        return self._lib.bridge_influence_loser()

    def exchange_player(self):
        return self._lib.bridge_exchange_player()

    def blocker_id(self):
        return self._lib.bridge_blocker_id()

    # --- Exchange ---

    def exchange_card(self, idx):
        return self._lib.bridge_exchange_card(idx)

    def exchange_count(self):
        return self._lib.bridge_exchange_count()

    # --- Pending ---

    def pending_response(self, pid):
        return bool(self._lib.bridge_pending_response(pid))

    def pending_count(self):
        return self._lib.bridge_pending_count()

    # --- Bot AI (shared C library) ---

    def bot_decide(self, bot_id, difficulty, rng_state):
        """Ask the C bot library for a decision.

        Returns (new_rng_state, decision_dict_or_None).
        decision_dict has keys based on input_type:
          ACTION:         {type, player_id, action, target}
          RESPONSE:       {type, player_id, response}
          LOSE_INFLUENCE: {type, player_id, card_idx}
          EXCHANGE:       {type, player_id, keep0, keep1}
        Plus optional block_claim: {character} if has_block_claim is set.
        """
        new_rng = self._lib.bot_bridge_decide(bot_id, difficulty, rng_state)
        if not self._lib.bot_bridge_result_valid():
            return new_rng, None

        input_type = int(self._lib.bot_bridge_result_input_type())
        player_id = int(self._lib.bot_bridge_result_player_id())

        decision = {"type": input_type, "player_id": player_id}

        # INPUT_ACTION = 1
        if input_type == 1:
            decision["action"] = int(self._lib.bot_bridge_result_action())
            decision["target"] = int(self._lib.bot_bridge_result_target())
        # INPUT_RESPONSE = 2
        elif input_type == 2:
            decision["response"] = int(self._lib.bot_bridge_result_response())
        # INPUT_LOSE_INFLUENCE = 4
        elif input_type == 4:
            decision["card_idx"] = int(self._lib.bot_bridge_result_card_idx())
        # INPUT_EXCHANGE_CHOICE = 5
        elif input_type == 5:
            decision["keep0"] = int(self._lib.bot_bridge_result_keep0())
            decision["keep1"] = int(self._lib.bot_bridge_result_keep1())

        if self._lib.bot_bridge_result_has_block_claim():
            decision["block_claim"] = {
                "character": int(self._lib.bot_bridge_result_block_claim_char())
            }

        return new_rng, decision
