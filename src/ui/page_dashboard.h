#ifndef UI_PAGE_DASHBOARD_H
#define UI_PAGE_DASHBOARD_H

#include "app.h"

/* Builds the dashboard and stores its value labels in ctx->dashboard. */
GtkWidget *page_dashboard_new(AppCtx *ctx);

/* Re-reads the core state and updates the four summary values. */
void page_dashboard_refresh(AppCtx *ctx);

#endif
