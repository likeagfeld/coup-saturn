/**
 * saturn_cd.c - Streaming scene data off the disc. See saturn_cd.h.
 */

#include "saturn_cd.h"

#ifdef __SATURN__
/* sgl_defs.h carries the CD declarations. SGL_CD.H itself cannot be included
 * here - it pulls in SGL.H/SL_DEF.H, which collide with this PAL's own bare
 * declarations. See the CD section of sgl_defs.h. */
#include "sgl_defs.h"
#endif

/* Exported deliberately, not static: the QA harness locates this symbol in
 * the linker map and peeks it live over READ_CORE_RAM, which is how the
 * 1.0 s load budget is checked on hardware output instead of estimated. */
saturn_cd_stats_t g_saturn_cd_stats;

static bool s_ready = false;

#ifdef __SATURN__

/* slCdInit caches one directory. The root holds 0.BIN plus the scene files,
 * so this only has to exceed the real entry count; 64 is comfortable and the
 * work area is 64 * sizeof(GfsDirName), which is small. */
#define SATURN_CD_MAX_FILES 64

/* A load that has not finished after this many vblanks has failed. At 60 Hz
 * this is ~8.5 s, still inside the server's 12 s challenge window, so a stuck
 * drive surfaces as an error rather than as a missed turn. */
#define SATURN_CD_TIMEOUT_FRAMES 512

/* VDP2 TVSTAT (ST-058-R2): bit 3 is VBLANK. Counting its rising edges gives
 * frame-accurate timing without knowing how SGL has programmed the SH-2 free
 * running timer's prescaler - which SL_DEF.H exposes no way to ask about. */
#define VDP2_TVSTAT   (*(volatile uint16_t*)0x25F80004u)
#define TVSTAT_VBLANK 0x0008u

/* GfsDirName is 12 bytes in SEGA_GFS.H; sized from the macro regardless. */
static uint8_t s_work[SLCD_WORK_SIZE(SATURN_CD_MAX_FILES)];

static int s_vb_prev;

static void vb_reset(void)
{
    s_vb_prev = (VDP2_TVSTAT & TVSTAT_VBLANK) ? 1 : 0;
}

/** Count one vblank rising edge, if one has happened since the last call. */
static int vb_tick(void)
{
    int now = (VDP2_TVSTAT & TVSTAT_VBLANK) ? 1 : 0;
    int edge = (now && !s_vb_prev) ? 1 : 0;

    s_vb_prev = now;
    return edge;
}

bool saturn_cd_init(void)
{
    Sint32 rc;

    if (s_ready) {
        return true;
    }

    g_saturn_cd_stats.magic = SATURN_CD_MAGIC;

    rc = slCdInit(SATURN_CD_MAX_FILES, s_work);
    if (rc < 0) {
        g_saturn_cd_stats.last_result = (int32_t)rc;
        return false;
    }

    s_ready = true;
    return true;
}

int saturn_cd_load(const char* name, void* dest, uint32_t nbytes)
{
    CDKEY key[2];
    CDBUF buf[2];
    CDHN  hn;
    Sint32 ndata[4];
    Sint32 st;
    int frames = 0;

    if (!s_ready || !name || !dest || nbytes == 0u) {
        g_saturn_cd_stats.last_result = SATURN_CD_ERR_NOINIT;
        return SATURN_CD_ERR_NOINIT;
    }

    g_saturn_cd_stats.last_bytes = (int32_t)nbytes;

    /* Take every sector of the file - no channel/submode filtering. */
    key[0].cn = CDKEY_NONE;
    key[0].sm = CDKEY_NONE;
    key[0].ci = CDKEY_NONE;
    key[1].cn = CDKEY_TERM;
    key[1].sm = 0;
    key[1].ci = 0;

    hn = slCdOpen((Sint8*)name, key);
    if (hn == 0) {
        g_saturn_cd_stats.last_result = SATURN_CD_ERR_OPEN;
        return SATURN_CD_ERR_OPEN;
    }

    /* Copy straight into the caller's buffer, counted in bytes rather than
     * sectors so a file that is not a whole multiple of 2048 still lands
     * exactly. */
    buf[0].type = CDBUF_COPY;
    buf[0].trans.copy.addr = dest;
    buf[0].trans.copy.unit = CDBUF_BYTE;
    buf[0].trans.copy.size = (Sint32)nbytes;
    buf[1].type = CDBUF_TERM;

    if (slCdLoadFile(hn, buf) < 0) {
        slCdAbort(hn);
        g_saturn_cd_stats.last_result = SATURN_CD_ERR_LOAD;
        return SATURN_CD_ERR_LOAD;
    }

    vb_reset();
    for (;;) {
        slCdEvent();                    /* drive the transfer */

        st = slCdGetStatus(hn, ndata);
        if (st == CDSTAT_COMPLETED) {
            break;
        }
        if (st < 0) {                   /* a CDERR_* code */
            slCdAbort(hn);
            g_saturn_cd_stats.last_result = (int32_t)st;
            return (int)st;
        }

        frames += vb_tick();
        if (frames > SATURN_CD_TIMEOUT_FRAMES) {
            slCdAbort(hn);
            g_saturn_cd_stats.last_frames = (int32_t)frames;
            g_saturn_cd_stats.last_result = SATURN_CD_ERR_TIMEOUT;
            return SATURN_CD_ERR_TIMEOUT;
        }
    }

    slCdAbort(hn);                      /* releases the handle */

    g_saturn_cd_stats.last_frames = (int32_t)frames;
    g_saturn_cd_stats.last_result = 0;
    g_saturn_cd_stats.loads++;
    if (frames > g_saturn_cd_stats.worst_frames) {
        g_saturn_cd_stats.worst_frames = (int32_t)frames;
    }
    return 0;
}

#else  /* host build */

bool saturn_cd_init(void)
{
    g_saturn_cd_stats.magic = SATURN_CD_MAGIC;
    s_ready = true;
    return true;
}

int saturn_cd_load(const char* name, void* dest, uint32_t nbytes)
{
    if (!s_ready || !name || !dest || nbytes == 0u) {
        g_saturn_cd_stats.last_result = SATURN_CD_ERR_NOINIT;
        return SATURN_CD_ERR_NOINIT;
    }
    g_saturn_cd_stats.last_bytes = (int32_t)nbytes;
    g_saturn_cd_stats.last_result = SATURN_CD_ERR_NOINIT;
    return SATURN_CD_ERR_NOINIT;        /* no disc off-target */
}

#endif /* __SATURN__ */

bool saturn_cd_ready(void)
{
    return s_ready;
}

int saturn_cd_last_frames(void)
{
    return (int)g_saturn_cd_stats.last_frames;
}

const saturn_cd_stats_t* saturn_cd_stats(void)
{
    return &g_saturn_cd_stats;
}
