#ifndef CQUEUE_H
#define CQUEUE_H

#include "common.h"

#define CQUEUE_CAPACITY MAX_FLIGHTS

/* Circular queue of copied flight IDs. Live items are ids[front], ids[front + 1], ...
 * wrapping past the end with modulo; rear is the next free slot. */
typedef struct {
    char ids[CQUEUE_CAPACITY][ID_MAX_LEN + 1];
    int  front;
    int  rear;
    int  count;
} CircularQueue;

void        cqueue_init(CircularQueue *q);

/* O(1). INVALID_ARG, INVALID_ID, or FULL. */
ArmsStatus  cqueue_enqueue(CircularQueue *q, const char *id);

/* O(1). Copies the front ID into out_id (needs ID_MAX_LEN + 1 bytes). INVALID_ARG or EMPTY. */
ArmsStatus  cqueue_dequeue(CircularQueue *q, char *out_id);

/* O(n). Removes id from anywhere in the queue, keeping FIFO order. INVALID_ARG or NOT_FOUND. */
ArmsStatus  cqueue_remove(CircularQueue *q, const char *id);

/* NULL if empty. The pointer is invalidated by the next change to the queue. */
const char *cqueue_front(const CircularQueue *q);

int         cqueue_is_empty(const CircularQueue *q);
int         cqueue_is_full(const CircularQueue *q);
int         cqueue_count(const CircularQueue *q);

/* index 0 is the front; NULL if out of range. Same pointer lifetime as cqueue_front. */
const char *cqueue_at(const CircularQueue *q, int index);

int         cqueue_contains(const CircularQueue *q, const char *id);
void        cqueue_print(FILE *out, const CircularQueue *q);

#endif
