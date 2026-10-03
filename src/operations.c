#include "operations.h"

#include <string.h>

/* First AVAILABLE runway (in store order) that can take this flight, or NULL. */
static Runway *first_free_runway(RunwayStore *rs, const Flight *f)
{
    for (int i = 0; i < rs->count; i++) {
        Runway *r = &rs->items[i];
        if (r->status == RUNWAY_AVAILABLE && runway_is_compatible(r, f))
            return r;
    }
    return NULL;
}

/* The runway a SCHEDULED/IN_OPERATION flight holds, or NULL if the two-way link is broken. */
static Runway *linked_runway(RunwayStore *rs, const Flight *f)
{
    Runway *r = runway_find(rs, f->runway_id);
    if (r == NULL || r->status != RUNWAY_OCCUPIED || strcmp(r->current_flight_id, f->id) != 0)
        return NULL;
    return r;
}

ArmsStatus ops_schedule_next(Scheduler *s, FlightStore *fs, RunwayStore *rs,
                             char *out_flight_id, char *out_runway_id)
{
    if (s == NULL || fs == NULL || rs == NULL || out_flight_id == NULL || out_runway_id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (heap_is_empty(&s->heap))
        return ARMS_ERR_EMPTY;

    /* Plan: the heap array is not sorted, so look at every entry and keep the best one that
     * has a free runway. Nothing is changed yet, so the heap order cannot be disturbed. */
    Flight *best = NULL;
    Runway *best_runway = NULL;
    for (int i = 0; i < s->heap.count; i++) {
        Flight *f = flight_find(fs, s->heap.ids[i]);
        if (f == NULL || f->state != STATE_WAITING)
            continue;
        Runway *r = first_free_runway(rs, f);
        if (r != NULL && (best == NULL || flight_priority_higher(f, best))) {
            best = f;
            best_runway = r;
        }
    }
    if (best == NULL)
        return ARMS_ERR_NO_RUNWAY;

    /* Commit. The checks above make each step succeed; the rollback is defensive only. */
    ArmsStatus st = runway_assign(rs, best_runway->id, best);
    if (st != ARMS_OK)
        return st;
    heap_remove(&s->heap, fs, best->id);
    st = flight_transition(fs, best->id, STATE_SCHEDULED);
    if (st != ARMS_OK) {
        heap_insert(&s->heap, fs, best->id);   /* flight is still WAITING */
        runway_release(rs, best_runway->id);
        return st;
    }

    snprintf(best->runway_id, sizeof best->runway_id, "%s", best_runway->id);
    snprintf(out_flight_id, ID_MAX_LEN + 1, "%s", best->id);
    snprintf(out_runway_id, ID_MAX_LEN + 1, "%s", best_runway->id);
    return ARMS_OK;
}

ArmsStatus ops_start(FlightStore *fs, RunwayStore *rs, const char *flight_id)
{
    if (fs == NULL || rs == NULL || flight_id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, flight_id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (f->state != STATE_SCHEDULED)
        return ARMS_ERR_INVALID_TRANSITION;
    if (linked_runway(rs, f) == NULL)
        return ARMS_ERR_NOT_OCCUPIED;

    return flight_transition(fs, flight_id, STATE_IN_OPERATION);
}

ArmsStatus ops_complete(FlightStore *fs, RunwayStore *rs, MovementHistory *mh,
                        const char *flight_id)
{
    if (fs == NULL || rs == NULL || mh == NULL || flight_id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, flight_id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (f->state != STATE_IN_OPERATION)
        return ARMS_ERR_INVALID_TRANSITION;
    Runway *r = linked_runway(rs, f);
    if (r == NULL)
        return ARMS_ERR_NOT_OCCUPIED;

    /* Record first, while runway_id is still set. If this fails nothing has changed. */
    Movement m;
    snprintf(m.flight_id, sizeof m.flight_id, "%s", f->id);
    m.type = f->type;
    m.category = f->category;
    snprintf(m.runway_id, sizeof m.runway_id, "%s", r->id);
    m.request_tick = f->request_tick;
    m.request_seq = f->request_seq;
    m.completed_tick = fs->now;
    ArmsStatus st = history_append(mh, &m);
    if (st != ARMS_OK)
        return st;

    runway_release(rs, r->id);
    flight_transition(fs, flight_id, STATE_COMPLETED);
    f->runway_id[0] = '\0';
    return ARMS_OK;
}

ArmsStatus ops_cancel(Scheduler *s, HoldingQueue *h, RequestQueues *rq,
                      FlightStore *fs, RunwayStore *rs, const char *flight_id)
{
    if (s == NULL || h == NULL || rq == NULL || fs == NULL || rs == NULL || flight_id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, flight_id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;

    /* Check where the flight really is first, then remove it; flight_transition cannot fail
     * for these states, so a failed call never leaves a half-done cancel. */
    switch (f->state) {
    case STATE_REGISTERED:
        break;
    case STATE_WAITING: {
        Queue *normal = f->type == FLIGHT_ARRIVAL ? &rq->landing : &rq->takeoff;
        if (heap_contains(&s->heap, flight_id))
            heap_remove(&s->heap, fs, flight_id);
        else if (queue_contains(normal, flight_id))
            queue_remove(normal, flight_id);
        else
            return ARMS_ERR_NOT_FOUND;
        break;
    }
    case STATE_HOLDING:
        if (!cqueue_contains(&h->queue, flight_id))
            return ARMS_ERR_NOT_FOUND;
        cqueue_remove(&h->queue, flight_id);
        break;
    case STATE_SCHEDULED: {
        Runway *r = linked_runway(rs, f);
        if (r == NULL)
            return ARMS_ERR_NOT_OCCUPIED;
        runway_release(rs, r->id);
        f->runway_id[0] = '\0';
        break;
    }
    default:   /* IN_OPERATION, COMPLETED, CANCELLED */
        return ARMS_ERR_INVALID_TRANSITION;
    }

    return flight_transition(fs, flight_id, STATE_CANCELLED);
}

/* ---- invariant checker (read-only, so it uses its own const lookups) ---- */

static const Flight *find_flight(const FlightStore *fs, const char *id)
{
    for (int i = 0; i < fs->count; i++) {
        if (strcmp(fs->items[i].id, id) == 0)
            return &fs->items[i];
    }
    return NULL;
}

static int holds_runway(const Flight *f)
{
    return f->state == STATE_SCHEDULED || f->state == STATE_IN_OPERATION;
}

int ops_check_invariants(const FlightStore *fs, const RunwayStore *rs, const Scheduler *s)
{
    if (fs == NULL || rs == NULL || s == NULL)
        return 0;

    for (int i = 0; i < rs->count; i++) {
        const Runway *r = &rs->items[i];
        if (r->status == RUNWAY_OCCUPIED) {
            const Flight *f = find_flight(fs, r->current_flight_id);
            if (f == NULL || !holds_runway(f) || strcmp(f->runway_id, r->id) != 0)
                return 0;
        } else if (r->current_flight_id[0] != '\0') {
            return 0;
        }
    }

    for (int i = 0; i < fs->count; i++) {
        const Flight *f = &fs->items[i];
        if (!holds_runway(f)) {
            if (f->runway_id[0] != '\0')
                return 0;
            continue;
        }

        /* Exactly one OCCUPIED runway names this flight, and it is the one in runway_id. */
        int owners = 0, matches = 0;
        for (int j = 0; j < rs->count; j++) {
            const Runway *r = &rs->items[j];
            if (r->status == RUNWAY_OCCUPIED && strcmp(r->current_flight_id, f->id) == 0) {
                owners++;
                if (strcmp(r->id, f->runway_id) == 0)
                    matches++;
            }
        }
        if (owners != 1 || matches != 1)
            return 0;

        for (int j = i + 1; j < fs->count; j++) {
            if (holds_runway(&fs->items[j]) && strcmp(fs->items[j].runway_id, f->runway_id) == 0)
                return 0;
        }
    }

    for (int i = 0; i < s->heap.count; i++) {
        const Flight *f = find_flight(fs, s->heap.ids[i]);
        if (f == NULL || f->state != STATE_WAITING)
            return 0;
    }
    return 1;
}
