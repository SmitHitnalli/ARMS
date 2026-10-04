#ifndef SERVICE_H
#define SERVICE_H

#include "system.h"

/* Service layer: the frontend's single entry point for actions that must be audited.
 * Each svc_* function calls the matching core function unchanged and, only if it returned
 * ARMS_OK, appends one event to sys->events. The core result is always returned as is:
 * logging is best-effort, so a failed event allocation (counted in events.dropped) never
 * undoes or fails a valid operation. Failed calls and no-op changes log nothing.
 * scheduler_pull is deliberately not wrapped: it moves flights between structures but is
 * not an operational decision. No GTK here. */

ArmsStatus svc_flight_add(ArmsSystem *s, const char *id, FlightType type, AircraftCategory cat);
ArmsStatus svc_runway_add(ArmsSystem *s, const char *id, AircraftCategory max_category);
ArmsStatus svc_runway_set_status(ArmsSystem *s, const char *id, RunwayStatus to);

ArmsStatus svc_request_submit(ArmsSystem *s, const char *id);
ArmsStatus svc_hold_enter(ArmsSystem *s, const char *id);   /* scheduler_hold: heap or queue */
ArmsStatus svc_hold_leave(ArmsSystem *s, const char *id);

/* Logged only when the value actually changes. */
ArmsStatus svc_set_emergency(ArmsSystem *s, const char *id, EmergencyLevel level);
ArmsStatus svc_set_fuel(ArmsSystem *s, const char *id, FuelLevel level);

/* ops_schedule_next: the caller still runs scheduler_pull first, as with the core call. */
ArmsStatus svc_schedule_next(ArmsSystem *s, char *out_flight_id, char *out_runway_id);
ArmsStatus svc_start(ArmsSystem *s, const char *id);
ArmsStatus svc_complete(ArmsSystem *s, const char *id);
ArmsStatus svc_cancel(ArmsSystem *s, const char *id);

#endif
