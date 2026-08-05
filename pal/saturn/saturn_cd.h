/**
 * saturn_cd.h - Streaming scene data off the disc.
 *
 * WHY THIS EXISTS
 *   Every background used to be linked into the binary as a const table. Three
 *   of them cost 215,040 bytes of WRAM and the build had only 1,032 bytes of
 *   slack left above the measured hang point, so the remaining four scenes
 *   could not be added at any colour depth - not even 4bpp, which still
 *   overran by 34,808 bytes (MASTER-GOAL.md section 9). Streaming keeps one
 *   staging buffer instead of N resident tables: all seven scenes fit, at full
 *   8bpp, and roughly 143 KB comes back.
 *
 * THE LINK GROUP IS NOT OPTIONAL
 *   LIBCD.A needs slDMAXCopy, slDMAStatus, SetCDFunc, CSH_Purge and the
 *   DMA_Scu* helpers, which live in libraries listed BEFORE it on the link
 *   line. Without -Wl,--start-group ... -Wl,--end-group every one of them
 *   comes back undefined, and the natural reading of that error is that SGL
 *   3.02j has no CD support. It does. (MEASURED by link probe 2026-08-05.)
 *
 * TIMING
 *   The binding constraint is the server, which must stay turnkey. Its
 *   tightest window is CHALLENGE_TIMEOUT / BLOCK_TIMEOUT at 12.0 s. A scene
 *   load must finish well inside that; the budget is 1.0 s, and
 *   saturn_cd_last_frames() reports what it actually took so the budget is
 *   checked against hardware rather than against disc-speed arithmetic.
 */

#ifndef SATURN_CD_H
#define SATURN_CD_H

#include <stdint.h>
#include <stdbool.h>

/** Result of the most recent load. Also the QA peek target. */
typedef struct {
    uint32_t magic;        /* SATURN_CD_MAGIC once init has run */
    int32_t  last_frames;  /* vblanks spent inside the last load */
    int32_t  last_bytes;   /* bytes the last load asked for */
    int32_t  last_result;  /* 0 = ok, negative = CDERR_* or our own code */
    int32_t  loads;        /* completed loads since boot */
    int32_t  worst_frames; /* slowest load seen since boot */
} saturn_cd_stats_t;

#define SATURN_CD_MAGIC 0x43445354u   /* 'CDST' */

/* Our own failure codes, kept clear of the CDERR_* range. */
#define SATURN_CD_ERR_NOINIT   (-100)
#define SATURN_CD_ERR_OPEN     (-101)
#define SATURN_CD_ERR_LOAD     (-102)
#define SATURN_CD_ERR_TIMEOUT  (-103)

/**
 * Bring up the CD file system. Safe to call more than once.
 * Returns false if the drive or the disc is not usable.
 */
bool saturn_cd_init(void);

/** True once saturn_cd_init() has succeeded. */
bool saturn_cd_ready(void);

/**
 * Read `nbytes` of `name` into `dest`.
 *
 * Synchronous: it returns when the data is there. Callers must not invoke it
 * inside a server response window - scene changes happen at phase boundaries,
 * never inside a challenge.
 *
 * Returns 0 on success, or a negative SATURN_CD_ERR_* / CDERR_* code.
 */
int saturn_cd_load(const char* name, void* dest, uint32_t nbytes);

/** Vblanks spent in the most recent load. Divide by 60 for NTSC seconds. */
int saturn_cd_last_frames(void);

/** The stats block, for host tests and the QA peek. */
const saturn_cd_stats_t* saturn_cd_stats(void);

#endif /* SATURN_CD_H */
