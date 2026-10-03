#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#define QUEUE_CAPACITY MAX_FLIGHTS

/* Linear (non-circular) queue of copied flight IDs.
 * Live items are ids[front] .. ids[rear - 1]; rear is the next free slot. */
typedef struct {
    char ids[QUEUE_CAPACITY][ID_MAX_LEN + 1];
    int  front;
    int  rear;
    int  count;
} Queue;

void        queue_init(Queue *q);

/* INVALID_ARG, INVALID_ID, or FULL. Compacts to the start when rear hits the end. */
ArmsStatus  queue_enqueue(Queue *q, const char *id);

/* Copies the front ID into out_id (needs ID_MAX_LEN + 1 bytes). INVALID_ARG or EMPTY. */
ArmsStatus  queue_dequeue(Queue *q, char *out_id);

/* NULL if empty. The pointer is invalidated by the next enqueue or dequeue. */
const char *queue_front(const Queue *q);

int         queue_is_empty(const Queue *q);
int         queue_is_full(const Queue *q);
int         queue_count(const Queue *q);

/* index 0 is the front; NULL if out of range. Same pointer lifetime as queue_front. */
const char *queue_at(const Queue *q, int index);

int         queue_contains(const Queue *q, const char *id);
void        queue_print(FILE *out, const Queue *q);

#endif
