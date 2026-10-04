#ifndef UI_PAGE_RUNWAYS_H
#define UI_PAGE_RUNWAYS_H

#include "app.h"

/* Builds the Runways page (Add form + table) and stores its widgets in ctx->runways_page. */
GtkWidget *page_runways_new(AppCtx *ctx);

/* Rebuilds the table from ctx->sys.runways. */
void page_runways_refresh(AppCtx *ctx);

#endif
