/**
 * test_coup_protocol.c - Tests for INPUT_RELAY decode and protocol helpers
 *
 * Covers coup_decode_input_relay() from coup_protocol.h:
 *   - Valid decode for every relay type (with 2-byte seq prefix)
 *   - Seq extraction
 *   - Truncated/malformed payloads
 *   - Unknown relay type codes
 */

#include "cui_test_framework.h"
#include "coup_rules.h"
#include "coup_protocol.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Valid Decodes
 * ====================================================================== */

CUI_TEST(decode_relay_start_game)
{
    /* [seq_hi=0][seq_lo=0][type=START_GAME][pid=0] */
    uint8_t payload[] = { 0, 0, COUP_RELAY_START_GAME, 0 };
    coup_input_t out;
    uint16_t seq = 0xFFFF;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 4, &out, &seq);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_START_GAME, (int)out.type);
    CUI_ASSERT_EQ(0, (int)out.player_id);
    CUI_ASSERT_EQ(0, (int)seq);
}

CUI_TEST(decode_relay_action)
{
    /* [seq_hi=0][seq_lo=1][type=ACTION][pid=2][action=STEAL][target=1] */
    uint8_t payload[] = { 0, 1, COUP_RELAY_ACTION, 2, COUP_RULES_ACT_STEAL, 1 };
    coup_input_t out;
    uint16_t seq = 0xFFFF;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 6, &out, &seq);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_ACTION, (int)out.type);
    CUI_ASSERT_EQ(2, (int)out.player_id);
    CUI_ASSERT_EQ((int)COUP_RULES_ACT_STEAL, (int)out.data.action.action);
    CUI_ASSERT_EQ(1, (int)out.data.action.target_id);
    CUI_ASSERT_EQ(1, (int)seq);
}

CUI_TEST(decode_relay_response)
{
    uint8_t payload[] = { 0, 2, COUP_RELAY_RESPONSE, 1, COUP_RULES_RESP_CHALLENGE };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 5, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_RESPONSE, (int)out.type);
    CUI_ASSERT_EQ(1, (int)out.player_id);
    CUI_ASSERT_EQ((int)COUP_RULES_RESP_CHALLENGE, (int)out.data.response.response);
}

CUI_TEST(decode_relay_block_claim)
{
    uint8_t payload[] = { 0, 0, COUP_RELAY_BLOCK_CLAIM, 3, COUP_RULES_CHAR_CAPTAIN };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 5, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_BLOCK_CLAIM, (int)out.type);
    CUI_ASSERT_EQ(3, (int)out.player_id);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_CAPTAIN, (int)out.data.block_claim.character);
}

CUI_TEST(decode_relay_lose_influence)
{
    uint8_t payload[] = { 0, 0, COUP_RELAY_LOSE_INFLUENCE, 0, 1 };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 5, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_LOSE_INFLUENCE, (int)out.type);
    CUI_ASSERT_EQ(0, (int)out.player_id);
    CUI_ASSERT_EQ(1, (int)out.data.lose_influence.card_idx);
}

CUI_TEST(decode_relay_exchange_choice)
{
    uint8_t payload[] = { 0, 0, COUP_RELAY_EXCHANGE_CHOICE, 2, 0, 3 };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 6, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_EXCHANGE_CHOICE, (int)out.type);
    CUI_ASSERT_EQ(2, (int)out.player_id);
    CUI_ASSERT_EQ(0, (int)out.data.exchange_choice.keep[0]);
    CUI_ASSERT_EQ(3, (int)out.data.exchange_choice.keep[1]);
}

CUI_TEST(decode_relay_timeout)
{
    uint8_t payload[] = { 0, 0, COUP_RELAY_TIMEOUT, 0 };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 4, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_TIMEOUT, (int)out.type);
}

/* ======================================================================
 * Seq extraction
 * ====================================================================== */

CUI_TEST(decode_relay_seq_big_endian)
{
    /* seq = 0x0102 = 258 */
    uint8_t payload[] = { 0x01, 0x02, COUP_RELAY_START_GAME, 0 };
    coup_input_t out;
    uint16_t seq = 0;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 4, &out, &seq);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ(258, (int)seq);
}

CUI_TEST(decode_relay_seq_null_ok)
{
    /* Passing NULL for out_seq should not crash */
    uint8_t payload[] = { 0, 5, COUP_RELAY_START_GAME, 0 };
    coup_input_t out;
    memset(&out, 0xFF, sizeof(out));

    int rc = coup_decode_input_relay(payload, 4, &out, NULL);
    CUI_ASSERT_EQ(0, rc);
    CUI_ASSERT_EQ((int)COUP_INPUT_START_GAME, (int)out.type);
}

/* ======================================================================
 * Malformed / Truncated Payloads
 * ====================================================================== */

CUI_TEST(decode_relay_empty_payload_fails)
{
    uint8_t payload[] = { 0 };
    coup_input_t out;

    /* remaining=0: not even seq bytes */
    int rc = coup_decode_input_relay(payload, 0, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_too_short_fails)
{
    uint8_t payload[] = { 0, 0, COUP_RELAY_ACTION };
    coup_input_t out;

    /* remaining=3: has seq+type but no pid */
    int rc = coup_decode_input_relay(payload, 3, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_action_truncated_fails)
{
    /* ACTION needs 6 bytes: seq_hi, seq_lo, type, pid, action, target */
    uint8_t payload[] = { 0, 0, COUP_RELAY_ACTION, 0, COUP_RULES_ACT_STEAL };
    coup_input_t out;

    int rc = coup_decode_input_relay(payload, 5, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_response_truncated_fails)
{
    /* RESPONSE needs 5 bytes: seq_hi, seq_lo, type, pid, response */
    uint8_t payload[] = { 0, 0, COUP_RELAY_RESPONSE, 0 };
    coup_input_t out;

    int rc = coup_decode_input_relay(payload, 4, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_exchange_truncated_fails)
{
    /* EXCHANGE_CHOICE needs 6 bytes: seq_hi, seq_lo, type, pid, keep0, keep1 */
    uint8_t payload[] = { 0, 0, COUP_RELAY_EXCHANGE_CHOICE, 0, 1 };
    coup_input_t out;

    int rc = coup_decode_input_relay(payload, 5, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_unknown_type_fails)
{
    uint8_t payload[] = { 0, 0, 0xFF, 0 };
    coup_input_t out;

    int rc = coup_decode_input_relay(payload, 4, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}

CUI_TEST(decode_relay_high_unknown_type_fails)
{
    uint8_t payload[] = { 0, 0, 99, 0 };
    coup_input_t out;

    int rc = coup_decode_input_relay(payload, 4, &out, NULL);
    CUI_ASSERT_EQ(-1, rc);
}
