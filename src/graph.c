#include <string.h>

#include "graph.h"

void graph_init(AirportGraph *g)
{
    if (g != NULL) {
        memset(g, 0, sizeof *g);
    }
}

int graph_node_count(const AirportGraph *g)
{
    return g == NULL ? 0 : g->count;
}

int graph_find_node(const AirportGraph *g, const char *id)
{
    if (g == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < g->count; i++) {
        if (strcmp(g->nodes[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

const GraphNode *graph_node_at(const AirportGraph *g, int index)
{
    if (g == NULL || index < 0 || index >= g->count) {
        return NULL;
    }
    return &g->nodes[index];
}

ArmsStatus graph_add_node(AirportGraph *g, const char *id, const char *label)
{
    if (g == NULL || id == NULL) {
        return ARMS_ERR_INVALID_ARG;
    }
    if (!arms_id_valid(id)) {
        return ARMS_ERR_INVALID_ID;
    }
    if (graph_find_node(g, id) >= 0) {
        return ARMS_ERR_DUPLICATE;
    }
    if (g->count >= MAX_NODES) {
        return ARMS_ERR_FULL;
    }
    GraphNode *node = &g->nodes[g->count];
    strcpy(node->id, id);                       /* length checked by arms_id_valid */
    node->label[0] = '\0';
    if (label != NULL) {
        strncpy(node->label, label, NODE_LABEL_LEN - 1);
        node->label[NODE_LABEL_LEN - 1] = '\0';
    }
    g->count++;
    return ARMS_OK;
}

ArmsStatus graph_add_edge(AirportGraph *g, const char *a, const char *b)
{
    if (g == NULL || a == NULL || b == NULL) {
        return ARMS_ERR_INVALID_ARG;
    }
    int ia = graph_find_node(g, a);
    int ib = graph_find_node(g, b);
    if (ia < 0 || ib < 0) {
        return ARMS_ERR_NOT_FOUND;
    }
    if (ia == ib) {
        return ARMS_ERR_INVALID_ARG;            /* no self-edges */
    }
    if (g->adj[ia][ib]) {
        return ARMS_ERR_DUPLICATE;
    }
    g->adj[ia][ib] = 1;
    g->adj[ib][ia] = 1;                         /* undirected: keep the matrix symmetric */
    return ARMS_OK;
}

int graph_adjacent(const AirportGraph *g, const char *a, const char *b)
{
    int ia = graph_find_node(g, a);
    int ib = graph_find_node(g, b);
    if (ia < 0 || ib < 0) {
        return 0;
    }
    return g->adj[ia][ib];
}

/* BFS from node index start into order[]; returns how many nodes were reached.
 * order is also used as the queue: a node is appended when it is first seen (marked
 * visited then), and head walks along it, so the queue contents are the visit order. */
static int bfs_collect(const AirportGraph *g, int start, int *order)
{
    int visited[MAX_NODES] = {0};
    int head = 0;
    int tail = 0;

    visited[start] = 1;
    order[tail++] = start;
    while (head < tail) {
        int v = order[head++];
        for (int w = 0; w < g->count; w++) {
            if (g->adj[v][w] && !visited[w]) {
                visited[w] = 1;
                order[tail++] = w;
            }
        }
    }
    return tail;
}

static void dfs_visit(const AirportGraph *g, int v, int *visited, int *order, int *n)
{
    visited[v] = 1;
    order[(*n)++] = v;
    for (int w = 0; w < g->count; w++) {
        if (g->adj[v][w] && !visited[w]) {
            dfs_visit(g, w, visited, order, n);
        }
    }
}

static int dfs_collect(const AirportGraph *g, int start, int *order)
{
    int visited[MAX_NODES] = {0};
    int n = 0;
    dfs_visit(g, start, visited, order, &n);
    return n;
}

/* Shared front end: validate, traverse into a local array, copy out only if it fits. */
static ArmsStatus traverse(const AirportGraph *g, const char *start, int *order, int cap,
                           int *n, int (*collect)(const AirportGraph *, int, int *))
{
    if (n != NULL) {
        *n = 0;
    }
    if (g == NULL || start == NULL || order == NULL || n == NULL || cap < 0) {
        return ARMS_ERR_INVALID_ARG;
    }
    int s = graph_find_node(g, start);
    if (s < 0) {
        return ARMS_ERR_NOT_FOUND;
    }
    int tmp[MAX_NODES];
    int count = collect(g, s, tmp);
    if (count > cap) {
        return ARMS_ERR_FULL;
    }
    memcpy(order, tmp, (size_t)count * sizeof tmp[0]);
    *n = count;
    return ARMS_OK;
}

ArmsStatus graph_bfs(const AirportGraph *g, const char *start, int *order, int cap, int *n)
{
    return traverse(g, start, order, cap, n, bfs_collect);
}

ArmsStatus graph_dfs(const AirportGraph *g, const char *start, int *order, int cap, int *n)
{
    return traverse(g, start, order, cap, n, dfs_collect);
}

ArmsStatus graph_is_reachable(const AirportGraph *g, const char *start, const char *target,
                              int *reachable)
{
    if (reachable != NULL) {
        *reachable = 0;
    }
    if (g == NULL || start == NULL || target == NULL || reachable == NULL) {
        return ARMS_ERR_INVALID_ARG;
    }
    int s = graph_find_node(g, start);
    int t = graph_find_node(g, target);
    if (s < 0 || t < 0) {
        return ARMS_ERR_NOT_FOUND;
    }
    int tmp[MAX_NODES];
    int count = bfs_collect(g, s, tmp);
    for (int i = 0; i < count; i++) {
        if (tmp[i] == t) {
            *reachable = 1;
            break;
        }
    }
    return ARMS_OK;
}
