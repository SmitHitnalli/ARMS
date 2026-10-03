#ifndef HOLDING_H
#define HOLDING_H

#include "common.h"
#include "cqueue.h"
#include "flight.h"
#include "request.h"

/* Flights parked in holding. A flight's ID is in the holding queue OR in a normal
 * queue (landing/takeoff), never both. */
typedef struct {
    CircularQueue queue;
} HoldingQueue;

void       holding_init(HoldingQueue *h);

/* WAITING -> HOLDING. Moves the ID from its landing/takeoff queue into holding.
 * Errors: INVALID_ARG, NOT_FOUND (no such flight, or ID not in its normal queue),
 * DUPLICATE (already in holding), INVALID_TRANSITION (not WAITING), FULL (holding full).
 * On any error no queue and no flight state changes. */
ArmsStatus holding_enter(HoldingQueue *h, RequestQueues *rq, FlightStore *fs, const char *id);

/* HOLDING -> WAITING. Removes the ID from holding and appends it to the REAR of its
 * landing/takeoff queue.
 * Errors: INVALID_ARG, NOT_FOUND (no such flight, or ID not in holding),
 * INVALID_TRANSITION (not HOLDING), DUPLICATE (already in a normal queue),
 * FULL (normal queue full).
 * On any error no queue and no flight state changes. */
ArmsStatus holding_leave(HoldingQueue *h, RequestQueues *rq, FlightStore *fs, const char *id);

int        holding_count(const HoldingQueue *h);
void       holding_print(FILE *out, const HoldingQueue *h);

#endif
