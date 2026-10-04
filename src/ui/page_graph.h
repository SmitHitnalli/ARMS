#ifndef UI_PAGE_GRAPH_H
#define UI_PAGE_GRAPH_H

#include "app.h"

/* Builds the Airport Graph page and stores its widgets in ctx->graph_page. */
GtkWidget *page_graph_new(AppCtx *ctx);

/* Rebuilds the node table, refills the node dropdowns (keeping selections), refreshes the
 * matrix view and clears the last traversal/reachability result. */
void page_graph_refresh(AppCtx *ctx);

#endif
