#ifndef UI_PAGE_HOLDING_H
#define UI_PAGE_HOLDING_H

#include "app.h"

/* Builds the Holding page and stores its widgets in ctx->holding_page. */
GtkWidget *page_holding_new(AppCtx *ctx);

/* Rebuilds the holding table, the Enter dropdown and the developer view. */
void page_holding_refresh(AppCtx *ctx);

#endif
