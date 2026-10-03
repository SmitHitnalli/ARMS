#ifndef SYSTEM_H
#define SYSTEM_H

#include "common.h"
#include "flight.h"
#include "graph.h"
#include "history.h"
#include "holding.h"
#include "request.h"
#include "runway.h"
#include "scheduler.h"

/* The whole core state in one place, so it can be saved, loaded and handed to a frontend
 * as a single object. No GTK here. */
typedef struct {
    FlightStore     flights;
    RunwayStore     runways;
    RequestQueues   requests;   /* landing + takeoff queues */
    HoldingQueue    holding;
    Scheduler       scheduler;  /* owns the priority heap */
    MovementHistory history;    /* linked list: the only part that owns heap memory */
    AirportGraph    graph;
} ArmsSystem;

/* Makes every part empty. Call once on a new ArmsSystem (never on a live one: it would
 * leak the history list; use arms_system_clear for that). */
void arms_system_init(ArmsSystem *s);

/* Frees the history list and resets everything to the empty state. The system must
 * have been initialised before. */
void arms_system_clear(ArmsSystem *s);

#endif
