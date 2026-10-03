#include "history.h"

#include <stdlib.h>
#include <string.h>

#ifdef ARMS_TESTING
/* Test builds only: set to 1 to make the next history_append fail as if malloc returned NULL. */
int history_fail_next_alloc = 0;
#endif

void history_init(MovementHistory *h)
{
    h->head = NULL;
    h->tail = NULL;
    h->count = 0;
}

ArmsStatus history_append(MovementHistory *h, const Movement *m)
{
    if (h == NULL || m == NULL)
        return ARMS_ERR_INVALID_ARG;

#ifdef ARMS_TESTING
    if (history_fail_next_alloc) {
        history_fail_next_alloc = 0;
        return ARMS_ERR_NO_MEMORY;
    }
#endif

    MovementNode *node = malloc(sizeof *node);
    if (node == NULL)
        return ARMS_ERR_NO_MEMORY;   /* nothing linked yet, so the list is unchanged */
    node->data = *m;
    node->next = NULL;

    if (h->tail == NULL)
        h->head = node;
    else
        h->tail->next = node;
    h->tail = node;
    h->count++;
    return ARMS_OK;
}

int history_count(const MovementHistory *h)
{
    return h == NULL ? 0 : h->count;
}

const Movement *history_find(const MovementHistory *h, const char *flight_id)
{
    if (h == NULL || flight_id == NULL)
        return NULL;
    for (const MovementNode *n = h->head; n != NULL; n = n->next) {
        if (strcmp(n->data.flight_id, flight_id) == 0)
            return &n->data;
    }
    return NULL;
}

const Movement *history_at(const MovementHistory *h, int index)
{
    if (h == NULL || index < 0 || index >= h->count)
        return NULL;
    const MovementNode *n = h->head;
    while (index-- > 0)
        n = n->next;
    return &n->data;
}

void history_list(FILE *out, const MovementHistory *h)
{
    if (out == NULL || h == NULL)
        return;
    if (h->count == 0) {
        fprintf(out, "(no movements)\n");
        return;
    }
    int i = 1;
    for (const MovementNode *n = h->head; n != NULL; n = n->next, i++) {
        const Movement *m = &n->data;
        fprintf(out, "%d. %s  %s  %s  runway=%s  requested(tick=%d seq=%u)  completed_tick=%d\n",
                i, m->flight_id, flight_type_name(m->type), aircraft_category_name(m->category),
                m->runway_id, m->request_tick, m->request_seq, m->completed_tick);
    }
}

void history_clear(MovementHistory *h)
{
    if (h == NULL)
        return;
    MovementNode *n = h->head;
    while (n != NULL) {
        MovementNode *next = n->next;
        free(n);
        n = next;
    }
    history_init(h);
}
