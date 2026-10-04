#include "page_scheduler.h"
#include "operations.h"
#include "reports.h"

static const char *const FLIGHT_HEADERS[] = { "ID", "Type", "Emergency", "Fuel", "Request Seq" };
#define FLIGHT_COLS 5
static const char *const HEAP_HEADERS[] = { "#", "ID", "Type", "Emergency", "Fuel", "Request Seq" };
static const char *const RAW_HEADERS[]  = { "Index", "Flight ID", "Parent Index" };
static const char *const OPS_HEADERS[]  = { "ID", "Type", "State", "Runway", "Actions" };
static const char *const EMERGENCY_CHOICES[] = { "NONE", "ACTIVE", NULL };
static const char *const FUEL_CHOICES[]      = { "NORMAL", "LOW", "CRITICAL", NULL };

typedef enum { ACT_START, ACT_COMPLETE, ACT_CANCEL } RowAction;

/* ---- small widget helpers ---- */

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

/* Empties the grid and writes the header row. */
static void reset_grid(GtkWidget *grid, const char *const *headers, int cols)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), child);
    for (int c = 0; c < cols; c++)
        add_cell(grid, c, 0, headers[c], "table-header");
}

static void add_empty_row(GtkWidget *grid, int cols, const char *text)
{
    GtkWidget *empty = gtk_label_new(text);
    gtk_widget_add_css_class(empty, "dim-label");
    gtk_widget_set_halign(empty, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), empty, 0, 1, cols, 1);
}

/* ID, Type, Emergency, Fuel, Request Seq starting at column col0. */
static void add_flight_cells(GtkWidget *grid, int col0, int row, const Flight *f)
{
    char seq[16];
    snprintf(seq, sizeof seq, "%u", f->request_seq);
    const char *cells[FLIGHT_COLS] = {
        f->id, flight_type_name(f->type), flight_emergency_name(f->emergency),
        flight_fuel_name(f->fuel), seq
    };
    for (int c = 0; c < FLIGHT_COLS; c++)
        add_cell(grid, col0 + c, row, cells[c], "table-cell");
}

static GtkWidget *section_title(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "title-4");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    return label;
}

static GtkWidget *grid_in_box(GtkWidget *parent_box, GtkWidget *grid)
{
    gtk_box_append(GTK_BOX(parent_box), grid);
    return grid;
}

/* ---- flight dropdowns ---- */

static int want_registered(FlightState s) { return s == STATE_REGISTERED; }
static int want_not_final(FlightState s)  { return s != STATE_COMPLETED && s != STATE_CANCELLED; }

/* Copies the selected flight ID into out (ID_MAX_LEN + 1 bytes); 0 if nothing is selected. */
static int selected_id(GtkWidget *dd, char *out)
{
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd));
    if (item == NULL)
        return 0;
    snprintf(out, ID_MAX_LEN + 1, "%s", gtk_string_object_get_string(GTK_STRING_OBJECT(item)));
    return 1;
}

/* Refills the dropdown with the IDs of flights whose state passes want(); keeps the
 * previous selection when that flight is still listed. */
static void fill_dropdown(GtkWidget *dd, const FlightStore *fs, int (*want)(FlightState))
{
    char keep[ID_MAX_LEN + 1] = "";
    selected_id(dd, keep);

    GtkStringList *list = gtk_string_list_new(NULL);
    guint pick = 0;
    for (int i = 0; i < flight_count(fs); i++) {
        const Flight *f = flight_at(fs, i);
        if (!want(f->state))
            continue;
        if (strcmp(f->id, keep) == 0)
            pick = g_list_model_get_n_items(G_LIST_MODEL(list));
        gtk_string_list_append(list, f->id);
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(dd), G_LIST_MODEL(list));
    g_object_unref(list);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), pick);
}

/* ---- actions: each calls one core API; the core validates ---- */

/* Shows the core error, or refreshes every page and shows ok_msg. */
static void finish(AppCtx *ctx, ArmsStatus st, const char *ok_msg)
{
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }
    app_refresh_all(ctx);
    app_show_notice(ctx, 0, ok_msg);
}

static void on_submit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    char id[ID_MAX_LEN + 1], msg[96];
    if (!selected_id(ctx->scheduler_page.submit_dd, id)) {
        app_show_notice(ctx, 1, "No REGISTERED flight to submit.");
        return;
    }
    ArmsStatus st = request_submit(&ctx->sys.requests, &ctx->sys.flights, id);
    snprintf(msg, sizeof msg, "Flight %s submitted and is now WAITING.", id);
    finish(ctx, st, msg);
}

static void on_pull_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    char msg[96];
    int before = scheduler_count(&ctx->sys.scheduler);
    ArmsStatus st = scheduler_pull(&ctx->sys.scheduler, &ctx->sys.requests, &ctx->sys.flights);
    snprintf(msg, sizeof msg, "Pulled %d flight(s) into the heap.",
             scheduler_count(&ctx->sys.scheduler) - before);
    if (st != ARMS_OK)   /* FULL: some flights were still moved, so refresh before reporting */
        app_refresh_all(ctx);
    finish(ctx, st, msg);
}

static void on_emergency_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    SchedulerWidgets *w = &ctx->scheduler_page;
    char id[ID_MAX_LEN + 1], msg[96];
    if (!selected_id(w->prio_dd, id)) {
        app_show_notice(ctx, 1, "No flight selected.");
        return;
    }
    EmergencyLevel lv = (EmergencyLevel)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->emergency_dd));
    ArmsStatus st = scheduler_set_emergency(&ctx->sys.scheduler, &ctx->sys.flights, id, lv);
    snprintf(msg, sizeof msg, "Flight %s emergency set to %s.", id, flight_emergency_name(lv));
    finish(ctx, st, msg);
}

static void on_fuel_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    SchedulerWidgets *w = &ctx->scheduler_page;
    char id[ID_MAX_LEN + 1], msg[96];
    if (!selected_id(w->prio_dd, id)) {
        app_show_notice(ctx, 1, "No flight selected.");
        return;
    }
    FuelLevel lv = (FuelLevel)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->fuel_dd));
    ArmsStatus st = scheduler_set_fuel(&ctx->sys.scheduler, &ctx->sys.flights, id, lv);
    snprintf(msg, sizeof msg, "Flight %s fuel set to %s.", id, flight_fuel_name(lv));
    finish(ctx, st, msg);
}

static void on_schedule_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    char flight[ID_MAX_LEN + 1], runway[ID_MAX_LEN + 1], msg[128];

    /* ops_schedule_next only sees the heap, so pull queued flights in first. */
    ArmsStatus st = scheduler_pull(&ctx->sys.scheduler, &ctx->sys.requests, &ctx->sys.flights);
    if (st != ARMS_OK && st != ARMS_ERR_FULL) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }
    st = ops_schedule_next(&ctx->sys.scheduler, &ctx->sys.flights, &ctx->sys.runways,
                           flight, runway);
    if (st == ARMS_ERR_NO_RUNWAY || st == ARMS_ERR_EMPTY) {
        app_refresh_all(ctx);   /* the pull may have moved flights into the heap */
        app_show_notice(ctx, 1, st == ARMS_ERR_NO_RUNWAY
            ? "Flights are waiting, but no AVAILABLE runway can handle them. Nothing was scheduled."
            : "No waiting flights to schedule.");
        return;
    }
    snprintf(msg, sizeof msg, "Flight %s scheduled on runway %s.", flight, runway);
    finish(ctx, st, msg);
}

static void on_row_action(GtkButton *button, gpointer user_data)
{
    AppCtx *ctx = user_data;
    char id[ID_MAX_LEN + 1], msg[128];
    snprintf(id, sizeof id, "%s", (const char *)g_object_get_data(G_OBJECT(button), "flight-id"));
    RowAction act = (RowAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));

    /* The refresh in finish() destroys this button: copy everything needed above. */
    if (act == ACT_START) {
        ArmsStatus st = ops_start(&ctx->sys.flights, &ctx->sys.runways, id);
        snprintf(msg, sizeof msg, "Flight %s is now IN_OPERATION.", id);
        finish(ctx, st, msg);
    } else if (act == ACT_COMPLETE) {
        ArmsStatus st = ops_complete(&ctx->sys.flights, &ctx->sys.runways, &ctx->sys.history, id);
        snprintf(msg, sizeof msg, "Flight %s completed. Movement history: %d record(s).",
                 id, history_count(&ctx->sys.history));
        finish(ctx, st, msg);
    } else {
        ArmsStatus st = ops_cancel(&ctx->sys.scheduler, &ctx->sys.holding, &ctx->sys.requests,
                                   &ctx->sys.flights, &ctx->sys.runways, id);
        snprintf(msg, sizeof msg, "Flight %s cancelled.", id);
        finish(ctx, st, msg);
    }
}

static GtkWidget *action_button(AppCtx *ctx, const char *label, const char *id, RowAction act)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    g_object_set_data_full(G_OBJECT(button), "flight-id", g_strdup(id), g_free);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(act));
    g_signal_connect(button, "clicked", G_CALLBACK(on_row_action), ctx);
    return button;
}

/* ---- refresh ---- */

static void fill_queue_grid(AppCtx *ctx, GtkWidget *grid, const Queue *q)
{
    reset_grid(grid, FLIGHT_HEADERS, FLIGHT_COLS);
    int n = queue_count(q);
    int row = 1;
    for (int i = 0; i < n; i++) {
        const Flight *f = flight_find(&ctx->sys.flights, queue_at(q, i));
        if (f != NULL)
            add_flight_cells(grid, 0, row++, f);
    }
    if (n == 0)
        add_empty_row(grid, FLIGHT_COLS, "Empty");
}

/* Priority preview: report_flights_sorted orders a pointer array only, so the heap and
 * the FlightStore are never touched. Heap members are picked out by scheduler_contains. */
static void fill_heap_grid(AppCtx *ctx, GtkWidget *grid)
{
    reset_grid(grid, HEAP_HEADERS, FLIGHT_COLS + 1);
    const Flight *sorted[MAX_FLIGHTS];
    int n = 0, rank = 0;
    if (report_flights_sorted(&ctx->sys.flights, FLIGHT_SORT_PRIORITY, 1,
                              sorted, MAX_FLIGHTS, &n) == ARMS_OK) {
        for (int i = 0; i < n; i++) {
            if (!scheduler_contains(&ctx->sys.scheduler, sorted[i]->id))
                continue;
            char num[16];
            snprintf(num, sizeof num, "%d", ++rank);
            add_cell(grid, 0, rank, num, "table-cell");
            add_flight_cells(grid, 1, rank, sorted[i]);
        }
    }
    if (rank == 0)
        add_empty_row(grid, FLIGHT_COLS + 1, "Heap is empty. Use Pull Waiting Flights.");
}

static void fill_raw_grid(AppCtx *ctx, GtkWidget *grid)
{
    reset_grid(grid, RAW_HEADERS, 3);
    const MaxHeap *h = &ctx->sys.scheduler.heap;
    for (int i = 0; i < h->count; i++) {
        char idx[16], parent[16];
        snprintf(idx, sizeof idx, "%d", i);
        snprintf(parent, sizeof parent, "%d", (i - 1) / 2);
        add_cell(grid, 0, i + 1, idx, "table-cell");
        add_cell(grid, 1, i + 1, h->ids[i], "table-cell");
        add_cell(grid, 2, i + 1, i == 0 ? "-" : parent, "table-cell");
    }
    if (h->count == 0)
        add_empty_row(grid, 3, "Empty");
}

static void fill_ops_grid(AppCtx *ctx, GtkWidget *grid)
{
    reset_grid(grid, OPS_HEADERS, 5);
    int row = 1;
    for (int i = 0; i < flight_count(&ctx->sys.flights); i++) {
        const Flight *f = flight_at(&ctx->sys.flights, i);
        if (!want_not_final(f->state))
            continue;
        const char *cells[4] = { f->id, flight_type_name(f->type), flight_state_name(f->state),
                                 f->runway_id[0] != '\0' ? f->runway_id : "-" };
        for (int c = 0; c < 4; c++)
            add_cell(grid, c, row, cells[c], "table-cell");

        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        if (f->state == STATE_SCHEDULED)
            gtk_box_append(GTK_BOX(actions), action_button(ctx, "Start", f->id, ACT_START));
        if (f->state == STATE_IN_OPERATION)
            gtk_box_append(GTK_BOX(actions), action_button(ctx, "Complete", f->id, ACT_COMPLETE));
        else   /* IN_OPERATION cannot be cancelled */
            gtk_box_append(GTK_BOX(actions), action_button(ctx, "Cancel", f->id, ACT_CANCEL));
        gtk_grid_attach(GTK_GRID(grid), actions, 4, row, 1, 1);
        row++;
    }
    if (row == 1)
        add_empty_row(grid, 5, "No active flights");
}

void page_scheduler_refresh(AppCtx *ctx)
{
    SchedulerWidgets *w = &ctx->scheduler_page;
    fill_dropdown(w->submit_dd, &ctx->sys.flights, want_registered);
    fill_dropdown(w->prio_dd, &ctx->sys.flights, want_not_final);
    fill_queue_grid(ctx, w->landing_grid, &ctx->sys.requests.landing);
    fill_queue_grid(ctx, w->takeoff_grid, &ctx->sys.requests.takeoff);
    fill_heap_grid(ctx, w->heap_grid);
    fill_raw_grid(ctx, w->raw_grid);
    fill_ops_grid(ctx, w->ops_grid);
}

/* ---- construction ---- */

static GtkWidget *row_box(void)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "form-row");
    return row;
}

static GtkWidget *button_new(const char *label, GCallback cb, AppCtx *ctx)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, ctx);
    return b;
}

GtkWidget *page_scheduler_new(AppCtx *ctx)
{
    SchedulerWidgets *w = &ctx->scheduler_page;
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(content, "page");

    GtkWidget *heading = gtk_label_new("Scheduler");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), heading);

    /* Request submission */
    gtk_box_append(GTK_BOX(content), section_title("Request runway service"));
    GtkWidget *row = row_box();
    w->submit_dd = gtk_drop_down_new(NULL, NULL);
    gtk_box_append(GTK_BOX(row), w->submit_dd);
    gtk_box_append(GTK_BOX(row), button_new("Submit Request", G_CALLBACK(on_submit_clicked), ctx));
    gtk_box_append(GTK_BOX(content), row);

    /* Normal queues */
    gtk_box_append(GTK_BOX(content), section_title("Landing queue (FIFO)"));
    w->landing_grid = grid_in_box(content, gtk_grid_new());
    gtk_box_append(GTK_BOX(content), section_title("Takeoff queue (FIFO)"));
    w->takeoff_grid = grid_in_box(content, gtk_grid_new());

    /* Heap */
    gtk_box_append(GTK_BOX(content), section_title("Priority heap (most urgent first)"));
    row = row_box();
    gtk_box_append(GTK_BOX(row),
                   button_new("Pull Waiting Flights", G_CALLBACK(on_pull_clicked), ctx));
    gtk_box_append(GTK_BOX(row),
                   button_new("Schedule Next", G_CALLBACK(on_schedule_clicked), ctx));
    gtk_box_append(GTK_BOX(content), row);
    w->heap_grid = grid_in_box(content, gtk_grid_new());

    GtkWidget *dev = gtk_expander_new("Developer: raw heap array");
    w->raw_grid = gtk_grid_new();
    gtk_expander_set_child(GTK_EXPANDER(dev), w->raw_grid);
    gtk_box_append(GTK_BOX(content), dev);

    /* Priority controls */
    gtk_box_append(GTK_BOX(content), section_title("Emergency and fuel"));
    row = row_box();
    w->prio_dd = gtk_drop_down_new(NULL, NULL);
    w->emergency_dd = gtk_drop_down_new_from_strings(EMERGENCY_CHOICES);
    w->fuel_dd = gtk_drop_down_new_from_strings(FUEL_CHOICES);
    gtk_box_append(GTK_BOX(row), w->prio_dd);
    gtk_box_append(GTK_BOX(row), w->emergency_dd);
    gtk_box_append(GTK_BOX(row),
                   button_new("Set Emergency", G_CALLBACK(on_emergency_clicked), ctx));
    gtk_box_append(GTK_BOX(row), w->fuel_dd);
    gtk_box_append(GTK_BOX(row), button_new("Set Fuel", G_CALLBACK(on_fuel_clicked), ctx));
    gtk_box_append(GTK_BOX(content), row);

    /* Operations */
    gtk_box_append(GTK_BOX(content), section_title("Active flights"));
    w->ops_grid = grid_in_box(content, gtk_grid_new());

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);

    page_scheduler_refresh(ctx);
    return scroll;
}
