/**
 * coup_event_log.h - Fixed-size ring buffer of game events
 *
 * Stores events with monotonically increasing sequence offsets.
 * Supports catch-up queries: "give me all events since offset N."
 * Used by server heartbeat to sync clients.
 */

#ifndef COUP_EVENT_LOG_H
#define COUP_EVENT_LOG_H

#include "coup_rules.h"

#define COUP_MAX_EVENT_LOG 256

typedef struct {
    uint32_t     offset;
    coup_event_t event;
} coup_event_entry_t;

typedef struct {
    coup_event_entry_t entries[COUP_MAX_EVENT_LOG];
    uint32_t head;
    uint32_t count;
    uint32_t next_offset;
} coup_event_log_t;

/**
 * Initialize an empty event log.
 */
void coup_event_log_init(coup_event_log_t* log);

/**
 * Append an event to the log. Assigns the next sequence offset.
 * Old events are evicted when the ring buffer is full.
 */
void coup_event_log_append(coup_event_log_t* log, const coup_event_t* evt);

/**
 * Retrieve events with offsets strictly greater than `offset`.
 * Writes up to `max` entries into `out`.
 * Returns the number of entries written.
 */
int coup_event_log_since(const coup_event_log_t* log, uint32_t offset,
                         coup_event_entry_t* out, int max);

/**
 * Return the offset of the most recently appended event,
 * or 0 if the log is empty.
 */
uint32_t coup_event_log_latest_offset(const coup_event_log_t* log);

#endif /* COUP_EVENT_LOG_H */
