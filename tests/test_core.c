/* Core tests: plain asserts, no GTK. Build and run with `make test`. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "flight.h"
#include "runway.h"
#include "queue.h"
#include "request.h"
#include "cqueue.h"
#include "holding.h"
#include "heap.h"
#include "scheduler.h"

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

/* Enqueues "<prefix><n>" for n in [from, to). */
static void fill_queue(Queue *q, const char *prefix, int from, int to)
{
    char id[ID_MAX_LEN + 1];
    for (int n = from; n < to; n++) {
        snprintf(id, sizeof id, "%s%d", prefix, n);
        assert(queue_enqueue(q, id) == ARMS_OK);
    }
}

static void test_queue_basic(void)
{
    Queue q;
    char out[ID_MAX_LEN + 1] = "keep";

    queue_init(&q);
    assert(queue_is_empty(&q) && !queue_is_full(&q) && queue_count(&q) == 0);
    assert(queue_front(&q) == NULL && queue_at(&q, 0) == NULL);
    assert(q.front == 0 && q.rear == 0);

    /* Empty queue: dequeue fails and leaves the output buffer alone. */
    assert(queue_dequeue(&q, out) == ARMS_ERR_EMPTY);
    assert(strcmp(out, "keep") == 0);

    /* FIFO order. */
    assert(queue_enqueue(&q, "A-1") == ARMS_OK);
    assert(queue_enqueue(&q, "B-2") == ARMS_OK);
    assert(queue_enqueue(&q, "C-3") == ARMS_OK);
    assert(queue_count(&q) == 3 && q.rear == 3);
    assert(strcmp(queue_front(&q), "A-1") == 0);
    assert(queue_front(&q) == queue_at(&q, 0));
    assert(strcmp(queue_at(&q, 2), "C-3") == 0);
    assert(queue_at(&q, 3) == NULL && queue_at(&q, -1) == NULL);
    assert(queue_contains(&q, "B-2") && !queue_contains(&q, "Z-9"));

    /* Partial dequeue: front advances, indexing starts at the new front. */
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "A-1") == 0);
    assert(q.front == 1 && q.rear == 3 && queue_count(&q) == 2);
    assert(strcmp(queue_at(&q, 0), "B-2") == 0);
    assert(!queue_contains(&q, "A-1"));

    /* Draining resets front and rear. */
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "B-2") == 0);
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "C-3") == 0);
    assert(queue_is_empty(&q) && q.front == 0 && q.rear == 0);
    assert(queue_enqueue(&q, "D-4") == ARMS_OK);
    assert(q.front == 0 && q.rear == 1 && strcmp(queue_front(&q), "D-4") == 0);

    /* Invalid IDs are rejected without changing the queue. */
    assert(queue_enqueue(&q, "bad id") == ARMS_ERR_INVALID_ID);
    assert(queue_enqueue(&q, "") == ARMS_ERR_INVALID_ID);
    assert(queue_enqueue(&q, "ABCDEFGHIJKLMNOP") == ARMS_ERR_INVALID_ID);   /* 16 chars */
    assert(queue_count(&q) == 1);

    /* NULL arguments. */
    assert(queue_enqueue(NULL, "A-1") == ARMS_ERR_INVALID_ARG);
    assert(queue_enqueue(&q, NULL) == ARMS_ERR_INVALID_ARG);
    assert(queue_dequeue(NULL, out) == ARMS_ERR_INVALID_ARG);
    assert(queue_dequeue(&q, NULL) == ARMS_ERR_INVALID_ARG);
    assert(queue_front(NULL) == NULL && queue_at(NULL, 0) == NULL);
    assert(queue_is_empty(NULL) && !queue_is_full(NULL) && queue_count(NULL) == 0);
    assert(!queue_contains(NULL, "A-1") && !queue_contains(&q, NULL));
    queue_init(NULL);
    queue_print(NULL, &q);
    queue_print(stdout, NULL);
    assert(queue_count(&q) == 1);
}

static void test_queue_full_and_compaction(void)
{
    Queue q;
    char out[ID_MAX_LEN + 1];

    queue_init(&q);
    fill_queue(&q, "Q-", 0, QUEUE_CAPACITY);
    assert(queue_is_full(&q) && q.rear == QUEUE_CAPACITY);

    /* Truly full: front is 0, so there is nothing to reclaim. */
    assert(queue_enqueue(&q, "X-1") == ARMS_ERR_FULL);
    assert(queue_count(&q) == QUEUE_CAPACITY && !queue_contains(&q, "X-1"));

    /* Free 3 slots at the front. rear is still at the end, but the space is reusable. */
    for (int i = 0; i < 3; i++)
        assert(queue_dequeue(&q, out) == ARMS_OK);
    assert(q.front == 3 && q.rear == QUEUE_CAPACITY && !queue_is_full(&q));

    /* Enqueue compacts: front/rear reset and FIFO order is preserved. */
    assert(queue_enqueue(&q, "NEW-1") == ARMS_OK);
    assert(q.front == 0 && q.rear == QUEUE_CAPACITY - 2 && queue_count(&q) == QUEUE_CAPACITY - 2);
    assert(strcmp(queue_front(&q), "Q-3") == 0);
    assert(strcmp(queue_at(&q, 1), "Q-4") == 0);
    assert(strcmp(queue_at(&q, QUEUE_CAPACITY - 4), "Q-99") == 0);
    assert(strcmp(queue_at(&q, QUEUE_CAPACITY - 3), "NEW-1") == 0);

    /* The other two freed slots are usable too, then the queue is full again. */
    assert(queue_enqueue(&q, "NEW-2") == ARMS_OK);
    assert(queue_enqueue(&q, "NEW-3") == ARMS_OK);
    assert(queue_is_full(&q));
    assert(queue_enqueue(&q, "NEW-4") == ARMS_ERR_FULL);

    /* Full drain returns every ID in original FIFO order. */
    for (int n = 3; n < QUEUE_CAPACITY; n++) {
        char want[ID_MAX_LEN + 1];
        snprintf(want, sizeof want, "Q-%d", n);
        assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, want) == 0);
    }
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "NEW-1") == 0);
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "NEW-2") == 0);
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "NEW-3") == 0);
    assert(queue_is_empty(&q) && q.front == 0 && q.rear == 0);
}

static void test_queue_print(void)
{
    Queue q;
    char buf[256];
    FILE *f = fopen(SCRATCH_FILE, "w+");
    assert(f != NULL);

    queue_init(&q);
    queue_print(f, &q);
    read_back(f, buf, sizeof buf);
    assert(strstr(buf, "(queue empty)") != NULL);

    fclose(f);
    f = fopen(SCRATCH_FILE, "w+");
    assert(f != NULL);
    assert(queue_enqueue(&q, "AI-1") == ARMS_OK);
    assert(queue_enqueue(&q, "AI-2") == ARMS_OK);
    queue_print(f, &q);
    read_back(f, buf, sizeof buf);
    assert(strstr(buf, "1. AI-1") != NULL && strstr(buf, "2. AI-2") != NULL);

    fclose(f);
    remove(SCRATCH_FILE);
}

/* Snapshot of a queue's bookkeeping, to prove a failed call changed nothing. */
static int same_queue_state(const Queue *a, const Queue *b)
{
    return a->front == b->front && a->rear == b->rear && a->count == b->count;
}

static void test_request_submit(void)
{
    FlightStore fs;
    RequestQueues rq;
    char out[ID_MAX_LEN + 1];

    flight_store_init(&fs);
    request_init(&rq);
    assert(flight_add(&fs, "ARR-1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_add(&fs, "ARR-2", FLIGHT_ARRIVAL, AIRCRAFT_LARGE) == ARMS_OK);
    assert(flight_add(&fs, "DEP-1", FLIGHT_DEPARTURE, AIRCRAFT_MEDIUM) == ARMS_OK);

    /* Routing by type, with REGISTERED -> WAITING. */
    assert(request_submit(&rq, &fs, "ARR-1") == ARMS_OK);
    assert(queue_count(&rq.landing) == 1 && queue_count(&rq.takeoff) == 0);
    assert(flight_find(&fs, "ARR-1")->state == STATE_WAITING);
    assert(request_submit(&rq, &fs, "DEP-1") == ARMS_OK);
    assert(queue_count(&rq.landing) == 1 && queue_count(&rq.takeoff) == 1);
    assert(strcmp(queue_front(&rq.takeoff), "DEP-1") == 0);
    assert(flight_find(&fs, "DEP-1")->state == STATE_WAITING);

    /* FIFO across submissions. */
    assert(request_submit(&rq, &fs, "ARR-2") == ARMS_OK);
    assert(strcmp(queue_at(&rq.landing, 0), "ARR-1") == 0);
    assert(strcmp(queue_at(&rq.landing, 1), "ARR-2") == 0);

    /* Reference behaviour: the queue holds a copy of the ID, resolved via the store. */
    Flight *f = flight_find(&fs, "ARR-1");
    assert(queue_front(&rq.landing) != f->id);
    assert(strcmp(queue_front(&rq.landing), f->id) == 0);
    assert(flight_transition(&fs, "ARR-1", STATE_HOLDING) == ARMS_OK);
    assert(flight_find(&fs, queue_front(&rq.landing)) == f && f->state == STATE_HOLDING);
    assert(queue_dequeue(&rq.landing, out) == ARMS_OK && strcmp(out, "ARR-1") == 0);
    assert(flight_find(&fs, out) == f);

    /* Duplicate: a second submit changes nothing. */
    RequestQueues before = rq;
    assert(request_submit(&rq, &fs, "DEP-1") == ARMS_ERR_DUPLICATE);
    assert(same_queue_state(&rq.takeoff, &before.takeoff));
    assert(same_queue_state(&rq.landing, &before.landing));
    assert(flight_find(&fs, "DEP-1")->state == STATE_WAITING);

    /* Unknown flight and NULL arguments. */
    before = rq;
    assert(request_submit(&rq, &fs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(request_submit(NULL, &fs, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(request_submit(&rq, NULL, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(request_submit(&rq, &fs, NULL) == ARMS_ERR_INVALID_ARG);
    assert(same_queue_state(&rq.landing, &before.landing));
    assert(same_queue_state(&rq.takeoff, &before.takeoff));
    request_init(NULL);
}

static void test_request_rejects_non_registered(void)
{
    FlightStore fs;
    RequestQueues rq;
    char id[ID_MAX_LEN + 1];

    flight_store_init(&fs);
    request_init(&rq);

    /* One flight per non-REGISTERED state (HOLDING included: HOLDING -> WAITING is not a request). */
    for (int s = STATE_WAITING; s < STATE_COUNT; s++) {
        snprintf(id, sizeof id, "ST-%d", s);
        add_flight_in_state(&fs, id, (FlightState)s);
        assert(request_submit(&rq, &fs, id) == ARMS_ERR_INVALID_TRANSITION);
        assert(flight_find(&fs, id)->state == (FlightState)s);
    }
    assert(queue_is_empty(&rq.landing) && queue_is_empty(&rq.takeoff));
    assert(rq.landing.rear == 0 && rq.takeoff.rear == 0);
}

static void test_request_full_queue(void)
{
    FlightStore fs;
    RequestQueues rq;

    flight_store_init(&fs);
    request_init(&rq);
    fill_queue(&rq.landing, "Q-", 0, QUEUE_CAPACITY);
    assert(flight_add(&fs, "ARR-1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_add(&fs, "DEP-1", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);

    /* Full landing queue: the flight stays REGISTERED and nothing is queued. */
    RequestQueues before = rq;
    assert(request_submit(&rq, &fs, "ARR-1") == ARMS_ERR_FULL);
    assert(flight_find(&fs, "ARR-1")->state == STATE_REGISTERED);
    assert(same_queue_state(&rq.landing, &before.landing));
    assert(!queue_contains(&rq.landing, "ARR-1"));

    /* The takeoff queue is independent and still accepts requests. */
    assert(request_submit(&rq, &fs, "DEP-1") == ARMS_OK);
    assert(flight_find(&fs, "DEP-1")->state == STATE_WAITING);

    /* Once a landing slot frees up (via compaction), the arrival can be queued. */
    char out[ID_MAX_LEN + 1];
    assert(queue_dequeue(&rq.landing, out) == ARMS_OK);
    assert(request_submit(&rq, &fs, "ARR-1") == ARMS_OK);
    assert(strcmp(queue_at(&rq.landing, QUEUE_CAPACITY - 1), "ARR-1") == 0);
    assert(flight_find(&fs, "ARR-1")->state == STATE_WAITING);
}

/* ---- Phase 4: queue_remove, circular queue, holding ---- */

static void test_queue_remove(void)
{
    Queue q;
    char out[ID_MAX_LEN + 1];

    queue_init(&q);
    assert(queue_remove(&q, "A") == ARMS_ERR_NOT_FOUND);
    assert(queue_remove(NULL, "A") == ARMS_ERR_INVALID_ARG);
    assert(queue_remove(&q, NULL) == ARMS_ERR_INVALID_ARG);

    queue_enqueue(&q, "A");
    queue_enqueue(&q, "B");
    queue_enqueue(&q, "C");
    queue_enqueue(&q, "D");
    assert(queue_remove(&q, "B") == ARMS_OK);       /* middle */
    assert(queue_remove(&q, "B") == ARMS_ERR_NOT_FOUND);
    assert(queue_remove(&q, "D") == ARMS_OK);       /* last */
    assert(queue_count(&q) == 2);
    assert(strcmp(queue_at(&q, 0), "A") == 0 && strcmp(queue_at(&q, 1), "C") == 0);
    assert(queue_remove(&q, "A") == ARMS_OK);       /* front */
    assert(queue_dequeue(&q, out) == ARMS_OK && strcmp(out, "C") == 0);
    assert(queue_is_empty(&q));

    /* Still usable afterwards, and removal works after the front has moved. */
    queue_enqueue(&q, "E");
    queue_enqueue(&q, "F");
    queue_dequeue(&q, out);
    queue_enqueue(&q, "G");
    assert(queue_remove(&q, "G") == ARMS_OK);
    assert(queue_count(&q) == 1 && strcmp(queue_front(&q), "F") == 0);
}

static void test_cqueue_basic(void)
{
    CircularQueue q;
    char out[ID_MAX_LEN + 1];

    cqueue_init(&q);
    assert(cqueue_is_empty(&q) && !cqueue_is_full(&q));
    assert(cqueue_front(&q) == NULL);
    assert(cqueue_dequeue(&q, out) == ARMS_ERR_EMPTY);

    assert(cqueue_enqueue(&q, "A") == ARMS_OK);
    assert(cqueue_enqueue(&q, "B") == ARMS_OK);
    assert(cqueue_enqueue(&q, "bad id") == ARMS_ERR_INVALID_ID);
    assert(cqueue_enqueue(&q, NULL) == ARMS_ERR_INVALID_ARG);
    assert(cqueue_enqueue(NULL, "C") == ARMS_ERR_INVALID_ARG);
    assert(cqueue_dequeue(&q, NULL) == ARMS_ERR_INVALID_ARG);
    assert(cqueue_count(&q) == 2 && cqueue_contains(&q, "B") && !cqueue_contains(&q, "C"));

    assert(cqueue_dequeue(&q, out) == ARMS_OK && strcmp(out, "A") == 0);
    assert(cqueue_dequeue(&q, out) == ARMS_OK && strcmp(out, "B") == 0);
    assert(cqueue_is_empty(&q));
    assert(cqueue_at(&q, 0) == NULL);
}

static void test_cqueue_wraparound(void)
{
    CircularQueue q;
    char id[ID_MAX_LEN + 1];
    char out[ID_MAX_LEN + 1];

    cqueue_init(&q);
    for (int i = 0; i < CQUEUE_CAPACITY; i++) {
        snprintf(id, sizeof id, "W-%d", i);
        assert(cqueue_enqueue(&q, id) == ARMS_OK);
    }
    assert(cqueue_is_full(&q));
    assert(cqueue_enqueue(&q, "EXTRA") == ARMS_ERR_FULL);
    assert(q.rear == 0);                            /* rear wrapped to the start */

    /* Free 3 slots at the front, then refill: the new items wrap around. */
    for (int i = 0; i < 3; i++) {
        assert(cqueue_dequeue(&q, out) == ARMS_OK);
        snprintf(id, sizeof id, "W-%d", i);
        assert(strcmp(out, id) == 0);
    }
    for (int i = 0; i < 3; i++) {
        snprintf(id, sizeof id, "N-%d", i);
        assert(cqueue_enqueue(&q, id) == ARMS_OK);
    }
    assert(cqueue_is_full(&q) && q.rear == 3 && q.front == 3);
    assert(strcmp(cqueue_at(&q, 0), "W-3") == 0);
    assert(strcmp(cqueue_at(&q, CQUEUE_CAPACITY - 1), "N-2") == 0);
    assert(cqueue_contains(&q, "N-0") && !cqueue_contains(&q, "W-0"));

    /* Remove from the wrapped part and from the front; order must be preserved. */
    assert(cqueue_remove(&q, "N-1") == ARMS_OK);
    assert(cqueue_remove(&q, "W-3") == ARMS_OK);
    assert(cqueue_remove(&q, "W-3") == ARMS_ERR_NOT_FOUND);
    assert(cqueue_remove(&q, NULL) == ARMS_ERR_INVALID_ARG);
    assert(cqueue_count(&q) == CQUEUE_CAPACITY - 2);
    assert(strcmp(cqueue_at(&q, 0), "W-4") == 0);
    assert(strcmp(cqueue_at(&q, CQUEUE_CAPACITY - 4), "N-0") == 0);
    assert(strcmp(cqueue_at(&q, CQUEUE_CAPACITY - 3), "N-2") == 0);

    /* Drain: strict FIFO order across the wrap point. */
    for (int i = 4; i < CQUEUE_CAPACITY; i++) {
        snprintf(id, sizeof id, "W-%d", i);
        assert(cqueue_dequeue(&q, out) == ARMS_OK && strcmp(out, id) == 0);
    }
    assert(cqueue_dequeue(&q, out) == ARMS_OK && strcmp(out, "N-0") == 0);
    assert(cqueue_dequeue(&q, out) == ARMS_OK && strcmp(out, "N-2") == 0);
    assert(cqueue_is_empty(&q));
}

/* Registers a flight and submits it, so it is WAITING and in its normal queue. */
static void make_waiting(FlightStore *fs, RequestQueues *rq, const char *id, FlightType type)
{
    assert(flight_add(fs, id, type, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(request_submit(rq, fs, id) == ARMS_OK);
}

/* 1 if id is in the holding queue or a normal queue, counted: used to prove "exactly one". */
static int where_count(const HoldingQueue *h, const RequestQueues *rq, const char *id)
{
    return cqueue_contains(&h->queue, id) + queue_contains(&rq->landing, id)
         + queue_contains(&rq->takeoff, id);
}

static void test_holding_enter_leave(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue h;

    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&h);
    make_waiting(&fs, &rq, "ARR-1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "DEP-1", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "ARR-2", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "ARR-3", FLIGHT_ARRIVAL);

    /* Enter from the middle of the landing queue: removed there, FIFO order kept. */
    assert(holding_enter(&h, &rq, &fs, "ARR-2") == ARMS_OK);
    assert(flight_find(&fs, "ARR-2")->state == STATE_HOLDING);
    assert(!queue_contains(&rq.landing, "ARR-2") && !queue_contains(&rq.takeoff, "ARR-2"));
    assert(cqueue_contains(&h.queue, "ARR-2") && holding_count(&h) == 1);
    assert(queue_count(&rq.landing) == 2);
    assert(strcmp(queue_at(&rq.landing, 0), "ARR-1") == 0);
    assert(strcmp(queue_at(&rq.landing, 1), "ARR-3") == 0);

    /* A departure is taken from the takeoff queue, not the landing queue. */
    assert(holding_enter(&h, &rq, &fs, "DEP-1") == ARMS_OK);
    assert(queue_is_empty(&rq.takeoff) && queue_count(&rq.landing) == 2);
    assert(holding_count(&h) == 2);

    /* Duplicate entry is rejected. */
    assert(holding_enter(&h, &rq, &fs, "ARR-2") == ARMS_ERR_DUPLICATE);

    /* Leaving puts the flight at the REAR of the correct normal queue. */
    assert(holding_leave(&h, &rq, &fs, "ARR-2") == ARMS_OK);
    assert(flight_find(&fs, "ARR-2")->state == STATE_WAITING);
    assert(queue_count(&rq.landing) == 3);
    assert(strcmp(queue_at(&rq.landing, 0), "ARR-1") == 0);
    assert(strcmp(queue_at(&rq.landing, 1), "ARR-3") == 0);
    assert(strcmp(queue_at(&rq.landing, 2), "ARR-2") == 0);
    assert(queue_is_empty(&rq.takeoff));

    assert(holding_leave(&h, &rq, &fs, "DEP-1") == ARMS_OK);
    assert(queue_count(&rq.takeoff) == 1 && strcmp(queue_front(&rq.takeoff), "DEP-1") == 0);
    assert(!queue_contains(&rq.landing, "DEP-1"));
    assert(holding_count(&h) == 0);

    /* A flight is in exactly one place at all times; enter again after leaving works. */
    const char *all[] = { "ARR-1", "DEP-1", "ARR-2", "ARR-3" };
    for (int i = 0; i < 4; i++)
        assert(where_count(&h, &rq, all[i]) == 1);
    assert(holding_enter(&h, &rq, &fs, "ARR-1") == ARMS_OK);
    for (int i = 0; i < 4; i++)
        assert(where_count(&h, &rq, all[i]) == 1);
    assert(flight_find(&fs, "ARR-1")->state == STATE_HOLDING);
}

static void test_holding_enter_failures(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue h;

    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&h);
    make_waiting(&fs, &rq, "ARR-1", FLIGHT_ARRIVAL);
    assert(flight_add(&fs, "REG-1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    /* WAITING in the store but missing from its normal queue (inconsistent on purpose). */
    assert(flight_add(&fs, "GHOST", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_transition(&fs, "GHOST", STATE_WAITING) == ARMS_OK);

    RequestQueues rq_before = rq;
    assert(holding_enter(NULL, &rq, &fs, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(holding_enter(&h, NULL, &fs, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(holding_enter(&h, &rq, NULL, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(holding_enter(&h, &rq, &fs, NULL) == ARMS_ERR_INVALID_ARG);
    assert(holding_enter(&h, &rq, &fs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(holding_enter(&h, &rq, &fs, "REG-1") == ARMS_ERR_INVALID_TRANSITION);
    assert(holding_enter(&h, &rq, &fs, "GHOST") == ARMS_ERR_NOT_FOUND);
    assert(flight_find(&fs, "REG-1")->state == STATE_REGISTERED);
    assert(flight_find(&fs, "GHOST")->state == STATE_WAITING);
    assert(holding_count(&h) == 0);
    assert(same_queue_state(&rq.landing, &rq_before.landing));
    assert(same_queue_state(&rq.takeoff, &rq_before.takeoff));

    /* Full holding queue: ARR-1 stays WAITING and stays in the landing queue. */
    char id[ID_MAX_LEN + 1];
    for (int i = 0; i < CQUEUE_CAPACITY; i++) {
        snprintf(id, sizeof id, "Z-%d", i);
        assert(cqueue_enqueue(&h.queue, id) == ARMS_OK);
    }
    assert(holding_enter(&h, &rq, &fs, "ARR-1") == ARMS_ERR_FULL);
    assert(flight_find(&fs, "ARR-1")->state == STATE_WAITING);
    assert(queue_contains(&rq.landing, "ARR-1") && !cqueue_contains(&h.queue, "ARR-1"));
    assert(holding_count(&h) == CQUEUE_CAPACITY);
    assert(same_queue_state(&rq.landing, &rq_before.landing));
}

static void test_holding_leave_failures(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue h;
    char id[ID_MAX_LEN + 1];

    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&h);
    make_waiting(&fs, &rq, "ARR-1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "ARR-2", FLIGHT_ARRIVAL);
    /* HOLDING in the store but missing from the holding queue (inconsistent on purpose). */
    assert(flight_add(&fs, "GHOST", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_transition(&fs, "GHOST", STATE_WAITING) == ARMS_OK);
    assert(flight_transition(&fs, "GHOST", STATE_HOLDING) == ARMS_OK);

    assert(holding_leave(NULL, &rq, &fs, "ARR-1") == ARMS_ERR_INVALID_ARG);
    assert(holding_leave(&h, &rq, &fs, NULL) == ARMS_ERR_INVALID_ARG);
    assert(holding_leave(&h, &rq, &fs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(holding_leave(&h, &rq, &fs, "ARR-1") == ARMS_ERR_INVALID_TRANSITION);  /* WAITING */
    assert(holding_leave(&h, &rq, &fs, "GHOST") == ARMS_ERR_NOT_FOUND);
    assert(flight_find(&fs, "ARR-1")->state == STATE_WAITING);
    assert(flight_find(&fs, "GHOST")->state == STATE_HOLDING);
    assert(queue_count(&rq.landing) == 2 && holding_count(&h) == 0);

    /* Full normal queue: the flight stays in holding and stays HOLDING. */
    assert(holding_enter(&h, &rq, &fs, "ARR-1") == ARMS_OK);
    queue_init(&rq.landing);
    for (int i = 0; i < QUEUE_CAPACITY; i++) {
        snprintf(id, sizeof id, "Q-%d", i);
        assert(queue_enqueue(&rq.landing, id) == ARMS_OK);
    }
    RequestQueues rq_before = rq;
    assert(holding_leave(&h, &rq, &fs, "ARR-1") == ARMS_ERR_FULL);
    assert(flight_find(&fs, "ARR-1")->state == STATE_HOLDING);
    assert(cqueue_contains(&h.queue, "ARR-1") && !queue_contains(&rq.landing, "ARR-1"));
    assert(same_queue_state(&rq.landing, &rq_before.landing));

    /* Already in a normal queue as well (inconsistent on purpose): rejected, nothing moves. */
    queue_init(&rq.takeoff);
    assert(flight_add(&fs, "DEP-1", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_transition(&fs, "DEP-1", STATE_WAITING) == ARMS_OK);
    assert(flight_transition(&fs, "DEP-1", STATE_HOLDING) == ARMS_OK);
    assert(cqueue_enqueue(&h.queue, "DEP-1") == ARMS_OK);
    assert(queue_enqueue(&rq.takeoff, "DEP-1") == ARMS_OK);
    assert(holding_leave(&h, &rq, &fs, "DEP-1") == ARMS_ERR_DUPLICATE);
    assert(flight_find(&fs, "DEP-1")->state == STATE_HOLDING);
    assert(cqueue_contains(&h.queue, "DEP-1") && queue_count(&rq.takeoff) == 1);
}

/* ---------- Phase 5: max heap + scheduler ---------- */

/* Pops the whole scheduler and returns the IDs joined by spaces. */
static void drain_order(Scheduler *s, FlightStore *fs, char *out, size_t size)
{
    char id[ID_MAX_LEN + 1];
    out[0] = '\0';
    while (scheduler_pop(s, fs, id) == ARMS_OK) {
        strncat(out, id, size - strlen(out) - 1);
        strncat(out, " ", size - strlen(out) - 1);
    }
}

static void test_priority_comparator(void)
{
    FlightStore fs;
    RequestQueues rq;
    flight_store_init(&fs);
    request_init(&rq);
    make_waiting(&fs, &rq, "A", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "B", FLIGHT_ARRIVAL);
    Flight *a = flight_find(&fs, "A");
    Flight *b = flight_find(&fs, "B");

    assert(flight_priority_higher(b, a));          /* ARRIVAL before DEPARTURE */
    assert(!flight_priority_higher(a, b));
    assert(!flight_priority_higher(a, a));         /* never higher than itself */
    assert(!flight_priority_higher(NULL, a));

    assert(flight_set_fuel(&fs, "A", FUEL_LOW) == ARMS_OK);
    assert(flight_priority_higher(a, b));          /* fuel beats ARRIVAL */
    assert(flight_set_emergency(&fs, "B", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(flight_priority_higher(b, a));          /* emergency beats fuel */

    assert(flight_set_fuel(&fs, "A", (FuelLevel)9) == ARMS_ERR_INVALID_ARG);
    assert(flight_set_emergency(&fs, "NOPE", EMERGENCY_ACTIVE) == ARMS_ERR_NOT_FOUND);
    assert(flight_set_fuel(NULL, "A", FUEL_LOW) == ARMS_ERR_INVALID_ARG);
}

static void test_request_stamps(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue h;
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&h);

    assert(flight_add(&fs, "F1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_find(&fs, "F1")->emergency == EMERGENCY_NONE);
    assert(flight_find(&fs, "F1")->fuel == FUEL_NORMAL);
    fs.now = 7;
    make_waiting(&fs, &rq, "F0", FLIGHT_ARRIVAL);
    fs.now = 9;
    assert(request_submit(&rq, &fs, "F1") == ARMS_OK);
    assert(flight_find(&fs, "F0")->request_tick == 7 && flight_find(&fs, "F0")->request_seq == 0);
    assert(flight_find(&fs, "F1")->request_tick == 9 && flight_find(&fs, "F1")->request_seq == 1);

    /* Holding does not reset the request age. */
    assert(holding_enter(&h, &rq, &fs, "F0") == ARMS_OK);
    assert(holding_leave(&h, &rq, &fs, "F0") == ARMS_OK);
    assert(flight_find(&fs, "F0")->request_tick == 7 && flight_find(&fs, "F0")->request_seq == 0);

    /* A failed submit does not use up a sequence number. */
    assert(request_submit(&rq, &fs, "F1") != ARMS_OK);
    assert(fs.next_seq == 2);
}

static void test_heap_basic(void)
{
    FlightStore fs;
    RequestQueues rq;
    MaxHeap h;
    char out[ID_MAX_LEN + 1];
    flight_store_init(&fs);
    request_init(&rq);
    heap_init(&h);

    assert(heap_is_empty(&h) && heap_peek(&h) == NULL);
    assert(heap_extract(&h, &fs, out) == ARMS_ERR_EMPTY);

    /* Submitted in this order: D1(dep), A1(arr), A2(arr), D2(dep). */
    make_waiting(&fs, &rq, "D1", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "A1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "A2", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "D2", FLIGHT_DEPARTURE);
    const char *ids[] = { "D1", "A1", "A2", "D2" };
    for (int i = 0; i < 4; i++)
        assert(heap_insert(&h, &fs, ids[i]) == ARMS_OK);
    assert(heap_count(&h) == 4 && heap_contains(&h, "A2"));
    assert(strcmp(heap_peek(&h), "A1") == 0);

    /* Extraction order: arrivals first (FIFO), then departures (FIFO). */
    const char *expect[] = { "A1", "A2", "D1", "D2" };
    for (int i = 0; i < 4; i++) {
        assert(heap_extract(&h, &fs, out) == ARMS_OK);
        assert(strcmp(out, expect[i]) == 0);
    }
    assert(heap_is_empty(&h));

    /* Rejections. */
    assert(heap_insert(&h, &fs, "A1") == ARMS_OK);
    assert(heap_insert(&h, &fs, "A1") == ARMS_ERR_DUPLICATE);
    assert(heap_insert(&h, &fs, "GHOST") == ARMS_ERR_NOT_FOUND);
    assert(heap_insert(NULL, &fs, "A1") == ARMS_ERR_INVALID_ARG);
    assert(heap_count(&h) == 1);
}

static void test_heap_rejects_invalid_states(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue hq;
    MaxHeap h;
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&hq);
    heap_init(&h);

    assert(flight_add(&fs, "REG", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    make_waiting(&fs, &rq, "HOLD", FLIGHT_ARRIVAL);
    assert(holding_enter(&hq, &rq, &fs, "HOLD") == ARMS_OK);
    add_flight_in_state(&fs, "SCH", STATE_SCHEDULED);
    add_flight_in_state(&fs, "DONE", STATE_COMPLETED);
    add_flight_in_state(&fs, "CANC", STATE_CANCELLED);

    const char *bad[] = { "REG", "HOLD", "SCH", "DONE", "CANC" };
    for (int i = 0; i < 5; i++)
        assert(heap_insert(&h, &fs, bad[i]) == ARMS_ERR_NOT_ELIGIBLE);
    assert(heap_is_empty(&h));
}

static void test_heap_full(void)
{
    FlightStore fs;
    RequestQueues rq;
    MaxHeap h;
    flight_store_init(&fs);
    request_init(&rq);
    heap_init(&h);

    for (int i = 0; i < HEAP_CAPACITY; i++) {
        char id[ID_MAX_LEN + 1];
        snprintf(id, sizeof id, "F%d", i);
        assert(flight_add(&fs, id, FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
        assert(request_submit(&rq, &fs, id) == ARMS_OK);
        assert(heap_insert(&h, &fs, id) == ARMS_OK);
    }
    assert(heap_is_full(&h));
    assert(heap_insert(&h, &fs, "F0") == ARMS_ERR_DUPLICATE);
    assert(strcmp(heap_peek(&h), "F0") == 0);          /* oldest request first */

    /* Every extract is the next-oldest request. */
    for (int i = 0; i < HEAP_CAPACITY; i++) {
        char out[ID_MAX_LEN + 1], want[ID_MAX_LEN + 1];
        snprintf(want, sizeof want, "F%d", i);
        assert(heap_extract(&h, &fs, out) == ARMS_OK);
        assert(strcmp(out, want) == 0);
    }
    assert(heap_is_empty(&h));
}

static void test_heap_update_remove(void)
{
    FlightStore fs;
    RequestQueues rq;
    MaxHeap h;
    char out[ID_MAX_LEN + 1];
    flight_store_init(&fs);
    request_init(&rq);
    heap_init(&h);

    const char *ids[] = { "W1", "W2", "W3", "W4", "W5" };
    for (int i = 0; i < 5; i++) {
        make_waiting(&fs, &rq, ids[i], FLIGHT_ARRIVAL);
        assert(heap_insert(&h, &fs, ids[i]) == ARMS_OK);
    }
    assert(strcmp(heap_peek(&h), "W1") == 0);

    /* Raise priority of the newest flight: it must rise to the top. */
    assert(flight_set_fuel(&fs, "W5", FUEL_LOW) == ARMS_OK);
    assert(heap_update(&h, &fs, "W5") == ARMS_OK);
    assert(strcmp(heap_peek(&h), "W5") == 0);

    /* Lower it again: it must sink back. */
    assert(flight_set_fuel(&fs, "W5", FUEL_NORMAL) == ARMS_OK);
    assert(heap_update(&h, &fs, "W5") == ARMS_OK);
    assert(strcmp(heap_peek(&h), "W1") == 0);
    assert(heap_update(&h, &fs, "GHOST") == ARMS_ERR_NOT_FOUND);

    /* Remove from the middle, then the rest still come out in order. */
    assert(heap_remove(&h, &fs, "W3") == ARMS_OK);
    assert(!heap_contains(&h, "W3") && heap_count(&h) == 4);
    assert(heap_remove(&h, &fs, "W3") == ARMS_ERR_NOT_FOUND);
    const char *expect[] = { "W1", "W2", "W4", "W5" };
    for (int i = 0; i < 4; i++) {
        assert(heap_extract(&h, &fs, out) == ARMS_OK);
        assert(strcmp(out, expect[i]) == 0);
    }
}

static void test_scheduler_priority_order(void)
{
    FlightStore fs;
    RequestQueues rq;
    Scheduler s;
    char order[256];
    flight_store_init(&fs);
    request_init(&rq);
    scheduler_init(&s);

    make_waiting(&fs, &rq, "DEP-OLD", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "ARR-1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "ARR-2", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "ARR-FUEL", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "DEP-EMG", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "ARR-CRIT", FLIGHT_ARRIVAL);
    assert(flight_set_fuel(&fs, "ARR-FUEL", FUEL_LOW) == ARMS_OK);
    assert(flight_set_fuel(&fs, "ARR-CRIT", FUEL_CRITICAL) == ARMS_OK);
    assert(flight_set_emergency(&fs, "DEP-EMG", EMERGENCY_ACTIVE) == ARMS_OK);

    assert(scheduler_peek(&s) == NULL);
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(queue_is_empty(&rq.landing) && queue_is_empty(&rq.takeoff));
    assert(scheduler_count(&s) == 6);

    /* emergency, fuel critical, fuel low, then arrivals FIFO, then the departure. */
    drain_order(&s, &fs, order, sizeof order);
    assert(strcmp(order, "DEP-EMG ARR-CRIT ARR-FUEL ARR-1 ARR-2 DEP-OLD ") == 0);
    assert(flight_find(&fs, "ARR-1")->state == STATE_SCHEDULED);

    char id[ID_MAX_LEN + 1];
    assert(scheduler_pop(&s, &fs, id) == ARMS_ERR_EMPTY);
    assert(scheduler_pull(NULL, &rq, &fs) == ARMS_ERR_INVALID_ARG);
}

static void test_scheduler_waiting_time_tiebreak(void)
{
    FlightStore fs;
    RequestQueues rq;
    Scheduler s;
    char id[ID_MAX_LEN + 1];
    flight_store_init(&fs);
    request_init(&rq);
    scheduler_init(&s);

    /* Same type/fuel/emergency: smaller request_tick wins even with a larger seq. */
    make_waiting(&fs, &rq, "LATE", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "EARLY", FLIGHT_ARRIVAL);
    flight_find(&fs, "LATE")->request_tick = 10;
    flight_find(&fs, "EARLY")->request_tick = 3;
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "EARLY") == 0);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "LATE") == 0);
}

static void test_scheduler_pull_rules(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue hq;
    Scheduler s;
    char id[ID_MAX_LEN + 1];
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&hq);
    scheduler_init(&s);

    make_waiting(&fs, &rq, "W1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "H1", FLIGHT_ARRIVAL);
    assert(holding_enter(&hq, &rq, &fs, "H1") == ARMS_OK);
    assert(flight_add(&fs, "R1", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);

    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(scheduler_contains(&s, "W1"));
    assert(!scheduler_contains(&s, "H1") && !scheduler_contains(&s, "R1"));
    assert(scheduler_count(&s) == 1);

    /* A second pull never duplicates entries. */
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(scheduler_count(&s) == 1);

    /* peek is non-destructive. */
    assert(strcmp(scheduler_peek(&s), "W1") == 0);
    assert(scheduler_count(&s) == 1);
    assert(flight_find(&fs, "W1")->state == STATE_WAITING);

    /* A flight leaving holding is requeued, then pulled with its ORIGINAL age. */
    make_waiting(&fs, &rq, "W2", FLIGHT_ARRIVAL);
    assert(holding_leave(&hq, &rq, &fs, "H1") == ARMS_OK);
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(scheduler_count(&s) == 3);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "W1") == 0);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "H1") == 0);   /* seq 1 < W2's 2 */
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "W2") == 0);
}

static void test_scheduler_priority_update(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue hq;
    Scheduler s;
    char id[ID_MAX_LEN + 1];
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&hq);
    scheduler_init(&s);

    make_waiting(&fs, &rq, "A1", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "A2", FLIGHT_ARRIVAL);
    make_waiting(&fs, &rq, "A3", FLIGHT_ARRIVAL);
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(strcmp(scheduler_peek(&s), "A1") == 0);

    /* Emergency escalation of a flight already in the heap. */
    assert(scheduler_set_emergency(&s, &fs, "A3", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(strcmp(scheduler_peek(&s), "A3") == 0);
    /* Fuel change on a heap flight. */
    assert(scheduler_set_fuel(&s, &fs, "A2", FUEL_CRITICAL) == ARMS_OK);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "A3") == 0);
    assert(strcmp(scheduler_peek(&s), "A2") == 0);

    /* Escalating a queued flight needs no heap work; it lands correctly on the next pull. */
    make_waiting(&fs, &rq, "Q1", FLIGHT_DEPARTURE);
    make_waiting(&fs, &rq, "Q2", FLIGHT_DEPARTURE);
    assert(scheduler_set_emergency(&s, &fs, "Q2", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(!scheduler_contains(&s, "Q2"));
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    assert(scheduler_pop(&s, &fs, id) == ARMS_OK && strcmp(id, "Q2") == 0);

    /* Escalating a HOLDING flight only sets the field; it stays in holding. */
    make_waiting(&fs, &rq, "H1", FLIGHT_ARRIVAL);
    assert(holding_enter(&hq, &rq, &fs, "H1") == ARMS_OK);
    assert(scheduler_set_emergency(&s, &fs, "H1", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(flight_find(&fs, "H1")->emergency == EMERGENCY_ACTIVE);
    assert(flight_find(&fs, "H1")->state == STATE_HOLDING);
    assert(cqueue_contains(&hq.queue, "H1") && !scheduler_contains(&s, "H1"));

    /* Errors. */
    assert(scheduler_set_emergency(&s, &fs, "GHOST", EMERGENCY_ACTIVE) == ARMS_ERR_NOT_FOUND);
    assert(scheduler_set_fuel(&s, &fs, "A2", (FuelLevel)9) == ARMS_ERR_INVALID_ARG);
    add_flight_in_state(&fs, "DONE", STATE_COMPLETED);
    assert(scheduler_set_fuel(&s, &fs, "DONE", FUEL_LOW) == ARMS_ERR_NOT_ELIGIBLE);
}

static void test_scheduler_hold(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue hq;
    Scheduler s;
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&hq);
    scheduler_init(&s);

    make_waiting(&fs, &rq, "IN-HEAP", FLIGHT_ARRIVAL);
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);
    make_waiting(&fs, &rq, "IN-QUEUE", FLIGHT_DEPARTURE);

    /* From the heap: leaves the heap and is only in holding afterwards. */
    assert(scheduler_hold(&s, &hq, &rq, &fs, "IN-HEAP") == ARMS_OK);
    assert(!scheduler_contains(&s, "IN-HEAP"));
    assert(where_count(&hq, &rq, "IN-HEAP") == 1 && cqueue_contains(&hq.queue, "IN-HEAP"));
    assert(flight_find(&fs, "IN-HEAP")->state == STATE_HOLDING);

    /* From a normal queue: same as holding_enter. */
    assert(scheduler_hold(&s, &hq, &rq, &fs, "IN-QUEUE") == ARMS_OK);
    assert(where_count(&hq, &rq, "IN-QUEUE") == 1 && !scheduler_contains(&s, "IN-QUEUE"));

    /* Errors change nothing. */
    assert(scheduler_hold(&s, &hq, &rq, &fs, "IN-HEAP") == ARMS_ERR_DUPLICATE);
    assert(scheduler_hold(&s, &hq, &rq, &fs, "GHOST") == ARMS_ERR_NOT_FOUND);
    assert(scheduler_hold(NULL, &hq, &rq, &fs, "X") == ARMS_ERR_INVALID_ARG);
    assert(holding_count(&hq) == 2 && scheduler_count(&s) == 0);
}

static void test_scheduler_hold_full_holding(void)
{
    FlightStore fs;
    RequestQueues rq;
    HoldingQueue hq;
    Scheduler s;
    flight_store_init(&fs);
    request_init(&rq);
    holding_init(&hq);
    scheduler_init(&s);

    for (int i = 0; i < CQUEUE_CAPACITY; i++) {
        char id[ID_MAX_LEN + 1];
        snprintf(id, sizeof id, "P%d", i);
        assert(cqueue_enqueue(&hq.queue, id) == ARMS_OK);
    }
    make_waiting(&fs, &rq, "WAIT", FLIGHT_ARRIVAL);
    assert(scheduler_pull(&s, &rq, &fs) == ARMS_OK);

    /* The heap flight must stay in the heap, untouched. */
    assert(scheduler_hold(&s, &hq, &rq, &fs, "WAIT") == ARMS_ERR_FULL);
    assert(scheduler_contains(&s, "WAIT") && queue_is_empty(&rq.landing));
    assert(flight_find(&fs, "WAIT")->state == STATE_WAITING);
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
    test_queue_basic();
    test_queue_full_and_compaction();
    test_queue_print();
    test_request_submit();
    test_request_rejects_non_registered();
    test_request_full_queue();
    test_queue_remove();
    test_cqueue_basic();
    test_cqueue_wraparound();
    test_holding_enter_leave();
    test_holding_enter_failures();
    test_holding_leave_failures();
    test_priority_comparator();
    test_request_stamps();
    test_heap_basic();
    test_heap_rejects_invalid_states();
    test_heap_full();
    test_heap_update_remove();
    test_scheduler_priority_order();
    test_scheduler_waiting_time_tiebreak();
    test_scheduler_pull_rules();
    test_scheduler_priority_update();
    test_scheduler_hold();
    test_scheduler_hold_full_holding();

    printf("All core tests passed.\n");
    return 0;
}
