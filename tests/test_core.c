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
#include "operations.h"
#include "history.h"
#include "reports.h"
#include "graph.h"
#include "system.h"
#include "eventlog.h"
#include "service.h"
#include "persist.h"
#include "demo.h"

/* Test-only hook defined in history.c when built with -DARMS_TESTING. */
extern int history_fail_next_alloc;
extern int eventlog_fail_next_alloc;

/* Independent copy of the approved transition table, to check flight.c against. */
static const int expected[STATE_COUNT][STATE_COUNT] = {
    { 0, 1, 0, 0, 0, 0, 1 },
    { 0, 0, 1, 1, 0, 0, 1 },
    { 0, 1, 0, 1, 0, 0, 1 },
    { 0, 0, 0, 0, 1, 0, 1 },
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
    assert(flight_transition_allowed(STATE_SCHEDULED, STATE_CANCELLED));   /* via ops_cancel */
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

/* ---------- Phase 6: operations ---------- */

typedef struct {
    FlightStore   fs;
    RunwayStore   rs;
    RequestQueues rq;
    HoldingQueue  hq;
    Scheduler     s;
    MovementHistory hist;
} Sys;

static void sys_init(Sys *y)
{
    history_init(&y->hist);
    flight_store_init(&y->fs);
    runway_store_init(&y->rs);
    request_init(&y->rq);
    holding_init(&y->hq);
    scheduler_init(&y->s);
}

/* Registers a flight and submits it: WAITING, in a normal queue. */
static void sys_flight(Sys *y, const char *id, FlightType t, AircraftCategory c)
{
    assert(flight_add(&y->fs, id, t, c) == ARMS_OK);
    assert(request_submit(&y->rq, &y->fs, id) == ARMS_OK);
}

static void sys_pull(Sys *y)
{
    assert(scheduler_pull(&y->s, &y->rq, &y->fs) == ARMS_OK);
}

static ArmsStatus sys_schedule(Sys *y, char *fid, char *rid)
{
    sys_pull(y);
    return ops_schedule_next(&y->s, &y->fs, &y->rs, fid, rid);
}

static ArmsStatus sys_cancel(Sys *y, const char *id)
{
    return ops_cancel(&y->s, &y->hq, &y->rq, &y->fs, &y->rs, id);
}

/* Byte-for-byte copy of every structure, to prove a failed call changed nothing. */
static void snap_take(const Sys *y, Sys *copy) { memcpy(copy, y, sizeof *copy); }
static int  snap_same(const Sys *a, const Sys *b) { return memcmp(a, b, sizeof *a) == 0; }

static void test_ops_schedule_basic(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    assert(flight_find(&y.fs, "F1")->runway_id[0] == '\0');
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "F1") == 0 && strcmp(rid, "R1") == 0);
    Flight *f = flight_find(&y.fs, "F1");
    assert(f->state == STATE_SCHEDULED && strcmp(f->runway_id, "R1") == 0);
    Runway *r = runway_find(&y.rs, "R1");
    assert(r->status == RUNWAY_OCCUPIED && strcmp(r->current_flight_id, "F1") == 0);
    assert(scheduler_count(&y.s) == 0);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    assert(sys_schedule(&y, fid, rid) == ARMS_ERR_EMPTY);
    assert(ops_schedule_next(NULL, &y.fs, &y.rs, fid, rid) == ARMS_ERR_INVALID_ARG);
    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, NULL, rid) == ARMS_ERR_INVALID_ARG);
}

static void test_ops_priority_order(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "FIRST", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    sys_flight(&y, "URGENT", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    sys_pull(&y);
    assert(scheduler_set_emergency(&y.s, &y.fs, "URGENT", EMERGENCY_ACTIVE) == ARMS_OK);

    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "URGENT") == 0);
    assert(flight_find(&y.fs, "FIRST")->state == STATE_WAITING && scheduler_contains(&y.s, "FIRST"));
}

static void test_ops_runway_selection(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_SMALL) == ARMS_OK);
    assert(runway_add(&y.rs, "R2", AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(runway_add(&y.rs, "R3", AIRCRAFT_LARGE) == ARMS_OK);

    /* First compatible AVAILABLE runway in store order: R1 is too small, so R2. */
    sys_flight(&y, "A", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(rid, "R2") == 0);

    /* R2 is now occupied, so the next medium flight moves on to R3. */
    sys_flight(&y, "B", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(rid, "R3") == 0);

    /* Only R1 is free and it is too small for C; D is small and can use it. */
    sys_flight(&y, "C", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    assert(sys_schedule(&y, fid, rid) == ARMS_ERR_NO_RUNWAY);
    sys_flight(&y, "D", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "D") == 0 && strcmp(rid, "R1") == 0);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
}

static void test_ops_runway_unavailable(void)
{
    Sys y, before;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    /* Closed runway. */
    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(runway_set_status(&y.rs, "R1", RUNWAY_CLOSED) == ARMS_OK);
    sys_flight(&y, "F1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    sys_pull(&y);
    snap_take(&y, &before);
    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_ERR_NO_RUNWAY);
    assert(snap_same(&y, &before));
    assert(flight_find(&y.fs, "F1")->state == STATE_WAITING && scheduler_contains(&y.s, "F1"));

    /* Reopened: now it works. */
    assert(runway_set_status(&y.rs, "R1", RUNWAY_AVAILABLE) == ARMS_OK);
    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_OK);

    /* Occupied runway, plus a free runway that is too small for the flight. */
    assert(runway_add(&y.rs, "R2", AIRCRAFT_SMALL) == ARMS_OK);
    sys_flight(&y, "F2", FLIGHT_ARRIVAL, AIRCRAFT_LARGE);
    sys_pull(&y);
    snap_take(&y, &before);
    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_ERR_NO_RUNWAY);
    assert(snap_same(&y, &before));
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
}

static void test_ops_blocked_top_skipped(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_SMALL) == ARMS_OK);
    sys_flight(&y, "BIG", FLIGHT_ARRIVAL, AIRCRAFT_LARGE);
    sys_flight(&y, "SML", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    sys_pull(&y);
    assert(scheduler_set_emergency(&y.s, &y.fs, "BIG", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(strcmp(scheduler_peek(&y.s), "BIG") == 0);

    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "SML") == 0 && strcmp(rid, "R1") == 0);

    /* The blocked flight is still WAITING, still in the heap, still on top. */
    assert(flight_find(&y.fs, "BIG")->state == STATE_WAITING);
    assert(flight_find(&y.fs, "BIG")->runway_id[0] == '\0');
    assert(scheduler_count(&y.s) == 1 && strcmp(scheduler_peek(&y.s), "BIG") == 0);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
}

static void test_ops_start_complete(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "F1", FLIGHT_DEPARTURE, AIRCRAFT_MEDIUM);
    sys_flight(&y, "F2", FLIGHT_DEPARTURE, AIRCRAFT_MEDIUM);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "F1") == 0);

    /* Wrong states and unknown flights. */
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F1") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_start(&y.fs, &y.rs, "F2") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_start(&y.fs, &y.rs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(ops_start(&y.fs, &y.rs, NULL) == ARMS_ERR_INVALID_ARG);

    assert(ops_start(&y.fs, &y.rs, "F1") == ARMS_OK);
    assert(flight_find(&y.fs, "F1")->state == STATE_IN_OPERATION);
    assert(runway_find(&y.rs, "R1")->status == RUNWAY_OCCUPIED);
    assert(strcmp(flight_find(&y.fs, "F1")->runway_id, "R1") == 0);
    assert(ops_start(&y.fs, &y.rs, "F1") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    /* R1 is still busy, so F2 cannot be scheduled yet. */
    assert(sys_schedule(&y, fid, rid) == ARMS_ERR_NO_RUNWAY);

    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F1") == ARMS_OK);
    assert(flight_find(&y.fs, "F1")->state == STATE_COMPLETED);
    assert(flight_find(&y.fs, "F1")->runway_id[0] == '\0');
    Runway *r = runway_find(&y.rs, "R1");
    assert(r->status == RUNWAY_AVAILABLE && r->current_flight_id[0] == '\0');
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F1") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    /* The released runway is reusable. */
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "F2") == 0);
    history_clear(&y.hist);
}

static void test_ops_cancel(void)
{
    Sys y, before;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);
    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);

    /* REGISTERED */
    assert(flight_add(&y.fs, "REG", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(sys_cancel(&y, "REG") == ARMS_OK);
    assert(flight_find(&y.fs, "REG")->state == STATE_CANCELLED);

    /* WAITING in a normal queue */
    sys_flight(&y, "WQ", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(queue_contains(&y.rq.landing, "WQ"));
    assert(sys_cancel(&y, "WQ") == ARMS_OK);
    assert(!queue_contains(&y.rq.landing, "WQ"));
    assert(flight_find(&y.fs, "WQ")->state == STATE_CANCELLED);

    /* WAITING in the heap */
    sys_flight(&y, "WH", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    sys_pull(&y);
    assert(scheduler_contains(&y.s, "WH"));
    assert(sys_cancel(&y, "WH") == ARMS_OK);
    assert(!scheduler_contains(&y.s, "WH") && queue_is_empty(&y.rq.takeoff));
    assert(flight_find(&y.fs, "WH")->state == STATE_CANCELLED);

    /* HOLDING */
    sys_flight(&y, "HLD", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(holding_enter(&y.hq, &y.rq, &y.fs, "HLD") == ARMS_OK);
    assert(holding_count(&y.hq) == 1);
    assert(sys_cancel(&y, "HLD") == ARMS_OK);
    assert(holding_count(&y.hq) == 0);
    assert(flight_find(&y.fs, "HLD")->state == STATE_CANCELLED);

    /* SCHEDULED: runway released */
    sys_flight(&y, "SCH", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "SCH") == 0);
    assert(sys_cancel(&y, "SCH") == ARMS_OK);
    assert(flight_find(&y.fs, "SCH")->state == STATE_CANCELLED);
    assert(flight_find(&y.fs, "SCH")->runway_id[0] == '\0');
    Runway *r = runway_find(&y.rs, "R1");
    assert(r->status == RUNWAY_AVAILABLE && r->current_flight_id[0] == '\0');
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    /* IN_OPERATION: rejected, nothing changes; it can still complete. */
    sys_flight(&y, "OP", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(ops_start(&y.fs, &y.rs, "OP") == ARMS_OK);
    snap_take(&y, &before);
    assert(sys_cancel(&y, "OP") == ARMS_ERR_INVALID_TRANSITION);
    assert(snap_same(&y, &before));
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "OP") == ARMS_OK);

    /* Terminal states, unknown flight, bad arguments. */
    assert(sys_cancel(&y, "OP") == ARMS_ERR_INVALID_TRANSITION);
    assert(sys_cancel(&y, "REG") == ARMS_ERR_INVALID_TRANSITION);
    assert(sys_cancel(&y, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(sys_cancel(&y, NULL) == ARMS_ERR_INVALID_ARG);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
    history_clear(&y.hist);
}

static void test_ops_atomic_failures(void)
{
    Sys y, before;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(runway_add(&y.rs, "R2", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "OP", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(ops_start(&y.fs, &y.rs, "OP") == ARMS_OK);
    sys_flight(&y, "SC", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    sys_flight(&y, "HD", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(holding_enter(&y.hq, &y.rq, &y.fs, "HD") == ARMS_OK);
    sys_flight(&y, "WT", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);   /* both runways busy */
    sys_pull(&y);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    snap_take(&y, &before);
    assert(ops_schedule_next(&y.s, &y.fs, &y.rs, fid, rid) == ARMS_ERR_NO_RUNWAY);
    assert(ops_start(&y.fs, &y.rs, "OP") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_start(&y.fs, &y.rs, "WT") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_start(&y.fs, &y.rs, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "SC") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "WT") == ARMS_ERR_INVALID_TRANSITION);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(sys_cancel(&y, "OP") == ARMS_ERR_INVALID_TRANSITION);
    assert(sys_cancel(&y, "NOPE") == ARMS_ERR_NOT_FOUND);
    assert(snap_same(&y, &before));

    /* Broken runway link: the call must refuse and leave everything as it was. */
    Flight *sc = flight_find(&y.fs, "SC");
    sc->runway_id[0] = '\0';
    snap_take(&y, &before);
    assert(ops_start(&y.fs, &y.rs, "SC") == ARMS_ERR_NOT_OCCUPIED);
    assert(sys_cancel(&y, "SC") == ARMS_ERR_NOT_OCCUPIED);
    assert(snap_same(&y, &before));
    snprintf(sc->runway_id, sizeof sc->runway_id, "%s", "R2");
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    Runway *r1 = runway_find(&y.rs, "R1");
    r1->current_flight_id[0] = '\0';
    snap_take(&y, &before);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "OP") == ARMS_ERR_NOT_OCCUPIED);
    assert(snap_same(&y, &before));
    snprintf(r1->current_flight_id, sizeof r1->current_flight_id, "%s", "OP");
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    /* A WAITING flight that is in no queue or heap cannot be cancelled cleanly. */
    assert(flight_add(&y.fs, "LOST", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_transition(&y.fs, "LOST", STATE_WAITING) == ARMS_OK);
    snap_take(&y, &before);
    assert(sys_cancel(&y, "LOST") == ARMS_ERR_NOT_FOUND);
    assert(snap_same(&y, &before));
}

static void test_ops_invariant_checker(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);

    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(runway_add(&y.rs, "R2", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "A", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);   /* A on R1 */
    sys_flight(&y, "W", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    sys_pull(&y);                                      /* W waits in the heap */
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
    assert(!ops_check_invariants(NULL, &y.rs, &y.s));

    Flight *a = flight_find(&y.fs, "A");
    Flight *w = flight_find(&y.fs, "W");
    Runway *r1 = runway_find(&y.rs, "R1");
    Runway *r2 = runway_find(&y.rs, "R2");

    a->runway_id[0] = '\0';                            /* active flight without a runway */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    snprintf(a->runway_id, sizeof a->runway_id, "%s", "R1");
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    snprintf(a->runway_id, sizeof a->runway_id, "%s", "R2");   /* link points elsewhere */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    snprintf(a->runway_id, sizeof a->runway_id, "%s", "R1");

    snprintf(w->runway_id, sizeof w->runway_id, "%s", "R1");   /* WAITING keeps a runway */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    w->runway_id[0] = '\0';

    r2->status = RUNWAY_OCCUPIED;                      /* OCCUPIED with no flight */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    r2->status = RUNWAY_AVAILABLE;

    snprintf(r2->current_flight_id, sizeof r2->current_flight_id, "%s", "A");   /* stale id */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    r2->current_flight_id[0] = '\0';

    snprintf(r1->current_flight_id, sizeof r1->current_flight_id, "%s", "ZZ");  /* no such flight */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    snprintf(r1->current_flight_id, sizeof r1->current_flight_id, "%s", "A");

    w->state = STATE_HOLDING;                          /* non-WAITING flight in the heap */
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    w->state = STATE_WAITING;

    /* Two flights claiming the same runway. */
    add_flight_in_state(&y.fs, "B", STATE_SCHEDULED);
    snprintf(flight_find(&y.fs, "B")->runway_id, ID_MAX_LEN + 1, "%s", "R1");
    assert(!ops_check_invariants(&y.fs, &y.rs, &y.s));
    flight_find(&y.fs, "B")->runway_id[0] = '\0';
    flight_find(&y.fs, "B")->state = STATE_CANCELLED;
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));
}

/* ---------- Phase 7: movement history ---------- */

static Movement make_movement(const char *id, const char *rwy)
{
    Movement m;
    memset(&m, 0, sizeof m);
    snprintf(m.flight_id, sizeof m.flight_id, "%s", id);
    snprintf(m.runway_id, sizeof m.runway_id, "%s", rwy);
    return m;
}

static void test_history_list_basic(void)
{
    MovementHistory h;
    history_init(&h);
    assert(history_count(&h) == 0 && h.head == NULL && h.tail == NULL);
    assert(history_at(&h, 0) == NULL && history_find(&h, "A") == NULL);

    /* first insertion */
    Movement a = make_movement("A", "R1");
    assert(history_append(&h, &a) == ARMS_OK);
    assert(history_count(&h) == 1 && h.head == h.tail);
    assert(strcmp(history_at(&h, 0)->flight_id, "A") == 0);

    /* multiple records keep append order */
    Movement b = make_movement("B", "R2"), c = make_movement("C", "R1");
    assert(history_append(&h, &b) == ARMS_OK);
    assert(history_append(&h, &c) == ARMS_OK);
    assert(history_count(&h) == 3);
    assert(strcmp(history_at(&h, 0)->flight_id, "A") == 0);
    assert(strcmp(history_at(&h, 1)->flight_id, "B") == 0);
    assert(strcmp(history_at(&h, 2)->flight_id, "C") == 0);
    assert(history_at(&h, 3) == NULL && history_at(&h, -1) == NULL);

    /* search */
    assert(strcmp(history_find(&h, "B")->runway_id, "R2") == 0);
    assert(history_find(&h, "NOPE") == NULL);

    /* bad arguments */
    assert(history_append(NULL, &a) == ARMS_ERR_INVALID_ARG);
    assert(history_append(&h, NULL) == ARMS_ERR_INVALID_ARG);
    assert(history_find(&h, NULL) == NULL && history_find(NULL, "A") == NULL);
    assert(history_count(NULL) == 0);

    /* failed allocation leaves the list untouched */
    MovementNode *old_tail = h.tail;
    history_fail_next_alloc = 1;
    assert(history_append(&h, &a) == ARMS_ERR_NO_MEMORY);
    assert(history_count(&h) == 3 && h.tail == old_tail && old_tail->next == NULL);
    assert(history_append(&h, &a) == ARMS_OK);   /* hook fires only once */
    assert(history_count(&h) == 4);

    /* list output */
    FILE *f = fopen(SCRATCH_FILE, "w+");
    assert(f != NULL);
    history_list(f, &h);
    char buf[1024];
    read_back(f, buf, sizeof buf);
    fclose(f);
    assert(strstr(buf, "1. A") != NULL && strstr(buf, "runway=R2") != NULL);

    /* clear */
    history_clear(&h);
    assert(history_count(&h) == 0 && h.head == NULL && h.tail == NULL);
    assert(history_find(&h, "A") == NULL);
    assert(history_append(&h, &a) == ARMS_OK && history_count(&h) == 1);   /* reusable */
    history_clear(&h);
    history_clear(&h);   /* clearing an empty list is fine */
}

static void test_history_from_completion(void)
{
    Sys y;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);
    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    sys_flight(&y, "F2", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);

    /* Nothing is recorded before completion, including by a cancelled SCHEDULED flight. */
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "F1") == 0);
    assert(sys_cancel(&y, "F1") == ARMS_OK);
    assert(history_count(&y.hist) == 0);

    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "F2") == 0);
    assert(ops_start(&y.fs, &y.rs, "F2") == ARMS_OK);
    assert(history_count(&y.hist) == 0);
    y.fs.now = 7;
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F2") == ARMS_OK);

    /* Exactly one record, with the runway captured before it was cleared. */
    assert(history_count(&y.hist) == 1);
    const Movement *m = history_find(&y.hist, "F2");
    assert(m != NULL);
    assert(m->type == FLIGHT_DEPARTURE && m->category == AIRCRAFT_SMALL);
    assert(strcmp(m->runway_id, "R1") == 0);
    assert(m->request_seq == flight_find(&y.fs, "F2")->request_seq);
    assert(m->request_tick == flight_find(&y.fs, "F2")->request_tick);
    assert(m->completed_tick == 7);
    assert(flight_find(&y.fs, "F2")->runway_id[0] == '\0');

    /* Repeated completion is rejected and adds nothing. */
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F2") == ARMS_ERR_INVALID_TRANSITION);
    assert(history_count(&y.hist) == 1);
    assert(ops_complete(&y.fs, &y.rs, NULL, "F2") == ARMS_ERR_INVALID_ARG);

    /* History survives the runway being released and reused by a later flight. */
    sys_flight(&y, "F3", FLIGHT_ARRIVAL, AIRCRAFT_LARGE);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK && strcmp(fid, "F3") == 0);
    assert(runway_find(&y.rs, "R1")->status == RUNWAY_OCCUPIED);
    assert(strcmp(history_find(&y.hist, "F2")->runway_id, "R1") == 0);
    assert(ops_start(&y.fs, &y.rs, "F3") == ARMS_OK);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "F3") == ARMS_OK);
    assert(history_count(&y.hist) == 2);
    assert(strcmp(history_at(&y.hist, 0)->flight_id, "F2") == 0);
    assert(strcmp(history_at(&y.hist, 1)->flight_id, "F3") == 0);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    history_clear(&y.hist);
}

static void test_history_alloc_failure_in_completion(void)
{
    Sys y, before;
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    sys_init(&y);
    assert(runway_add(&y.rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    sys_flight(&y, "OLD", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(ops_start(&y.fs, &y.rs, "OLD") == ARMS_OK);
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "OLD") == ARMS_OK);

    sys_flight(&y, "NEW", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(sys_schedule(&y, fid, rid) == ARMS_OK);
    assert(ops_start(&y.fs, &y.rs, "NEW") == ARMS_OK);

    /* Allocation fails: the flight stays IN_OPERATION on R1 and the history is unchanged. */
    snap_take(&y, &before);
    history_fail_next_alloc = 1;
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "NEW") == ARMS_ERR_NO_MEMORY);
    assert(snap_same(&y, &before));
    assert(flight_find(&y.fs, "NEW")->state == STATE_IN_OPERATION);
    assert(strcmp(flight_find(&y.fs, "NEW")->runway_id, "R1") == 0);
    assert(runway_find(&y.rs, "R1")->status == RUNWAY_OCCUPIED);
    assert(history_count(&y.hist) == 1 && history_find(&y.hist, "NEW") == NULL);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    /* A retry succeeds and records the right runway. */
    assert(ops_complete(&y.fs, &y.rs, &y.hist, "NEW") == ARMS_OK);
    assert(history_count(&y.hist) == 2);
    assert(strcmp(history_find(&y.hist, "NEW")->runway_id, "R1") == 0);
    assert(ops_check_invariants(&y.fs, &y.rs, &y.s));

    history_clear(&y.hist);
}

/* ---- Phase 8: reports (sorting and binary search) ---- */

/* "A,B,C" style summary of a sorted pointer array. */
static void flight_order(const Flight **a, int n, char *buf)
{
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strcat(buf, ",");
        strcat(buf, a[i]->id);
    }
}

static void move_order(const Movement **a, int n, char *buf)
{
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strcat(buf, ",");
        strcat(buf, a[i]->flight_id);
    }
}

/* Registers the given IDs in order. */
static void store_with(FlightStore *fs, const char *const *ids, int n)
{
    flight_store_init(fs);
    for (int i = 0; i < n; i++)
        assert(flight_add(fs, ids[i], FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
}

/* Sorts, checks the order string, and checks the call succeeded. */
static void check_flights(const FlightStore *fs, FlightSortKey key, int asc, const char *want)
{
    const Flight *out[MAX_FLIGHTS];
    char buf[256];
    int n = -1;
    assert(report_flights_sorted(fs, key, asc, out, MAX_FLIGHTS, &n) == ARMS_OK);
    flight_order(out, n, buf);
    assert(strcmp(buf, want) == 0);
}

static void check_history(const MovementHistory *h, HistorySortKey key, int asc, const char *want)
{
    const Movement *out[16];
    char buf[256];
    int n = -1;
    assert(report_history_sorted(h, key, asc, out, 16, &n) == ARMS_OK);
    move_order(out, n, buf);
    assert(strcmp(buf, want) == 0);
}

static void test_reports_flights_by_id(void)
{
    FlightStore fs;
    const Flight *out[MAX_FLIGHTS];
    int n = -1;

    /* empty (also with cap 0) */
    flight_store_init(&fs);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, MAX_FLIGHTS, &n) == ARMS_OK && n == 0);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, 0, &n) == ARMS_OK && n == 0);

    /* single item */
    const char *one[] = { "A" };
    store_with(&fs, one, 1);
    check_flights(&fs, FLIGHT_SORT_ID, 1, "A");
    check_flights(&fs, FLIGHT_SORT_ID, 0, "A");

    /* already sorted */
    const char *sorted[] = { "A", "B", "C" };
    store_with(&fs, sorted, 3);
    check_flights(&fs, FLIGHT_SORT_ID, 1, "A,B,C");
    check_flights(&fs, FLIGHT_SORT_ID, 0, "C,B,A");

    /* reverse order and mixed order */
    const char *rev[] = { "C", "B", "A" };
    store_with(&fs, rev, 3);
    check_flights(&fs, FLIGHT_SORT_ID, 1, "A,B,C");
    check_flights(&fs, FLIGHT_SORT_ID, 0, "C,B,A");
    const char *mixed[] = { "F2", "F10", "A-1", "F1" };
    store_with(&fs, mixed, 4);
    check_flights(&fs, FLIGHT_SORT_ID, 1, "A-1,F1,F10,F2");   /* plain strcmp order */
    check_flights(&fs, FLIGHT_SORT_ID, 0, "F2,F10,F1,A-1");

    /* source store is not reordered */
    FlightStore before = fs;
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, MAX_FLIGHTS, &n) == ARMS_OK);
    assert(memcmp(&fs, &before, sizeof fs) == 0);
    assert(strcmp(flight_at(&fs, 0)->id, "F2") == 0 && strcmp(flight_at(&fs, 3)->id, "F1") == 0);
    assert(out[0] == flight_find(&fs, "A-1"));   /* pointers refer to the real flights */
}

static void test_reports_flights_by_request_and_priority(void)
{
    FlightStore fs;
    const char *ids[] = { "X", "Y", "Z" };
    store_with(&fs, ids, 3);

    /* request_tick first, then request_seq */
    flight_find(&fs, "X")->request_tick = 5; flight_find(&fs, "X")->request_seq = 1;
    flight_find(&fs, "Y")->request_tick = 2; flight_find(&fs, "Y")->request_seq = 3;
    flight_find(&fs, "Z")->request_tick = 2; flight_find(&fs, "Z")->request_seq = 2;
    check_flights(&fs, FLIGHT_SORT_REQUEST, 1, "Z,Y,X");
    check_flights(&fs, FLIGHT_SORT_REQUEST, 0, "X,Y,Z");

    /* fully equal keys keep store order in BOTH directions (stability) */
    const char *ties[] = { "P", "Q", "R" };
    store_with(&fs, ties, 3);
    flight_find(&fs, "P")->request_tick = 1; flight_find(&fs, "P")->request_seq = 1;
    flight_find(&fs, "Q")->request_tick = 1; flight_find(&fs, "Q")->request_seq = 1;
    flight_find(&fs, "R")->request_tick = 0; flight_find(&fs, "R")->request_seq = 1;
    check_flights(&fs, FLIGHT_SORT_REQUEST, 1, "R,P,Q");
    check_flights(&fs, FLIGHT_SORT_REQUEST, 0, "P,Q,R");   /* not the reverse of ascending */

    /* priority: emergency, then ARRIVAL before DEPARTURE, then request_seq */
    FlightStore ps;
    flight_store_init(&ps);
    assert(flight_add(&ps, "DEP", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_add(&ps, "ARR", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_add(&ps, "EMG", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);
    flight_find(&ps, "DEP")->request_seq = 1;
    flight_find(&ps, "ARR")->request_seq = 2;
    flight_find(&ps, "EMG")->request_seq = 3;
    flight_find(&ps, "EMG")->emergency = EMERGENCY_ACTIVE;
    check_flights(&ps, FLIGHT_SORT_PRIORITY, 1, "EMG,ARR,DEP");   /* most urgent first */
    check_flights(&ps, FLIGHT_SORT_PRIORITY, 0, "DEP,ARR,EMG");

    /* priority agrees with the scheduler's comparator */
    const Flight *out[MAX_FLIGHTS];
    int n;
    assert(report_flights_sorted(&ps, FLIGHT_SORT_PRIORITY, 1, out, MAX_FLIGHTS, &n) == ARMS_OK);
    for (int i = 0; i + 1 < n; i++)
        assert(flight_priority_higher(out[i], out[i + 1]));
    assert(strcmp(flight_at(&ps, 0)->id, "DEP") == 0);   /* store order unchanged */
}

static void test_reports_flight_errors_and_capacity(void)
{
    FlightStore fs;
    const char *ids[] = { "A", "B", "C" };
    store_with(&fs, ids, 3);
    const Flight *out[3];
    int n = 99;

    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, 2, &n) == ARMS_ERR_FULL && n == 0);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, 3, &n) == ARMS_OK && n == 3);

    assert(report_flights_sorted(NULL, FLIGHT_SORT_ID, 1, out, 3, &n) == ARMS_ERR_INVALID_ARG);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, NULL, 3, &n) == ARMS_ERR_INVALID_ARG);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, 3, NULL) == ARMS_ERR_INVALID_ARG);
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, out, -1, &n) == ARMS_ERR_INVALID_ARG);
    assert(report_flights_sorted(&fs, (FlightSortKey)99, 1, out, 3, &n) == ARMS_ERR_INVALID_ARG);
}

static void test_reports_history(void)
{
    MovementHistory h;
    const Movement *out[16];
    int n = -1;
    history_init(&h);

    /* empty and single */
    assert(report_history_sorted(&h, HIST_SORT_FLIGHT_ID, 1, out, 16, &n) == ARMS_OK && n == 0);
    assert(report_history_sorted(&h, HIST_SORT_COMPLETION, 1, out, 0, &n) == ARMS_OK && n == 0);
    Movement a = make_movement("C", "R1"); a.completed_tick = 5;
    Movement b = make_movement("A", "R1"); b.completed_tick = 5;
    Movement c = make_movement("B", "R2"); c.completed_tick = 2;
    assert(history_append(&h, &a) == ARMS_OK);
    check_history(&h, HIST_SORT_FLIGHT_ID, 1, "C");
    check_history(&h, HIST_SORT_COMPLETION, 0, "C");

    /* list order is C, A, B; completion ticks 5, 5, 2 */
    assert(history_append(&h, &b) == ARMS_OK);
    assert(history_append(&h, &c) == ARMS_OK);
    check_history(&h, HIST_SORT_FLIGHT_ID, 1, "A,B,C");
    check_history(&h, HIST_SORT_FLIGHT_ID, 0, "C,B,A");
    check_history(&h, HIST_SORT_COMPLETION, 1, "B,C,A");   /* tie (C, A) keeps list order */
    check_history(&h, HIST_SORT_COMPLETION, 0, "C,A,B");   /* tie keeps list order; not "A,C,B" */

    /* source list is unchanged, and the pointers refer to its nodes */
    assert(report_history_sorted(&h, HIST_SORT_FLIGHT_ID, 1, out, 16, &n) == ARMS_OK && n == 3);
    assert(history_count(&h) == 3);
    assert(strcmp(history_at(&h, 0)->flight_id, "C") == 0);
    assert(strcmp(history_at(&h, 1)->flight_id, "A") == 0);
    assert(strcmp(history_at(&h, 2)->flight_id, "B") == 0);
    assert(out[0] == history_find(&h, "A") && out[2] == history_find(&h, "C"));
    assert(h.head->data.flight_id[0] == 'C' && h.tail->data.flight_id[0] == 'B' && h.tail->next == NULL);

    /* capacity and bad arguments */
    assert(report_history_sorted(&h, HIST_SORT_FLIGHT_ID, 1, out, 2, &n) == ARMS_ERR_FULL && n == 0);
    assert(report_history_sorted(NULL, HIST_SORT_FLIGHT_ID, 1, out, 16, &n) == ARMS_ERR_INVALID_ARG);
    assert(report_history_sorted(&h, HIST_SORT_FLIGHT_ID, 1, NULL, 16, &n) == ARMS_ERR_INVALID_ARG);
    assert(report_history_sorted(&h, (HistorySortKey)99, 1, out, 16, &n) == ARMS_ERR_INVALID_ARG);
    history_clear(&h);

    /* already sorted and reverse completion order */
    Movement m1 = make_movement("M1", "R1"); m1.completed_tick = 1;
    Movement m2 = make_movement("M2", "R1"); m2.completed_tick = 2;
    Movement m3 = make_movement("M3", "R1"); m3.completed_tick = 3;
    assert(history_append(&h, &m1) == ARMS_OK && history_append(&h, &m2) == ARMS_OK
           && history_append(&h, &m3) == ARMS_OK);
    check_history(&h, HIST_SORT_COMPLETION, 1, "M1,M2,M3");
    check_history(&h, HIST_SORT_COMPLETION, 0, "M3,M2,M1");
    history_clear(&h);
    assert(history_append(&h, &m3) == ARMS_OK && history_append(&h, &m2) == ARMS_OK
           && history_append(&h, &m1) == ARMS_OK);
    check_history(&h, HIST_SORT_COMPLETION, 1, "M1,M2,M3");
    check_history(&h, HIST_SORT_COMPLETION, 0, "M3,M2,M1");
    history_clear(&h);
}

static void test_reports_searches(void)
{
    FlightStore fs;
    const char *ids[] = { "D", "B", "G", "A", "F", "C", "E" };
    store_with(&fs, ids, 7);
    const Flight *sorted[MAX_FLIGHTS];
    int n;
    assert(report_flights_sorted(&fs, FLIGHT_SORT_ID, 1, sorted, MAX_FLIGHTS, &n) == ARMS_OK && n == 7);

    /* binary search: first, middle, last, between, below, above */
    assert(report_find_flight_sorted(sorted, n, "A") == flight_find(&fs, "A"));
    assert(report_find_flight_sorted(sorted, n, "D") == flight_find(&fs, "D"));
    assert(report_find_flight_sorted(sorted, n, "G") == flight_find(&fs, "G"));
    assert(report_find_flight_sorted(sorted, n, "B") == flight_find(&fs, "B"));
    assert(report_find_flight_sorted(sorted, n, "DD") == NULL);
    assert(report_find_flight_sorted(sorted, n, "0") == NULL);
    assert(report_find_flight_sorted(sorted, n, "H") == NULL);
    assert(report_find_flight_sorted(sorted, 0, "A") == NULL);
    assert(report_find_flight_sorted(sorted, 1, "A") == sorted[0]);
    assert(report_find_flight_sorted(sorted, 1, "B") == NULL);
    assert(report_find_flight_sorted(NULL, 7, "A") == NULL);
    assert(report_find_flight_sorted(sorted, n, NULL) == NULL);

    /* existing linear searches: hit and miss */
    assert(flight_find(&fs, "E") != NULL && strcmp(flight_find(&fs, "E")->id, "E") == 0);
    assert(flight_find(&fs, "ZZ") == NULL);
    RunwayStore rs;
    runway_store_init(&rs);
    assert(runway_add(&rs, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(runway_find(&rs, "R1") != NULL && runway_find(&rs, "R9") == NULL);
    MovementHistory h;
    history_init(&h);
    Movement m = make_movement("F1", "R1");
    assert(history_append(&h, &m) == ARMS_OK);
    assert(history_find(&h, "F1") != NULL && history_find(&h, "F9") == NULL);
    history_clear(&h);
}

/* Test graph: A-B, A-C, B-D, C-D, D-E, and F isolated. Indexes A=0 .. F=5. */
static void make_test_graph(AirportGraph *g)
{
    const char *ids[] = { "A", "B", "C", "D", "E", "F" };
    graph_init(g);
    for (int i = 0; i < 6; i++) {
        assert(graph_add_node(g, ids[i], NULL) == ARMS_OK);
    }
    assert(graph_add_edge(g, "A", "B") == ARMS_OK);
    assert(graph_add_edge(g, "A", "C") == ARMS_OK);
    assert(graph_add_edge(g, "B", "D") == ARMS_OK);
    assert(graph_add_edge(g, "C", "D") == ARMS_OK);
    assert(graph_add_edge(g, "D", "E") == ARMS_OK);
}

static void test_graph_empty_and_single(void)
{
    AirportGraph g;
    int order[MAX_NODES], n = -1, r = -1;
    graph_init(&g);
    assert(graph_node_count(&g) == 0);
    assert(graph_find_node(&g, "A") == -1);
    assert(graph_node_at(&g, 0) == NULL);
    assert(graph_bfs(&g, "A", order, MAX_NODES, &n) == ARMS_ERR_NOT_FOUND && n == 0);
    assert(graph_is_reachable(&g, "A", "A", &r) == ARMS_ERR_NOT_FOUND && r == 0);

    assert(graph_add_node(&g, "RWY-09", "Runway 09 entry") == ARMS_OK);
    assert(graph_node_count(&g) == 1);
    assert(graph_find_node(&g, "RWY-09") == 0);
    assert(strcmp(graph_node_at(&g, 0)->id, "RWY-09") == 0);
    assert(strcmp(graph_node_at(&g, 0)->label, "Runway 09 entry") == 0);
    assert(graph_node_at(&g, 1) == NULL && graph_node_at(&g, -1) == NULL);
    assert(graph_bfs(&g, "RWY-09", order, MAX_NODES, &n) == ARMS_OK && n == 1 && order[0] == 0);
    assert(graph_dfs(&g, "RWY-09", order, MAX_NODES, &n) == ARMS_OK && n == 1 && order[0] == 0);
    assert(graph_is_reachable(&g, "RWY-09", "RWY-09", &r) == ARMS_OK && r == 1);
}

static void test_graph_node_rules(void)
{
    AirportGraph g;
    graph_init(&g);
    assert(graph_add_node(&g, "A", NULL) == ARMS_OK);
    assert(strcmp(graph_node_at(&g, 0)->label, "") == 0);          /* NULL label -> "" */
    assert(graph_add_node(&g, "A", "again") == ARMS_ERR_DUPLICATE);
    assert(graph_add_node(&g, "bad id", NULL) == ARMS_ERR_INVALID_ID);
    assert(graph_add_node(&g, "", NULL) == ARMS_ERR_INVALID_ID);
    assert(graph_add_node(&g, NULL, NULL) == ARMS_ERR_INVALID_ARG);
    assert(graph_add_node(NULL, "B", NULL) == ARMS_ERR_INVALID_ARG);
    assert(graph_node_count(&g) == 1);
}

static void test_graph_edge_rules(void)
{
    AirportGraph g;
    graph_init(&g);
    assert(graph_add_node(&g, "A", NULL) == ARMS_OK);
    assert(graph_add_node(&g, "B", NULL) == ARMS_OK);
    assert(!graph_adjacent(&g, "A", "B"));
    assert(graph_add_edge(&g, "A", "B") == ARMS_OK);
    assert(graph_adjacent(&g, "A", "B") && graph_adjacent(&g, "B", "A"));   /* symmetric */
    assert(graph_add_edge(&g, "A", "B") == ARMS_ERR_DUPLICATE);
    assert(graph_add_edge(&g, "B", "A") == ARMS_ERR_DUPLICATE);             /* reverse too */
    assert(graph_add_edge(&g, "A", "A") == ARMS_ERR_INVALID_ARG);           /* self-edge */
    assert(!graph_adjacent(&g, "A", "A"));
    assert(graph_add_edge(&g, "A", "Z") == ARMS_ERR_NOT_FOUND);
    assert(graph_add_edge(&g, "Z", "A") == ARMS_ERR_NOT_FOUND);
    assert(graph_add_edge(&g, "Z", "Z") == ARMS_ERR_NOT_FOUND);
    assert(graph_add_edge(&g, NULL, "A") == ARMS_ERR_INVALID_ARG);
    assert(!graph_adjacent(&g, "A", "Z"));
}

static void test_graph_bfs_dfs_order(void)
{
    AirportGraph g;
    int order[MAX_NODES], n = -1;
    make_test_graph(&g);

    const int bfs_expected[] = { 0, 1, 2, 3, 4 };          /* A B C D E */
    assert(graph_bfs(&g, "A", order, MAX_NODES, &n) == ARMS_OK);
    assert(n == 5 && memcmp(order, bfs_expected, sizeof bfs_expected) == 0);

    const int dfs_expected[] = { 0, 1, 3, 2, 4 };          /* A B D C E */
    assert(graph_dfs(&g, "A", order, MAX_NODES, &n) == ARMS_OK);
    assert(n == 5 && memcmp(order, dfs_expected, sizeof dfs_expected) == 0);

    const int bfs_from_e[] = { 4, 3, 1, 2, 0 };            /* E D B C A */
    assert(graph_bfs(&g, "E", order, MAX_NODES, &n) == ARMS_OK);
    assert(n == 5 && memcmp(order, bfs_from_e, sizeof bfs_from_e) == 0);
}

static void test_graph_disconnected_and_reachability(void)
{
    AirportGraph g;
    int order[MAX_NODES], n = -1, r = -1;
    make_test_graph(&g);

    assert(graph_bfs(&g, "F", order, MAX_NODES, &n) == ARMS_OK && n == 1 && order[0] == 5);
    assert(graph_dfs(&g, "A", order, MAX_NODES, &n) == ARMS_OK && n == 5);   /* F not reached */
    for (int i = 0; i < n; i++) {
        assert(order[i] != 5);
    }

    assert(graph_is_reachable(&g, "A", "E", &r) == ARMS_OK && r == 1);
    assert(graph_is_reachable(&g, "E", "A", &r) == ARMS_OK && r == 1);
    assert(graph_is_reachable(&g, "A", "F", &r) == ARMS_OK && r == 0);
    assert(graph_is_reachable(&g, "F", "A", &r) == ARMS_OK && r == 0);
    assert(graph_is_reachable(&g, "F", "F", &r) == ARMS_OK && r == 1);       /* start == target */
    assert(graph_is_reachable(&g, "A", "Z", &r) == ARMS_ERR_NOT_FOUND && r == 0);
    assert(graph_is_reachable(&g, "Z", "A", &r) == ARMS_ERR_NOT_FOUND && r == 0);
    assert(graph_is_reachable(&g, "Z", "Z", &r) == ARMS_ERR_NOT_FOUND && r == 0);
    assert(graph_is_reachable(&g, "A", "B", NULL) == ARMS_ERR_INVALID_ARG);
}

static void test_graph_traversal_errors(void)
{
    AirportGraph g;
    int order[MAX_NODES], n = -1;
    make_test_graph(&g);

    /* unknown start */
    for (int i = 0; i < MAX_NODES; i++) order[i] = -99;
    assert(graph_bfs(&g, "Z", order, MAX_NODES, &n) == ARMS_ERR_NOT_FOUND && n == 0);
    assert(order[0] == -99);
    n = -1;
    assert(graph_dfs(&g, "Z", order, MAX_NODES, &n) == ARMS_ERR_NOT_FOUND && n == 0);
    assert(order[0] == -99);

    /* output too small: the A..E component has 5 nodes */
    n = -1;
    assert(graph_bfs(&g, "A", order, 4, &n) == ARMS_ERR_FULL && n == 0);
    assert(order[0] == -99 && order[3] == -99);
    n = -1;
    assert(graph_dfs(&g, "A", order, 4, &n) == ARMS_ERR_FULL && n == 0);
    assert(order[0] == -99 && order[3] == -99);
    assert(graph_bfs(&g, "A", order, 5, &n) == ARMS_OK && n == 5);           /* exact fit */
    assert(graph_bfs(&g, "F", order, 0, &n) == ARMS_ERR_FULL && n == 0);     /* cap 0 */

    /* bad arguments */
    assert(graph_bfs(NULL, "A", order, MAX_NODES, &n) == ARMS_ERR_INVALID_ARG);
    assert(graph_bfs(&g, NULL, order, MAX_NODES, &n) == ARMS_ERR_INVALID_ARG);
    assert(graph_dfs(&g, "A", NULL, MAX_NODES, &n) == ARMS_ERR_INVALID_ARG);
    assert(graph_dfs(&g, "A", order, -1, &n) == ARMS_ERR_INVALID_ARG);
    assert(graph_dfs(&g, "A", order, MAX_NODES, NULL) == ARMS_ERR_INVALID_ARG);
}

static void test_graph_full(void)
{
    AirportGraph g;
    char id[8];
    graph_init(&g);
    for (int i = 0; i < MAX_NODES; i++) {
        snprintf(id, sizeof id, "N%d", i);
        assert(graph_add_node(&g, id, NULL) == ARMS_OK);
    }
    assert(graph_node_count(&g) == MAX_NODES);
    assert(graph_add_node(&g, "EXTRA", NULL) == ARMS_ERR_FULL);
    assert(graph_node_count(&g) == MAX_NODES);
    assert(graph_add_node(&g, "N0", NULL) == ARMS_ERR_DUPLICATE);            /* dup beats full */

    /* chain all nodes, then traverse the full-size graph */
    int order[MAX_NODES], n = 0;
    for (int i = 0; i + 1 < MAX_NODES; i++) {
        char a[16], b[16];
        snprintf(a, sizeof a, "N%d", i);
        snprintf(b, sizeof b, "N%d", i + 1);
        assert(graph_add_edge(&g, a, b) == ARMS_OK);
    }
    assert(graph_dfs(&g, "N0", order, MAX_NODES, &n) == ARMS_OK && n == MAX_NODES);
    for (int i = 0; i < MAX_NODES; i++) {
        assert(order[i] == i);
    }
    assert(graph_bfs(&g, "N15", order, MAX_NODES, &n) == ARMS_OK && n == MAX_NODES);
    assert(order[0] == 15 && order[MAX_NODES - 1] == 0);
}

static void test_graph_long_label_truncated(void)
{
    AirportGraph g;
    graph_init(&g);
    assert(graph_add_node(&g, "A", "0123456789012345678901234567890123456789") == ARMS_OK);
    assert(strlen(graph_node_at(&g, 0)->label) == NODE_LABEL_LEN - 1);
}

static void test_graph_unchanged_by_traversal(void)
{
    AirportGraph g, before;
    int order[MAX_NODES], n, r;
    make_test_graph(&g);
    memcpy(&before, &g, sizeof g);

    assert(graph_bfs(&g, "A", order, MAX_NODES, &n) == ARMS_OK);
    assert(graph_dfs(&g, "A", order, MAX_NODES, &n) == ARMS_OK);
    assert(graph_is_reachable(&g, "A", "F", &r) == ARMS_OK);
    assert(graph_bfs(&g, "A", order, 2, &n) == ARMS_ERR_FULL);
    assert(graph_dfs(&g, "Z", order, MAX_NODES, &n) == ARMS_ERR_NOT_FOUND);
    assert(memcmp(&before, &g, sizeof g) == 0);
}

/* ---------- Phase 10: persistence ---------- */

#define SAVE_FILE "build/test_persist.dat"
#define BAD_FILE  "build/test_persist_bad.dat"
#define TEXT_MAX  16384

static void remove_save_files(const char *path)
{
    char p[128];
    remove(path);
    snprintf(p, sizeof p, "%s.tmp", path);
    remove(p);
    snprintf(p, sizeof p, "%s.bak", path);
    remove(p);
}

static void read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    assert(f != NULL);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void write_file(const char *path, const char *text, size_t len)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fwrite(text, 1, len, f) == len);
    fclose(f);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

/* Copies src to out with the first occurrence of `from` replaced by `to`. */
static void mutate(const char *src, const char *from, const char *to, char *out, size_t size)
{
    const char *p = strstr(src, from);
    assert(p != NULL);
    size_t head = (size_t)(p - src);
    const char *tail = p + strlen(from);
    assert(head + strlen(to) + strlen(tail) < size);
    memcpy(out, src, head);
    strcpy(out + head, to);
    strcpy(out + head + strlen(to), tail);
}

static void add_and_submit(ArmsSystem *s, const char *id, FlightType t, AircraftCategory c)
{
    assert(flight_add(&s->flights, id, t, c) == ARMS_OK);
    assert(request_submit(&s->requests, &s->flights, id) == ARMS_OK);
}

/* Runs the highest-priority queued flight through schedule -> start -> complete. */
static void run_to_completion(ArmsSystem *s, int done_tick)
{
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    assert(scheduler_pull(&s->scheduler, &s->requests, &s->flights) == ARMS_OK);
    assert(ops_schedule_next(&s->scheduler, &s->flights, &s->runways, fid, rid) == ARMS_OK);
    assert(ops_start(&s->flights, &s->runways, fid) == ARMS_OK);
    s->flights.now = done_tick;
    assert(ops_complete(&s->flights, &s->runways, &s->history, fid) == ARMS_OK);
}

/* Deterministic system with every FlightState, a wrapped holding queue, a landing queue whose
 * order differs from request_seq order, a heap member, history and a graph. 15 flights. */
static void build_full_system(ArmsSystem *s)
{
    char fid[ID_MAX_LEN + 1], rid[ID_MAX_LEN + 1];
    arms_system_init(s);

    assert(runway_add(&s->runways, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(runway_add(&s->runways, "R2", AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(runway_add(&s->runways, "R3", AIRCRAFT_SMALL) == ARMS_OK);
    assert(runway_set_status(&s->runways, "R3", RUNWAY_CLOSED) == ARMS_OK);

    s->flights.now = 1;
    add_and_submit(s, "C1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    run_to_completion(s, 4);
    s->flights.now = 5;
    add_and_submit(s, "C2", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    run_to_completion(s, 9);

    /* HEAP1 -> R1 (IN_OPERATION), HEAP3 -> R2 (SCHEDULED), HEAP2 stays in the heap. */
    add_and_submit(s, "HEAP1", FLIGHT_ARRIVAL, AIRCRAFT_LARGE);
    add_and_submit(s, "HEAP2", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    add_and_submit(s, "HEAP3", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    assert(scheduler_pull(&s->scheduler, &s->requests, &s->flights) == ARMS_OK);
    assert(scheduler_set_emergency(&s->scheduler, &s->flights, "HEAP1", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(scheduler_set_fuel(&s->scheduler, &s->flights, "HEAP3", FUEL_LOW) == ARMS_OK);
    assert(ops_schedule_next(&s->scheduler, &s->flights, &s->runways, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "HEAP1") == 0 && strcmp(rid, "R1") == 0);
    assert(ops_schedule_next(&s->scheduler, &s->flights, &s->runways, fid, rid) == ARMS_OK);
    assert(strcmp(fid, "HEAP3") == 0 && strcmp(rid, "R2") == 0);
    assert(ops_start(&s->flights, &s->runways, "HEAP1") == ARMS_OK);

    assert(flight_add(&s->flights, "REG1", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);
    assert(flight_add(&s->flights, "X1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(ops_cancel(&s->scheduler, &s->holding, &s->requests, &s->flights, &s->runways, "X1") == ARMS_OK);

    add_and_submit(s, "QL1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    add_and_submit(s, "QL2", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    add_and_submit(s, "QL3", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM);
    add_and_submit(s, "QT1", FLIGHT_DEPARTURE, AIRCRAFT_LARGE);
    add_and_submit(s, "QT2", FLIGHT_DEPARTURE, AIRCRAFT_LARGE);
    add_and_submit(s, "H1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
    add_and_submit(s, "H2", FLIGHT_DEPARTURE, AIRCRAFT_SMALL);
    add_and_submit(s, "H3", FLIGHT_ARRIVAL, AIRCRAFT_SMALL);

    /* Move the holding queue's front/rear near the end of the array so H1..H3 wrap around. */
    for (int i = 0; i < CQUEUE_CAPACITY - 2; i++) {
        assert(cqueue_enqueue(&s->holding.queue, "DUMMY") == ARMS_OK);
        assert(cqueue_dequeue(&s->holding.queue, fid) == ARMS_OK);
    }
    /* QL1 goes to holding and back: it ends up at the rear of landing, after QL3. */
    assert(holding_enter(&s->holding, &s->requests, &s->flights, "QL1") == ARMS_OK);
    assert(holding_leave(&s->holding, &s->requests, &s->flights, "QL1") == ARMS_OK);
    assert(holding_enter(&s->holding, &s->requests, &s->flights, "H1") == ARMS_OK);
    assert(holding_enter(&s->holding, &s->requests, &s->flights, "H2") == ARMS_OK);
    assert(holding_enter(&s->holding, &s->requests, &s->flights, "H3") == ARMS_OK);

    assert(graph_add_node(&s->graph, "RWY1", "Runway 1 exit") == ARMS_OK);
    assert(graph_add_node(&s->graph, "TWY-A", "Taxiway A") == ARMS_OK);
    assert(graph_add_node(&s->graph, "GATE1", "Gate 1") == ARMS_OK);
    assert(graph_add_node(&s->graph, "HOLD1", NULL) == ARMS_OK);
    assert(graph_add_edge(&s->graph, "RWY1", "TWY-A") == ARMS_OK);
    assert(graph_add_edge(&s->graph, "TWY-A", "GATE1") == ARMS_OK);

    assert(flight_count(&s->flights) == 15);
    assert(ops_check_invariants(&s->flights, &s->runways, &s->scheduler));
}

/* No child may have higher priority than its parent. */
static int heap_is_valid(const MaxHeap *h, FlightStore *fs)
{
    for (int i = 1; i < h->count; i++) {
        const Flight *child = flight_find(fs, h->ids[i]);
        const Flight *parent = flight_find(fs, h->ids[(i - 1) / 2]);
        if (child == NULL || parent == NULL || flight_priority_higher(child, parent))
            return 0;
    }
    return 1;
}

/* Logical equality: same data, same queue order, same heap members, same graph. */
static void assert_systems_equal(const ArmsSystem *a, const ArmsSystem *b)
{
    assert(a->flights.count == b->flights.count);
    assert(a->flights.now == b->flights.now);
    assert(a->flights.next_seq == b->flights.next_seq);
    for (int i = 0; i < a->flights.count; i++) {
        const Flight *x = &a->flights.items[i], *y = &b->flights.items[i];
        assert(strcmp(x->id, y->id) == 0);
        assert(x->type == y->type && x->category == y->category && x->state == y->state);
        assert(x->emergency == y->emergency && x->fuel == y->fuel);
        assert(x->request_tick == y->request_tick && x->request_seq == y->request_seq);
        assert(strcmp(x->runway_id, y->runway_id) == 0);
    }

    assert(a->runways.count == b->runways.count);
    for (int i = 0; i < a->runways.count; i++) {
        const Runway *x = &a->runways.items[i], *y = &b->runways.items[i];
        assert(strcmp(x->id, y->id) == 0);
        assert(x->status == y->status && x->max_category == y->max_category);
        assert(strcmp(x->current_flight_id, y->current_flight_id) == 0);
    }

    assert(queue_count(&a->requests.landing) == queue_count(&b->requests.landing));
    for (int i = 0; i < queue_count(&a->requests.landing); i++)
        assert(strcmp(queue_at(&a->requests.landing, i), queue_at(&b->requests.landing, i)) == 0);
    assert(queue_count(&a->requests.takeoff) == queue_count(&b->requests.takeoff));
    for (int i = 0; i < queue_count(&a->requests.takeoff); i++)
        assert(strcmp(queue_at(&a->requests.takeoff, i), queue_at(&b->requests.takeoff, i)) == 0);
    assert(cqueue_count(&a->holding.queue) == cqueue_count(&b->holding.queue));
    for (int i = 0; i < cqueue_count(&a->holding.queue); i++)
        assert(strcmp(cqueue_at(&a->holding.queue, i), cqueue_at(&b->holding.queue, i)) == 0);

    assert(heap_count(&a->scheduler.heap) == heap_count(&b->scheduler.heap));
    for (int i = 0; i < a->scheduler.heap.count; i++)
        assert(heap_contains(&b->scheduler.heap, a->scheduler.heap.ids[i]));

    assert(history_count(&a->history) == history_count(&b->history));
    for (int i = 0; i < history_count(&a->history); i++) {
        const Movement *x = history_at(&a->history, i), *y = history_at(&b->history, i);
        assert(strcmp(x->flight_id, y->flight_id) == 0 && strcmp(x->runway_id, y->runway_id) == 0);
        assert(x->type == y->type && x->category == y->category);
        assert(x->request_tick == y->request_tick && x->request_seq == y->request_seq);
        assert(x->completed_tick == y->completed_tick);
    }

    assert(a->events.count == b->events.count);
    assert(a->events.next_seq == b->events.next_seq && a->events.dropped == b->events.dropped);
    for (const EventNode *x = a->events.head, *y = b->events.head; x != NULL; x = x->next, y = y->next) {
        assert(x->data.seq == y->data.seq && x->data.tick == y->data.tick);
        assert(x->data.type == y->data.type);
        assert(strcmp(x->data.flight_id, y->data.flight_id) == 0);
        assert(strcmp(x->data.runway_id, y->data.runway_id) == 0);
        assert(strcmp(x->data.details, y->data.details) == 0);
    }

    assert(a->graph.count == b->graph.count);
    for (int i = 0; i < a->graph.count; i++) {
        assert(strcmp(a->graph.nodes[i].id, b->graph.nodes[i].id) == 0);
        assert(strcmp(a->graph.nodes[i].label, b->graph.nodes[i].label) == 0);
        for (int j = 0; j < a->graph.count; j++)
            assert(a->graph.adj[i][j] == b->graph.adj[i][j]);
    }
}

static void test_persist_empty_round_trip(void)
{
    ArmsSystem empty, dest;
    arms_system_init(&empty);
    build_full_system(&dest);          /* load must replace this completely */
    remove_save_files(SAVE_FILE);

    assert(persist_save(SAVE_FILE, &empty) == ARMS_OK);
    assert(persist_load(SAVE_FILE, &dest) == ARMS_OK);
    assert(dest.flights.count == 0 && history_count(&dest.history) == 0);
    assert_systems_equal(&dest, &empty);

    arms_system_clear(&dest);
    arms_system_clear(&empty);
}

static void test_persist_full_round_trip(void)
{
    ArmsSystem a, b;
    build_full_system(&a);
    arms_system_init(&b);
    remove_save_files(SAVE_FILE);

    /* The holding queue really wraps: rear is behind front in the array. */
    assert(a.holding.queue.rear < a.holding.queue.front);

    assert(persist_save(SAVE_FILE, &a) == ARMS_OK);
    assert(!file_exists(SAVE_FILE ".tmp"));    /* temp file is gone after a successful save */
    assert(persist_load(SAVE_FILE, &b) == ARMS_OK);
    assert_systems_equal(&a, &b);

    /* Every FlightState survived. */
    int seen = 0;
    for (int i = 0; i < b.flights.count; i++)
        seen |= 1 << b.flights.items[i].state;
    assert(seen == (1 << STATE_COUNT) - 1);

    /* Exact queue order, including a landing order that differs from request_seq order. */
    assert(queue_count(&b.requests.landing) == 3);
    assert(strcmp(queue_at(&b.requests.landing, 0), "QL2") == 0);
    assert(strcmp(queue_at(&b.requests.landing, 1), "QL3") == 0);
    assert(strcmp(queue_at(&b.requests.landing, 2), "QL1") == 0);
    assert(flight_find(&b.flights, "QL1")->request_seq < flight_find(&b.flights, "QL2")->request_seq);
    assert(queue_count(&b.requests.takeoff) == 2);
    assert(strcmp(queue_at(&b.requests.takeoff, 0), "QT1") == 0);
    assert(strcmp(queue_at(&b.requests.takeoff, 1), "QT2") == 0);
    assert(cqueue_count(&b.holding.queue) == 3);
    assert(strcmp(cqueue_at(&b.holding.queue, 0), "H1") == 0);
    assert(strcmp(cqueue_at(&b.holding.queue, 1), "H2") == 0);
    assert(strcmp(cqueue_at(&b.holding.queue, 2), "H3") == 0);

    /* Heap membership and a valid heap. */
    assert(heap_count(&b.scheduler.heap) == 1 && heap_contains(&b.scheduler.heap, "HEAP2"));
    assert(heap_is_valid(&b.scheduler.heap, &b.flights));

    /* Runway links, both directions. */
    const Flight *h1 = flight_find(&b.flights, "HEAP1");
    const Flight *h3 = flight_find(&b.flights, "HEAP3");
    assert(h1->state == STATE_IN_OPERATION && strcmp(h1->runway_id, "R1") == 0);
    assert(h3->state == STATE_SCHEDULED && strcmp(h3->runway_id, "R2") == 0);
    assert(strcmp(runway_find(&b.runways, "R1")->current_flight_id, "HEAP1") == 0);
    assert(strcmp(runway_find(&b.runways, "R2")->current_flight_id, "HEAP3") == 0);
    assert(runway_find(&b.runways, "R3")->status == RUNWAY_CLOSED);
    assert(ops_check_invariants(&b.flights, &b.runways, &b.scheduler));

    /* History order. */
    assert(history_count(&b.history) == 2);
    assert(strcmp(history_at(&b.history, 0)->flight_id, "C1") == 0);
    assert(strcmp(history_at(&b.history, 1)->flight_id, "C2") == 0);

    /* Graph. */
    assert(graph_node_count(&b.graph) == 4);
    assert(strcmp(graph_node_at(&b.graph, 0)->label, "Runway 1 exit") == 0);
    assert(graph_node_at(&b.graph, 3)->label[0] == '\0');
    assert(graph_adjacent(&b.graph, "RWY1", "TWY-A") && graph_adjacent(&b.graph, "GATE1", "TWY-A"));
    assert(!graph_adjacent(&b.graph, "RWY1", "GATE1"));
    int order[MAX_NODES], n;
    assert(graph_bfs(&b.graph, "RWY1", order, MAX_NODES, &n) == ARMS_OK && n == 3);

    /* The loaded system still works: finish the in-operation flight. */
    assert(ops_complete(&b.flights, &b.runways, &b.history, "HEAP1") == ARMS_OK);
    assert(history_count(&b.history) == 3);
    assert(runway_find(&b.runways, "R1")->status == RUNWAY_AVAILABLE);
    assert(ops_check_invariants(&b.flights, &b.runways, &b.scheduler));

    arms_system_clear(&a);
    arms_system_clear(&b);
    remove_save_files(SAVE_FILE);
}

static void test_persist_save_load_save(void)
{
    static char first[TEXT_MAX], second[TEXT_MAX], backup[TEXT_MAX];
    ArmsSystem a, b;
    build_full_system(&a);
    arms_system_init(&b);
    remove_save_files(SAVE_FILE);
    remove_save_files(BAD_FILE);

    assert(persist_save(SAVE_FILE, &a) == ARMS_OK);
    assert(persist_load(SAVE_FILE, &b) == ARMS_OK);
    assert(persist_save(BAD_FILE, &b) == ARMS_OK);
    read_file(SAVE_FILE, first, sizeof first);
    read_file(BAD_FILE, second, sizeof second);
    assert(strncmp(first, "ARMS-SAVE 2\n", 12) == 0);
    assert(strstr(first, "\nEND\n") != NULL);
    assert(strcmp(first, second) == 0);

    /* A second save keeps the first as .bak and leaves no .tmp behind. */
    assert(persist_save(SAVE_FILE, &b) == ARMS_OK);
    read_file(SAVE_FILE ".bak", backup, sizeof backup);
    assert(strcmp(backup, first) == 0);
    assert(!file_exists(SAVE_FILE ".tmp"));

    arms_system_clear(&a);
    arms_system_clear(&b);
    remove_save_files(SAVE_FILE);
    remove_save_files(BAD_FILE);
}

static void test_persist_capacity_round_trip(void)
{
    ArmsSystem a, b;
    char id[ID_MAX_LEN + 1];
    arms_system_init(&a);
    arms_system_init(&b);
    for (int i = 0; i < MAX_FLIGHTS; i++) {
        snprintf(id, sizeof id, "F%d", i);
        assert(flight_add(&a.flights, id, FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    }
    remove_save_files(SAVE_FILE);
    assert(persist_save(SAVE_FILE, &a) == ARMS_OK);
    assert(persist_load(SAVE_FILE, &b) == ARMS_OK);
    assert(flight_count(&b.flights) == MAX_FLIGHTS);
    assert_systems_equal(&a, &b);
    remove_save_files(SAVE_FILE);
}

static void test_persist_missing_and_io_errors(void)
{
    ArmsSystem dest, ref;
    build_full_system(&dest);
    build_full_system(&ref);
    remove_save_files(SAVE_FILE);

    assert(persist_load(SAVE_FILE, &dest) == ARMS_ERR_NOT_FOUND);
    assert_systems_equal(&dest, &ref);
    assert(persist_load(NULL, &dest) == ARMS_ERR_INVALID_ARG);
    assert(persist_save(NULL, &dest) == ARMS_ERR_INVALID_ARG);

    /* Directory does not exist: save fails with IO. */
    assert(persist_save("build/no_such_dir/arms.dat", &dest) == ARMS_ERR_IO);

    /* A label with a line break would corrupt the file: refused before anything is written. */
    assert(graph_add_node(&dest.graph, "BAD", "two\nlines") == ARMS_OK);
    assert(persist_save(SAVE_FILE, &dest) == ARMS_ERR_INVALID_ARG);
    assert(!file_exists(SAVE_FILE) && !file_exists(SAVE_FILE ".tmp"));

    arms_system_clear(&dest);
    arms_system_clear(&ref);
}

typedef struct {
    const char *from;
    const char *to;
    ArmsStatus  expect;
} Mutation;

static void test_persist_rejects_bad_files(void)
{
    static char good[TEXT_MAX], bad[TEXT_MAX];
    static const Mutation cases[] = {
        /* header / syntax */
        { "ARMS-SAVE 2", "ARMS-SAVE 9", ARMS_ERR_FORMAT },
        { "FLIGHTS 15 ", "FLIGHTS x ", ARMS_ERR_FORMAT },
        { "R R1 OCCUPIED LARGE HEAP1", "R R1 OCCUPIED LARGE", ARMS_ERR_FORMAT },
        { "END\n", "", ARMS_ERR_FORMAT },                        /* truncated: no END */
        { "END\n", "END\nextra\n", ARMS_ERR_FORMAT },            /* trailing junk */
        /* values */
        { "F C1 ARRIVAL", "F C1 SIDEWAYS", ARMS_ERR_FORMAT },    /* bad enum */
        { "F REG1 ", "F reg1 ", ARMS_ERR_INVALID_ID },
        /* duplicates */
        { "F C2 ", "F C1 ", ARMS_ERR_DUPLICATE },
        { "M C2 ", "M C1 ", ARMS_ERR_DUPLICATE },
        /* two submitted flights sharing a non-zero request_seq (HEAP1 has 2, HEAP2 has 3) */
        { "F HEAP2 DEPARTURE SMALL WAITING NONE NORMAL 9 3 ~",
          "F HEAP2 DEPARTURE SMALL WAITING NONE NORMAL 9 2 ~", ARMS_ERR_FORMAT },
        /* capacity */
        { "FLIGHTS 15 ", "FLIGHTS 101 ", ARMS_ERR_FULL },
        { "RUNWAYS 3\n", "RUNWAYS 11\n", ARMS_ERR_FULL },
        { "NODES 4\n", "NODES 17\n", ARMS_ERR_FULL },
        { "LANDING 3\n", "LANDING 101\n", ARMS_ERR_FULL },
        /* duplicate active-structure membership */
        { "L QL2\nL QL3\n", "L QL2\nL QL2\n", ARMS_ERR_DUPLICATE },
        { "HEAP 1\nP HEAP2\n", "HEAP 2\nP HEAP2\nP QL1\n", ARMS_ERR_DUPLICATE },
        /* wrong state / type for a structure, or missing membership */
        { "LANDING 3\n", "LANDING 4\nL QT1\n", ARMS_ERR_FORMAT },         /* DEPARTURE in landing */
        { "HEAP 1\nP HEAP2\n", "HEAP 1\nP HEAP1\n", ARMS_ERR_FORMAT },    /* IN_OPERATION in heap */
        { "HOLDING 3\n", "HOLDING 4\nH REG1\n", ARMS_ERR_FORMAT },        /* REGISTERED in holding */
        { "LANDING 3\nL QL2\nL QL3\nL QL1\n", "LANDING 2\nL QL2\nL QL3\n", ARMS_ERR_FORMAT },
        /* runway links */
        { "R R1 OCCUPIED LARGE HEAP1", "R R1 OCCUPIED LARGE HEAP3", ARMS_ERR_FORMAT },
        { "R R1 OCCUPIED LARGE HEAP1", "R R1 AVAILABLE LARGE ~", ARMS_ERR_FORMAT },
        { "M C1 ARRIVAL MEDIUM R1", "M C1 ARRIVAL MEDIUM R9", ARMS_ERR_FORMAT },
        { "M C1 ", "M HEAP1 ", ARMS_ERR_FORMAT },                         /* flight not COMPLETED */
        /* graph */
        { "E RWY1 TWY-A", "E RWY1 NOPE", ARMS_ERR_FORMAT },
        { "E RWY1 TWY-A\n", "E RWY1 TWY-A\nE TWY-A RWY1\n", ARMS_ERR_DUPLICATE },
    };
    ArmsSystem src, dest, ref;
    build_full_system(&src);
    build_full_system(&ref);
    remove_save_files(SAVE_FILE);
    assert(persist_save(SAVE_FILE, &src) == ARMS_OK);
    read_file(SAVE_FILE, good, sizeof good);

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mutate(good, cases[i].from, cases[i].to, bad, sizeof bad);
        write_file(BAD_FILE, bad, strlen(bad));
        build_full_system(&dest);
        ArmsStatus st = persist_load(BAD_FILE, &dest);
        if (st != cases[i].expect)
            printf("bad-file case %zu (%s): got %s\n", i, cases[i].from, arms_status_str(st));
        assert(st == cases[i].expect);
        assert_systems_equal(&dest, &ref);           /* destination unchanged */
        arms_system_clear(&dest);
    }

    /* Truncated mid-file, and an empty file. */
    build_full_system(&dest);
    write_file(BAD_FILE, good, strlen(good) / 2);
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    write_file(BAD_FILE, "", 0);
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    /* History node allocation fails during load: NO_MEMORY, destination unchanged. */
    build_full_system(&dest);
    history_fail_next_alloc = 1;
    assert(persist_load(SAVE_FILE, &dest) == ARMS_ERR_NO_MEMORY);
    assert(history_fail_next_alloc == 0);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    arms_system_clear(&src);
    arms_system_clear(&ref);
    remove_save_files(SAVE_FILE);
    remove_save_files(BAD_FILE);
}

/* ======================================================================= */
/* Event log and service layer                                             */
/* ======================================================================= */

static const Event *event_at(const EventLog *log, int index)
{
    const EventNode *n = log->head;
    while (index-- > 0)
        n = n->next;
    return &n->data;
}

static void test_eventlog_basic(void)
{
    EventLog log;
    eventlog_init(&log);
    assert(eventlog_count(&log) == 0 && log.head == NULL && log.tail == NULL);
    assert(log.next_seq == 1 && log.dropped == 0);

    assert(eventlog_append(&log, 5, EVENT_FLIGHT_REGISTERED, "F1", NULL, "ARRIVAL") == ARMS_OK);
    assert(eventlog_count(&log) == 1 && log.head == log.tail);
    const Event *e = event_at(&log, 0);
    assert(e->seq == 1 && e->tick == 5 && e->type == EVENT_FLIGHT_REGISTERED);
    assert(strcmp(e->flight_id, "F1") == 0 && e->runway_id[0] == '\0');
    assert(strcmp(e->details, "ARRIVAL") == 0);

    /* Chronological order and consecutive sequence numbers. */
    assert(eventlog_append(&log, 6, EVENT_RUNWAY_ADDED, NULL, "R1", "LARGE") == ARMS_OK);
    assert(eventlog_append(&log, 7, EVENT_OP_STARTED, "F1", "R1", NULL) == ARMS_OK);
    assert(eventlog_count(&log) == 3 && log.next_seq == 4);
    for (int i = 0; i < 3; i++)
        assert(event_at(&log, i)->seq == (unsigned)i + 1);
    assert(event_at(&log, 1)->type == EVENT_RUNWAY_ADDED && log.tail->data.seq == 3);

    /* Invalid arguments change nothing. */
    assert(eventlog_append(NULL, 0, EVENT_HOLD_LEFT, NULL, NULL, NULL) == ARMS_ERR_INVALID_ARG);
    assert(eventlog_append(&log, 0, EVENT_TYPE_COUNT, NULL, NULL, NULL) == ARMS_ERR_INVALID_ARG);
    assert(eventlog_count(&log) == 3 && log.next_seq == 4 && log.dropped == 0);

    eventlog_clear(&log);
    assert(eventlog_count(&log) == 0 && log.head == NULL && log.next_seq == 1);
    eventlog_clear(&log);                       /* clearing an empty log is fine */
}

static void test_eventlog_type_names(void)
{
    for (int t = 0; t < EVENT_TYPE_COUNT; t++) {
        EventType back;
        const char *name = eventlog_type_name((EventType)t);
        assert(strcmp(name, "UNKNOWN") != 0);
        assert(eventlog_type_parse(name, &back) && (int)back == t);
        for (int u = 0; u < t; u++)
            assert(strcmp(name, eventlog_type_name((EventType)u)) != 0);
    }
    EventType x;
    assert(!eventlog_type_parse("NOPE", &x) && !eventlog_type_parse("", &x));
    assert(strcmp(eventlog_type_name(EVENT_TYPE_COUNT), "UNKNOWN") == 0);
    assert(strcmp(eventlog_type_name(EVENT_FLIGHT_REGISTERED), "FLIGHT_REGISTERED") == 0);
}

static void test_eventlog_details_safety(void)
{
    EventLog log;
    eventlog_init(&log);
    assert(eventlog_append(&log, 0, EVENT_FUEL_CHANGED, "F1", NULL,
                           "THIS DETAILS TEXT IS WAY TOO LONG TO FIT") == ARMS_OK);
    const Event *e = event_at(&log, 0);
    assert(strlen(e->details) == EVENT_DETAILS_LEN - 1);
    assert(strchr(e->details, ' ') == NULL);                 /* spaces became '_' */
    assert(strncmp(e->details, "THIS_DETAILS_TEXT_IS", 20) == 0);
    assert(eventlog_append(&log, 0, EVENT_FUEL_CHANGED, "F1", NULL, "A\tB\nC") == ARMS_OK);
    assert(strcmp(event_at(&log, 1)->details, "A_B_C") == 0);
    /* An over-long ID is truncated, never overflowed. */
    assert(eventlog_append(&log, 0, EVENT_HOLD_LEFT, "AAAAAAAAAAAAAAAAAAAAAAAA", NULL, NULL) == ARMS_OK);
    assert(strlen(event_at(&log, 2)->flight_id) == ID_MAX_LEN);
    eventlog_clear(&log);
}

static void test_eventlog_alloc_failure(void)
{
    EventLog log;
    eventlog_init(&log);
    assert(eventlog_append(&log, 0, EVENT_HOLD_LEFT, "F1", NULL, NULL) == ARMS_OK);
    eventlog_fail_next_alloc = 1;
    assert(eventlog_append(&log, 0, EVENT_HOLD_LEFT, "F2", NULL, NULL) == ARMS_ERR_NO_MEMORY);
    assert(eventlog_count(&log) == 1 && log.dropped == 1 && log.next_seq == 2);
    assert(eventlog_append(&log, 0, EVENT_HOLD_LEFT, "F3", NULL, NULL) == ARMS_OK);
    assert(event_at(&log, 1)->seq == 2);                     /* no gap in the sequence */
    eventlog_clear(&log);
}

/* Counts events of one type in a system's log. */
static int count_type(const ArmsSystem *s, EventType t)
{
    int n = 0;
    for (const EventNode *e = s->events.head; e != NULL; e = e->next)
        n += e->data.type == t;
    return n;
}

static void test_service_logs_one_event_each(void)
{
    ArmsSystem s;
    arms_system_init(&s);
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];

    assert(svc_flight_add(&s, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(svc_runway_add(&s, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(svc_request_submit(&s, "F1") == ARMS_OK);
    assert(svc_hold_enter(&s, "F1") == ARMS_OK);
    assert(svc_hold_leave(&s, "F1") == ARMS_OK);
    assert(svc_set_emergency(&s, "F1", EMERGENCY_ACTIVE) == ARMS_OK);
    assert(svc_set_fuel(&s, "F1", FUEL_LOW) == ARMS_OK);
    assert(scheduler_pull(&s.scheduler, &s.requests, &s.flights) == ARMS_OK);   /* not logged */
    assert(svc_schedule_next(&s, f, r) == ARMS_OK);
    assert(svc_start(&s, "F1") == ARMS_OK);
    assert(svc_complete(&s, "F1") == ARMS_OK);
    assert(svc_runway_set_status(&s, "R1", RUNWAY_CLOSED) == ARMS_OK);
    assert(svc_runway_set_status(&s, "R1", RUNWAY_AVAILABLE) == ARMS_OK);
    assert(svc_flight_add(&s, "F2", FLIGHT_DEPARTURE, AIRCRAFT_SMALL) == ARMS_OK);
    assert(svc_cancel(&s, "F2") == ARMS_OK);

    static const EventType want[] = {
        EVENT_FLIGHT_REGISTERED, EVENT_RUNWAY_ADDED, EVENT_REQUEST_SUBMITTED, EVENT_HOLD_ENTERED,
        EVENT_HOLD_LEFT, EVENT_EMERGENCY_CHANGED, EVENT_FUEL_CHANGED, EVENT_FLIGHT_SCHEDULED,
        EVENT_OP_STARTED, EVENT_OP_COMPLETED, EVENT_RUNWAY_CLOSED, EVENT_RUNWAY_REOPENED,
        EVENT_FLIGHT_REGISTERED, EVENT_FLIGHT_CANCELLED
    };
    int n = (int)(sizeof want / sizeof want[0]);
    assert(eventlog_count(&s.events) == n);                  /* the pull added nothing */
    for (int i = 0; i < n; i++) {
        assert(event_at(&s.events, i)->type == want[i]);
        assert(event_at(&s.events, i)->seq == (unsigned)i + 1);
    }

    /* Ids and details are captured correctly, including the runway cleared by completion. */
    assert(strcmp(event_at(&s.events, 0)->details, "ARRIVAL") == 0);
    assert(strcmp(event_at(&s.events, 2)->details, "LANDING") == 0);
    assert(strcmp(event_at(&s.events, 5)->details, "ACTIVE") == 0);
    assert(strcmp(event_at(&s.events, 6)->details, "LOW") == 0);
    assert(strcmp(event_at(&s.events, 7)->flight_id, "F1") == 0);
    assert(strcmp(event_at(&s.events, 7)->runway_id, "R1") == 0);
    assert(strcmp(event_at(&s.events, 9)->runway_id, "R1") == 0);
    assert(strcmp(event_at(&s.events, 10)->details, "CLOSED") == 0);
    assert(strcmp(event_at(&s.events, 13)->details, "REGISTERED") == 0);   /* state before */
    assert(ops_check_invariants(&s.flights, &s.runways, &s.scheduler));
    arms_system_clear(&s);
}

static void test_service_failures_log_nothing(void)
{
    ArmsSystem s;
    arms_system_init(&s);
    assert(svc_flight_add(&s, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(svc_runway_add(&s, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    int base = eventlog_count(&s.events);
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];

    assert(svc_flight_add(&s, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_ERR_DUPLICATE);
    assert(svc_flight_add(&s, "bad id", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) != ARMS_OK);
    assert(svc_runway_add(&s, "R1", AIRCRAFT_LARGE) == ARMS_ERR_DUPLICATE);
    assert(svc_runway_set_status(&s, "NOPE", RUNWAY_CLOSED) != ARMS_OK);
    assert(svc_request_submit(&s, "NOPE") != ARMS_OK);
    assert(svc_hold_enter(&s, "F1") != ARMS_OK);              /* REGISTERED, not WAITING */
    assert(svc_hold_leave(&s, "F1") != ARMS_OK);
    assert(svc_set_emergency(&s, "NOPE", EMERGENCY_ACTIVE) != ARMS_OK);
    assert(svc_set_fuel(&s, "NOPE", FUEL_LOW) != ARMS_OK);
    assert(svc_schedule_next(&s, f, r) == ARMS_ERR_EMPTY);
    assert(svc_start(&s, "F1") != ARMS_OK);
    assert(svc_complete(&s, "F1") != ARMS_OK);
    assert(svc_cancel(&s, "NOPE") != ARMS_OK);
    assert(svc_start(&s, NULL) != ARMS_OK);
    assert(eventlog_count(&s.events) == base && s.events.dropped == 0);

    /* Unchanged emergency / fuel values are not events. */
    assert(svc_set_emergency(&s, "F1", EMERGENCY_NONE) == ARMS_OK);
    assert(svc_set_fuel(&s, "F1", FUEL_NORMAL) == ARMS_OK);
    assert(eventlog_count(&s.events) == base);
    assert(svc_set_fuel(&s, "F1", FUEL_CRITICAL) == ARMS_OK);
    assert(svc_set_fuel(&s, "F1", FUEL_CRITICAL) == ARMS_OK);
    assert(count_type(&s, EVENT_FUEL_CHANGED) == 1);
    arms_system_clear(&s);
}

static void test_service_log_failure_keeps_operation(void)
{
    ArmsSystem s;
    arms_system_init(&s);
    assert(svc_flight_add(&s, "F1", FLIGHT_ARRIVAL, AIRCRAFT_MEDIUM) == ARMS_OK);
    assert(svc_runway_add(&s, "R1", AIRCRAFT_LARGE) == ARMS_OK);
    assert(svc_request_submit(&s, "F1") == ARMS_OK);
    assert(scheduler_pull(&s.scheduler, &s.requests, &s.flights) == ARMS_OK);
    int base = eventlog_count(&s.events);
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];

    eventlog_fail_next_alloc = 1;
    assert(svc_schedule_next(&s, f, r) == ARMS_OK);          /* the operation still succeeds */
    assert(strcmp(f, "F1") == 0 && strcmp(r, "R1") == 0);
    assert(flight_find(&s.flights, "F1")->state == STATE_SCHEDULED);
    assert(runway_find(&s.runways, "R1")->status == RUNWAY_OCCUPIED);
    assert(eventlog_count(&s.events) == base && s.events.dropped == 1);
    assert(s.events.next_seq == (unsigned)base + 1);
    assert(ops_check_invariants(&s.flights, &s.runways, &s.scheduler));

    assert(svc_start(&s, "F1") == ARMS_OK);                  /* the next event is fine */
    assert(eventlog_count(&s.events) == base + 1);
    assert(event_at(&s.events, base)->seq == (unsigned)base + 1);   /* no gap */

    eventlog_fail_next_alloc = 1;
    assert(svc_complete(&s, "F1") == ARMS_OK);
    assert(history_count(&s.history) == 1 && s.events.dropped == 2);
    assert(ops_check_invariants(&s.flights, &s.runways, &s.scheduler));
    arms_system_clear(&s);
}

/* The same action script, run twice, must give identical logs. */
static void run_script(ArmsSystem *s)
{
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];
    arms_system_init(s);
    svc_runway_add(s, "R1", AIRCRAFT_LARGE);
    for (int i = 0; i < 3; i++) {
        char id[8];
        snprintf(id, sizeof id, "F%d", i);
        svc_flight_add(s, id, i % 2 ? FLIGHT_DEPARTURE : FLIGHT_ARRIVAL, AIRCRAFT_SMALL);
        svc_request_submit(s, id);
    }
    svc_set_emergency(s, "F2", EMERGENCY_ACTIVE);
    for (int i = 0; i < 3; i++) {
        scheduler_pull(&s->scheduler, &s->requests, &s->flights);
        if (svc_schedule_next(s, f, r) == ARMS_OK) {
            svc_start(s, f);
            svc_complete(s, f);
        }
    }
}

static void test_service_deterministic_order(void)
{
    ArmsSystem a, b;
    run_script(&a);
    run_script(&b);
    assert(eventlog_count(&a.events) == eventlog_count(&b.events));
    assert(eventlog_count(&a.events) > 10);
    assert_systems_equal(&a, &b);
    /* The emergency flight F2 is scheduled first. */
    const EventNode *e = a.events.head;
    while (e != NULL && e->data.type != EVENT_FLIGHT_SCHEDULED)
        e = e->next;
    assert(e != NULL && strcmp(e->data.flight_id, "F2") == 0);
    arms_system_clear(&a);
    arms_system_clear(&b);
}

static void test_persist_events(void)
{
    static char good[TEXT_MAX], bad[TEXT_MAX];
    ArmsSystem src, dest, ref;
    arms_system_init(&src);
    run_script(&src);
    src.events.dropped = 3;                                  /* must survive the round trip */
    remove_save_files(SAVE_FILE);
    assert(persist_save(SAVE_FILE, &src) == ARMS_OK);
    read_file(SAVE_FILE, good, sizeof good);
    assert(strstr(good, "\nEVENTS ") != NULL);
    assert(strstr(good, "\nV 1 0 RUNWAY_ADDED ~ R1 LARGE\n") != NULL);

    arms_system_init(&dest);
    assert(persist_load(SAVE_FILE, &dest) == ARMS_OK);
    assert_systems_equal(&src, &dest);
    assert(dest.events.dropped == 3 && dest.events.next_seq == src.events.next_seq);
    /* A loaded log keeps counting from next_seq. */
    assert(svc_flight_add(&dest, "NEW1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(dest.events.tail->data.seq == src.events.next_seq);
    arms_system_clear(&dest);

    /* An empty log round-trips, replacing whatever the destination had. */
    arms_system_clear(&src);
    assert(persist_save(SAVE_FILE, &src) == ARMS_OK);
    arms_system_init(&dest);
    assert(svc_flight_add(&dest, "X", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(persist_load(SAVE_FILE, &dest) == ARMS_OK);
    assert(dest.events.count == 0 && dest.events.next_seq == 1);
    arms_system_clear(&dest);

    /* Malformed EVENTS sections are rejected and leave the destination unchanged. */
    run_script(&src);
    assert(persist_save(SAVE_FILE, &src) == ARMS_OK);
    read_file(SAVE_FILE, good, sizeof good);
    static const Mutation cases[] = {
        { "V 2 ", "V 1 ", ARMS_ERR_FORMAT },                         /* duplicate seq */
        { "V 3 ", "V 9 ", ARMS_ERR_FORMAT },                         /* seq not increasing */
        { "RUNWAY_ADDED", "RUNWAY_ADDEDX", ARMS_ERR_FORMAT },        /* unknown type name */
        { "RUNWAY_ADDED ~ R1", "RUNWAY_ADDED ~ r1", ARMS_ERR_INVALID_ID },
        { "RUNWAY_ADDED ~ R1 LARGE", "RUNWAY_ADDED ~ R1 LA RGE", ARMS_ERR_FORMAT },
        { "V 1 0 RUNWAY_ADDED", "V 1 -1 RUNWAY_ADDED", ARMS_ERR_FORMAT },
        { "RUNWAY_ADDED ~ R1 LARGE", "RUNWAY_ADDED ~ R1", ARMS_ERR_FORMAT },
        { "EVENTS ", "EVENTS 99", ARMS_ERR_FORMAT },                 /* count too big */
        { "EVENTS ", "EVENTS x", ARMS_ERR_FORMAT },
        { "END\n", "", ARMS_ERR_FORMAT },
    };
    build_full_system(&ref);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mutate(good, cases[i].from, cases[i].to, bad, sizeof bad);
        write_file(BAD_FILE, bad, strlen(bad));
        build_full_system(&dest);
        ArmsStatus st = persist_load(BAD_FILE, &dest);
        if (st != cases[i].expect)
            printf("events case %zu (%s): got %s\n", i, cases[i].from, arms_status_str(st));
        assert(st == cases[i].expect);
        assert_systems_equal(&dest, &ref);
        arms_system_clear(&dest);
    }

    /* next_seq must be above every stored seq; dropped must parse. */
    char hdr[64], bad_hdr[64];
    snprintf(hdr, sizeof hdr, "EVENTS %d %u 0", src.events.count, src.events.next_seq);
    snprintf(bad_hdr, sizeof bad_hdr, "EVENTS %d %u 0", src.events.count, src.events.count);
    mutate(good, hdr, bad_hdr, bad, sizeof bad);
    write_file(BAD_FILE, bad, strlen(bad));
    build_full_system(&dest);
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    snprintf(bad_hdr, sizeof bad_hdr, "EVENTS %d %u zz", src.events.count, src.events.next_seq);
    mutate(good, hdr, bad_hdr, bad, sizeof bad);
    write_file(BAD_FILE, bad, strlen(bad));
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    /* Event allocation fails during load: NO_MEMORY, destination unchanged. */
    build_full_system(&dest);
    eventlog_fail_next_alloc = 1;
    assert(persist_load(SAVE_FILE, &dest) == ARMS_ERR_NO_MEMORY);
    assert(eventlog_fail_next_alloc == 0);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    arms_system_clear(&src);
    arms_system_clear(&ref);
    remove_save_files(SAVE_FILE);
    remove_save_files(BAD_FILE);
}

/* A version-1 file (no EVENTS section) still loads, with an empty log. */
static void test_persist_v1_compat(void)
{
    static char text[TEXT_MAX], v1[TEXT_MAX], v2_no_events[TEXT_MAX];
    ArmsSystem src, dest, ref;
    build_full_system(&src);
    build_full_system(&ref);
    remove_save_files(SAVE_FILE);
    assert(persist_save(SAVE_FILE, &src) == ARMS_OK);
    read_file(SAVE_FILE, text, sizeof text);

    char *ev = strstr(text, "EVENTS ");
    assert(ev != NULL);
    strcpy(v2_no_events, text);
    strcpy(v2_no_events + (ev - text), "END\n");             /* v2 header, EVENTS removed */
    mutate(v2_no_events, "ARMS-SAVE 2", "ARMS-SAVE 1", v1, sizeof v1);
    write_file(BAD_FILE, v1, strlen(v1));

    arms_system_init(&dest);
    assert(svc_flight_add(&dest, "OLD", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(persist_load(BAD_FILE, &dest) == ARMS_OK);        /* replaces everything */
    assert(dest.events.count == 0 && dest.events.next_seq == 1 && dest.events.dropped == 0);
    assert(dest.flights.count == src.flights.count);
    assert(svc_flight_add(&dest, "AFTER1", FLIGHT_ARRIVAL, AIRCRAFT_SMALL) == ARMS_OK);
    assert(dest.events.head->data.seq == 1);
    arms_system_clear(&dest);

    /* Neither version is quietly read as the other: a v2 header without EVENTS is
     * truncated, and a v1 header followed by EVENTS is trailing junk. */
    write_file(BAD_FILE, v2_no_events, strlen(v2_no_events));
    build_full_system(&dest);
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    mutate(text, "ARMS-SAVE 2", "ARMS-SAVE 1", v1, sizeof v1);
    write_file(BAD_FILE, v1, strlen(v1));
    build_full_system(&dest);
    assert(persist_load(BAD_FILE, &dest) == ARMS_ERR_FORMAT);
    assert_systems_equal(&dest, &ref);
    arms_system_clear(&dest);

    arms_system_clear(&src);
    arms_system_clear(&ref);
    remove_save_files(SAVE_FILE);
    remove_save_files(BAD_FILE);
}

/* ---- demo scenario, end to end ---- */

static int count_state(const FlightStore *fs, FlightState st)
{
    int n = 0;
    for (int i = 0; i < flight_count(fs); i++)
        if (flight_at(fs, i)->state == st)
            n++;
    return n;
}

static int count_available_runways(const RunwayStore *rs)
{
    int n = 0;
    for (int i = 0; i < runway_count(rs); i++)
        if (runway_at(rs, i)->status == RUNWAY_AVAILABLE)
            n++;
    return n;
}

static int count_emergencies(const FlightStore *fs)
{
    int n = 0;
    for (int i = 0; i < flight_count(fs); i++)
        if (flight_at(fs, i)->emergency == EMERGENCY_ACTIVE)
            n++;
    return n;
}

static void test_demo_end_to_end(void)
{
    ArmsSystem a, b;
    char f[ID_MAX_LEN + 1], r[ID_MAX_LEN + 1];
    int order[MAX_NODES], n, reach;

    assert(demo_build(&a) == ARMS_OK);
    arms_system_init(&b);

    /* Headline counts. */
    assert(flight_count(&a.flights) == 11);
    assert(runway_count(&a.runways) == 3);
    assert(count_state(&a.flights, STATE_WAITING) == 7);
    assert(count_state(&a.flights, STATE_HOLDING) == 1);
    assert(count_state(&a.flights, STATE_REGISTERED) == 2);
    assert(count_state(&a.flights, STATE_COMPLETED) == 1);
    assert(count_state(&a.flights, STATE_SCHEDULED) == 0);
    assert(count_state(&a.flights, STATE_IN_OPERATION) == 0);
    assert(count_emergencies(&a.flights) == 1);
    assert(count_available_runways(&a.runways) == 2);
    assert(runway_find(&a.runways, "RWY-03")->status == RUNWAY_CLOSED);
    assert(history_count(&a.history) == 1);
    assert(history_find(&a.history, "AI101") != NULL);

    /* Structures: heap membership, valid heap, emergency on top. */
    assert(heap_count(&a.scheduler.heap) == 5);
    const char *in_heap[] = {"BA202", "AF150", "KL330", "SQ610", "EK505"};
    for (int i = 0; i < 5; i++)
        assert(scheduler_contains(&a.scheduler, in_heap[i]));
    assert(heap_is_valid(&a.scheduler.heap, &a.flights));
    assert(strcmp(scheduler_peek(&a.scheduler), "AF150") == 0);
    assert(queue_count(&a.requests.landing) == 1);   /* DL410 */
    assert(queue_count(&a.requests.takeoff) == 1);   /* TK150 */
    assert(holding_count(&a.holding) == 1);
    assert(flight_find(&a.flights, "LH720")->state == STATE_HOLDING);
    assert(flight_find(&a.flights, "UA901")->state == STATE_REGISTERED);
    assert(flight_find(&a.flights, "QR077")->state == STATE_REGISTERED);
    assert(ops_check_invariants(&a.flights, &a.runways, &a.scheduler));

    /* Graph: populated, MAINT isolated. */
    assert(graph_node_count(&a.graph) == 8);
    assert(graph_bfs(&a.graph, "TERMINAL", order, MAX_NODES, &n) == ARMS_OK && n == 7);
    assert(graph_dfs(&a.graph, "TERMINAL", order, MAX_NODES, &n) == ARMS_OK && n == 7);
    assert(graph_is_reachable(&a.graph, "TERMINAL", "RWY-02-ENT", &reach) == ARMS_OK && reach);
    assert(graph_is_reachable(&a.graph, "TERMINAL", "MAINT", &reach) == ARMS_OK && !reach);

    /* Event log: one event per successful svc_* call made by demo_build. */
    assert(eventlog_count(&a.events) == 31);
    assert(a.events.dropped == 0);

    /* Pull the two queued flights, then check the full priority order via the report. */
    assert(scheduler_pull(&a.scheduler, &a.requests, &a.flights) == ARMS_OK);
    assert(heap_count(&a.scheduler.heap) == 7);
    assert(heap_is_valid(&a.scheduler.heap, &a.flights));
    const Flight *sorted[MAX_FLIGHTS];
    assert(report_flights_sorted(&a.flights, FLIGHT_SORT_PRIORITY, 1, sorted, MAX_FLIGHTS, &n) == ARMS_OK);
    const char *expect[] = {"AF150", "BA202", "KL330", "DL410", "SQ610", "EK505", "TK150"};
    int k = 0;
    for (int i = 0; i < n; i++)
        if (sorted[i]->state == STATE_WAITING) {
            assert(k < 7);
            assert(strcmp(sorted[i]->id, expect[k++]) == 0);
        }
    assert(k == 7);

    /* AF150 takes RWY-01 (first compatible runway). */
    int events_before = eventlog_count(&a.events);
    assert(svc_schedule_next(&a, f, r) == ARMS_OK);
    assert(strcmp(f, "AF150") == 0 && strcmp(r, "RWY-01") == 0);
    assert(svc_start(&a, "AF150") == ARMS_OK);
    assert(flight_find(&a.flights, "AF150")->state == STATE_IN_OPERATION);

    /* BA202 (LARGE) is blocked: RWY-01 busy, RWY-02 too small. KL330 (SMALL) is the next
     * eligible flight and uses RWY-02. BA202 stays WAITING in the heap. */
    assert(svc_schedule_next(&a, f, r) == ARMS_OK);
    assert(strcmp(f, "KL330") == 0 && strcmp(r, "RWY-02") == 0);
    assert(flight_find(&a.flights, "BA202")->state == STATE_WAITING);
    assert(scheduler_contains(&a.scheduler, "BA202"));
    assert(ops_check_invariants(&a.flights, &a.runways, &a.scheduler));

    /* Complete AF150: runway released, history and events grow. */
    assert(svc_complete(&a, "AF150") == ARMS_OK);
    assert(runway_find(&a.runways, "RWY-01")->status == RUNWAY_AVAILABLE);
    assert(flight_find(&a.flights, "AF150")->state == STATE_COMPLETED);
    assert(history_count(&a.history) == 2);
    assert(eventlog_count(&a.events) == events_before + 4);   /* 2 schedule + start + complete */
    assert(ops_check_invariants(&a.flights, &a.runways, &a.scheduler));

    /* Save, load into another system, compare. */
    remove_save_files(SAVE_FILE);
    assert(persist_save(SAVE_FILE, &a) == ARMS_OK);
    assert(persist_load(SAVE_FILE, &b) == ARMS_OK);
    assert_systems_equal(&a, &b);
    assert(history_count(&b.history) == 2);
    assert(eventlog_count(&b.events) == eventlog_count(&a.events));
    assert(holding_count(&b.holding) == 1);
    assert(ops_check_invariants(&b.flights, &b.runways, &b.scheduler));

    arms_system_clear(&a);
    arms_system_clear(&b);
    remove_save_files(SAVE_FILE);

    /* Deterministic: two builds give the same system. */
    assert(demo_build(&a) == ARMS_OK);
    assert(demo_build(&b) == ARMS_OK);
    assert_systems_equal(&a, &b);
    arms_system_clear(&a);
    arms_system_clear(&b);
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
    test_ops_schedule_basic();
    test_ops_priority_order();
    test_ops_runway_selection();
    test_ops_runway_unavailable();
    test_ops_blocked_top_skipped();
    test_ops_start_complete();
    test_ops_cancel();
    test_ops_atomic_failures();
    test_ops_invariant_checker();
    test_history_list_basic();
    test_history_from_completion();
    test_history_alloc_failure_in_completion();
    test_reports_flights_by_id();
    test_reports_flights_by_request_and_priority();
    test_reports_flight_errors_and_capacity();
    test_reports_history();
    test_reports_searches();
    test_graph_empty_and_single();
    test_graph_node_rules();
    test_graph_edge_rules();
    test_graph_bfs_dfs_order();
    test_graph_disconnected_and_reachability();
    test_graph_traversal_errors();
    test_graph_full();
    test_graph_long_label_truncated();
    test_graph_unchanged_by_traversal();
    test_persist_empty_round_trip();
    test_persist_full_round_trip();
    test_persist_save_load_save();
    test_persist_capacity_round_trip();
    test_persist_missing_and_io_errors();
    test_persist_rejects_bad_files();
    test_eventlog_basic();
    test_eventlog_type_names();
    test_eventlog_details_safety();
    test_eventlog_alloc_failure();
    test_service_logs_one_event_each();
    test_service_failures_log_nothing();
    test_service_log_failure_keeps_operation();
    test_service_deterministic_order();
    test_persist_events();
    test_persist_v1_compat();
    test_demo_end_to_end();

    printf("All core tests passed.\n");
    return 0;
}
