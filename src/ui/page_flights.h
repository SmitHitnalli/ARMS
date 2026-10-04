#ifndef UI_PAGE_FLIGHTS_H
#define UI_PAGE_FLIGHTS_H

#include "app.h"

/* Builds the Flights page (Add form + table) and stores its widgets in ctx->flights_page. */
GtkWidget *page_flights_new(AppCtx *ctx);

/* Rebuilds the table from ctx->sys.flights. */
void page_flights_refresh(AppCtx *ctx);

#endif
