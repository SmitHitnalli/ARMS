#include "heap.h"

#include <string.h>

/* 1 if the flight at heap slot i must be served before the one at slot j. */
static int higher(MaxHeap *h, FlightStore *fs, int i, int j)
{
    return flight_priority_higher(flight_find(fs, h->ids[i]), flight_find(fs, h->ids[j]));
}

static void swap(MaxHeap *h, int i, int j)
{
    char tmp[ID_MAX_LEN + 1];
    memcpy(tmp, h->ids[i], sizeof tmp);
    memcpy(h->ids[i], h->ids[j], sizeof tmp);
    memcpy(h->ids[j], tmp, sizeof tmp);
}

/* Move slot i up while it outranks its parent. */
static void sift_up(MaxHeap *h, FlightStore *fs, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!higher(h, fs, i, parent))
            break;
        swap(h, i, parent);
        i = parent;
    }
}

/* Move slot i down while a child outranks it. */
static void sift_down(MaxHeap *h, FlightStore *fs, int i)
{
    for (;;) {
        int best = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        if (left < h->count && higher(h, fs, left, best))
            best = left;
        if (right < h->count && higher(h, fs, right, best))
            best = right;
        if (best == i)
            break;
        swap(h, i, best);
        i = best;
    }
}

/* Slot of id, or -1. */
static int find_index(const MaxHeap *h, const char *id)
{
    for (int i = 0; i < h->count; i++) {
        if (strcmp(h->ids[i], id) == 0)
            return i;
    }
    return -1;
}

void heap_init(MaxHeap *h)
{
    if (h != NULL)
        h->count = 0;
}

ArmsStatus heap_insert(MaxHeap *h, FlightStore *fs, const char *id)
{
    if (h == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    Flight *f = flight_find(fs, id);
    if (f == NULL)
        return ARMS_ERR_NOT_FOUND;
    if (f->state != STATE_WAITING)
        return ARMS_ERR_NOT_ELIGIBLE;
    if (find_index(h, id) >= 0)
        return ARMS_ERR_DUPLICATE;
    if (h->count >= HEAP_CAPACITY)
        return ARMS_ERR_FULL;

    snprintf(h->ids[h->count], sizeof h->ids[0], "%s", id);
    h->count++;
    sift_up(h, fs, h->count - 1);
    return ARMS_OK;
}

const char *heap_peek(const MaxHeap *h)
{
    if (h == NULL || h->count == 0)
        return NULL;
    return h->ids[0];
}

ArmsStatus heap_remove(MaxHeap *h, FlightStore *fs, const char *id)
{
    if (h == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    int i = find_index(h, id);
    if (i < 0)
        return ARMS_ERR_NOT_FOUND;

    /* Fill the hole with the last item, then repair in whichever direction it needs. */
    h->count--;
    if (i < h->count) {
        memcpy(h->ids[i], h->ids[h->count], sizeof h->ids[0]);
        sift_up(h, fs, i);
        sift_down(h, fs, i);
    }
    return ARMS_OK;
}

ArmsStatus heap_extract(MaxHeap *h, FlightStore *fs, char *out_id)
{
    if (h == NULL || fs == NULL || out_id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (h->count == 0)
        return ARMS_ERR_EMPTY;

    memcpy(out_id, h->ids[0], sizeof h->ids[0]);
    return heap_remove(h, fs, out_id);
}

ArmsStatus heap_update(MaxHeap *h, FlightStore *fs, const char *id)
{
    if (h == NULL || fs == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    int i = find_index(h, id);
    if (i < 0)
        return ARMS_ERR_NOT_FOUND;
    sift_up(h, fs, i);
    sift_down(h, fs, i);
    return ARMS_OK;
}

int heap_contains(const MaxHeap *h, const char *id)
{
    return h != NULL && id != NULL && find_index(h, id) >= 0;
}

int heap_count(const MaxHeap *h)
{
    return h == NULL ? 0 : h->count;
}

int heap_is_empty(const MaxHeap *h)
{
    return h == NULL || h->count == 0;
}

int heap_is_full(const MaxHeap *h)
{
    return h != NULL && h->count >= HEAP_CAPACITY;
}

void heap_print(FILE *out, const MaxHeap *h)
{
    if (out == NULL || h == NULL)
        return;
    fprintf(out, "Heap (%d):", h->count);
    for (int i = 0; i < h->count; i++)
        fprintf(out, " %s", h->ids[i]);
    fprintf(out, "\n");
}
