#ifndef REPORTS_H
#define REPORTS_H

#include "common.h"
#include "flight.h"
#include "history.h"

/* Report helpers: sorted views of flights and movement history for display.
 * They never reorder the FlightStore or the history list. Each function fills a
 * caller-provided array of const pointers that point into the original data and sorts
 * only that array. Nothing is allocated here.
 *
 * The pointers stay valid until the store/history is changed (flight_add, history_clear, ...).
 * Sort: stable insertion sort. Equal keys keep their original order, ascending or descending.
 * Time O(n^2) worst/average, O(n) when already in order. Collecting the pointers is O(n). */

typedef enum {
    FLIGHT_SORT_ID,        /* flight ID, strcmp order */
    FLIGHT_SORT_REQUEST,   /* request_tick, then request_seq */
    FLIGHT_SORT_PRIORITY   /* flight_priority_higher(); ascending = most urgent first */
} FlightSortKey;

typedef enum {
    HIST_SORT_FLIGHT_ID,   /* flight ID, strcmp order */
    HIST_SORT_COMPLETION   /* completed_tick; ties keep linked-list order */
} HistorySortKey;

/* Writes every flight of fs, sorted, to out[0..*n-1]. cap = size of out.
 * Errors: INVALID_ARG (NULL pointer, bad key, cap < 0), FULL (cap < flight count;
 * nothing is written and *n is set to 0). */
ArmsStatus report_flights_sorted(const FlightStore *fs, FlightSortKey key, int ascending,
                                 const Flight **out, int cap, int *n);

/* Same for the movement history. Same errors. */
ArmsStatus report_history_sorted(const MovementHistory *h, HistorySortKey key, int ascending,
                                 const Movement **out, int cap, int *n);

/* Binary search, O(log n). sorted must already be sorted ASCENDING by flight ID
 * (e.g. from report_flights_sorted with FLIGHT_SORT_ID, ascending = 1); this function
 * does not sort. Returns the matching flight, or NULL if absent or on NULL arguments. */
const Flight *report_find_flight_sorted(const Flight *const *sorted, int n, const char *id);

#endif
