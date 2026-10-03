#include "scheduler.h"

void scheduler_init(Scheduler *s)
{
    if (s != NULL)
        heap_init(&s->heap);
}

/* Drain one normal queue into the heap. */
static ArmsStatus pull_queue(Scheduler *s, Queue *q, FlightStore *fs)
{
    char id[ID_MAX_LEN + 1];

    while (!queue_is_empty(q)) {
        if (heap_is_full(&s->heap))
            return ARMS_ERR_FULL;
        queue_dequeue(q, id);
        /* A non-WAITING ID cannot be queued through the API; if one ever is, it is dropped. */
        heap_insert(&s->heap, fs, id);
    }
    return ARMS_OK;
}

ArmsStatus scheduler_pull(Scheduler *s, RequestQueues *rq, FlightStore *fs)
{
    if (s == NULL || rq == NULL || fs == NULL)
        return ARMS_ERR_INVALID_ARG;

    ArmsStatus a = pull_queue(s, &rq->landing, fs);
    ArmsStatus b = pull_queue(s, &rq->takeoff, fs);
    return a != ARMS_OK ? a : b;
}

const char *scheduler_peek(const Scheduler *s)
{
    return s == NULL ? NULL : heap_peek(&s->heap);
}

ArmsStatus scheduler_pop(Scheduler *s, FlightStore *fs, char *out_id)
{
    if (s == NULL || fs == NULL || out_id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (heap_is_empty(&s->heap))
        return ARMS_ERR_EMPTY;

    heap_extract(&s->heap, fs, out_id);
    return flight_transition(fs, out_id, STATE_SCHEDULED);
}

ArmsStatus scheduler_set_emergency(Scheduler *s, FlightStore *fs, const char *id,
                                   EmergencyLevel level)
{
    if (s == NULL)
        return ARMS_ERR_INVALID_ARG;
    ArmsStatus st = flight_set_emergency(fs, id, level);
    if (st == ARMS_OK && heap_contains(&s->heap, id))
        heap_update(&s->heap, fs, id);
    return st;
}

ArmsStatus scheduler_set_fuel(Scheduler *s, FlightStore *fs, const char *id, FuelLevel level)
{
    if (s == NULL)
        return ARMS_ERR_INVALID_ARG;
    ArmsStatus st = flight_set_fuel(fs, id, level);
    if (st == ARMS_OK && heap_contains(&s->heap, id))
        heap_update(&s->heap, fs, id);
    return st;
}

ArmsStatus scheduler_hold(Scheduler *s, HoldingQueue *h, RequestQueues *rq,
                          FlightStore *fs, const char *id)
{
    if (s == NULL || h == NULL || rq == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    /* Not in the heap: it is in a normal queue (or invalid), so holding does all the work. */
    if (!heap_contains(&s->heap, id))
        return holding_enter(h, rq, fs, id);

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    Queue *normal = f->type == FLIGHT_ARRIVAL ? &rq->landing : &rq->takeoff;
    if (cqueue_is_full(&h->queue))
        return ARMS_ERR_FULL;
    if (queue_is_full(normal))
        return ARMS_ERR_FULL;

    /* Checks done: put the flight back in its normal queue, then holding_enter moves it on.
     * heap_remove/queue_enqueue cannot fail here, and holding_enter's checks all pass. */
    heap_remove(&s->heap, fs, id);
    queue_enqueue(normal, id);
    return holding_enter(h, rq, fs, id);
}

int scheduler_count(const Scheduler *s)
{
    return s == NULL ? 0 : heap_count(&s->heap);
}

int scheduler_contains(const Scheduler *s, const char *id)
{
    return s != NULL && heap_contains(&s->heap, id);
}
