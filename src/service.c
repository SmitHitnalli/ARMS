#include "service.h"

#include <string.h>

#include "operations.h"

/* Best-effort: eventlog_append counts a failure in events.dropped, so the result is ignored. */
static void log_event(ArmsSystem *s, EventType type, const char *flight_id,
                      const char *runway_id, const char *details)
{
    (void)eventlog_append(&s->events, s->flights.now, type, flight_id, runway_id, details);
}

/* Runway the flight holds right now ("" if none), copied before the core call clears it. */
static void runway_of(ArmsSystem *s, const char *id, char out[ID_MAX_LEN + 1])
{
    const Flight *f = id != NULL ? flight_find(&s->flights, id) : NULL;
    snprintf(out, ID_MAX_LEN + 1, "%s", f != NULL ? f->runway_id : "");
}

ArmsStatus svc_flight_add(ArmsSystem *s, const char *id, FlightType type, AircraftCategory cat)
{
    ArmsStatus st = flight_add(&s->flights, id, type, cat);
    if (st == ARMS_OK)
        log_event(s, EVENT_FLIGHT_REGISTERED, id, NULL, flight_type_name(type));
    return st;
}

ArmsStatus svc_runway_add(ArmsSystem *s, const char *id, AircraftCategory max_category)
{
    ArmsStatus st = runway_add(&s->runways, id, max_category);
    if (st == ARMS_OK)
        log_event(s, EVENT_RUNWAY_ADDED, NULL, id, aircraft_category_name(max_category));
    return st;
}

ArmsStatus svc_runway_set_status(ArmsSystem *s, const char *id, RunwayStatus to)
{
    ArmsStatus st = runway_set_status(&s->runways, id, to);
    if (st == ARMS_OK && to == RUNWAY_CLOSED)
        log_event(s, EVENT_RUNWAY_CLOSED, NULL, id, runway_status_name(to));
    else if (st == ARMS_OK && to == RUNWAY_AVAILABLE)
        log_event(s, EVENT_RUNWAY_REOPENED, NULL, id, runway_status_name(to));
    return st;
}

ArmsStatus svc_request_submit(ArmsSystem *s, const char *id)
{
    ArmsStatus st = request_submit(&s->requests, &s->flights, id);
    if (st == ARMS_OK) {
        const Flight *f = flight_find(&s->flights, id);
        log_event(s, EVENT_REQUEST_SUBMITTED, id, NULL,
                  f != NULL && f->type == FLIGHT_ARRIVAL ? "LANDING" : "TAKEOFF");
    }
    return st;
}

ArmsStatus svc_hold_enter(ArmsSystem *s, const char *id)
{
    ArmsStatus st = scheduler_hold(&s->scheduler, &s->holding, &s->requests, &s->flights, id);
    if (st == ARMS_OK)
        log_event(s, EVENT_HOLD_ENTERED, id, NULL, NULL);
    return st;
}

ArmsStatus svc_hold_leave(ArmsSystem *s, const char *id)
{
    ArmsStatus st = holding_leave(&s->holding, &s->requests, &s->flights, id);
    if (st == ARMS_OK)
        log_event(s, EVENT_HOLD_LEFT, id, NULL, NULL);
    return st;
}

ArmsStatus svc_set_emergency(ArmsSystem *s, const char *id, EmergencyLevel level)
{
    const Flight *f = id != NULL ? flight_find(&s->flights, id) : NULL;
    int changed = f != NULL && f->emergency != level;
    ArmsStatus st = scheduler_set_emergency(&s->scheduler, &s->flights, id, level);
    if (st == ARMS_OK && changed)
        log_event(s, EVENT_EMERGENCY_CHANGED, id, NULL, flight_emergency_name(level));
    return st;
}

ArmsStatus svc_set_fuel(ArmsSystem *s, const char *id, FuelLevel level)
{
    const Flight *f = id != NULL ? flight_find(&s->flights, id) : NULL;
    int changed = f != NULL && f->fuel != level;
    ArmsStatus st = scheduler_set_fuel(&s->scheduler, &s->flights, id, level);
    if (st == ARMS_OK && changed)
        log_event(s, EVENT_FUEL_CHANGED, id, NULL, flight_fuel_name(level));
    return st;
}

ArmsStatus svc_schedule_next(ArmsSystem *s, char *out_flight_id, char *out_runway_id)
{
    ArmsStatus st = ops_schedule_next(&s->scheduler, &s->flights, &s->runways,
                                      out_flight_id, out_runway_id);
    if (st == ARMS_OK)
        log_event(s, EVENT_FLIGHT_SCHEDULED, out_flight_id, out_runway_id, NULL);
    return st;
}

ArmsStatus svc_start(ArmsSystem *s, const char *id)
{
    char runway[ID_MAX_LEN + 1];
    runway_of(s, id, runway);
    ArmsStatus st = ops_start(&s->flights, &s->runways, id);
    if (st == ARMS_OK)
        log_event(s, EVENT_OP_STARTED, id, runway, NULL);
    return st;
}

ArmsStatus svc_complete(ArmsSystem *s, const char *id)
{
    char runway[ID_MAX_LEN + 1];
    runway_of(s, id, runway);            /* ops_complete clears flight.runway_id */
    ArmsStatus st = ops_complete(&s->flights, &s->runways, &s->history, id);
    if (st == ARMS_OK)
        log_event(s, EVENT_OP_COMPLETED, id, runway, NULL);
    return st;
}

ArmsStatus svc_cancel(ArmsSystem *s, const char *id)
{
    char runway[ID_MAX_LEN + 1];
    const Flight *f = id != NULL ? flight_find(&s->flights, id) : NULL;
    char before[EVENT_DETAILS_LEN];
    runway_of(s, id, runway);            /* only a SCHEDULED flight has one; it is released */
    snprintf(before, sizeof before, "%s", f != NULL ? flight_state_name(f->state) : "");
    ArmsStatus st = ops_cancel(&s->scheduler, &s->holding, &s->requests,
                               &s->flights, &s->runways, id);
    if (st == ARMS_OK)
        log_event(s, EVENT_FLIGHT_CANCELLED, id, runway, before);   /* details = state before */
    return st;
}
