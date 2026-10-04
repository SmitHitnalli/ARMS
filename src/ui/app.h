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

#endif
