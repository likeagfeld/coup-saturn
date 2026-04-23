/**
 * coup_event_log.c - Fixed-size ring buffer of game events
 */

#include "coup_event_log.h"
#include <string.h>

void coup_event_log_init(coup_event_log_t* log)
{
    memset(log, 0, sizeof(*log));
    log->next_offset = 1; /* offsets start at 1 so 0 means "give me everything" */
}

void coup_event_log_append(coup_event_log_t* log, const coup_event_t* evt)
{
    uint32_t idx = (log->head + log->count) % COUP_MAX_EVENT_LOG;

    if (log->count == COUP_MAX_EVENT_LOG) {
        /* Ring buffer full — overwrite oldest */
        log->head = (log->head + 1) % COUP_MAX_EVENT_LOG;
    } else {
        log->count++;
    }

    log->entries[idx].offset = log->next_offset++;
    log->entries[idx].event = *evt;
}

int coup_event_log_since(const coup_event_log_t* log, uint32_t offset,
                         coup_event_entry_t* out, int max)
{
    int written = 0;
    uint32_t i;

    for (i = 0; i < log->count && written < max; i++) {
        uint32_t idx = (log->head + i) % COUP_MAX_EVENT_LOG;
        if (log->entries[idx].offset > offset) {
            out[written++] = log->entries[idx];
        }
    }

    return written;
}

uint32_t coup_event_log_latest_offset(const coup_event_log_t* log)
{
    if (log->count == 0) return 0;

    uint32_t last_idx = (log->head + log->count - 1) % COUP_MAX_EVENT_LOG;
    return log->entries[last_idx].offset;
}
