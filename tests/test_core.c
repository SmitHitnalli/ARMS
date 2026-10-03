/* Core tests: plain asserts, no GTK. Build and run with `make test`. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "flight.h"
#include "runway.h"

/* Independent copy of the approved transition table, to check flight.c against. */
static const int expected[STATE_COUNT][STATE_COUNT] = {
    { 0, 1, 0, 0, 0, 0, 1 },
    { 0, 0, 1, 1, 0, 0, 1 },
    { 0, 1, 0, 1, 0, 0, 1 },
    { 0, 0, 0, 0, 1, 0, 0 },
    { 0, 0, 0, 0, 0, 1, 0 },
    { 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }
};

/* Adds a flight and walks it to the wanted state using valid transitions. */
static void add_flight_in_state(FlightStore *fs, const char *id, FlightState target)
{
    assert(flight_add(fs, id, FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    switch (target) {
    case STATE_REGISTERED:
        break;
    case STATE_WAITING:
        assert(flight_transition(fs, id, STATE_WAITING) == ARMS_OK);
        break;
    case STATE_HOLDING:
        assert(flight_transition(fs, id, STATE_WAITING) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_HOLDING) == ARMS_OK);
        break;
    case STATE_SCHEDULED:
        assert(flight_transition(fs, id, STATE_WAITING) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_SCHEDULED) == ARMS_OK);
        break;
    case STATE_IN_OPERATION:
        assert(flight_transition(fs, id, STATE_WAITING) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_SCHEDULED) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_IN_OPERATION) == ARMS_OK);
        break;
    case STATE_COMPLETED:
        assert(flight_transition(fs, id, STATE_WAITING) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_SCHEDULED) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_IN_OPERATION) == ARMS_OK);
        assert(flight_transition(fs, id, STATE_COMPLETED) == ARMS_OK);
        break;
    case STATE_CANCELLED:
        assert(flight_transition(fs, id, STATE_CANCELLED) == ARMS_OK);
        break;
    default:
        assert(0);
    }
    assert(flight_find(fs, id)->state == target);
}

/* tmpfile() needs admin rights on some Windows setups, so use a file in build/. */
#define SCRATCH_FILE "build/test_print.tmp"

/* Reads everything written to f back into buf. */
static void read_back(FILE *f, char *buf, size_t size)
{
    rewind(f);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
}

static void test_ids_and_common(void)
{
    assert(arms_id_valid("AI-204"));
    assert(arms_id_valid("RWY-01"));
    assert(arms_id_valid("A"));
    assert(arms_id_valid("123456789012345"));      /* exactly 15 */

    assert(!arms_id_valid(NULL));
    assert(!arms_id_valid(""));
    assert(!arms_id_valid("1234567890123456"));    /* 16 */
    assert(!arms_id_valid("ai-204"));              /* lowercase */
    assert(!arms_id_valid("AI 204"));
    assert(!arms_id_valid("AI_204"));

    for (int s = ARMS_OK; s <= ARMS_ERR_INCOMPATIBLE; s++)
        assert(strlen(arms_status_str((ArmsStatus)s)) > 0);
}

static void test_flight_add_find(void)
{
    FlightStore fs;
    flight_store_init(&fs);
    assert(flight_count(&fs) == 0);

    assert(flight_add(&fs, "AI-204", FLIGHT_DEPARTURE, AIRCRAFT_LARGE) == ARMS_OK);
    const Flight *f = flight_find(&fs, "AI-204");
    assert(f != NULL);
    assert(f->state == STATE_REGISTERED);
    assert(f->type == FLIGHT_DEPARTURE);
    assert(f->category == AIRCRAFT_LARGE);
    assert(flight_count(&fs) == 1);

    assert(flight_add(&fs, "AI-204", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_DUPLICATE);
    assert(flight_count(&fs) == 1);

    assert(flight_add(&fs, "bad id", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_INVALID_ID);
    assert(flight_add(&fs, NULL, FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_INVALID_ID);
    assert(flight_add(&fs, "", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_INVALID_ID);
    assert(flight_count(&fs) == 1);

    assert(flight_add(&fs, "X1", (FlightType)7, AIRCRAFT_SMALL) == ARMS_ERR_INVALID_ARG);
    assert(flight_add(&fs, "X1", FLIGHT_ARRIVAL, (AircraftCategory)9) == ARMS_ERR_INVALID_ARG);
    assert(flight_add(NULL, "X1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_INVALID_ARG);
    assert(flight_count(&fs) == 1);

    assert(flight_find(&fs, "NOPE") == NULL);
    assert(flight_find(&fs, NULL) == NULL);

    assert(flight_at(&fs, 0) != NULL);
    assert(strcmp(flight_at(&fs, 0)->id, "AI-204") == 0);
    assert(flight_at(&fs, -1) == NULL);
    assert(flight_at(&fs, flight_count(&fs)) == NULL);
}

static void test_flight_capacity(void)
{
    FlightStore fs;
    char id[ID_MAX_LEN + 1];
    flight_store_init(&fs);

    for (int i = 0; i < MAX_FLIGHTS; i++) {
        snprintf(id, sizeof id, "F%d", i);
        assert(flight_add(&fs, id, FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    }
    assert(flight_count(&fs) == MAX_FLIGHTS);
    assert(flight_add(&fs, "EXTRA", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_ERR_FULL);
    assert(flight_count(&fs) == MAX_FLIGHTS);
}

static void test_flight_transitions(void)
{
    FlightStore fs;
    flight_store_init(&fs);

    /* Happy path with the WAITING <-> HOLDING loop. */
    assert(flight_add(&fs, "HAPPY", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_WAITING) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_HOLDING) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_WAITING) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_SCHEDULED) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_IN_OPERATION) == ARMS_OK);
    assert(flight_transition(&fs, "HAPPY", STATE_COMPLETED) == ARMS_OK);

    /* Every table cell, checked against both the query and the real transition. */
    for (int from = 0; from < STATE_COUNT; from++) {
        for (int to = 0; to < STATE_COUNT; to++) {
            FlightStore t;
            flight_store_init(&t);
            add_flight_in_state(&t, "T", (FlightState)from);

            assert(flight_transition_allowed((FlightState)from, (FlightState)to) == expected[from][to]);

            ArmsStatus st = flight_transition(&t, "T", (FlightState)to);
            if (expected[from][to]) {
                assert(st == ARMS_OK);
                assert(flight_find(&t, "T")->state == (FlightState)to);
            } else {
                assert(st == ARMS_ERR_INVALID_TRANSITION);
                assert(flight_find(&t, "T")->state == (FlightState)from);   /* unchanged */
            }
        }
    }

    /* Cancellation rules called out in the design. */
    assert(!flight_transition_allowed(STATE_SCHEDULED, STATE_CANCELLED));
    assert(!flight_transition_allowed(STATE_IN_OPERATION, STATE_CANCELLED));

    /* Bad arguments. */
    assert(flight_transition(&fs, "HAPPY", (FlightState)99) == ARMS_ERR_INVALID_ARG);
    assert(flight_transition(&fs, "HAPPY", (FlightState)-1) == ARMS_ERR_INVALID_ARG);
    assert(flight_transition(&fs, "NOPE", STATE_WAITING) == ARMS_ERR_NOT_FOUND);
    assert(flight_transition(&fs, NULL, STATE_WAITING) == ARMS_ERR_INVALID_ARG);
    assert(flight_transition(NULL, "HAPPY", STATE_WAITING) == ARMS_ERR_INVALID_ARG);
    assert(!flight_transition_allowed((FlightState)99, STATE_WAITING));
}

static void test_runway_add_find_status(void)
{
    RunwayStore rs;
    char id[ID_MAX_LEN + 1];
    runway_store_init(&rs);

    assert(runway_add(&rs, "RWY-01", AIRCRAFT_MEDIUM) == ARMS_OK);
    const Runway *r = runway_find(&rs, "RWY-01");
    assert(r != NULL);
    assert(r->status == RUNWAY_AVAILABLE);
    assert(r->max_category == AIRCRAFT_MEDIUM);
    assert(r->current_flight_id[0] == '\0');

    assert(runway_add(&rs, "RWY-01", AIRCRAFT_LARGE) == ARMS_ERR_DUPLICATE);
    assert(runway_add(&rs, "rwy-02", AIRCRAFT_LARGE) == ARMS_ERR_INVALID_ID);
    assert(runway_add(&rs, NULL, AIRCRAFT_LARGE) == ARMS_ERR_INVALID_ID);
    assert(runway_add(&rs, "RWY-02", (AircraftCategory)9) == ARMS_ERR_INVALID_ARG);
    assert(runway_add(NULL, "RWY-02", AIRCRAFT_LARGE) == ARMS_ERR_INVALID_ARG);
    assert(runway_count(&rs) == 1);

    assert(runway_find(&rs, "NOPE") == NULL);
    assert(runway_find(&rs, NULL) == NULL);
    assert(runway_at(&rs, 0) != NULL);
    assert(runway_at(&rs, -1) == NULL);
    assert(runway_at(&rs, 1) == NULL);

    /* Capacity: RWY-01 already added, fill up to MAX_RUNWAYS. */
    for (int i = 1; i < MAX_RUNWAYS; i++) {
        snprintf(id, sizeof id, "R%d", i);
        assert(runway_add(&rs, id, AIRCRAFT_SMALL) == ARMS_OK);
    }
    assert(runway_count(&rs) == MAX_RUNWAYS);
    assert(runway_add(&rs, "EXTRA", AIRCRAFT_SMALL) == ARMS_ERR_FULL);

    /* AVAILABLE <-> CLOSED only. */
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_CLOSED) == ARMS_OK);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_CLOSED);
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_AVAILABLE) == ARMS_OK);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_AVAILABLE);

    assert(runway_set_status(&rs, "RWY-01", RUNWAY_OCCUPIED) == ARMS_ERR_INVALID_ARG);
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_AVAILABLE) == ARMS_ERR_INVALID_ARG);   /* same status */
    assert(runway_set_status(&rs, "NOPE", RUNWAY_CLOSED) == ARMS_ERR_NOT_FOUND);
    assert(runway_set_status(&rs, "RWY-01", (RunwayStatus)9) == ARMS_ERR_INVALID_ARG);
}

static void test_runway_assign_release(void)
{
    FlightStore fs;
    RunwayStore rs;
    flight_store_init(&fs);
    runway_store_init(&rs);

    assert(runway_add(&rs, "RWY-01", AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(runway_add(&rs, "RWY-02", AIRCRAFT_LARGE) == ARMS_OK);

    add_flight_in_state(&fs, "WAIT1", STATE_WAITING);
    add_flight_in_state(&fs, "HOLD1", STATE_HOLDING);
    add_flight_in_state(&fs, "WAIT2", STATE_WAITING);

    /* WAITING flight -> AVAILABLE compatible runway. Flight must not change. */
    assert(runway_assign(&rs, "RWY-01", flight_find(&fs, "WAIT1")) == ARMS_OK);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_OCCUPIED);
    assert(strcmp(runway_find(&rs, "RWY-01")->current_flight_id, "WAIT1") == 0);
    assert(flight_find(&fs, "WAIT1")->state == STATE_WAITING);

    /* Occupied runway cannot be assigned again; original holder is kept. */
    assert(runway_assign(&rs, "RWY-01", flight_find(&fs, "WAIT2")) == ARMS_ERR_RUNWAY_OCCUPIED);
    assert(strcmp(runway_find(&rs, "RWY-01")->current_flight_id, "WAIT1") == 0);

    /* Cannot close an occupied runway, nor "open" it via set_status. */
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_CLOSED) == ARMS_ERR_RUNWAY_OCCUPIED);
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_AVAILABLE) == ARMS_ERR_RUNWAY_OCCUPIED);

    /* Release, then reassign (HOLDING flight is also eligible). */
    assert(runway_release(&rs, "RWY-01") == ARMS_OK);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_AVAILABLE);
    assert(runway_find(&rs, "RWY-01")->current_flight_id[0] == '\0');
    assert(runway_assign(&rs, "RWY-01", flight_find(&fs, "HOLD1")) == ARMS_OK);
    assert(runway_release(&rs, "RWY-01") == ARMS_OK);

    /* Releasing when not occupied. */
    assert(runway_release(&rs, "RWY-01") == ARMS_ERR_NOT_OCCUPIED);
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_CLOSED) == ARMS_OK);
    assert(runway_release(&rs, "RWY-01") == ARMS_ERR_NOT_OCCUPIED);

    /* Closed runway. A failed assign leaves the runway unchanged. */
    assert(runway_assign(&rs, "RWY-01", flight_find(&fs, "WAIT2")) == ARMS_ERR_RUNWAY_CLOSED);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_CLOSED);
    assert(runway_find(&rs, "RWY-01")->current_flight_id[0] == '\0');

    /* Ineligible flight states. */
    add_flight_in_state(&fs, "REG1", STATE_REGISTERED);
    add_flight_in_state(&fs, "SCH1", STATE_SCHEDULED);
    add_flight_in_state(&fs, "OPS1", STATE_IN_OPERATION);
    add_flight_in_state(&fs, "DON1", STATE_COMPLETED);
    add_flight_in_state(&fs, "CAN1", STATE_CANCELLED);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "REG1")) == ARMS_ERR_NOT_ELIGIBLE);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "SCH1")) == ARMS_ERR_NOT_ELIGIBLE);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "OPS1")) == ARMS_ERR_NOT_ELIGIBLE);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "DON1")) == ARMS_ERR_NOT_ELIGIBLE);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "CAN1")) == ARMS_ERR_NOT_ELIGIBLE);
    assert(runway_find(&rs, "RWY-02")->status == RUNWAY_AVAILABLE);

    /* Category compatibility: category <= runway max. */
    assert(flight_add(&fs, "BIG", FLIGHT_ARRIVAL, AIRCRAFT_LARGE) == ARMS_OK);
    assert(flight_add(&fs, "SMALL", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_transition(&fs, "BIG", STATE_WAITING) == ARMS_OK);
    assert(flight_transition(&fs, "SMALL", STATE_WAITING) == ARMS_OK);
    assert(runway_set_status(&rs, "RWY-01", RUNWAY_AVAILABLE) == ARMS_OK);
    assert(!runway_is_compatible(runway_find(&rs, "RWY-01"), flight_find(&fs, "BIG")));
    assert(runway_is_compatible(runway_find(&rs, "RWY-01"), flight_find(&fs, "SMALL")));
    assert(runway_is_compatible(runway_find(&rs, "RWY-01"), flight_find(&fs, "WAIT1")));   /* MEDIUM */
    assert(runway_assign(&rs, "RWY-01", flight_find(&fs, "BIG")) == ARMS_ERR_INCOMPATIBLE);
    assert(runway_find(&rs, "RWY-01")->status == RUNWAY_AVAILABLE);
    assert(runway_assign(&rs, "RWY-02", flight_find(&fs, "BIG")) == ARMS_OK);

    /* Bad arguments. */
    assert(runway_assign(&rs, "NOPE", flight_find(&fs, "SMALL")) == ARMS_ERR_NOT_FOUND);
    assert(runway_assign(&rs, "RWY-01", NULL) == ARMS_ERR_INVALID_ARG);
    assert(runway_assign(&rs, NULL, flight_find(&fs, "SMALL")) == ARMS_ERR_INVALID_ARG);
    assert(runway_assign(NULL, "RWY-01", flight_find(&fs, "SMALL")) == ARMS_ERR_INVALID_ARG);
    assert(runway_release(&rs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(runway_release(NULL, "RWY-01") == ARMS_ERR_INVALID_ARG);
}

static void test_printing(void)
{
    FlightStore fs;
    RunwayStore rs;
    char buf[1024];
    FILE *f = fopen(SCRATCH_FILE, "w+");
    assert(f != NULL);

    flight_store_init(&fs);
    runway_store_init(&rs);

    /* Empty stores print without crashing. */
    flight_list(f, &fs);
    runway_list(f, &rs);
    read_back(f, buf, sizeof buf);
    assert(strstr(buf, "(no flights)") != NULL);
    assert(strstr(buf, "(no runways)") != NULL);

    assert(flight_add(&fs, "AI-204", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(runway_add(&rs, "RWY-01", AIRCRAFT_LARGE) == ARMS_OK);

    fclose(f);
    f = fopen(SCRATCH_FILE, "w+");     /* start with an empty file */
    assert(f != NULL);
    flight_print(f, flight_at(&fs, 0));
    flight_list(f, &fs);
    runway_print(f, runway_at(&rs, 0));
    runway_list(f, &rs);
    read_back(f, buf, sizeof buf);
    assert(strstr(buf, "AI-204") != NULL);
    assert(strstr(buf, "REGISTERED") != NULL);
    assert(strstr(buf, "RWY-01") != NULL);
    assert(strstr(buf, "AVAILABLE") != NULL);

    /* NULL arguments are ignored. */
    flight_print(f, NULL);
    runway_print(NULL, runway_at(&rs, 0));
    flight_list(f, NULL);
    runway_list(f, NULL);

    fclose(f);
    remove(SCRATCH_FILE);
}

int main(void)
{
    test_ids_and_common();
    test_flight_add_find();
    test_flight_capacity();
    test_flight_transitions();
    test_runway_add_find_status();
    test_runway_assign_release();
    test_printing();

    printf("All core tests passed.\n");
    return 0;
}
