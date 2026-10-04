#ifndef UI_PAGE_EVENTLOG_H
#define UI_PAGE_EVENTLOG_H

#include "app.h"

/* Builds the read-only Event Log page and stores its widgets in ctx->eventlog_page. */
GtkWidget *page_eventlog_new(AppCtx *ctx);

/* Rebuilds the table (newest first, filters applied) in one pass over the linked list.
 * Never changes the EventLog. */
void page_eventlog_refresh(AppCtx *ctx);

#endif
