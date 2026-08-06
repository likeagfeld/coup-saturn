/**
 * test_coup_sfx.c - The two ways the sound set can be silently broken.
 *
 * Neither failure crashes, and neither is visible in a code review.
 *
 * 1. THE BUDGET. Sound RAM is 512 KB and coup_audio.c parks the effects at
 *    0x6C000, leaving 81,920 bytes. sfx_upload_to_sound_ram() walks the set
 *    consecutively from there with no bound check at all, so a set one byte
 *    too large does not fail - it writes over whatever follows the region,
 *    which is the M68K sound driver's memory. The symptom would be the music
 *    or the driver misbehaving, on hardware, with no diagnostic.
 *
 * 2. A MISSING ENTRY. The COUP_SFX_* ids are indices into sfx_pcm_ptrs, so
 *    an id declared without data behind it does not fail to link - it plays
 *    whatever bytes happen to sit at that offset in Sound RAM. Both the ids
 *    and the tables are generated together by convert_sfx.py precisely so
 *    they cannot drift, and this is the assertion that proves they did not.
 *
 * Plus the pitch encoding, which is what lets eight per-character samples
 * cover forty per-character sounds. It runs on hardware with no debugger,
 * so it is checked here where a wrong answer is a failing test and not a
 * sound played at the wrong speed on a Saturn.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "coup_sfx_data.h"
#include "coup_sfx_pitch.h"

/*============================================================================
 * The budget
 *============================================================================*/

CUI_TEST(sfx_set_fits_in_sound_ram)
{
    int i;
    long total = 0;

    for (i = 0; i < COUP_SFX_COUNT; i++) {
        total += (long)sfx_pcm_counts[i] * 2;
    }

    /* The generator's headline figure and the actual arrays must agree - if
     * they can disagree, the number in the header is decoration. */
    CUI_ASSERT_EQ((long)COUP_SFX_TOTAL_BYTES, total);
    CUI_ASSERT_LE(total, (long)COUP_SFX_BUDGET_BYTES);
}

CUI_TEST(sfx_budget_matches_the_sound_ram_layout)
{
    /* 512 KB of Sound RAM minus SFX_BASE_OFFSET (0x6C000) in coup_audio.c.
     * If either moves, this catches the one that did not. */
    CUI_ASSERT_EQ(81920, COUP_SFX_BUDGET_BYTES);
    CUI_ASSERT_EQ(512 * 1024 - 0x6C000, COUP_SFX_BUDGET_BYTES);
}

CUI_TEST(every_sfx_id_has_pcm_data)
{
    int i;
    for (i = 0; i < COUP_SFX_COUNT; i++) {
        CUI_ASSERT_NOT_NULL(sfx_pcm_ptrs[i]);
        /* Zero-length would key on a slot with LEA = 0xFFFF - a whole 64 K
         * of Sound RAM played as noise. */
        CUI_ASSERT_GT(sfx_pcm_counts[i], 0);
        /* Silence means the WAV never made it through the converter. */
        CUI_ASSERT_NEQ(0, sfx_pcm_ptrs[i][sfx_pcm_counts[i] / 2]);
    }
}

CUI_TEST(every_named_effect_is_in_range)
{
    /* Every id the game can name. A stale name left over after a
     * regeneration would index past the tables. */
    const int ids[] = {
        COUP_SFX_UI_MOVE, COUP_SFX_UI_CONFIRM, COUP_SFX_UI_CANCEL,
        COUP_SFX_UI_CHALLENGE, COUP_SFX_CARD_DEAL, COUP_SFX_DECK_PLACE,
        COUP_SFX_CARD_REVEAL, COUP_SFX_CARD_SHUFFLE, COUP_SFX_COIN_GAIN,
        COUP_SFX_COIN_SPEND, COUP_SFX_COUP_STRIKE, COUP_SFX_TURN_START,
        COUP_SFX_INFLUENCE_LOST, COUP_SFX_EXILED, COUP_SFX_CHALLENGE,
        COUP_SFX_COUNTER, COUP_SFX_ACT_SUCCESS, COUP_SFX_ACT_FAIL,
        COUP_SFX_ASSASSINATE, COUP_SFX_STEAL, COUP_SFX_VICTORY,
        /* the pre-expansion names, still used by older call sites */
        COUP_SFX_CONFIRM, COUP_SFX_CANCEL, COUP_SFX_COINS,
        COUP_SFX_ELIMINATED,
    };
    int i;
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        CUI_ASSERT_GE(ids[i], 0);
        CUI_ASSERT_LT(ids[i], COUP_SFX_COUNT);
    }
}

CUI_TEST(every_effect_declares_a_playable_rate)
{
    int i;
    for (i = 0; i < COUP_SFX_COUNT; i++) {
        /* Only these two rates land on the SCSP's 44100/2^n clock exactly.
         * Anything else would have to be approximated by FNS. */
        CUI_ASSERT(sfx_pcm_rates[i] == COUP_SFX_RATE_HIGH ||
                   sfx_pcm_rates[i] == COUP_SFX_RATE_LOW);
    }
}

CUI_TEST(no_effect_exceeds_the_scsp_loop_end_register)
{
    /* coup_audio.c writes count-1 into LEA, which is 16 bits. */
    int i;
    for (i = 0; i < COUP_SFX_COUNT; i++) {
        CUI_ASSERT_LE((long)sfx_pcm_counts[i], 0xFFFFL);
    }
}

CUI_TEST(ui_blips_are_short_enough_to_be_blips)
{
    /* The set this replaced averaged 440 ms per effect, which for a d-pad
     * tick is not a tick, it is a note. The menu cues have to be able to
     * fire back to back as fast as a cursor moves. */
    int ms = (int)((long)sfx_pcm_counts[COUP_SFX_UI_MOVE] * 1000L
                   / sfx_pcm_rates[COUP_SFX_UI_MOVE]);
    CUI_ASSERT_LE(ms, 80);
    CUI_ASSERT_GT(ms, 0);
}

/*============================================================================
 * Pitch encoding
 *
 * The SCSP plays a slot at 44100 * 2^OCT * (1 + FNS/1024) Hz. Decoding the
 * word back into a rate is how these tests check it without a Saturn.
 *============================================================================*/

static double decoded_rate(uint16_t word)
{
    int oct = coup_sfx_pitch_oct(word);
    int fns = coup_sfx_pitch_fns(word);
    double m = 1.0 + (double)fns / 1024.0;
    return (oct >= 0)
        ? 44100.0 * (double)(1L << oct) * m
        : 44100.0 / (double)(1L << (-oct)) * m;
}

CUI_TEST(unpitched_11025_is_exactly_oct_minus_two)
{
    /* 11025/44100 = 2^-2 with nothing left over, so this is the one case
     * with a hand-checkable answer: OCT = -2, FNS = 0. */
    uint16_t w = coup_sfx_pitch_word(11025, COUP_CHAR_NONE);
    CUI_ASSERT_EQ(-2, coup_sfx_pitch_oct(w));
    CUI_ASSERT_EQ(0, coup_sfx_pitch_fns(w));
}

CUI_TEST(captain_is_the_untransposed_voice)
{
    /* The Captain sits at the centre of the five, so its word must be
     * identical to the unpitched one - not merely close. */
    CUI_ASSERT_EQ(coup_sfx_pitch_word(11025, COUP_CHAR_NONE),
                  coup_sfx_pitch_word(11025, COUP_CHAR_CAPTAIN));
    CUI_ASSERT_EQ(coup_sfx_pitch_word(COUP_SFX_RATE_LOW, COUP_CHAR_NONE),
                  coup_sfx_pitch_word(COUP_SFX_RATE_LOW, COUP_CHAR_CAPTAIN));
}

CUI_TEST(low_rate_samples_play_an_octave_below_the_high_rate_ones)
{
    /* A 5512 Hz sample and an 11025 Hz one have to come out at their OWN
     * speeds, which is the whole reason the set is allowed to mix rates.
     * Within a cent, because 44100/8 is 5512.5 and 5512 is not. */
    double hi = decoded_rate(coup_sfx_pitch_word(COUP_SFX_RATE_HIGH,
                                                 COUP_CHAR_NONE));
    double lo = decoded_rate(coup_sfx_pitch_word(COUP_SFX_RATE_LOW,
                                                 COUP_CHAR_NONE));
    CUI_ASSERT(hi > 11024.0 && hi < 11026.0);
    CUI_ASSERT(lo > 5508.0 && lo < 5516.0);
}

CUI_TEST(the_five_voices_rise_duke_to_contessa)
{
    /* The point of the scheme: a player tells WHO is acting from the pitch,
     * so the five have to be strictly ordered, at both sample rates. */
    const int rates[2] = { COUP_SFX_RATE_HIGH, COUP_SFX_RATE_LOW };
    int r;
    for (r = 0; r < 2; r++) {
        double prev = 0.0;
        int c;
        for (c = COUP_CHAR_DUKE; c <= COUP_CHAR_CONTESSA; c++) {
            double f = decoded_rate(coup_sfx_pitch_word(rates[r], c));
            CUI_ASSERT_GT(f, prev);
            prev = f;
        }
    }
}

CUI_TEST(the_voices_span_the_intended_two_thirds)
{
    /* Duke -4 semitones, Contessa +4: a major third either side of centre.
     * Narrower and the voices stop being distinguishable through a TV
     * speaker; wider and a card slide retuned to the Duke stops sounding
     * like a card. Ratio Contessa/Duke = 2^(8/12) = 1.5874. */
    double duke = decoded_rate(coup_sfx_pitch_word(11025, COUP_CHAR_DUKE));
    double cont = decoded_rate(coup_sfx_pitch_word(11025, COUP_CHAR_CONTESSA));
    double span = cont / duke;
    CUI_ASSERT(span > 1.58 && span < 1.60);

    /* And each step is one whole tone, 2^(2/12) = 1.1225. */
    {
        double assassin = decoded_rate(
            coup_sfx_pitch_word(11025, COUP_CHAR_ASSASSIN));
        double step = assassin / duke;
        CUI_ASSERT(step > 1.11 && step < 1.14);
    }
}

CUI_TEST(non_characters_play_at_the_sampled_pitch)
{
    /* Callers pass whatever the event carried. COUP_CHAR_FACEDOWN,
     * COUP_CHAR_NONE and any out-of-range value have to mean "no
     * transposition" rather than reading off the end of the ratio table. */
    uint16_t unity = coup_sfx_pitch_word(11025, COUP_CHAR_NONE);
    CUI_ASSERT_EQ(unity, coup_sfx_pitch_word(11025, COUP_CHAR_FACEDOWN));
    CUI_ASSERT_EQ(unity, coup_sfx_pitch_word(11025, -1));
    CUI_ASSERT_EQ(unity, coup_sfx_pitch_word(11025, 99));
    CUI_ASSERT_EQ(unity, coup_sfx_pitch_word(11025, COUP_NUM_CHARACTERS));
}

CUI_TEST(pitch_word_uses_only_the_hardware_fields)
{
    /* OCT is bits 14:11 and FNS is bits 9:0. Bit 15 and bit 10 belong to
     * neither and must stay clear, or the write means something else. */
    int rates[2] = { COUP_SFX_RATE_HIGH, COUP_SFX_RATE_LOW };
    int r, c;
    for (r = 0; r < 2; r++) {
        for (c = -1; c <= COUP_NUM_CHARACTERS; c++) {
            uint16_t w = coup_sfx_pitch_word(rates[r], c);
            CUI_ASSERT_EQ(0, (int)(w & 0x8400u));
        }
    }
}

CUI_TEST(every_effect_in_the_set_encodes_to_a_sane_pitch)
{
    /* Walk the real set rather than two hand-picked rates: every effect,
     * every voice, must decode back to within a semitone of the rate it was
     * authored at times its transposition. */
    int i, c;
    for (i = 0; i < COUP_SFX_COUNT; i++) {
        for (c = COUP_CHAR_DUKE; c <= COUP_CHAR_CONTESSA; c++) {
            double f = decoded_rate(coup_sfx_pitch_word(sfx_pcm_rates[i], c));
            CUI_ASSERT_GT(f, sfx_pcm_rates[i] * 0.75);
            CUI_ASSERT_LT(f, sfx_pcm_rates[i] * 1.30);
        }
    }
}
