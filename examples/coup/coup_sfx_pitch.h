/**
 * coup_sfx_pitch.h - SCSP pitch encoding for per-character sound effects.
 *
 * Why this is a header and not part of coup_audio.c:
 *
 *   The design calls for eight per-character cues across five characters.
 *   Forty sounds do not fit in the 81,920 bytes Sound RAM leaves for SFX -
 *   at the shipped encoding they would need roughly seven times that.  They
 *   do not need to fit: the SCSP retunes a slot in hardware through its
 *   OCT/FNS pitch register, so ONE sample serves all five characters at five
 *   pitches, and the player hears WHO is acting without reading the screen.
 *
 *   That makes this arithmetic the load-bearing part of the whole scheme,
 *   and getting it wrong means a sound played at the wrong speed on a
 *   console with no debugger.  Putting it in a header - pure integer maths,
 *   no hardware access, no Saturn headers - is what lets tests/coup check it
 *   on the host without linking the SCSP register writes.
 *
 * The hardware contract (SCSP pitch register, slot offset +0x10):
 *
 *   bits [14:11] = OCT, signed 4-bit, -8..+7
 *   bits  [9:0]  = FNS, 10-bit fractional tuning
 *   playback rate = 44100 * 2^OCT * (1 + FNS/1024)
 *
 * So an 11025 Hz sample played at its own pitch is 11025/44100 = 2^-2
 * exactly: OCT = -2, FNS = 0.  A 5512 Hz sample is one octave below that.
 */

#ifndef COUP_SFX_PITCH_H
#define COUP_SFX_PITCH_H

#include <stdint.h>

/* The SCSP's own playback clock. Everything else is a ratio against it. */
#define COUP_SCSP_BASE_RATE  44100u

/* Q10 fixed point: 1024 means "play the sample at the rate it was authored". */
#define COUP_SCSP_PITCH_UNITY 1024u

/**
 * Pitch register word for a sample authored at `sample_rate_hz`, transposed
 * to `character`'s voice.
 *
 * `character` is a COUP_CHAR_* value.  Duke is lowest and Contessa highest,
 * which falls out of the COUP_CHAR_* order for free.  Anything outside
 * 0..4 - COUP_CHAR_FACEDOWN, COUP_CHAR_NONE, a bot with no claimed card -
 * plays at the authored pitch, so callers never have to special-case it.
 */
static inline uint16_t coup_sfx_pitch_word(int sample_rate_hz, int character)
{
    /* Per-character playback ratios, Q10.
     *
     * Four semitones apart, spanning a major third either side of centre:
     * wide enough that the five voices are told apart through a TV speaker,
     * narrow enough that a card slide retuned to the Duke still sounds like
     * a card and not like a door.
     *
     *   Duke        2^(-4/12) * 1024 =  812.7
     *   Assassin    2^(-2/12) * 1024 =  912.4
     *   Captain     2^( 0/12) * 1024 = 1024.0
     *   Ambassador  2^( 2/12) * 1024 = 1149.4
     *   Contessa    2^( 4/12) * 1024 = 1290.2
     */
    static const uint16_t ratio_q10[5] = {
         813,  /* COUP_CHAR_DUKE       */
         912,  /* COUP_CHAR_ASSASSIN   */
        1024,  /* COUP_CHAR_CAPTAIN    */
        1149,  /* COUP_CHAR_AMBASSADOR */
        1290,  /* COUP_CHAR_CONTESSA   */
    };

    uint32_t ratio;                             /* rate / 44100, Q16 */
    uint32_t r_q10 = COUP_SCSP_PITCH_UNITY;
    int      oct   = 0;
    uint32_t fns;

    if (sample_rate_hz <= 0) {
        return 0;
    }
    if (character >= 0 && character < 5) {
        r_q10 = ratio_q10[character];
    }

    /* (rate * ratio / 44100) in Q16; the * 64 promotes Q10 to Q16.
     *
     * Worst case operand product is 11025 * 1290 * 64 = 910,224,000, inside
     * a uint32 with room to spare - this runs on an SH-2, so no 64-bit
     * arithmetic and no floating point. */
    ratio = ((uint32_t)sample_rate_hz * r_q10 * 64u) / COUP_SCSP_BASE_RATE;
    if (ratio == 0u) {
        ratio = 1u;
    }

    /* Normalise into [1.0, 2.0) in Q16. The exponent that takes is OCT. */
    while (ratio >= (2u << 16)) {
        ratio >>= 1;
        oct++;
    }
    while (ratio < (1u << 16)) {
        ratio <<= 1;
        oct--;
    }

    /* OCT is a signed 4-bit field. Clamp rather than let it wrap: a wrapped
     * OCT plays the sample 256x too fast, which is a click on a one-shot
     * slot and a scream on a looped one. */
    if (oct < -8) oct = -8;
    if (oct >  7) oct =  7;

    fns = (ratio - (1u << 16)) >> 6;    /* Q16 fraction -> 10-bit FNS */
    if (fns > 0x3FFu) {
        fns = 0x3FFu;
    }

    return (uint16_t)(((uint32_t)(oct & 0x0F) << 11) | fns);
}

/** OCT field of a pitch word, sign-extended from its 4 bits. */
static inline int coup_sfx_pitch_oct(uint16_t word)
{
    int oct = (int)((word >> 11) & 0x0Fu);
    return (oct >= 8) ? oct - 16 : oct;
}

/** FNS field of a pitch word. */
static inline int coup_sfx_pitch_fns(uint16_t word)
{
    return (int)(word & 0x3FFu);
}

#endif /* COUP_SFX_PITCH_H */
