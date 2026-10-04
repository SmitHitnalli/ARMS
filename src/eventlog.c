#include "eventlog.h"

#include <stdlib.h>
#include <string.h>

#ifdef ARMS_TESTING
/* Test builds only: set to 1 to make the next event allocation fail as if malloc returned NULL. */
int eventlog_fail_next_alloc = 0;
#endif

static const char *const TYPE_NAMES[EVENT_TYPE_COUNT] = {
    "FLIGHT_REGISTERED", "RUNWAY_ADDED", "REQUEST_SUBMITTED", "HOLD_ENTERED", "HOLD_LEFT",
    "EMERGENCY_CHANGED", "FUEL_CHANGED", "FLIGHT_SCHEDULED", "OP_STARTED", "OP_COMPLETED",
    "FLIGHT_CANCELLED", "RUNWAY_CLOSED", "RUNWAY_REOPENED"
};

void eventlog_init(EventLog *log)
{
    log->head = NULL;
    log->tail = NULL;
    log->count = 0;
    log->next_seq = 1;
    log->dropped = 0;
}

/* Allocates a node holding a copy of *e and links it at the tail. NO_MEMORY changes nothing. */
static ArmsStatus link_node(EventLog *log, const Event *e)
{
#ifdef ARMS_TESTING
    if (eventlog_fail_next_alloc) {
        eventlog_fail_next_alloc = 0;
        return ARMS_ERR_NO_MEMORY;
    }
#endif
    EventNode *node = malloc(sizeof *node);
    if (node == NULL)
        return ARMS_ERR_NO_MEMORY;
    node->data = *e;
    node->next = NULL;

    if (log->tail == NULL)
        log->head = node;
    else
        log->tail->next = node;
    log->tail = node;
    log->count++;
    return ARMS_OK;
}

static void copy_id(char *dest, const char *id)
{
    if (id == NULL)
        id = "";
    snprintf(dest, ID_MAX_LEN + 1, "%s", id);
}

ArmsStatus eventlog_append(EventLog *log, int tick, EventType type,
                           const char *flight_id, const char *runway_id, const char *details)
{
    if (log == NULL || (int)type < 0 || type >= EVENT_TYPE_COUNT)
        return ARMS_ERR_INVALID_ARG;

    Event e;
    memset(&e, 0, sizeof e);
    e.seq = log->next_seq;
    e.tick = tick;
    e.type = type;
    copy_id(e.flight_id, flight_id);
    copy_id(e.runway_id, runway_id);
    snprintf(e.details, sizeof e.details, "%s", details != NULL ? details : "");
    for (char *p = e.details; *p != '\0'; p++) {
        if ((unsigned char)*p <= ' ' || *p == 127)
            *p = '_';
    }

    ArmsStatus st = link_node(log, &e);
    if (st == ARMS_OK)
        log->next_seq++;
    else
        log->dropped++;
    return st;
}

ArmsStatus eventlog_restore(EventLog *log, const Event *e)
{
    if (log == NULL || e == NULL)
        return ARMS_ERR_INVALID_ARG;
    return link_node(log, e);
}

int eventlog_count(const EventLog *log)
{
    return log == NULL ? 0 : log->count;
}

const char *eventlog_type_name(EventType t)
{
    return ((int)t >= 0 && t < EVENT_TYPE_COUNT) ? TYPE_NAMES[t] : "UNKNOWN";
}

int eventlog_type_parse(const char *name, EventType *out)
{
    if (name == NULL || out == NULL)
        return 0;
    for (int t = 0; t < EVENT_TYPE_COUNT; t++) {
        if (strcmp(TYPE_NAMES[t], name) == 0) {
            *out = (EventType)t;
            return 1;
        }
    }
    return 0;
}

void eventlog_clear(EventLog *log)
{
    if (log == NULL)
        return;
    EventNode *n = log->head;
    while (n != NULL) {
        EventNode *next = n->next;
        free(n);
        n = next;
    }
    eventlog_init(log);
}
