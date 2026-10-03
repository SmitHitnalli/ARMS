#include <string.h>

#include "reports.h"

/* Comparators return <0, 0 or >0 like strcmp. */
static int cmp_flight_id(const Flight *a, const Flight *b)
{
    return strcmp(a->id, b->id);
}

static int cmp_flight_request(const Flight *a, const Flight *b)
{
    if (a->request_tick != b->request_tick)
        return a->request_tick < b->request_tick ? -1 : 1;
    if (a->request_seq != b->request_seq)
        return a->request_seq < b->request_seq ? -1 : 1;
    return 0;
}

static int cmp_flight_priority(const Flight *a, const Flight *b)
{
    if (flight_priority_higher(a, b)) return -1;
    if (flight_priority_higher(b, a)) return 1;
    return 0;
}

static int cmp_move_id(const Movement *a, const Movement *b)
{
    return strcmp(a->flight_id, b->flight_id);
}

static int cmp_move_completion(const Movement *a, const Movement *b)
{
    if (a->completed_tick != b->completed_tick)
        return a->completed_tick < b->completed_tick ? -1 : 1;
    return 0;
}

/* Stable insertion sort. An element moves left only while it must come STRICTLY before
 * its left neighbour, so equal keys never swap. Descending uses the opposite strict test
 * (cmp > 0), so it is stable too and is not a reversed ascending result. */
static void sort_flights(const Flight **a, int n, int (*cmp)(const Flight *, const Flight *),
                         int ascending)
{
    for (int i = 1; i < n; i++) {
        const Flight *x = a[i];
        int j = i - 1;
        while (j >= 0 && (ascending ? cmp(x, a[j]) < 0 : cmp(x, a[j]) > 0)) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

static void sort_moves(const Movement **a, int n, int (*cmp)(const Movement *, const Movement *),
                       int ascending)
{
    for (int i = 1; i < n; i++) {
        const Movement *x = a[i];
        int j = i - 1;
        while (j >= 0 && (ascending ? cmp(x, a[j]) < 0 : cmp(x, a[j]) > 0)) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

ArmsStatus report_flights_sorted(const FlightStore *fs, FlightSortKey key, int ascending,
                                 const Flight **out, int cap, int *n)
{
    if (n == NULL)
        return ARMS_ERR_INVALID_ARG;
    *n = 0;
    if (fs == NULL || out == NULL || cap < 0)
        return ARMS_ERR_INVALID_ARG;

    int (*cmp)(const Flight *, const Flight *);
    switch (key) {
    case FLIGHT_SORT_ID:       cmp = cmp_flight_id;       break;
    case FLIGHT_SORT_REQUEST:  cmp = cmp_flight_request;  break;
    case FLIGHT_SORT_PRIORITY: cmp = cmp_flight_priority; break;
    default:                   return ARMS_ERR_INVALID_ARG;
    }

    int count = flight_count(fs);
    if (count > cap)
        return ARMS_ERR_FULL;

    for (int i = 0; i < count; i++)
        out[i] = flight_at(fs, i);
    sort_flights(out, count, cmp, ascending);
    *n = count;
    return ARMS_OK;
}

ArmsStatus report_history_sorted(const MovementHistory *h, HistorySortKey key, int ascending,
                                 const Movement **out, int cap, int *n)
{
    if (n == NULL)
        return ARMS_ERR_INVALID_ARG;
    *n = 0;
    if (h == NULL || out == NULL || cap < 0)
        return ARMS_ERR_INVALID_ARG;

    int (*cmp)(const Movement *, const Movement *);
    switch (key) {
    case HIST_SORT_FLIGHT_ID:   cmp = cmp_move_id;         break;
    case HIST_SORT_COMPLETION:  cmp = cmp_move_completion; break;
    default:                    return ARMS_ERR_INVALID_ARG;
    }

    int count = history_count(h);
    if (count > cap)
        return ARMS_ERR_FULL;

    /* Walk the nodes once; history_at() would make this O(n^2). */
    int i = 0;
    for (const MovementNode *p = h->head; p != NULL; p = p->next)
        out[i++] = &p->data;
    sort_moves(out, count, cmp, ascending);
    *n = count;
    return ARMS_OK;
}

const Flight *report_find_flight_sorted(const Flight *const *sorted, int n, const char *id)
{
    if (sorted == NULL || id == NULL)
        return NULL;
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(id, sorted[mid]->id);
        if (c == 0)
            return sorted[mid];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}
