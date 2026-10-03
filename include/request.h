#ifndef REQUEST_H
#define REQUEST_H

#include "common.h"
#include "flight.h"
#include "queue.h"

typedef struct {
    Queue landing;   /* ARRIVAL flights */
    Queue takeoff;   /* DEPARTURE flights */
} RequestQueues;

void       request_init(RequestQueues *rq);

/* Normal runway request: the flight must exist and be REGISTERED. It is queued by
 * type (ARRIVAL -> landing, DEPARTURE -> takeoff) and moved to WAITING.
 * Errors: INVALID_ARG, NOT_FOUND, DUPLICATE, INVALID_TRANSITION, FULL.
 * On any error neither the queues nor the flight change. */
ArmsStatus request_submit(RequestQueues *rq, FlightStore *fs, const char *id);

#endif
