#include "request.h"

void request_init(RequestQueues *rq)
{
    if (rq == NULL)
        return;
    queue_init(&rq->landing);
    queue_init(&rq->takeoff);
}

ArmsStatus request_submit(RequestQueues *rq, FlightStore *fs, const char *id)
{
    if (rq == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;

    Queue *target;
    if (f->type == FLIGHT_ARRIVAL)
        target = &rq->landing;
    else if (f->type == FLIGHT_DEPARTURE)
        target = &rq->takeoff;
    else
        return ARMS_ERR_INVALID_ARG;

    if (queue_contains(&rq->landing, id) || queue_contains(&rq->takeoff, id))
        return ARMS_ERR_DUPLICATE;
    /* Explicit check: the table also allows HOLDING -> WAITING, which is not a normal request. */
    if (f->state != STATE_REGISTERED)
        return ARMS_ERR_INVALID_TRANSITION;
    if (queue_is_full(target))
        return ARMS_ERR_FULL;

    /* Every check has passed, so neither call below can fail (no rollback needed). */
    ArmsStatus s = queue_enqueue(target, id);
    if (s != ARMS_OK)
        return s;
    /* Stamp the request once; holding never changes these, so a flight keeps its age. */
    f->request_tick = fs->now;
    f->request_seq = fs->next_seq++;
    return flight_transition(fs, id, STATE_WAITING);
}
