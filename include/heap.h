#ifndef HEAP_H
#define HEAP_H

#include "common.h"
#include "flight.h"

#define HEAP_CAPACITY MAX_FLIGHTS

/* Array-based max heap of copied flight IDs. The "biggest" flight is the one that
 * flight_priority_higher() says must be served first. Priority is looked up in the
 * FlightStore at compare time (no stored score), so after a flight's emergency or fuel
 * changes, call heap_update. Children of index i are 2i+1 and 2i+2; parent is (i-1)/2. */
typedef struct {
    char ids[HEAP_CAPACITY][ID_MAX_LEN + 1];
    int  count;
} MaxHeap;

void        heap_init(MaxHeap *h);

/* O(log n) (+ O(n) duplicate check). Only WAITING flights may enter.
 * Errors: INVALID_ARG, NOT_FOUND (no such flight), NOT_ELIGIBLE (not WAITING),
 * DUPLICATE, FULL. */
ArmsStatus  heap_insert(MaxHeap *h, FlightStore *fs, const char *id);

/* O(1). Highest-priority ID, or NULL if empty. Invalidated by the next change. */
const char *heap_peek(const MaxHeap *h);

/* O(log n). Copies the top ID into out_id (ID_MAX_LEN + 1 bytes). INVALID_ARG or EMPTY. */
ArmsStatus  heap_extract(MaxHeap *h, FlightStore *fs, char *out_id);

/* O(n) find + O(log n) repair. Removes id from anywhere in the heap. INVALID_ARG or NOT_FOUND. */
ArmsStatus  heap_remove(MaxHeap *h, FlightStore *fs, const char *id);

/* O(n) find + O(log n) repair. Call after the flight's priority fields changed.
 * INVALID_ARG or NOT_FOUND. */
ArmsStatus  heap_update(MaxHeap *h, FlightStore *fs, const char *id);

int         heap_contains(const MaxHeap *h, const char *id);
int         heap_count(const MaxHeap *h);
int         heap_is_empty(const MaxHeap *h);
int         heap_is_full(const MaxHeap *h);
void        heap_print(FILE *out, const MaxHeap *h);   /* array order, not sorted */

#endif
