/**
 * coup_audio.c - Audio System for Coup Card Game
 *
 * Music: CD-DA (Red Book Audio) played directly from disc.
 *   Full quality (44100 Hz stereo), no RAM usage, hardware-looped
 *   by the CD Block.  Track 2 on the disc = first audio track.
 *
 * SFX: Direct SCSP register writes for one-shot PCM playback.
 *   8 sound effects embedded as signed 16-bit mono PCM arrays at
 *   11025 Hz.  Uploaded to Sound RAM at init, triggered by
 *   configuring SCSP slot registers directly from the SH-2.
 *
 * Architecture:
 *   The M68K sound driver (SDDRVS.DAT, loaded via slInitSound)
 *   manages CD-DA routing through SCSP.  SFX bypass the driver
 *   entirely — they use SCSP slots 28-31 (high slots the driver
 *   doesn't touch) with direct register writes for maximum audio
 *   fidelity.  The previous slPCMOn approach went through the M68K
 *   driver which degraded SFX quality.
 *
 *   KYONEX safety: before every SFX key-on, we clear KYONB on all
 *   32 SCSP slots, then set KYONB only on the target SFX slot.
 *   This prevents stale key-on bits from accidentally re-triggering
 *   other slots (including any the M68K driver may have configured).
 */

#include "coup.h"

/*============================================================================
 * State Variables
 *============================================================================*/

static bool music_playing = false;
static int  sfx_timer     = 0;
static bool audio_ready   = false;

/*============================================================================
 * Saturn-Specific Implementation
 *============================================================================*/

#ifdef __SATURN__

#include <stdint.h>
#include "coup_sfx_data.h"
#include "../../pal/saturn/sgl_defs.h"

/* M68K sound driver binary (SGL 3.02j) — needed for CD-DA routing */
#include "sddrvs.dat"

/* CD-DA track number for music.
 * Track 1 = data track (ISO9660 filesystem).
 * Track 2 = first audio track (rebellion.mp3 converted to WAV). */
#define CDDA_MUSIC_TRACK  2

/*============================================================================
 * SCSP Direct Register Access (for SFX)
 *============================================================================*/

#define SCSP_SOUND_RAM_BASE  0x25A00000UL
#define SCSP_REG_BASE        0x25B00000UL

/* SFX waveforms in Sound RAM — placed at high offset to avoid the
 * M68K driver's program and data area (typically 0x00000-0x10000). */
#define SFX_BASE_OFFSET      0x6C000UL

/* SCSP slot register offsets */
#define SCSP_OFF_SA_CTRL     0x00
#define SCSP_OFF_SA_LO       0x02
#define SCSP_OFF_LSA         0x04
#define SCSP_OFF_LEA         0x06
#define SCSP_OFF_ENV1        0x08
#define SCSP_OFF_ENV2        0x0A
#define SCSP_OFF_TL          0x0C
#define SCSP_OFF_MOD         0x0E
#define SCSP_OFF_PITCH       0x10
#define SCSP_OFF_LFO         0x12
#define SCSP_OFF_ISEL        0x14
#define SCSP_OFF_MIXLVL      0x16

/* SCSP control bits */
#define SCSP_KYONEX          (1u << 12)
#define SCSP_KYONB           (1u << 11)
#define SCSP_NUM_SLOTS       32

/* SFX uses 4 high SCSP slots that the M68K driver doesn't manage */
#define SFX_SLOT_BASE        28
#define SFX_SLOT_COUNT       4
#define SFX_COUNT            8

static volatile uint16_t* scsp_slot_reg(int slot, int offset)
{
    return (volatile uint16_t*)(SCSP_REG_BASE
                                + (uint32_t)slot * 0x20UL
                                + (uint32_t)offset);
}

static void scsp_write(int slot, int offset, uint16_t val)
{
    *scsp_slot_reg(slot, offset) = val;
}

/*============================================================================
 * SCSP Pitch Encoding
 *
 * SCSP pitch register (+0x10):
 *   bits [14:11] = OCT (signed 4-bit, -8..+7)
 *   bits  [9:0]  = FNS (10-bit fine tuning)
 *
 * Effective rate = 44100 * 2^OCT * (1 + FNS/1024)
 *
 * For 11025 Hz: 11025/44100 = 0.25 = 2^(-2), so OCT=-2, FNS=0
 *============================================================================*/

static uint16_t scsp_pitch_word(int oct, int fns)
{
    return (uint16_t)(((oct & 0x0F) << 11) | (fns & 0x3FF));
}

#define SFX_OCT  (-2)
#define SFX_FNS  0

/*============================================================================
 * Volume Mapping
 *============================================================================*/

/* User volume 0-10 → SCSP TL (0=loudest, 0xFF=mute) */
static const uint8_t vol_to_tl[11] = {
    0xFF,  /*  0: mute   */
    0xD0,  /*  1         */
    0xA8,  /*  2         */
    0x88,  /*  3         */
    0x68,  /*  4         */
    0x50,  /*  5         */
    0x38,  /*  6         */
    0x28,  /*  7         */
    0x10,  /*  8: default */
    0x08,  /*  9         */
    0x00,  /* 10: max    */
};

/* CD-DA volume (0-127, maps from user 0-10 scale) */
static Uint8 cdda_vol = 127;

/* User volume scale (0-10) mapped to SGL volume (0-127) */
static const Uint8 vol_to_sgl[11] = {
    0,    /*  0: mute   */
    13,   /*  1         */
    26,   /*  2         */
    39,   /*  3         */
    51,   /*  4         */
    64,   /*  5         */
    77,   /*  6         */
    89,   /*  7         */
    102,  /*  8: default */
    115,  /*  9         */
    127,  /* 10: max    */
};

/*============================================================================
 * SFX State
 *============================================================================*/

static uint32_t sfx_offsets[SFX_COUNT];  /* Sound RAM byte offsets */
static uint8_t  sfx_tl = 0x10;          /* SFX volume TL (default=8/10) */
static int      sfx_next_slot = 0;      /* Round-robin slot index 0..3 */

/*============================================================================
 * KYONEX Safety
 *
 * KYONEX is global: writing it to ANY slot fires key-on/off for ALL
 * 32 slots based on their KYONB bits.  Before triggering SFX, we
 * clear KYONB on every slot to prevent stale key-ons, then set
 * KYONB only on our target slot.
 *============================================================================*/

static void scsp_kyonex_barrier(void)
{
    int i;
    for (i = 0; i < 128; i++) {
        (void)*scsp_slot_reg(0, SCSP_OFF_TL);
    }
}

static void scsp_safe_keyon(int slot, uint16_t sa_ctrl_base)
{
    int i;

    /* Clear KYONB on all 32 slots */
    for (i = 0; i < SCSP_NUM_SLOTS; i++) {
        if (i != slot) {
            /* Read-modify would be ideal but SCSP SA_CTRL is complex.
             * Writing 0 to SA_CTRL clears KYONB without affecting
             * playback of already-started slots (KYONEX hasn't fired). */
            scsp_write(i, SCSP_OFF_SA_CTRL, 0x0000);
        }
    }

    scsp_kyonex_barrier();

    /* Key-on our target slot */
    scsp_write(slot, SCSP_OFF_SA_CTRL,
               SCSP_KYONEX | SCSP_KYONB | sa_ctrl_base);
}

/*============================================================================
 * SFX Upload (one-time at init)
 *============================================================================*/

static void sfx_upload_to_sound_ram(void)
{
    uint32_t off;
    int i;

    /* Compute byte offsets for each SFX in Sound RAM */
    off = SFX_BASE_OFFSET;
    for (i = 0; i < SFX_COUNT; i++) {
        sfx_offsets[i] = off;
        off += (uint32_t)sfx_pcm_counts[i] * 2;
    }

    /* Copy embedded PCM arrays into Sound RAM.
     * SH-2 is big-endian, SCSP expects big-endian — direct copy. */
    for (i = 0; i < SFX_COUNT; i++) {
        volatile int16_t* dst = (volatile int16_t*)(SCSP_SOUND_RAM_BASE
                                                     + sfx_offsets[i]);
        const int16_t* src = sfx_pcm_ptrs[i];
        uint16_t count = sfx_pcm_counts[i];
        uint16_t j;
        for (j = 0; j < count; j++) {
            dst[j] = src[j];
        }
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

void coup_audio_init(void)
{
    /* Load the M68K sound driver — needed for CD-DA routing via slCDDAOn.
     * The driver manages its own SCSP slots (low numbers).  Our SFX
     * use slots 28-31 which the driver doesn't touch. */
    {
        const char map[] = {0xFF, 0xFF, 0xFF, 0xFF};
        slInitSound((unsigned char *)sddrvstsk, sizeof(sddrvstsk),
                    (unsigned char *)map, sizeof(map));
    }

    /* Disable the SH-2/68K handshake protocol */
    *(volatile unsigned char *)(0x25A004E1) = 0x00;

    /* Initialize the CD Block */
    CDC_CdInit(0x00, 0x00, 0x05, 0x0F);

    /* Enable CD-DA audio output through SCSP */
    slCDDAOn(cdda_vol, cdda_vol, 0, 0);

    /* Upload SFX PCM data to Sound RAM (one-time) */
    sfx_upload_to_sound_ram();

    music_playing = false;
    sfx_timer     = 0;
    audio_ready   = true;
}

void coup_audio_tick(void)
{
    if (!audio_ready) return;

    if (sfx_timer > 0) {
        sfx_timer--;
    }
}

void coup_audio_play_sfx(int sfx_id)
{
    int slot;
    uint32_t sa;
    uint16_t sa_hi, sa_lo, sa_ctrl;

    if (!audio_ready) return;
    if (sfx_id < 0 || sfx_id >= SFX_COUNT) return;

    /* Pick the next SCSP slot (round-robin across slots 28-31) */
    slot = SFX_SLOT_BASE + sfx_next_slot;
    sfx_next_slot = (sfx_next_slot + 1) % SFX_SLOT_COUNT;

    /* Compute Sound RAM address fields */
    sa = sfx_offsets[sfx_id];
    sa_hi = (uint16_t)((sa >> 16) & 0x000F);
    sa_lo = (uint16_t)(sa & 0xFFFF);

    /* LPCTL=00 (one-shot, no loop) + SA high bits */
    sa_ctrl = sa_hi;

    /* Configure SCSP slot registers for one-shot PCM playback */
    scsp_write(slot, SCSP_OFF_SA_LO, sa_lo);
    scsp_write(slot, SCSP_OFF_LSA, 0);
    scsp_write(slot, SCSP_OFF_LEA, (uint16_t)(sfx_pcm_counts[sfx_id] - 1));
    scsp_write(slot, SCSP_OFF_ENV1, 0x001F);   /* Instant attack (AR=31) */
    scsp_write(slot, SCSP_OFF_ENV2, 0x001F);   /* Fast release (RR=31) */
    scsp_write(slot, SCSP_OFF_TL, (uint16_t)(sfx_tl & 0xFF));
    scsp_write(slot, SCSP_OFF_MOD, 0x0000);
    scsp_write(slot, SCSP_OFF_PITCH, scsp_pitch_word(SFX_OCT, SFX_FNS));
    scsp_write(slot, SCSP_OFF_LFO, 0x0000);
    scsp_write(slot, SCSP_OFF_ISEL, 0x0000);
    /* DISDL=7 (max direct send), DIPAN=0 (center) */
    scsp_write(slot, SCSP_OFF_MIXLVL, (uint16_t)(7u << 13));

    /* Key-on with KYONEX isolation (clears all other KYONB bits first) */
    scsp_safe_keyon(slot, sa_ctrl);

    /* Timer for API tracking (frames at 60fps) */
    sfx_timer = ((uint32_t)sfx_pcm_counts[sfx_id] * 60u + 11024u) / 11025u + 2;
}

void coup_audio_start_music(void)
{
    CdcPly ply;

    if (!audio_ready) return;

    /* Play track 2 (first audio track) in endless loop.
     * The CD Block handles looping entirely in hardware. */
    CDC_PLY_STYPE(&ply) = CDC_PTYPE_TNO;
    CDC_PLY_STNO(&ply)  = CDDA_MUSIC_TRACK;
    CDC_PLY_SIDX(&ply)  = 1;

    CDC_PLY_ETYPE(&ply) = CDC_PTYPE_TNO;
    CDC_PLY_ETNO(&ply)  = CDDA_MUSIC_TRACK;
    CDC_PLY_EIDX(&ply)  = 1;

    CDC_PLY_PMODE(&ply) = CDC_PM_ENDLESS;

    CDC_CdPlay(&ply);
    music_playing = true;
}

void coup_audio_stop_music(void)
{
    CdcPos pos;

    if (!audio_ready) return;

    CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;
    CDC_CdSeek(&pos);

    music_playing = false;
}

void coup_audio_set_music_volume(int vol)
{
    if (!audio_ready) return;
    if (vol < 0) vol = 0;
    if (vol > 10) vol = 10;

    cdda_vol = vol_to_sgl[vol];
    slCDDAOn(cdda_vol, cdda_vol, 0, 0);
}

void coup_audio_set_sfx_volume(int vol)
{
    if (!audio_ready) return;
    if (vol < 0) vol = 0;
    if (vol > 10) vol = 10;

    sfx_tl = vol_to_tl[vol];
}

/* Debug menu stubs — called from main_saturn.c */
void coup_audio_debug_update(uint16_t pad_raw)
{
    (void)pad_raw;
}

void coup_audio_debug_render(void)
{
}

#else /* !__SATURN__ */

/*============================================================================
 * Non-Saturn Stubs
 *============================================================================*/

void coup_audio_init(void)
{
    audio_ready = true;
}

void coup_audio_tick(void)
{
    if (sfx_timer > 0) sfx_timer--;
}

void coup_audio_play_sfx(int sfx_id)
{
    (void)sfx_id;
}

void coup_audio_start_music(void)
{
    music_playing = true;
}

void coup_audio_stop_music(void)
{
    music_playing = false;
}

void coup_audio_set_music_volume(int vol)
{
    (void)vol;
}

void coup_audio_set_sfx_volume(int vol)
{
    (void)vol;
}

void coup_audio_debug_update(uint16_t pad_raw)
{
    (void)pad_raw;
}

void coup_audio_debug_render(void)
{
}

#endif /* __SATURN__ */
