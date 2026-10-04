#ifndef UI_APP_H
#define UI_APP_H

#include <gtk/gtk.h>
#include "system.h"

/* GTK shell only: every core call goes through the existing core API. */

typedef enum {
    PAGE_DASHBOARD,
    PAGE_FLIGHTS,
    PAGE_RUNWAYS,
    PAGE_SCHEDULER,
    PAGE_HOLDING,
    PAGE_HISTORY,
    PAGE_GRAPH,
    PAGE_REPORTS,
    PAGE_EVENT_LOG,
    PAGE_COUNT
} PageId;

/* Value labels of the dashboard cards (owned by the widget tree, referenced here). */
typedef struct {
    GtkWidget *total;
    GtkWidget *waiting;
    GtkWidget *emergencies;
    GtkWidget *runways;
} DashboardWidgets;

/* Flights page: the table grid and the inline Add form. */
typedef struct {
    GtkWidget *grid;
    GtkWidget *id_entry;
    GtkWidget *type_dd;
    GtkWidget *category_dd;
} FlightsWidgets;

/* Runways page: the table grid and the inline Add form. */
typedef struct {
    GtkWidget *grid;
    GtkWidget *id_entry;
    GtkWidget *category_dd;
} RunwaysWidgets;

/* Scheduler page: dropdowns and the tables that are rebuilt on refresh. */
typedef struct {
    GtkWidget *submit_dd;      /* REGISTERED flights */
    GtkWidget *prio_dd;        /* flights whose emergency/fuel can still change */
    GtkWidget *emergency_dd;
    GtkWidget *fuel_dd;
    GtkWidget *landing_grid;
    GtkWidget *takeoff_grid;
    GtkWidget *heap_grid;      /* priority-order preview */
    GtkWidget *raw_grid;       /* developer: raw heap array */
    GtkWidget *ops_grid;       /* Start / Complete / Cancel */
} SchedulerWidgets;

/* Holding page: the holding table, the Enter dropdown and the developer text. */
typedef struct {
    GtkWidget *grid;
    GtkWidget *enter_dd;       /* WAITING flights */
    GtkWidget *dev_label;
} HoldingWidgets;

/* Movement History page: the table and the record count. */
typedef struct {
    GtkWidget *grid;
    GtkWidget *count_label;
} HistoryWidgets;

/* Airport Graph page: forms, node dropdowns, result labels and the developer matrix. */
typedef struct {
    GtkWidget *id_entry;
    GtkWidget *label_entry;
    GtkWidget *edge_a_dd;
    GtkWidget *edge_b_dd;
    GtkWidget *nodes_grid;
    GtkWidget *trav_dd;
    GtkWidget *trav_label;
    GtkWidget *reach_from_dd;
    GtkWidget *reach_to_dd;
    GtkWidget *reach_label;
    GtkWidget *matrix_label;
} GraphWidgets;

/* Reports page: sort selectors, the two report tables and the binary-search control. */
typedef struct {
    GtkWidget *flight_key_dd;
    GtkWidget *flight_order_dd;
    GtkWidget *flight_grid;
    GtkWidget *hist_key_dd;
    GtkWidget *hist_order_dd;
    GtkWidget *hist_grid;
    GtkWidget *find_entry;
    GtkWidget *find_label;
} ReportsWidgets;

/* The whole application state. Lives on main()'s stack; no UI globals. */
typedef struct {
    ArmsSystem       sys;
    GtkApplication  *app;
    GtkWindow       *window;
    GtkWidget       *sidebar;       /* GtkListBox */
    GtkWidget       *stack;         /* GtkStack, one child per PageId */
    GtkWidget       *notice;        /* GtkRevealer */
    GtkWidget       *notice_box;
    GtkWidget       *notice_label;
    DashboardWidgets dashboard;
    FlightsWidgets   flights_page;
    RunwaysWidgets   runways_page;
    SchedulerWidgets scheduler_page;
    HoldingWidgets   holding_page;
    HistoryWidgets   history_page;
    GraphWidgets     graph_page;
    ReportsWidgets   reports_page;
    char             save_path[260];
    int              load_failed;   /* 1 = a save exists but could not be loaded: never overwrite it */
} AppCtx;

/* Initialises the core system and settings. Call once before g_application_run. */
void app_ctx_init(AppCtx *ctx, GtkApplication *app);

/* Releases the core system. Call after g_application_run returns. */
void app_ctx_free(AppCtx *ctx);

/* "activate" handler: user_data is the AppCtx. Loads the save and builds the window. */
void app_activate(GtkApplication *app, gpointer user_data);

/* Shows a dismissible message under the header (is_error selects the colour). */
void app_show_notice(AppCtx *ctx, int is_error, const char *message);

/* Refreshes every page except the Event Log placeholder. */
void app_refresh_all(AppCtx *ctx);

#endif
