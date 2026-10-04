#include "page_reports.h"
#include "reports.h"
#include "ui_common.h"

static const char *const FLIGHT_KEYS[] = { "ID", "Request order", "Scheduler priority", NULL };
static const char *const HIST_KEYS[]   = { "Flight ID", "Completion", NULL };
static const char *const ORDERS[]      = { "Ascending", "Descending", NULL };
static const char *const FLIGHT_HEADERS[] = { "ID", "Type", "Category", "State", "Emergency",
                                              "Fuel", "Runway" };
static const char *const HIST_HEADERS[]   = { "Flight ID", "Type", "Category", "Runway",
                                              "Request Tick", "Request Seq", "Completed Tick" };
#define COLUMNS 7
#define HIST_NUMERIC (7u << 4)   /* Request Tick, Request Seq, Completed Tick */

static void add_flight_row(GtkWidget *grid, int row, const Flight *f)
{
    const char *cells[COLUMNS] = {
        f->id, flight_type_name(f->type), aircraft_category_name(f->category),
        flight_state_name(f->state), flight_emergency_name(f->emergency),
        flight_fuel_name(f->fuel), f->runway_id[0] != '\0' ? f->runway_id : "-"
    };
    for (int c = 0; c < COLUMNS; c++)
        ui_add_cell(grid, c, row, cells[c], "table-cell", 0);
}

static void fill_flights(AppCtx *ctx)
{
    ReportsWidgets *w = &ctx->reports_page;
    ui_reset_grid(w->flight_grid, FLIGHT_HEADERS, COLUMNS, 0);

    int cap = flight_count(&ctx->sys.flights), n = 0;
    if (cap == 0) {
        ui_add_empty(w->flight_grid, COLUMNS, "No flights yet");
        return;
    }
    const Flight **rows = g_new(const Flight *, cap);
    FlightSortKey key = (FlightSortKey)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->flight_key_dd));
    int asc = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->flight_order_dd)) == 0;

    ArmsStatus st = report_flights_sorted(&ctx->sys.flights, key, asc, rows, cap, &n);
    if (st != ARMS_OK)
        app_show_notice(ctx, 1, arms_status_str(st));
    for (int i = 0; i < n; i++)
        add_flight_row(w->flight_grid, i + 1, rows[i]);
    g_free(rows);
}

static void fill_history(AppCtx *ctx)
{
    ReportsWidgets *w = &ctx->reports_page;
    ui_reset_grid(w->hist_grid, HIST_HEADERS, COLUMNS, HIST_NUMERIC);

    int cap = history_count(&ctx->sys.history), n = 0;
    if (cap == 0) {
        ui_add_empty(w->hist_grid, COLUMNS, "No completed movements yet");
        return;
    }
    const Movement **rows = g_new(const Movement *, cap);
    HistorySortKey key = (HistorySortKey)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->hist_key_dd));
    int asc = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->hist_order_dd)) == 0;

    ArmsStatus st = report_history_sorted(&ctx->sys.history, key, asc, rows, cap, &n);
    if (st != ARMS_OK)
        app_show_notice(ctx, 1, arms_status_str(st));
    for (int i = 0; i < n; i++) {
        const Movement *m = rows[i];
        char tick[16], seq[16], done[16];
        snprintf(tick, sizeof tick, "%d", m->request_tick);
        snprintf(seq, sizeof seq, "%u", m->request_seq);
        snprintf(done, sizeof done, "%d", m->completed_tick);
        const char *cells[COLUMNS] = {
            m->flight_id, flight_type_name(m->type), aircraft_category_name(m->category),
            m->runway_id, tick, seq, done
        };
        for (int c = 0; c < COLUMNS; c++)
            ui_add_cell(w->hist_grid, c, i + 1, cells[c], "table-cell", (HIST_NUMERIC >> c) & 1u);
    }
    g_free(rows);
}

void page_reports_refresh(AppCtx *ctx)
{
    fill_flights(ctx);
    fill_history(ctx);
    gtk_label_set_text(GTK_LABEL(ctx->reports_page.find_label), "");
}

/* Sort/order dropdown changed: re-render that table at once (no Apply button). */
static void on_flight_sort_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd;
    (void)pspec;
    fill_flights(user_data);
}

static void on_hist_sort_changed(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)dd;
    (void)pspec;
    fill_history(user_data);
}

/* Binary search needs an ascending ID-sorted pointer array, so build one for each search. */
static void on_find_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    ReportsWidgets *w = &ctx->reports_page;
    const char *id = gtk_editable_get_text(GTK_EDITABLE(w->find_entry));

    int cap = flight_count(&ctx->sys.flights), n = 0;
    const Flight **sorted = g_new(const Flight *, cap > 0 ? cap : 1);
    const Flight *hit = NULL;

    if (cap > 0) {
        ArmsStatus st = report_flights_sorted(&ctx->sys.flights, FLIGHT_SORT_ID, 1, sorted, cap, &n);
        if (st != ARMS_OK) {
            g_free(sorted);
            app_show_notice(ctx, 1, arms_status_str(st));
            return;
        }
        hit = report_find_flight_sorted((const Flight *const *)sorted, n, id);
    }

    char msg[200];
    if (hit == NULL)
        snprintf(msg, sizeof msg, "Not found");
    else
        snprintf(msg, sizeof msg, "Found %s: %s, %s, state %s, emergency %s, fuel %s, runway %s",
                 hit->id, flight_type_name(hit->type), aircraft_category_name(hit->category),
                 flight_state_name(hit->state), flight_emergency_name(hit->emergency),
                 flight_fuel_name(hit->fuel), hit->runway_id[0] != '\0' ? hit->runway_id : "-");
    gtk_label_set_text(GTK_LABEL(w->find_label), msg);
    g_free(sorted);
}

/* Sort row: label, key dropdown, order dropdown; both re-render the table on change. */
static GtkWidget *sort_row(AppCtx *ctx, const char *const *keys, GtkWidget **key_dd,
                           GtkWidget **order_dd, GCallback on_changed)
{
    GtkWidget *row = ui_form_row();
    *key_dd = gtk_drop_down_new_from_strings(keys);
    *order_dd = gtk_drop_down_new_from_strings(ORDERS);
    g_signal_connect(*key_dd, "notify::selected", on_changed, ctx);
    g_signal_connect(*order_dd, "notify::selected", on_changed, ctx);
    gtk_box_append(GTK_BOX(row), ui_form_label("Sort by"));
    gtk_box_append(GTK_BOX(row), *key_dd);
    gtk_box_append(GTK_BOX(row), *order_dd);
    return row;
}

GtkWidget *page_reports_new(AppCtx *ctx)
{
    ReportsWidgets *w = &ctx->reports_page;
    GtkWidget *content = ui_page_new("Reports");

    /* flight report */
    gtk_box_append(GTK_BOX(content), ui_section_title("Flight report"));
    gtk_box_append(GTK_BOX(content), sort_row(ctx, FLIGHT_KEYS, &w->flight_key_dd,
                                              &w->flight_order_dd,
                                              G_CALLBACK(on_flight_sort_changed)));
    GtkWidget *hint = gtk_label_new("Scheduler priority: ascending = most urgent first.");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), hint);
    w->flight_grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(content), w->flight_grid);

    /* binary search */
    gtk_box_append(GTK_BOX(content), ui_section_title("Find flight by ID (binary search)"));
    GtkWidget *find_form = ui_form_row();
    w->find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->find_entry), "Flight ID");
    GtkWidget *find = ui_button("Find", G_CALLBACK(on_find_clicked), ctx);
    g_signal_connect_swapped(w->find_entry, "activate", G_CALLBACK(gtk_widget_activate), find);
    gtk_box_append(GTK_BOX(find_form), ui_form_label("Flight ID"));
    gtk_box_append(GTK_BOX(find_form), w->find_entry);
    gtk_box_append(GTK_BOX(find_form), find);
    gtk_box_append(GTK_BOX(content), find_form);
    w->find_label = gtk_label_new("");
    gtk_widget_set_halign(w->find_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(w->find_label), TRUE);
    gtk_box_append(GTK_BOX(content), w->find_label);

    /* movement report */
    gtk_box_append(GTK_BOX(content), ui_section_title("Movement report"));
    gtk_box_append(GTK_BOX(content), sort_row(ctx, HIST_KEYS, &w->hist_key_dd,
                                              &w->hist_order_dd, G_CALLBACK(on_hist_sort_changed)));
    w->hist_grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(content), w->hist_grid);

    page_reports_refresh(ctx);
    return ui_scroll_new(content);
}
