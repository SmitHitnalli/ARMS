#ifndef GRAPH_H
#define GRAPH_H

#include "common.h"

/* Small simulated airport-connectivity graph (runway exits, taxiway junctions, holding
 * points, apron/terminal). It is an academic graph, not a real airport map.
 *
 * Representation: adjacency matrix. Nodes live in a fixed array; adj[i][j] is 1 when
 * nodes i and j are connected. Edges are undirected, so the matrix is always symmetric.
 * A node is referred to by its index (0 .. count-1, in the order it was added).
 * A matrix is a good fit for a small fixed-capacity graph: edge lookup is a single array
 * access and nothing is allocated.
 *
 * Complexity (V = number of nodes, at most MAX_NODES):
 *   graph_find_node        O(V)  linear scan of the IDs
 *   graph_add_node         O(V)  duplicate check
 *   graph_add_edge         O(V)  two ID lookups, then O(1) matrix writes
 *   graph_adjacent         O(V)  two ID lookups; the matrix lookup itself is O(1)
 *                                once both indexes are known
 *   graph_bfs / graph_dfs  O(V^2) every visited node scans one full matrix row
 *   graph_is_reachable     O(V^2) same reason (it runs a BFS)
 * With an adjacency list, BFS and DFS would instead be O(V+E).
 *
 * Neighbours are always visited in ascending node-index order, so traversal results are
 * deterministic. A traversal only visits the connected component of its start node. */

#define MAX_NODES      16
#define NODE_LABEL_LEN 32   /* includes the terminating '\0' */

typedef struct {
    char id[ID_MAX_LEN + 1];
    char label[NODE_LABEL_LEN];   /* readable name, "" when none was given */
} GraphNode;

typedef struct {
    GraphNode nodes[MAX_NODES];
    unsigned char adj[MAX_NODES][MAX_NODES];
    int count;
} AirportGraph;

void graph_init(AirportGraph *g);

/* label may be NULL (stored as ""); a label longer than NODE_LABEL_LEN-1 is truncated.
 * Errors: INVALID_ARG (NULL g or id), INVALID_ID (see arms_id_valid), DUPLICATE, FULL. */
ArmsStatus graph_add_node(AirportGraph *g, const char *id, const char *label);

/* Adds an undirected edge a-b.
 * Errors: INVALID_ARG (NULL pointer, or a == b), NOT_FOUND (unknown endpoint),
 * DUPLICATE (edge already exists, in either direction). */
ArmsStatus graph_add_edge(AirportGraph *g, const char *a, const char *b);

int graph_node_count(const AirportGraph *g);

/* Returns the node index, or -1 if g/id is NULL or the ID is unknown. */
int graph_find_node(const AirportGraph *g, const char *id);

/* Returns the node at index, or NULL if g is NULL or index is out of range. */
const GraphNode *graph_node_at(const AirportGraph *g, int index);

/* Returns 1 if a and b are connected by an edge; 0 if not, or if either ID is unknown. */
int graph_adjacent(const AirportGraph *g, const char *a, const char *b);

/* Breadth-first / depth-first traversal from start. Writes the node indices in visit
 * order to order[0..*n-1]. cap = size of order.
 * Errors: INVALID_ARG (NULL pointer or cap < 0), NOT_FOUND (unknown start),
 * FULL (cap < number of nodes reached). On any error nothing is written to order and
 * *n is set to 0 (when n is not NULL). */
ArmsStatus graph_bfs(const AirportGraph *g, const char *start, int *order, int cap, int *n);
ArmsStatus graph_dfs(const AirportGraph *g, const char *start, int *order, int cap, int *n);

/* Sets *reachable to 1 if target can be reached from start, else 0. start == target is
 * reachable when the node exists. Errors: INVALID_ARG (NULL pointer),
 * NOT_FOUND (either ID unknown; *reachable is then 0). */
ArmsStatus graph_is_reachable(const AirportGraph *g, const char *start, const char *target,
                              int *reachable);

#endif
