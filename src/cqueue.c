#include "cqueue.h"

#include <string.h>

void cqueue_init(CircularQueue *q)
{
    if (q == NULL)
        return;
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

ArmsStatus cqueue_enqueue(CircularQueue *q, const char *id)
{
    if (q == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (!arms_id_valid(id))
        return ARMS_ERR_INVALID_ID;
    if (q->count == CQUEUE_CAPACITY)
        return ARMS_ERR_FULL;

    snprintf(q->ids[q->rear], sizeof q->ids[q->rear], "%s", id);
    q->rear = (q->rear + 1) % CQUEUE_CAPACITY;
    q->count++;
    return ARMS_OK;
}

ArmsStatus cqueue_dequeue(CircularQueue *q, char *out_id)
{
    if (q == NULL || out_id == NULL)
        return ARMS_ERR_INVALID_ARG;
    if (q->count == 0)
        return ARMS_ERR_EMPTY;

    snprintf(out_id, ID_MAX_LEN + 1, "%s", q->ids[q->front]);
    q->front = (q->front + 1) % CQUEUE_CAPACITY;
    q->count--;
    return ARMS_OK;
}

ArmsStatus cqueue_remove(CircularQueue *q, const char *id)
{
    if (q == NULL || id == NULL)
        return ARMS_ERR_INVALID_ARG;

    int pos = -1;                       /* logical position of id (0 = front) */
    for (int i = 0; i < q->count; i++) {
        if (strcmp(cqueue_at(q, i), id) == 0) {
            pos = i;
            break;
        }
    }
    if (pos < 0)
        return ARMS_ERR_NOT_FOUND;

    /* Shift every later item one place toward the front. */
    for (int i = pos; i < q->count - 1; i++) {
        int to = (q->front + i) % CQUEUE_CAPACITY;
        int from = (q->front + i + 1) % CQUEUE_CAPACITY;
        memcpy(q->ids[to], q->ids[from], sizeof q->ids[to]);
    }
    q->rear = (q->rear - 1 + CQUEUE_CAPACITY) % CQUEUE_CAPACITY;
    q->count--;
    return ARMS_OK;
}

const char *cqueue_front(const CircularQueue *q)
{
    return cqueue_at(q, 0);
}

int cqueue_is_empty(const CircularQueue *q)
{
    return q == NULL || q->count == 0;
}

int cqueue_is_full(const CircularQueue *q)
{
    return q != NULL && q->count == CQUEUE_CAPACITY;
}

int cqueue_count(const CircularQueue *q)
{
    return q == NULL ? 0 : q->count;
}

const char *cqueue_at(const CircularQueue *q, int index)
{
    if (q == NULL || index < 0 || index >= q->count)
        return NULL;
    return q->ids[(q->front + index) % CQUEUE_CAPACITY];
}

int cqueue_contains(const CircularQueue *q, const char *id)
{
    if (q == NULL || id == NULL)
        return 0;
    for (int i = 0; i < q->count; i++) {
        if (strcmp(cqueue_at(q, i), id) == 0)
            return 1;
    }
    return 0;
}

void cqueue_print(FILE *out, const CircularQueue *q)
{
    if (out == NULL || q == NULL)
        return;
    if (q->count == 0) {
        fprintf(out, "(queue empty)\n");
        return;
    }
    for (int i = 0; i < q->count; i++)
        fprintf(out, "%d. %s\n", i + 1, cqueue_at(q, i));
}
