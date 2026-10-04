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

/* Scheduling inputs. Higher value = more urgent, so they compare directly. */
typedef enum { EMERGENCY_NONE, EMERGENCY_ACTIVE } EmergencyLevel;
typedef enum { FUEL_NORMAL, FUEL_LOW, FUEL_CRITICAL } FuelLevel;

typedef struct {
    char             id[ID_MAX_LEN + 1];
    FlightType       type;        /* fixed at registration */
    AircraftCategory category;
    FlightState      state;
    EmergencyLevel   emergency;   /* default EMERGENCY_NONE */
    FuelLevel        fuel;        /* default FUEL_NORMAL */
    int              request_tick; /* logical time of the runway request (set by request_submit) */
    unsigned         request_seq;  /* unique, increasing request number (set by request_submit) */
    char             runway_id[ID_MAX_LEN + 1];   /* "" = no active runway; set only by operations.c */
} Flight;

typedef struct {
    Flight   items[MAX_FLIGHTS];
    int      count;
    int      now;       /* logical time; stays 0 until the simulation clock exists */
    unsigned next_seq;  /* next request_seq to hand out */
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

/* Emergency/fuel can change while a flight is REGISTERED..IN_OPERATION, not after
 * COMPLETED/CANCELLED. Errors: INVALID_ARG, NOT_FOUND, NOT_ELIGIBLE.
 * If the flight is in the scheduler heap, use scheduler_set_* so the heap is updated. */
ArmsStatus  flight_set_emergency(FlightStore *fs, const char *id, EmergencyLevel level);
ArmsStatus  flight_set_fuel(FlightStore *fs, const char *id, FuelLevel level);

/* Runway priority: 1 if a must be served before b, else 0. Order:
 *   1. emergency (higher first)   2. fuel (higher first)   3. ARRIVAL before DEPARTURE
 *   4. waiting time (smaller request_tick first)   5. request_seq (smaller first)
 * request_seq is unique, so two different flights never tie.
 * Waiting time only becomes meaningful once the simulation clock advances fs->now;
 * until then every request_tick is 0 and request_seq gives plain FIFO order. */
int         flight_priority_higher(const Flight *a, const Flight *b);

int           flight_count(const FlightStore *fs);
const Flight *flight_at(const FlightStore *fs, int index);   /* NULL if out of range */

void        flight_print(FILE *out, const Flight *f);
void        flight_list(FILE *out, const FlightStore *fs);

const char *flight_state_name(FlightState s);
const char *flight_type_name(FlightType t);
const char *aircraft_category_name(AircraftCategory c);
const char *flight_emergency_name(EmergencyLevel e);
const char *flight_fuel_name(FuelLevel f);

#endif
