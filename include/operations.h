#ifndef OPERATIONS_H
#define OPERATIONS_H

#include "common.h"
#include "flight.h"
#include "history.h"
#include "holding.h"
#include "request.h"
#include "runway.h"
#include "scheduler.h"

/* Operations layer: the only place that changes a flight and a runway (or queue) together.
 * Application code must use these instead of calling flight_transition() for
 * SCHEDULED / IN_OPERATION / COMPLETED / CANCELLED.
 *
 * Invariants kept by every function (checked by ops_check_invariants):
 *  - An OCCUPIED runway names exactly one SCHEDULED or IN_OPERATION flight, and that
 *    flight's runway_id names the runway back.
 *  - A SCHEDULED or IN_OPERATION flight has exactly one runway; no runway serves two flights.
 *  - Flights in any other state have an empty runway_id.
 *  - Only WAITING flights are in the scheduler heap.
 *  - A failed call changes nothing. */

/* Schedules the highest-priority WAITING flight in the heap that has a compatible
 * AVAILABLE runway (first match in RunwayStore order). Blocked flights are skipped and stay
 * in the heap. WAITING -> SCHEDULED, runway OCCUPIED, flight.runway_id set. Copies the flight
 * and runway IDs to out_flight_id / out_runway_id (ID_MAX_LEN + 1 bytes each).
 * Only flights already in the Scheduler heap are considered. This function does NOT drain
 * RequestQueues: call scheduler_pull(s, rq, fs) first whenever newly queued WAITING flights
 * must be included. Flights still in the landing/takeoff queues are invisible to it.
 * Errors: INVALID_ARG, EMPTY (heap empty), NO_RUNWAY (flights waiting, none can be served). */
ArmsStatus ops_schedule_next(Scheduler *s, FlightStore *fs, RunwayStore *rs,
                             char *out_flight_id, char *out_runway_id);

/* SCHEDULED -> IN_OPERATION. The runway stays OCCUPIED.
 * Errors: INVALID_ARG, NOT_FOUND, INVALID_TRANSITION (not SCHEDULED),
 * NOT_OCCUPIED (runway link is broken). */
ArmsStatus ops_start(FlightStore *fs, RunwayStore *rs, const char *flight_id);

/* IN_OPERATION -> COMPLETED, releases the runway and clears runway_id. Adds exactly one
 * Movement record to mh (the runway ID is captured before it is cleared).
 * Same errors as ops_start (INVALID_TRANSITION if not IN_OPERATION), plus INVALID_ARG for
 * a NULL mh and NO_MEMORY if the record cannot be allocated; in that case nothing changes. */
ArmsStatus ops_complete(FlightStore *fs, RunwayStore *rs, MovementHistory *mh,
                        const char *flight_id);

/* Cancels a flight and removes it from whatever it currently occupies:
 *   REGISTERED   -> just cancelled
 *   WAITING      -> removed from the scheduler heap or its landing/takeoff queue
 *   HOLDING      -> removed from the holding queue
 *   SCHEDULED    -> runway released, runway_id cleared
 *   IN_OPERATION, COMPLETED, CANCELLED -> rejected (INVALID_TRANSITION)
 * Other errors: INVALID_ARG, NOT_FOUND (no such flight, or it is missing from the structure
 * its state says it is in), NOT_OCCUPIED (SCHEDULED flight with a broken runway link). */
ArmsStatus ops_cancel(Scheduler *s, HoldingQueue *h, RequestQueues *rq,
                      FlightStore *fs, RunwayStore *rs, const char *flight_id);

/* 1 if all the invariants above hold, else 0. Read-only; meant for tests and debug views. */
int        ops_check_invariants(const FlightStore *fs, const RunwayStore *rs, const Scheduler *s);

#endif
