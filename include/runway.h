#ifndef RUNWAY_H
#define RUNWAY_H

#include "common.h"
#include "flight.h"

typedef enum { RUNWAY_AVAILABLE, RUNWAY_OCCUPIED, RUNWAY_CLOSED } RunwayStatus;

typedef struct {
    char             id[ID_MAX_LEN + 1];
    RunwayStatus     status;      /* OCCUPIED = allocated from the moment of assignment */
    AircraftCategory max_category;
    char             current_flight_id[ID_MAX_LEN + 1];   /* "" when none */
} Runway;

typedef struct {
    Runway items[MAX_RUNWAYS];
    int    count;
} RunwayStore;

void        runway_store_init(RunwayStore *rs);

/* New runways start AVAILABLE. */
ArmsStatus  runway_add(RunwayStore *rs, const char *id, AircraftCategory max_category);

/* Returns NULL if not found. */
Runway     *runway_find(RunwayStore *rs, const char *id);

/* Only AVAILABLE <-> CLOSED. Use runway_assign / runway_release for OCCUPIED. */
ArmsStatus  runway_set_status(RunwayStore *rs, const char *id, RunwayStatus to);

/* Marks the runway OCCUPIED for a WAITING or HOLDING compatible flight.
 * Does not change the flight; the later service layer does that. */
ArmsStatus  runway_assign(RunwayStore *rs, const char *runway_id, const Flight *flight);
ArmsStatus  runway_release(RunwayStore *rs, const char *runway_id);

/* 1 if flight->category <= r->max_category, else 0. */
int         runway_is_compatible(const Runway *r, const Flight *f);

int           runway_count(const RunwayStore *rs);
const Runway *runway_at(const RunwayStore *rs, int index);   /* NULL if out of range */

void        runway_print(FILE *out, const Runway *r);
void        runway_list(FILE *out, const RunwayStore *rs);

const char *runway_status_name(RunwayStatus s);

#endif
