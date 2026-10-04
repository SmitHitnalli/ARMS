#ifndef UI_PAGE_REPORTS_H
#define UI_PAGE_REPORTS_H

#include "app.h"

/* Builds the Reports page and stores its widgets in ctx->reports_page. */
GtkWidget *page_reports_new(AppCtx *ctx);

/* Re-renders both report tables with the selected sort and clears the last search result.
 * The core data is only read, never reordered. */
void page_reports_refresh(AppCtx *ctx);

#endif
