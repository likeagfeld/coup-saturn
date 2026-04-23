/**
 * sgl_defs.h - SGL type definitions and function declarations
 *
 * Minimal SGL declarations for bare-metal Saturn programming.
 * Provides types and functions needed by the Saturn PAL without
 * requiring the full SGL headers (which have parsing issues with modern GCC).
 */

#ifndef SGL_DEFS_H
#define SGL_DEFS_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Basic SGL Types
 *============================================================================*/

typedef uint8_t     Uint8;
typedef int8_t      Sint8;
typedef uint16_t    Uint16;
typedef int16_t     Sint16;
typedef uint32_t    Uint32;
typedef int32_t     Sint32;

/* SGL fixed-point type (16.16 format) */
typedef Sint32 FIXED;
#define toFIXED(x) ((FIXED)(x) << 16)

/*============================================================================
 * TV Resolution Modes
 *============================================================================*/

enum tvsz {
    TV_320x224, TV_320x240, TV_320x256, TV_dummy1,
    TV_352x224, TV_352x240, TV_352x256, TV_dummy2,
    TV_640x224, TV_640x240, TV_640x256, TV_dummy3,
    TV_704x224, TV_704x240, TV_704x256, TV_dummy4,
    TV_320x448, TV_320x480, TV_320x512, TV_dummy5,
    TV_352x448, TV_352x480, TV_352x512, TV_dummy6,
    TV_640x448, TV_640x480, TV_640x512, TV_dummy7,
    TV_704x448, TV_704x480, TV_704x512, TV_dummy8
};

/*============================================================================
 * Texture Definition
 *============================================================================*/

typedef struct {
    Uint16 Hsize;
    Uint16 Vsize;
    Uint16 CGadr;
    Uint16 HVsize;
} TEXTURE;

/*============================================================================
 * Peripheral (Controller) Definitions
 *============================================================================*/

/* Button assignments for Saturn Pad (active-low: bit=0 means pressed) */
#define PER_DGT_KR  (1 << 15)   /* Direction key: right */
#define PER_DGT_KL  (1 << 14)   /* Direction key: left */
#define PER_DGT_KD  (1 << 13)   /* Direction key: down */
#define PER_DGT_KU  (1 << 12)   /* Direction key: up */
#define PER_DGT_ST  (1 << 11)   /* Start button */
#define PER_DGT_TA  (1 << 10)   /* Button A */
#define PER_DGT_TC  (1 << 9)    /* Button C */
#define PER_DGT_TB  (1 << 8)    /* Button B */
#define PER_DGT_TR  (1 << 7)    /* R trigger */
#define PER_DGT_TX  (1 << 6)    /* Button X */
#define PER_DGT_TY  (1 << 5)    /* Button Y */
#define PER_DGT_TZ  (1 << 4)    /* Button Z */
#define PER_DGT_TL  (1 << 3)    /* L trigger */

/* Peripheral data structure (from SGL SL_DEF.H:1404-1411) */
typedef struct {
    Uint8  id;           /* Peripheral ID */
    Uint8  ext;          /* Extension data size */
    Uint16 data;         /* Current button data (active-low) */
    Uint16 push;         /* Newly pressed buttons (edge detection) */
    Uint16 pull;         /* Newly released buttons (edge detection) */
    Uint32 dummy2[4];    /* Padding (matches SGL internal layout) */
} PerDigital;

/* Global peripheral data POINTER (provided by SGL) */
/* CRITICAL: This is a POINTER, not an array. SL_DEF.H:1499 declares:
 *   extern PerDigital* Smpc_Peripheral;
 * Using [] instead of * generates completely different machine code —
 * [] treats the symbol address AS the data, while * correctly follows
 * the pointer to the actual peripheral data. */
extern PerDigital* Smpc_Peripheral;

/* Connection status (provided by SGL) */
extern Uint8 Per_Connect1;

/*============================================================================
 * Core SGL Functions
 *============================================================================*/

/**
 * Initialize the SGL system
 * @param tv_mode - TV resolution (e.g., TV_320x224)
 * @param texture_table - Texture definition table (NULL for text-only)
 * @param framerate - Frame rate divisor (1 = 60fps, 2 = 30fps, etc.)
 */
extern void slInitSystem(Uint16 tv_mode, TEXTURE *texture_table, Sint8 framerate);

/**
 * Print text to screen
 * @param str - Null-terminated string
 * @param pos - Position from slLocate()
 */
extern void slPrint(char *str, void *pos);

/**
 * Get screen position for text
 * @param x - Column (0-39 for 320 width)
 * @param y - Row (0-27 for 224 height)
 * @return Position value for slPrint
 */
extern void *slLocate(Uint16 x, Uint16 y);

/**
 * Initialize V-BLANK synchronization and event processing
 * Must be called after slInitSystem() to enable peripheral reading
 */
extern void slInitSynch(void);

/**
 * Synchronize with vertical blank
 * Call once per frame for proper timing
 */
extern void slSynch(void);

/**
 * Turn display on
 */
extern void slTVOn(void);

/**
 * Turn display off
 */
extern void slTVOff(void);

/*============================================================================
 * VDP2 Color RAM Functions (Phase 1: Colored Text)
 *============================================================================*/

/**
 * Set Color RAM mode
 * @param mode - 0: 1024 colors RGB555, 1: 2048 colors RGB555, 2: 1024 colors RGB888
 */
extern void slColRAMMode(Uint16 mode);

/**
 * Write palette data to Color RAM
 * @param addr - CRAM destination address (palette offset)
 * @param data - Source data pointer (RGB555 words)
 * @param count - Number of colors to write
 * @return Pointer to next free CRAM address
 */
extern Uint16 *slSetColRAM(Uint32 addr, void *data, Uint32 count);

/*============================================================================
 * VDP2 Scroll Plane Functions (Phase 2: Rectangle Layer)
 *============================================================================*/

/**
 * Configure NBG0 character format
 * @param color_type - Color mode (COL_TYPE_16, COL_TYPE_256, etc.)
 * @param char_size  - Character size (CHAR_SIZE_1x1 or CHAR_SIZE_2x2)
 */
extern void slCharNbg0(Uint16 color_type, Uint16 char_size);

/**
 * Configure NBG1 character format
 */
extern void slCharNbg1(Uint16 color_type, Uint16 char_size);

/**
 * Set NBG0 page (pattern name data table)
 * @param cell_adr  Starting address of character patterns in VRAM
 * @param col_adr   Starting address of color palette (offset for 1-word mode, 0 = CRAM base)
 * @param data_type PNT data type (PNB_1WORD|CN_10BIT, PNB_1WORD|CN_12BIT, PNB_2WORD)
 */
extern void slPageNbg0(void *cell_adr, void *col_adr, Uint16 data_type);

/**
 * Set NBG1 page (pattern name data table)
 */
extern void slPageNbg1(void *cell_adr, void *col_adr, Uint16 data_type);

/**
 * Set scroll plane size (number of pages)
 * @param size - PL_SIZE_1x1, PL_SIZE_2x1, or PL_SIZE_2x2
 */
extern void slPlaneNbg0(Uint16 size);

/**
 * Set NBG0 scroll map page assignments
 * @param mapA - VRAM address of page A (top-left)
 * @param mapB - VRAM address of page B (top-right)
 * @param mapC - VRAM address of page C (bottom-left)
 * @param mapD - VRAM address of page D (bottom-right)
 */
extern void slMapNbg0(void *mapA, void *mapB, void *mapC, void *mapD);

/**
 * Set NBG0 scroll position
 * @param x - Horizontal scroll offset (16.16 fixed-point)
 * @param y - Vertical scroll offset (16.16 fixed-point)
 */
extern void slScrPosNbg0(FIXED x, FIXED y);

/**
 * Set priority for a screen layer (VDP2 planes and VDP1 sprites).
 * @param screen - SGL screen index (scnNBG0, scnNBG1, scnSPR0, etc.)
 * @param priority - Priority level (0-7)
 */
extern void slPriority(Sint16 screen, Uint16 priority);

/**
 * Enable/disable scroll planes
 * @param flags - Bitmask of NBGxON flags
 */
extern void slScrAutoDisp(Uint32 flags);

/*============================================================================
 * VDP2 Background Functions (Phase 3: Efficient Screen Clear)
 *============================================================================*/

/**
 * Set the back screen color
 * @param addr - Back color address in CRAM
 * @param color - RGB555 color value
 */
extern void slBack1ColSet(void *addr, Uint16 color);

/*============================================================================
 * VDP2 Color Calculation Functions (Phase 5: Transparency)
 *============================================================================*/

/**
 * Set color calculation mode
 */
extern void slColorCalc(Uint16 mode);

/**
 * Enable color calculation on specific targets
 */
extern void slColorCalcOn(Uint16 target);

/**
 * Set color calculation (transparency) rate for a screen
 * @param screen - SGL screen index (scnNBG0, scnNBG1, etc.)
 * @param rate - Transparency rate (0-31, 31 = opaque)
 */
extern void slColRate(Sint16 screen, Uint16 rate);

/**
 * Set color offset mode
 */
extern void slColorOffset(Uint16 mode);

/**
 * Set color offset values for offset A
 * @param r - Red offset (-256 to +255)
 * @param g - Green offset
 * @param b - Blue offset
 */
extern void slColorOffsetA(Sint16 r, Sint16 g, Sint16 b);

/**
 * Set color offset values for offset B
 */
extern void slColorOffsetB(Sint16 r, Sint16 g, Sint16 b);

/*============================================================================
 * VDP1 / Sprite Functions
 *============================================================================*/

extern void slSpriteType(Uint16 type);

/**
 * Set VDP2 sprite color mode (SPCLMD bit in SPCTL register).
 * CRITICAL for VDP1 RGB555 rendering — without this, VDP2 interprets
 * VDP1's RGB555 pixels as palette data, producing garbage.
 *
 * @param mode - SPR_PAL (0) = palette only, SPR_PAL_RGB (1) = palette + RGB
 */
extern void slSpriteColMode(Uint16 mode);

/* Sprite color mode constants (from SL_DEF.H:608-609) */
#define SPR_PAL       0   /* Palette code only */
#define SPR_PAL_RGB   1   /* Use Palette and RGB */

/*============================================================================
 * VDP2 Constants
 *============================================================================*/

/* Character sizes */
#define CHAR_SIZE_1x1   0
#define CHAR_SIZE_2x2   1

/* Scroll plane sizes (page layout) */
#define PL_SIZE_1x1     0   /* 1 page wide x 1 page tall */
#define PL_SIZE_2x1     1   /* 2 pages wide x 1 page tall */
#define PL_SIZE_2x2     3   /* 2 pages wide x 2 pages tall */

/* Color modes for character planes */
#define COL_TYPE_16     0   /* 16-color palette */
#define COL_TYPE_256    1   /* 256-color palette */
#define COL_TYPE_2048   2   /* 2048-color palette */
#define COL_TYPE_32768  3   /* 32768 direct color */
#define COL_TYPE_16M    4   /* 16M direct color */

/* Scroll plane enable flags */
#define NBG0ON          (1 << 0)
#define NBG1ON          (1 << 1)
#define NBG2ON          (1 << 2)
#define NBG3ON          (1 << 3)
#define RBG0ON          (1 << 4)
#define SPRON           (1 << 5)
#define BACKON          (1 << 6)

/* WARNING: These are SGL-internal dispatch indices, NOT VDP2 layer numbers.
 * The values are intentionally non-sequential. Do not "correct" them.
 * See SL_DEF.H lines 647-668 in the official SGL 3.02j SDK. */
#define scnNBG0         1
#define scnNBG1         0
#define scnNBG2         3
#define scnNBG3         2
#define scnRBG0         5

/* Sprite register indices (negative = SGL convention for sprite regs) */
#define scnSPR0         (-7)
#define scnSPR1         (-8)

/* Convenience macros (match real SGL 3.02j SL_DEF.H) */
#define slPriorityNbg0(num)   slPriority(scnNBG0, num)
#define slPriorityNbg1(num)   slPriority(scnNBG1, num)
#define slPrioritySpr0(num)   slPriority(scnSPR0, num)
#define slColRateNbg0(rate)   slColRate(scnNBG0, rate)
#define slColRateNbg1(rate)   slColRate(scnNBG1, rate)

/* Color calculation constants */
#define CC_RATE         (1 << 0)
#define CC_2ND          (1 << 1)
#define CC_NBG0         (1 << 4)
#define CC_NBG1         (1 << 5)

/* Color RAM modes */
#define CRAM_MODE_0     0   /* 1024 colors, RGB555 */
#define CRAM_MODE_1     1   /* 2048 colors, RGB555 */
#define CRAM_MODE_2     2   /* 1024 colors, RGB888 */

/* VDP2 VRAM base addresses */
#define VDP2_VRAM_A0    0x25E00000
#define VDP2_VRAM_A1    0x25E20000
#define VDP2_VRAM_B0    0x25E40000
#define VDP2_VRAM_B1    0x25E60000

/* VDP2 Color RAM base address */
#define VDP2_COLRAM     0x25F00000

/* Color RAM mode parameters (for slColRAMMode) */
#define CRM16_1024      0   /* Mode 0: 1024 colors, RGB555 */
#define CRM16_2048      1   /* Mode 1: 2048 colors, RGB555 (SGL default) */
#define CRM32_1024      2   /* Mode 2: 1024 colors, RGB888 */

/* Page/PNT configuration (for slPageNbg0 etc.) */
#define PNB_1WORD       0x8000
#define PNB_2WORD       0
#define CN_10BIT        0
#define CN_12BIT        0x4000

typedef int Bool;

/*============================================================================
 * Sound System (SGL M68K Sound Driver + CDC CD-DA)
 *============================================================================*/

/**
 * Initialize the SGL sound driver.
 * Loads the M68K sound driver binary into Sound RAM and starts the 68K.
 * MUST be called before slCDDAOn(), slPCMOn(), or any sound function.
 *
 * @param prg      - Sound driver program data (sddrvstsk from SDDRVS.DAT)
 * @param prg_size - Size of driver data in bytes
 * @param map      - Sound map data (tone bank definition)
 * @param map_size - Size of map data in bytes
 */
extern void slInitSound(unsigned char *prg, Uint32 prg_size,
                        unsigned char *map, Uint32 map_size);

/**
 * Enable CD-DA audio output through SCSP.
 * Routes CD Block digital audio to the DAC via the M68K sound driver.
 *
 * @param left_vol  - Left channel volume (0-127)
 * @param right_vol - Right channel volume (0-127)
 * @param left_pan  - Left channel pan (-128 to +127, 0=center)
 * @param right_pan - Right channel pan (-128 to +127, 0=center)
 */
extern Bool slCDDAOn(Uint8 left_vol, Uint8 right_vol,
                     Sint8 left_pan, Sint8 right_pan);

/** Disable CD-DA audio output. */
extern Bool slCDDAOff(void);

/** Set master sound volume (0-127). */
extern Bool slSndVolume(Uint8 vol);

/** Flush pending sound commands to the M68K driver. */
extern void slSndFlush(void);

/*============================================================================
 * PCM Sound Effects (via M68K Sound Driver)
 *============================================================================*/

/** PCM playback descriptor (matches SGL SL_DEF.H layout exactly). */
typedef struct {
    Uint8   mode;       /* _Mono/_Stereo | _PCM16Bit/_PCM8Bit */
    Uint8   channel;    /* PCM channel number (0-5 for 6 channels) */
    Uint8   level;      /* Volume: 0-127 */
    Sint8   pan;        /* Pan: -128 (left) to +127 (right), 0=center */
    Uint16  pitch;      /* Sample rate in Hz (e.g., 11025) */
    Uint8   eflevelR;   /* Effect level for right/mono: 0-7 */
    Uint8   efselectR;  /* Effect select for right/mono: 0-15 */
    Uint8   eflevelL;   /* Effect level for left: 0-7 */
    Uint8   efselectL;  /* Effect select for left: 0-15 */
} PCM;

#define _Stereo     0x80
#define _Mono       0x00
#define _PCM16Bit   0x00
#define _PCM8Bit    0x10

/**
 * Start PCM playback on the channel specified in the PCM struct.
 * @param pcm  - PCM descriptor (mode, channel, level, pan, pitch)
 * @param data - PCM sample data pointer (in Work RAM)
 * @param size - Data size in bytes
 * @return Channel number on success, negative on error
 */
extern Sint8 slPCMOn(PCM *pcm, void *data, Uint32 size);

/** Stop PCM playback on the given channel. */
extern Bool slPCMOff(PCM *pcm);

/** Change PCM parameters (volume, pan, pitch) on a playing channel. */
extern Bool slPCMParmChange(PCM *pcm);

/** Check if a PCM channel is currently playing. Returns true if playing. */
extern Bool slPCMStat(PCM *pcm);

/** Get the number of available PCM channels for the given mode. */
extern Sint8 slSndPCMNum(Uint8 mode);

/*============================================================================
 * CDC (CD Block) - CD-DA Playback
 *============================================================================*/

/** CD position parameter (for play/seek commands). */
typedef struct {
    Sint32 ptype;           /* Position type (CDC_PTYPE_*) */
    union {
        Sint32 fad;         /* Frame address */
        struct {
            Uint8 tno;      /* Track number */
            Uint8 idx;      /* Index number */
        } trkidx;
    } pbody;
} CdcPos;

/** CD playback parameter. */
typedef struct {
    CdcPos  start;          /* Start position */
    CdcPos  end;            /* End position */
    Uint8   pmode;          /* Play mode (repeat count) */
} CdcPly;

/* Position types */
#define CDC_PTYPE_DFL       0   /* Default/abbreviated */
#define CDC_PTYPE_FAD       1   /* Frame address */
#define CDC_PTYPE_TNO       2   /* Track number + index */

/* Play modes */
#define CDC_PM_DFL          0x00    /* Play once */
#define CDC_PM_REP_NOCHG    0x7F    /* Don't change repeat setting */
#define CDC_PM_NOCHG        0xFF    /* Don't change play mode */
#define CDC_PM_ENDLESS      0x0F    /* Endless repeat */

/* CdcPly accessor macros */
#define CDC_PLY_START(ply)      ((ply)->start)
#define CDC_PLY_END(ply)        ((ply)->end)
#define CDC_PLY_PMODE(ply)      ((ply)->pmode)

#define CDC_PLY_STYPE(ply)      ((ply)->start.ptype)
#define CDC_PLY_SFAD(ply)       ((ply)->start.pbody.fad)
#define CDC_PLY_STNO(ply)       ((ply)->start.pbody.trkidx.tno)
#define CDC_PLY_SIDX(ply)       ((ply)->start.pbody.trkidx.idx)

#define CDC_PLY_ETYPE(ply)      ((ply)->end.ptype)
#define CDC_PLY_ETNO(ply)       ((ply)->end.pbody.trkidx.tno)
#define CDC_PLY_EIDX(ply)       ((ply)->end.pbody.trkidx.idx)

/* CdcPos accessor macros */
#define CDC_POS_PTYPE(pos)      ((pos)->ptype)
#define CDC_POS_TNO(pos)        ((pos)->pbody.trkidx.tno)
#define CDC_POS_IDX(pos)        ((pos)->pbody.trkidx.idx)

/** Initialize the CD Block. */
extern Sint32 CDC_CdInit(Sint32 iflag, Sint32 stnby, Sint32 ecc, Sint32 retry);

/** Start CD playback (data or audio). */
extern Sint32 CDC_CdPlay(CdcPly *ply);

/** Seek to a CD position (also used to stop CD-DA). */
extern Sint32 CDC_CdSeek(CdcPos *pos);

/*============================================================================
 * SMPC RTC and Status (from SL_DEF.H)
 *============================================================================*/

typedef struct {
    Uint16 year;
    Uint8  month;     /* bits[7:4]=weekday, bits[3:0]=month */
    Uint8  date;
    Uint8  hour;
    Uint8  minute;
    Uint8  second;
    Uint8  dummy;
} SmpcDateTime;

typedef struct {
    Uint8        cond;
    Uint8        dummy1;
    Uint16       dummy2;
    SmpcDateTime rtc;
    Uint8        ctg;
    Uint8        area;
    Uint16       system;
    Uint32       smem;
} SmpcStatus;

extern SmpcStatus* Smpc_Status;

/*============================================================================
 * SMPC Commands
 *============================================================================*/

#define SMPC_RESENA   0x0b
#define SMPC_RESDIS   0x0c
#define SMPC_GETSTS   0x0d
#define SMPC_NO_WAIT  0x00

extern Bool slRequestCommand(Uint8 cmd, Uint8 wait);
extern Uint32 slDec2Hex(Uint32 bcd);

#define slResetEnable()   slRequestCommand(SMPC_RESENA, SMPC_NO_WAIT)
#define slResetDisable()  slRequestCommand(SMPC_RESDIS, SMPC_NO_WAIT)
#define slGetStatus()     slRequestCommand(SMPC_GETSTS, SMPC_NO_WAIT)

#endif /* SGL_DEFS_H */
