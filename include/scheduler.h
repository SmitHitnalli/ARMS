#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"
#include "flight.h"
#include "heap.h"
#include "holding.h"
#include "request.h"

/* Runway-priority scheduler. Phase 5 only decides ORDER; it does not touch runways.
 *
 * Invariants:
 *  - A WAITING flight is in exactly one of: landing queue, takeoff queue, or this heap.
 *  - A HOLDING flight is only in the holding queue (never in the heap or a normal queue).
 *  - Only WAITING flights are ever in the heap.
 * holding.c knows nothing about the scheduler: scheduler_hold() handles the case where
 * the flight to park is currently in the heap. */
typedef struct {
    MaxHeap heap;
} Scheduler;

void        scheduler_init(Scheduler *s);

/* Moves every flight from the landing and takeoff queues into the heap.
 * Returns FULL (leaving the rest queued) if the heap fills up. */
ArmsStatus  scheduler_pull(Scheduler *s, RequestQueues *rq, FlightStore *fs);

/* Highest-priority flight already in the heap, or NULL. Call scheduler_pull first. */
const char *scheduler_peek(const Scheduler *s);

/* Removes the top flight and moves it WAITING -> SCHEDULED (no runway is assigned here).
 * Copies its ID to out_id. Errors: INVALID_ARG, EMPTY. */
ArmsStatus  scheduler_pop(Scheduler *s, FlightStore *fs, char *out_id);

/* Change a priority field and, if the flight is in the heap, restore heap order.
 * A flight that is queued, holding, etc. just has the field updated (no auto-release
 * from holding). Errors as flight_set_*. */
ArmsStatus  scheduler_set_emergency(Scheduler *s, FlightStore *fs, const char *id,
                                    EmergencyLevel level);
ArmsStatus  scheduler_set_fuel(Scheduler *s, FlightStore *fs, const char *id, FuelLevel level);

/* WAITING -> HOLDING for a flight that may be in the heap or in a normal queue.
 * If it is in the heap it is returned to its normal queue first, then holding_enter runs.
 * Errors as holding_enter; on any error nothing changes. */
ArmsStatus  scheduler_hold(Scheduler *s, HoldingQueue *h, RequestQueues *rq,
                           FlightStore *fs, const char *id);

int         scheduler_count(const Scheduler *s);
int         scheduler_contains(const Scheduler *s, const char *id);

#endif
