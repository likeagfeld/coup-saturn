/**
 * test_coup_event_log.c - Tests for Coup event log
 */

#include "cui_test_framework.h"
#include "coup_event_log.h"

static coup_event_t make_evt(coup_event_type_t type)
{
    coup_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    return e;
}

CUI_TEST(log_init_empty)
{
    coup_event_log_t log;
    coup_event_log_init(&log);
    CUI_ASSERT_EQ(0, (int)log.count);
    CUI_ASSERT_EQ(0, (int)coup_event_log_latest_offset(&log));
}

CUI_TEST(log_append_increments_offset)
{
    coup_event_log_t log;
    coup_event_log_init(&log);

    coup_event_t e1 = make_evt(COUP_EVT_GAME_STARTED);
    coup_event_log_append(&log, &e1);
    CUI_ASSERT_EQ(1, (int)coup_event_log_latest_offset(&log));

    coup_event_t e2 = make_evt(COUP_EVT_TURN_STARTED);
    coup_event_log_append(&log, &e2);
    CUI_ASSERT_EQ(2, (int)coup_event_log_latest_offset(&log));

    CUI_ASSERT_EQ(2, (int)log.count);
}

CUI_TEST(log_since_returns_events_after_offset)
{
    coup_event_log_t log;
    coup_event_log_init(&log);

    coup_event_t e1 = make_evt(COUP_EVT_GAME_STARTED);
    coup_event_t e2 = make_evt(COUP_EVT_TURN_STARTED);
    coup_event_t e3 = make_evt(COUP_EVT_ACTION_DECLARED);
    coup_event_log_append(&log, &e1);
    coup_event_log_append(&log, &e2);
    coup_event_log_append(&log, &e3);

    /* Get events after offset 1 → should return offsets 2 and 3 */
    coup_event_entry_t out[10];
    int n = coup_event_log_since(&log, 1, out, 10);
    CUI_ASSERT_EQ(2, n);
    CUI_ASSERT_EQ(2, (int)out[0].offset);
    CUI_ASSERT_EQ((int)COUP_EVT_TURN_STARTED, (int)out[0].event.type);
    CUI_ASSERT_EQ(3, (int)out[1].offset);
    CUI_ASSERT_EQ((int)COUP_EVT_ACTION_DECLARED, (int)out[1].event.type);
}

CUI_TEST(log_since_zero_returns_all)
{
    coup_event_log_t log;
    coup_event_log_init(&log);

    coup_event_t e1 = make_evt(COUP_EVT_GAME_STARTED);
    coup_event_t e2 = make_evt(COUP_EVT_TURN_STARTED);
    coup_event_log_append(&log, &e1);
    coup_event_log_append(&log, &e2);

    coup_event_entry_t out[10];
    int n = coup_event_log_since(&log, 0, out, 10);
    CUI_ASSERT_EQ(2, n);
}

CUI_TEST(log_wraps_ring_buffer)
{
    coup_event_log_t log;
    coup_event_log_init(&log);

    /* Fill the buffer completely then add one more */
    int i;
    for (i = 0; i < COUP_MAX_EVENT_LOG + 1; i++) {
        coup_event_t e = make_evt(COUP_EVT_COINS_CHANGED);
        coup_event_log_append(&log, &e);
    }

    /* Count should be capped at COUP_MAX_EVENT_LOG */
    CUI_ASSERT_EQ(COUP_MAX_EVENT_LOG, (int)log.count);

    /* Latest offset should be COUP_MAX_EVENT_LOG + 1 */
    CUI_ASSERT_EQ(COUP_MAX_EVENT_LOG + 1,
                   (int)coup_event_log_latest_offset(&log));

    /* Oldest surviving event should have offset 2 (offset 1 was evicted) */
    coup_event_entry_t out[COUP_MAX_EVENT_LOG];
    int n = coup_event_log_since(&log, 0, out, COUP_MAX_EVENT_LOG);
    CUI_ASSERT_EQ(COUP_MAX_EVENT_LOG, n);
    CUI_ASSERT_EQ(2, (int)out[0].offset);
}

CUI_TEST(log_latest_offset_tracks_head)
{
    coup_event_log_t log;
    coup_event_log_init(&log);

    CUI_ASSERT_EQ(0, (int)coup_event_log_latest_offset(&log));

    coup_event_t e = make_evt(COUP_EVT_GAME_STARTED);
    coup_event_log_append(&log, &e);
    CUI_ASSERT_EQ(1, (int)coup_event_log_latest_offset(&log));

    e = make_evt(COUP_EVT_TURN_STARTED);
    coup_event_log_append(&log, &e);
    CUI_ASSERT_EQ(2, (int)coup_event_log_latest_offset(&log));

    e = make_evt(COUP_EVT_ACTION_DECLARED);
    coup_event_log_append(&log, &e);
    CUI_ASSERT_EQ(3, (int)coup_event_log_latest_offset(&log));
}
