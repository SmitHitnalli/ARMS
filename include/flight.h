#ifndef FLIGHT_H
#define FLIGHT_H

#include "common.h"

typedef enum { FLIGHT_ARRIVAL, FLIGHT_DEPARTURE } FlightType;

/* Ordered so that SMALL < MEDIUM < LARGE can be compared directly. */
typedef enum { AIRCRAFT_SMALL, AIRCRAFT_MEDIUM, AIRCRAFT_LARGE } AircraftCategory;

typedef enum {
    STATE_REGISTERED,
    STATE_WAITING,
    STATE_HOLDING,
    STATE_SCHEDULED,
    STATE_IN_OPERATION,
    STATE_COMPLETED,
    STATE_CANCELLED,
    STATE_COUNT
} FlightState;

typedef struct {
    char             id[ID_MAX_LEN + 1];
    FlightType       type;        /* fixed at registration */
    AircraftCategory category;
    FlightState      state;
} Flight;

typedef struct {
    Flight items[MAX_FLIGHTS];
    int    count;
} FlightStore;

void        flight_store_init(FlightStore *fs);

/* New flights start in STATE_REGISTERED. */
ArmsStatus  flight_add(FlightStore *fs, const char *id, FlightType type,
                       AircraftCategory category);

/* Returns NULL if not found. */
Flight     *flight_find(FlightStore *fs, const char *id);

/* The only way to change a flight's state; follows the transition table. */
ArmsStatus  flight_transition(FlightStore *fs, const char *id, FlightState to);

/* Query only: 1 if from -> to is allowed, else 0. */
int         flight_transition_allowed(FlightState from, FlightState to);

int           flight_count(const FlightStore *fs);
const Flight *flight_at(const FlightStore *fs, int index);   /* NULL if out of range */

void        flight_print(FILE *out, const Flight *f);
void        flight_list(FILE *out, const FlightStore *fs);

const char *flight_state_name(FlightState s);
const char *flight_type_name(FlightType t);
const char *aircraft_category_name(AircraftCategory c);

#endif
