#ifndef EVENTLOG_H
#define EVENTLOG_H

#include "common.h"

/* Event/audit log: a permanent, in-memory record of successful state-changing actions.
 * Events are kept in chronological order (oldest first). Logging is best-effort: if a node
 * cannot be allocated the event is counted in `dropped` and the caller carries on.
 * The log is historical: an event may name a flight or runway that no longer exists. */

#define EVENT_DETAILS_LEN 24   /* including the terminating '\0' */

typedef enum {
    EVENT_FLIGHT_REGISTERED,
    EVENT_RUNWAY_ADDED,
    EVENT_REQUEST_SUBMITTED,
    EVENT_HOLD_ENTERED,
    EVENT_HOLD_LEFT,
    EVENT_EMERGENCY_CHANGED,
    EVENT_FUEL_CHANGED,
    EVENT_FLIGHT_SCHEDULED,
    EVENT_OP_STARTED,
    EVENT_OP_COMPLETED,
    EVENT_FLIGHT_CANCELLED,
    EVENT_RUNWAY_CLOSED,
    EVENT_RUNWAY_REOPENED,
    EVENT_TYPE_COUNT
} EventType;

typedef struct {
    unsigned  seq;                          /* 1, 2, 3... in the order events were recorded */
    int       tick;                         /* FlightStore.now when it happened */
    EventType type;
    char      flight_id[ID_MAX_LEN + 1];    /* "" if not applicable */
    char      runway_id[ID_MAX_LEN + 1];    /* "" if not applicable */
    char      details[EVENT_DETAILS_LEN];   /* short token such as "LOW"; "" if none */
} Event;

/* Singly linked list. tail makes append O(1). */
typedef struct EventNode {
    Event             data;
    struct EventNode *next;
} EventNode;

typedef struct {
    EventNode *head;
    EventNode *tail;
    int        count;
    unsigned   next_seq;   /* sequence number the next event will get (starts at 1) */
    unsigned   dropped;    /* events lost because allocation failed */
} EventLog;

void        eventlog_init(EventLog *log);

/* Appends one event. NULL ids and details mean "none". details is copied, truncated to fit,
 * and any space or control character in it becomes '_' so it stays one token.
 * On NO_MEMORY the log is unchanged except that dropped is incremented (seq does not advance).
 * Errors: INVALID_ARG (NULL log or bad type), NO_MEMORY. O(1). */
ArmsStatus  eventlog_append(EventLog *log, int tick, EventType type,
                            const char *flight_id, const char *runway_id, const char *details);

/* Appends a complete event exactly as given (used by persist_load). Does not touch
 * next_seq or dropped. Errors: INVALID_ARG, NO_MEMORY (unchanged). */
ArmsStatus  eventlog_restore(EventLog *log, const Event *e);

int         eventlog_count(const EventLog *log);

/* Name <-> enum. Name of an invalid type is "UNKNOWN"; parse returns 1 on success, else 0. */
const char *eventlog_type_name(EventType t);
int         eventlog_type_parse(const char *name, EventType *out);

/* Frees every node and resets to a fresh empty log (next_seq = 1, dropped = 0). */
void        eventlog_clear(EventLog *log);

#endif
