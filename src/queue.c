#include "queue.h"

#include <string.h>

void queue_init(Queue *q)
{
    if (q == NULL)
        return;
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

ArmsStatus queue_enqueue(Queue *q, const char *id)
{
    if (q == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (!arms_id_valid(id))
        return ARMS_ERR_INVALID_ID;

    if (q->rear == QUEUE_CAPACITY) {
        if (q->front == 0)
            return ARMS_ERR_FULL;
        /* Reclaim the slots freed by earlier dequeues: move live items to the start. */
        memmove(q->ids[0], q->ids[q->front], (size_t)q->count * sizeof q->ids[0]);
        q->front = 0;
        q->rear = q->count;
    }

    snprintf(q->ids[q->rear], sizeof q->ids[q->rear], "%s", id);
    q->rear++;
    q->count++;
    return ARMS_OK;
}

ArmsStatus queue_dequeue(Queue *q, char *out_id)
{
    if (q == NULL || out_id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (q->count == 0)
        return ARMS_ERR_EMPTY;

    snprintf(out_id, ID_MAX_LEN + 1, "%s", q->ids[q->front]);
    q->front++;
    q->count--;
    if (q->count == 0) {
        q->front = 0;
        q->rear = 0;
    }
    return ARMS_OK;
}

const char *queue_front(const Queue *q)
{
    if (q == NULL || q->count == 0)
        return NULL;
    return q->ids[q->front];
}

int queue_is_empty(const Queue *q)
{
    return q == NULL || q->count == 0;
}

int queue_is_full(const Queue *q)
{
    return q != NULL && q->count == QUEUE_CAPACITY;
}

int queue_count(const Queue *q)
{
    return q == NULL ? 0 : q->count;
}

const char *queue_at(const Queue *q, int index)
{
    if (q == NULL || index < 0 || index >= q->count)
        return NULL;
    return q->ids[q->front + index];
}

int queue_contains(const Queue *q, const char *id)
{
    if (q == NULL || id == NULL)
        return 0;
    for (int i = q->front; i < q->rear; i++) {
        if (strcmp(q->ids[i], id) == 0)
            return 1;
    }
    return 0;
}

void queue_print(FILE *out, const Queue *q)
{
    if (out == NULL || q == NULL)
        return;
    if (q->count == 0) {
        fprintf(out, "(queue empty)\n");
        return;
    }
    for (int i = q->front; i < q->rear; i++)
        fprintf(out, "%d. %s\n", i - q->front + 1, q->ids[i]);
}
