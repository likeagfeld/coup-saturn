/**
 * test_coup_game_stubs.c - Stubs for coup_game.c external dependencies
 *
 * Audio stubs record calls for assertion; render and platform are no-ops.
 */

#include "coup.h"
#include <string.h>

/*============================================================================
 * Audio — Recording Stubs
 *============================================================================*/

#define STUB_SFX_LOG_SIZE 64

static int sfx_log[STUB_SFX_LOG_SIZE];
static int sfx_count = 0;

void coup_audio_play_sfx(int sfx_id)
{
    if (sfx_count < STUB_SFX_LOG_SIZE)
        sfx_log[sfx_count++] = sfx_id;
}

void stub_sfx_reset(void)
{
    sfx_count = 0;
    memset(sfx_log, 0, sizeof(sfx_log));
}

int stub_sfx_last(void)
{
    return sfx_count > 0 ? sfx_log[sfx_count - 1] : -1;
}

int stub_sfx_count(void)
{
    return sfx_count;
}

int stub_sfx_at(int index)
{
    if (index >= 0 && index < sfx_count)
        return sfx_log[index];
    return -1;
}

/* No-op audio stubs */
void coup_audio_init(void)                     { }
void coup_audio_tick(void)                     { }
void coup_audio_start_music(void)              { }
void coup_audio_stop_music(void)               { }
void coup_audio_set_music_volume(int vol)      { (void)vol; }
void coup_audio_set_sfx_volume(int vol)        { (void)vol; }
void coup_audio_debug_update(uint16_t pad_raw) { (void)pad_raw; }
void coup_audio_debug_render(void)             { }

/*============================================================================
 * Render — No-op Stub
 *============================================================================*/

void coup_render_screen(const coup_state_t* st)
{
    (void)st;
}

/*============================================================================
 * Platform — No-op Stub
 *============================================================================*/

void coup_platform_try_connect(void)
{
}
