#ifndef UI_PAGE_HISTORY_H
#define UI_PAGE_HISTORY_H

#include "app.h"

/* Builds the read-only Movement History page and stores its widgets in ctx->history_page. */
GtkWidget *page_history_new(AppCtx *ctx);

/* Rebuilds the table in one pass over the linked list. */
void page_history_refresh(AppCtx *ctx);

#endif
