#ifndef UI_PAGE_SCHEDULER_H
#define UI_PAGE_SCHEDULER_H

#include "app.h"

/* Builds the Scheduler page and stores its widgets in ctx->scheduler_page. */
GtkWidget *page_scheduler_new(AppCtx *ctx);

/* Rebuilds every table and dropdown from the core state. */
void page_scheduler_refresh(AppCtx *ctx);

#endif
