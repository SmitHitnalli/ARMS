#include "app.h"
#include "page_dashboard.h"
#include "page_flights.h"
#include "page_runways.h"
#include "page_scheduler.h"
#include "page_holding.h"
#include "page_history.h"
#include "page_graph.h"
#include "page_reports.h"
#include "page_eventlog.h"
#include "persist.h"
#include "demo.h"

/* Runtime files, relative to the project root (the executable is <root>/build/arms.exe). */
#define SAVE_PATH "data/arms.dat"
#define CSS_PATH  "assets/css/arms.css"

/* <exe dir>/../<rel>, normalised, independent of the working directory.
 * Returns a newly allocated path (g_free), or NULL if the executable location is unknown. */
static char *project_path(const char *rel)
{
    char *result = NULL;
#ifdef G_OS_WIN32
    gchar *exe_dir = g_win32_get_package_installation_directory_of_module(NULL);
    if (exe_dir != NULL) {
        char *raw = g_build_filename(exe_dir, "..", rel, NULL);
        result = g_canonicalize_filename(raw, NULL);
        g_free(raw);
        g_free(exe_dir);
    }
#else
    (void)rel;
#endif
    return result;
}

static const char *PAGE_TITLE[PAGE_COUNT] = {
    "Dashboard", "Flights", "Runways", "Scheduler", "Holding",
    "Movement History", "Airport Graph", "Reports", "Event Log"
};

void app_ctx_init(AppCtx *ctx, GtkApplication *app)
{
    memset(ctx, 0, sizeof *ctx);
    arms_system_init(&ctx->sys);
    ctx->app = app;

    /* An empty save_path means "could not be resolved": load_on_startup then blocks saving.
     * There is deliberately no CWD-relative fallback, which could touch the wrong save. */
    char *path = project_path(SAVE_PATH);
    if (path != NULL && strlen(path) < sizeof ctx->save_path)
        snprintf(ctx->save_path, sizeof ctx->save_path, "%s", path);
    g_free(path);
}

void app_ctx_free(AppCtx *ctx)
{
    arms_system_clear(&ctx->sys);
}

/* ---- notification area ---- */

void app_show_notice(AppCtx *ctx, int is_error, const char *message)
{
    gtk_widget_remove_css_class(ctx->notice_box, "notice-error");
    gtk_widget_remove_css_class(ctx->notice_box, "notice-success");
    gtk_widget_add_css_class(ctx->notice_box, is_error ? "notice-error" : "notice-success");
    gtk_label_set_text(GTK_LABEL(ctx->notice_label), message);
    gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->notice), TRUE);
}

static void on_notice_dismiss(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->notice), FALSE);
}

static GtkWidget *notice_new(AppCtx *ctx)
{
    ctx->notice = gtk_revealer_new();
    ctx->notice_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(ctx->notice_box, "notice");

    ctx->notice_label = gtk_label_new("");
    gtk_widget_set_hexpand(ctx->notice_label, TRUE);
    gtk_widget_set_halign(ctx->notice_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(ctx->notice_label), TRUE);

    GtkWidget *dismiss = gtk_button_new_with_label("Dismiss");
    g_signal_connect(dismiss, "clicked", G_CALLBACK(on_notice_dismiss), ctx);

    gtk_box_append(GTK_BOX(ctx->notice_box), ctx->notice_label);
    gtk_box_append(GTK_BOX(ctx->notice_box), dismiss);
    gtk_revealer_set_child(GTK_REVEALER(ctx->notice), ctx->notice_box);
    return ctx->notice;
}

/* ---- persistence ---- */

static void load_on_startup(AppCtx *ctx)
{
    if (ctx->save_path[0] == '\0') {
        ctx->load_failed = 1;
        app_show_notice(ctx, 1, "Could not locate the data folder next to the executable. "
                                "Saving is disabled.");
        return;
    }
    ArmsStatus st = persist_load(ctx->save_path, &ctx->sys);
    if (st == ARMS_OK || st == ARMS_ERR_NOT_FOUND)   /* NOT_FOUND = first run */
        return;

    char msg[400];
    ctx->load_failed = 1;
    snprintf(msg, sizeof msg, "Could not load %s (%s). Saving is disabled so the file is kept.",
             ctx->save_path, arms_status_str(st));
    app_show_notice(ctx, 1, msg);
}

/* Returns 1 if the save succeeded; on failure or a blocked save a notice explains why. */
static int save_now(AppCtx *ctx, int announce_success)
{
    char msg[400];

    if (ctx->load_failed) {
        app_show_notice(ctx, 1, "Saving is disabled: the existing save could not be loaded.");
        return 0;
    }
    ArmsStatus st = persist_save(ctx->save_path, &ctx->sys);
    if (st != ARMS_OK) {
        snprintf(msg, sizeof msg, "Save failed (%s).", arms_status_str(st));
        app_show_notice(ctx, 1, msg);
        return 0;
    }
    if (announce_success) {
        snprintf(msg, sizeof msg, "Saved to %s.", ctx->save_path);
        app_show_notice(ctx, 0, msg);
    }
    return 1;
}

static void on_save_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    save_now(user_data, 1);
}

/* ---- demo ---- */

static int system_is_empty(const ArmsSystem *s)
{
    return flight_count(&s->flights) == 0 && runway_count(&s->runways) == 0 &&
           graph_node_count(&s->graph) == 0 && history_count(&s->history) == 0 &&
           eventlog_count(&s->events) == 0;
}

/* Builds the demo into a temporary system first; the live system is replaced only if the
 * whole build succeeded. Memory only: nothing is written to disk here. */
static void load_demo(AppCtx *ctx)
{
    ArmsSystem demo;
    ArmsStatus st = demo_build(&demo);
    if (st != ARMS_OK) {            /* demo_build already freed its partial state */
        char msg[200];
        snprintf(msg, sizeof msg, "Could not build the demo (%s). Nothing was changed.",
                 arms_status_str(st));
        app_show_notice(ctx, 1, msg);
        return;
    }
    arms_system_clear(&ctx->sys);   /* free the old history/event lists, then take over demo's */
    ctx->sys = demo;
    app_refresh_all(ctx);
    app_show_notice(ctx, 0, "Demo loaded in memory only. Click Save (or close the app) to keep it.");
}

static void on_demo_confirmed(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AppCtx *ctx = user_data;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);
    if (choice == 1)                /* 0 = Cancel, 1 = Replace; errors/dismiss give -1 */
        load_demo(ctx);
}

static void on_demo_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;

    if (ctx->load_failed) {
        app_show_notice(ctx, 1, "Load Demo is disabled: the existing save could not be loaded.");
        return;
    }
    if (system_is_empty(&ctx->sys)) {
        load_demo(ctx);
        return;
    }
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Replace current data with the demo?");
    gtk_alert_dialog_set_detail(dialog, "The current in-memory data will be replaced. "
                                        "The save file is not touched until you save.");
    const char *buttons[] = {"Cancel", "Replace", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog, ctx->window, NULL, on_demo_confirmed, ctx);
    g_object_unref(dialog);
}

/* Returning TRUE cancels the close. A blocked save (load_failed) must not trap the user. */
static gboolean on_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    AppCtx *ctx = user_data;
    if (ctx->load_failed)
        return FALSE;
    return !save_now(ctx, 0);
}

/* ---- refresh ---- */

void app_refresh_all(AppCtx *ctx)
{
    page_dashboard_refresh(ctx);
    page_flights_refresh(ctx);
    page_runways_refresh(ctx);
    page_scheduler_refresh(ctx);
    page_holding_refresh(ctx);
    page_history_refresh(ctx);
    page_graph_refresh(ctx);
    page_reports_refresh(ctx);
    page_eventlog_refresh(ctx);
}

/* ---- navigation ---- */

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    AppCtx *ctx = user_data;
    if (row == NULL)
        return;

    int page = gtk_list_box_row_get_index(row);
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), PAGE_TITLE[page]);
    app_refresh_all(ctx);
}

static GtkWidget *build_pages(AppCtx *ctx)
{
    ctx->stack = gtk_stack_new();
    gtk_widget_set_hexpand(ctx->stack, TRUE);
    gtk_widget_set_vexpand(ctx->stack, TRUE);

    for (int i = 0; i < PAGE_COUNT; i++) {
        GtkWidget *page;
        if (i == PAGE_DASHBOARD)
            page = page_dashboard_new(ctx);
        else if (i == PAGE_FLIGHTS)
            page = page_flights_new(ctx);
        else if (i == PAGE_RUNWAYS)
            page = page_runways_new(ctx);
        else if (i == PAGE_SCHEDULER)
            page = page_scheduler_new(ctx);
        else if (i == PAGE_HOLDING)
            page = page_holding_new(ctx);
        else if (i == PAGE_HISTORY)
            page = page_history_new(ctx);
        else if (i == PAGE_GRAPH)
            page = page_graph_new(ctx);
        else if (i == PAGE_REPORTS)
            page = page_reports_new(ctx);
        else
            page = page_eventlog_new(ctx);
        gtk_stack_add_named(GTK_STACK(ctx->stack), page, PAGE_TITLE[i]);
    }
    return ctx->stack;
}

static GtkWidget *build_sidebar(AppCtx *ctx)
{
    ctx->sidebar = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx->sidebar), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(ctx->sidebar, "sidebar");
    gtk_widget_set_size_request(ctx->sidebar, 200, -1);

    for (int i = 0; i < PAGE_COUNT; i++) {
        GtkWidget *label = gtk_label_new(PAGE_TITLE[i]);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_add_css_class(label, "sidebar-item");
        gtk_list_box_append(GTK_LIST_BOX(ctx->sidebar), label);
    }
    g_signal_connect(ctx->sidebar, "row-selected", G_CALLBACK(on_row_selected), ctx);
    return ctx->sidebar;
}

/* ---- styling ---- */

/* Executable-relative first; the repo-root-relative path is only a convenience for CSS. */
static char *find_css(void)
{
    char *path = project_path(CSS_PATH);
    if (path != NULL && g_file_test(path, G_FILE_TEST_EXISTS))
        return path;
    g_free(path);
    return g_file_test(CSS_PATH, G_FILE_TEST_EXISTS) ? g_strdup(CSS_PATH) : NULL;
}

static void load_css(void)
{
    char *path = find_css();
    if (path == NULL) {
        g_warning("%s not found; using default styling", CSS_PATH);
        return;
    }
    GtkCssProvider *provider = gtk_css_provider_new();
    GFile *file = g_file_new_for_path(path);
    gtk_css_provider_load_from_file(provider, file);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(file);
    g_object_unref(provider);
    g_free(path);
}

/* ---- window ---- */

void app_activate(GtkApplication *app, gpointer user_data)
{
    AppCtx *ctx = user_data;

    ctx->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(ctx->window, "ARMS - Airport Runway Management System");
    gtk_window_set_default_size(ctx->window, 1100, 700);
    load_css();

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *save = gtk_button_new_with_label("Save");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), ctx);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), save);
    GtkWidget *demo = gtk_button_new_with_label("Load Demo");
    g_signal_connect(demo, "clicked", G_CALLBACK(on_demo_clicked), ctx);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), demo);
    gtk_window_set_titlebar(ctx->window, header);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(body), build_sidebar(ctx));
    gtk_box_append(GTK_BOX(body), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(body), build_pages(ctx));

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), notice_new(ctx));
    gtk_box_append(GTK_BOX(root), body);
    gtk_window_set_child(ctx->window, root);

    load_on_startup(ctx);
    gtk_widget_set_sensitive(demo, !ctx->load_failed);
    app_refresh_all(ctx);

    g_signal_connect(ctx->window, "close-request", G_CALLBACK(on_close_request), ctx);
    gtk_list_box_select_row(GTK_LIST_BOX(ctx->sidebar),
                            gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->sidebar), PAGE_DASHBOARD));
    gtk_window_present(ctx->window);
}
