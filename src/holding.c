#include "holding.h"

/* The normal queue a flight belongs to: ARRIVAL -> landing, DEPARTURE -> takeoff. */
static Queue *normal_queue_for(RequestQueues *rq, const Flight *f)
{
    if (f->type == FLIGHT_ARRIVAL)
        return &rq->landing;
    if (f->type == FLIGHT_DEPARTURE)
        return &rq->takeoff;
    return NULL;
}

void holding_init(HoldingQueue *h)
{
    if (h == NULL)
        return;
    cqueue_init(&h->queue);
}

ArmsStatus holding_enter(HoldingQueue *h, RequestQueues *rq, FlightStore *fs, const char *id)
{
    if (h == NULL || rq == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    Queue *normal = normal_queue_for(rq, f);
    if (normal == NULL)
        return ARMS_ERR_INVALID_ARG;

    if (cqueue_contains(&h->queue, id))
        return ARMS_ERR_DUPLICATE;
    if (f->state != STATE_WAITING)
        return ARMS_ERR_INVALID_TRANSITION;
    if (!queue_contains(normal, id))
        return ARMS_ERR_NOT_FOUND;
    if (cqueue_is_full(&h->queue))
        return ARMS_ERR_FULL;

    /* Every check has passed, so none of the three steps below can fail. */
    queue_remove(normal, id);
    cqueue_enqueue(&h->queue, id);
    return flight_transition(fs, id, STATE_HOLDING);
}

ArmsStatus holding_leave(HoldingQueue *h, RequestQueues *rq, FlightStore *fs, const char *id)
{
    if (h == NULL || rq == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    Queue *normal = normal_queue_for(rq, f);
    if (normal == NULL)
        return ARMS_ERR_INVALID_ARG;

    if (f->state != STATE_HOLDING)
        return ARMS_ERR_INVALID_TRANSITION;
    if (!cqueue_contains(&h->queue, id))
        return ARMS_ERR_NOT_FOUND;
    if (queue_contains(normal, id))
        return ARMS_ERR_DUPLICATE;
    if (queue_is_full(normal))
        return ARMS_ERR_FULL;

    /* Every check has passed, so none of the three steps below can fail. */
    cqueue_remove(&h->queue, id);
    queue_enqueue(normal, id);
    return flight_transition(fs, id, STATE_WAITING);
}

int holding_count(const HoldingQueue *h)
{
    return h == NULL ? 0 : cqueue_count(&h->queue);
}

void holding_print(FILE *out, const HoldingQueue *h)
{
    if (h == NULL)
        return;
    cqueue_print(out, &h->queue);
}
