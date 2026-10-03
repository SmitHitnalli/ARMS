#include "flight.h"

#include <string.h>

/* allowed[from][to] = 1 if the transition is valid.
 * Columns: REGISTERED, WAITING, HOLDING, SCHEDULED, IN_OPERATION, COMPLETED, CANCELLED.
 * SCHEDULED -> CANCELLED is intentionally 0 until Phase 6 can also release the runway. */
static const int allowed[STATE_COUNT][STATE_COUNT] = {
    /* REGISTERED   */ { 0, 1, 0, 0, 0, 0, 1 },
    /* WAITING      */ { 0, 0, 1, 1, 0, 0, 1 },
    /* HOLDING      */ { 0, 1, 0, 1, 0, 0, 1 },
    /* SCHEDULED    */ { 0, 0, 0, 0, 1, 0, 0 },
    /* IN_OPERATION */ { 0, 0, 0, 0, 0, 1, 0 },
    /* COMPLETED    */ { 0, 0, 0, 0, 0, 0, 0 },
    /* CANCELLED    */ { 0, 0, 0, 0, 0, 0, 0 }
};

static int state_in_range(FlightState s)
{
    return (int)s >= 0 && (int)s < STATE_COUNT;
}

void flight_store_init(FlightStore *fs)
{
    if (fs != NULL) {
        fs->count = 0;
        fs->now = 0;
        fs->next_seq = 0;
    }
}

ArmsStatus flight_add(FlightStore *fs, const char *id, FlightType type,
                      AircraftCategory category)
{
    if (fs == NULL)
        return ARMS_ERR_INVALID_ARG;
    if ((int)type < FLIGHT_ARRIVAL || (int)type > FLIGHT_DEPARTURE)
        return ARMS_ERR_INVALID_ARG;
    if ((int)category < AIRCRAFT_SMALL || (int)category > AIRCRAFT_LARGE)
        return ARMS_ERR_INVALID_ARG;
    if (!arms_id_valid(id))
        return ARMS_ERR_INVALID_ID;
    if (flight_find(fs, id) != NULL)
        return ARMS_ERR_DUPLICATE;
    if (fs->count >= MAX_FLIGHTS)
        return ARMS_ERR_FULL;

    Flight *f = &fs->items[fs->count];
    snprintf(f->id, sizeof f->id, "%s", id);
    f->type = type;
    f->category = category;
    f->state = STATE_REGISTERED;
    f->emergency = EMERGENCY_NONE;
    f->fuel = FUEL_NORMAL;
    f->request_tick = 0;
    f->request_seq = 0;
    fs->count++;
    return ARMS_OK;
}

Flight *flight_find(FlightStore *fs, const char *id)
{
    if (fs == NULL || id == NULL)
        return NULL;

    for (int i = 0; i < fs->count; i++) {
        if (strcmp(fs->items[i].id, id) == 0)
            return &fs->items[i];
    }
    return NULL;
}

int flight_transition_allowed(FlightState from, FlightState to)
{
    if (!state_in_range(from) || !state_in_range(to))
        return 0;
    return allowed[from][to];
}

ArmsStatus flight_transition(FlightStore *fs, const char *id, FlightState to)
{
    if (fs == NULL || id == NULL || !state_in_range(to))
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (!flight_transition_allowed(f->state, to))
        return ARMS_ERR_INVALID_TRANSITION;

    f->state = to;
    return ARMS_OK;
}

/* Shared checks for the two setters: returns the flight, or NULL with *err set. */
static Flight *editable_flight(FlightStore *fs, const char *id, ArmsStatus *err)
{
    if (fs == NULL || id == NULL) {
        *err = ARMS_ERR_INVALID_ARG;
        return NULL;
    }
    Flight *f = flight_find(fs, id);
    if (f == NULL)
        *err = ARMS_ERR_NOT_FOUND;
    else if (f->state == STATE_COMPLETED || f->state == STATE_CANCELLED) {
        *err = ARMS_ERR_NOT_ELIGIBLE;
        f = NULL;
    }
    return f;
}

ArmsStatus flight_set_emergency(FlightStore *fs, const char *id, EmergencyLevel level)
{
    ArmsStatus err = ARMS_OK;
    if ((int)level < EMERGENCY_NONE || (int)level > EMERGENCY_ACTIVE)
        return ARMS_ERR_INVALID_ARG;
    Flight *f = editable_flight(fs, id, &err);
    if (f == NULL)
        return err;
    f->emergency = level;
    return ARMS_OK;
}

ArmsStatus flight_set_fuel(FlightStore *fs, const char *id, FuelLevel level)
{
    ArmsStatus err = ARMS_OK;
    if ((int)level < FUEL_NORMAL || (int)level > FUEL_CRITICAL)
        return ARMS_ERR_INVALID_ARG;
    Flight *f = editable_flight(fs, id, &err);
    if (f == NULL)
        return err;
    f->fuel = level;
    return ARMS_OK;
}

int flight_priority_higher(const Flight *a, const Flight *b)
{
    if (a == NULL || b == NULL)
        return 0;
    if (a->emergency != b->emergency)
        return a->emergency > b->emergency;
    if (a->fuel != b->fuel)
        return a->fuel > b->fuel;
    if (a->type != b->type)
        return a->type == FLIGHT_ARRIVAL;
    if (a->request_tick != b->request_tick)
        return a->request_tick < b->request_tick;
    return a->request_seq < b->request_seq;
}

int flight_count(const FlightStore *fs)
{
    return fs == NULL ? 0 : fs->count;
}

const Flight *flight_at(const FlightStore *fs, int index)
{
    if (fs == NULL || index < 0 || index >= fs->count)
        return NULL;
    return &fs->items[index];
}

void flight_print(FILE *out, const Flight *f)
{
    if (out == NULL || f == NULL)
        return;
    fprintf(out, "%-15s %-10s %-7s %s\n", f->id, flight_type_name(f->type),
            aircraft_category_name(f->category), flight_state_name(f->state));
}

void flight_list(FILE *out, const FlightStore *fs)
{
    if (out == NULL || fs == NULL)
        return;
    if (fs->count == 0) {
        fprintf(out, "(no flights)\n");
        return;
    }
    fprintf(out, "%-15s %-10s %-7s %s\n", "ID", "TYPE", "SIZE", "STATE");
    for (int i = 0; i < fs->count; i++)
        flight_print(out, &fs->items[i]);
}

const char *flight_state_name(FlightState s)
{
    switch (s) {
    case STATE_REGISTERED:   return "REGISTERED";
    case STATE_WAITING:      return "WAITING";
    case STATE_HOLDING:      return "HOLDING";
    case STATE_SCHEDULED:    return "SCHEDULED";
    case STATE_IN_OPERATION: return "IN_OPERATION";
    case STATE_COMPLETED:    return "COMPLETED";
    case STATE_CANCELLED:    return "CANCELLED";
    default:                 return "UNKNOWN";
    }
}

const char *flight_type_name(FlightType t)
{
    switch (t) {
    case FLIGHT_ARRIVAL:   return "ARRIVAL";
    case FLIGHT_DEPARTURE: return "DEPARTURE";
    default:               return "UNKNOWN";
    }
}

const char *aircraft_category_name(AircraftCategory c)
{
    switch (c) {
    case AIRCRAFT_SMALL:  return "SMALL";
    case AIRCRAFT_MEDIUM: return "MEDIUM";
    case AIRCRAFT_LARGE:  return "LARGE";
    default:              return "UNKNOWN";
    }
}
