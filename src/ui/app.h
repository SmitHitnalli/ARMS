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

/* Refreshes the Dashboard, Flights, Runways, Scheduler and Holding pages. */
void app_refresh_all(AppCtx *ctx);

#endif
