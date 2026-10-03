#ifndef HISTORY_H
#define HISTORY_H

#include "common.h"
#include "flight.h"

/* Movement history: a permanent, in-memory record of COMPLETED runway movements only.
 * A record is added when a flight goes IN_OPERATION -> COMPLETED (see ops_complete).
 * Cancelled flights are not movements and are never recorded.
 * Records are kept in completion order (oldest first). */

typedef struct {
    char             flight_id[ID_MAX_LEN + 1];
    FlightType       type;
    AircraftCategory category;
    char             runway_id[ID_MAX_LEN + 1];   /* runway used, copied before it is released */
    int              request_tick;                /* copied from the flight */
    unsigned         request_seq;                 /* copied from the flight */
    int              completed_tick;              /* FlightStore.now at completion */
} Movement;

/* Singly linked list. tail makes append O(1). */
typedef struct MovementNode {
    Movement             data;
    struct MovementNode *next;
} MovementNode;

typedef struct {
    MovementNode *head;
    MovementNode *tail;
    int           count;
} MovementHistory;

void        history_init(MovementHistory *h);

/* Copies *m onto the end of the list. O(1).
 * Errors: INVALID_ARG, NO_MEMORY (the list is left unchanged). */
ArmsStatus  history_append(MovementHistory *h, const Movement *m);

int             history_count(const MovementHistory *h);

/* Both return NULL when not found / out of range. The pointer stays valid until history_clear. */
const Movement *history_find(const MovementHistory *h, const char *flight_id);   /* O(n) */
const Movement *history_at(const MovementHistory *h, int index);                 /* O(n) */

void        history_list(FILE *out, const MovementHistory *h);

/* Frees every node and leaves an empty, reusable list. */
void        history_clear(MovementHistory *h);

#endif
