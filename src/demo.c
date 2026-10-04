#include "demo.h"
#include "service.h"

/* Runs one step; on failure frees what was built so far and gives up. */
#define STEP(call) do { ArmsStatus st_ = (call); \
    if (st_ != ARMS_OK) { arms_system_clear(out); return st_; } } while (0)

static ArmsStatus build_graph(AirportGraph *g)
{
    static const char *const nodes[][2] = {
        {"TERMINAL", "Terminal"}, {"APRON", "Apron"}, {"HOLD-A", "Holding point A"},
        {"TWY-A", "Taxiway A"}, {"TWY-B", "Taxiway B"},
        {"RWY-01-ENT", "Runway 01 entry"}, {"RWY-02-ENT", "Runway 02 entry"},
        {"MAINT", "Maintenance (isolated)"}
    };
    static const char *const edges[][2] = {
        {"TERMINAL", "APRON"}, {"APRON", "TWY-A"}, {"TWY-A", "HOLD-A"}, {"TWY-A", "TWY-B"},
        {"TWY-B", "RWY-01-ENT"}, {"HOLD-A", "RWY-02-ENT"}, {"TWY-B", "RWY-02-ENT"}
    };
    for (size_t i = 0; i < sizeof nodes / sizeof nodes[0]; i++) {
        ArmsStatus st = graph_add_node(g, nodes[i][0], nodes[i][1]);
        if (st != ARMS_OK)
            return st;
    }
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        ArmsStatus st = graph_add_edge(g, edges[i][0], edges[i][1]);
        if (st != ARMS_OK)
            return st;
    }
    return ARMS_OK;
}

ArmsStatus demo_build(ArmsSystem *out)
{
    static const struct { const char *id; FlightType type; AircraftCategory cat; } flights[] = {
        {"AI101", FLIGHT_ARRIVAL,   AIRCRAFT_LARGE},
        {"BA202", FLIGHT_ARRIVAL,   AIRCRAFT_LARGE},
        {"AF150", FLIGHT_ARRIVAL,   AIRCRAFT_MEDIUM},
        {"KL330", FLIGHT_ARRIVAL,   AIRCRAFT_SMALL},
        {"SQ610", FLIGHT_DEPARTURE, AIRCRAFT_MEDIUM},
        {"EK505", FLIGHT_DEPARTURE, AIRCRAFT_LARGE},
        {"LH720", FLIGHT_ARRIVAL,   AIRCRAFT_LARGE},
        {"DL410", FLIGHT_ARRIVAL,   AIRCRAFT_MEDIUM},
        {"TK150", FLIGHT_DEPARTURE, AIRCRAFT_SMALL},
        {"UA901", FLIGHT_DEPARTURE, AIRCRAFT_MEDIUM},
        {"QR077", FLIGHT_ARRIVAL,   AIRCRAFT_SMALL}
    };
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];

    arms_system_init(out);

    STEP(svc_runway_add(out, "RWY-01", AIRCRAFT_LARGE));
    STEP(svc_runway_add(out, "RWY-02", AIRCRAFT_MEDIUM));
    STEP(svc_runway_add(out, "RWY-03", AIRCRAFT_SMALL));
    STEP(svc_runway_set_status(out, "RWY-03", RUNWAY_CLOSED));

    for (size_t i = 0; i < sizeof flights / sizeof flights[0]; i++)
        STEP(svc_flight_add(out, flights[i].id, flights[i].type, flights[i].cat));

    /* AI101 runs through the whole pipeline first: it is the completed movement. */
    STEP(svc_request_submit(out, "AI101"));
    STEP(scheduler_pull(&out->scheduler, &out->requests, &out->flights));
    STEP(svc_schedule_next(out, f, r));
    STEP(svc_start(out, f));
    STEP(svc_complete(out, f));

    /* Five flights end up in the heap. */
    const char *heap_ids[] = {"BA202", "AF150", "KL330", "SQ610", "EK505"};
    for (int i = 0; i < 5; i++)
        STEP(svc_request_submit(out, heap_ids[i]));
    STEP(scheduler_pull(&out->scheduler, &out->requests, &out->flights));
    STEP(svc_set_fuel(out, "BA202", FUEL_CRITICAL));
    STEP(svc_set_emergency(out, "AF150", EMERGENCY_ACTIVE));
    STEP(svc_set_fuel(out, "KL330", FUEL_LOW));

    /* LH720 waits in the landing queue, then is parked in the holding queue. */
    STEP(svc_request_submit(out, "LH720"));
    STEP(svc_hold_enter(out, "LH720"));

    /* These two stay in the normal queues (not pulled): the live demo pulls them. */
    STEP(svc_request_submit(out, "DL410"));
    STEP(svc_request_submit(out, "TK150"));
    /* UA901 and QR077 stay REGISTERED. */

    STEP(build_graph(&out->graph));
    return ARMS_OK;
}
