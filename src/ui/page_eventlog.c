#include "page_eventlog.h"

#include <string.h>

static const char *const HEADERS[] = { "Seq", "Tick", "Event", "Flight", "Runway", "Details" };
#define COLUMNS 6
#define ALL_TYPES 0   /* dropdown index 0 is "All"; index i + 1 is EventType i */

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

static int matches(const Event *e, guint type_sel, const char *flight_filter)
{
    if (type_sel != ALL_TYPES && (guint)e->type != type_sel - 1)
        return 0;
    return flight_filter[0] == '\0' || strstr(e->flight_id, flight_filter) != NULL;
}

void page_eventlog_refresh(AppCtx *ctx)
{
    EventLogWidgets *w = &ctx->eventlog_page;
    const EventLog *log = &ctx->sys.events;
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(w->grid)) != NULL)
        gtk_grid_remove(GTK_GRID(w->grid), child);
    for (int c = 0; c < COLUMNS; c++)
        add_cell(w->grid, c, 0, HEADERS[c], "table-header");

    /* Flight IDs are upper-case, so an upper-cased filter makes the search case-insensitive. */
    char flight_filter[ID_MAX_LEN + 1];
    snprintf(flight_filter, sizeof flight_filter, "%s",
             gtk_editable_get_text(GTK_EDITABLE(w->flight_entry)));
    for (char *p = flight_filter; *p != '\0'; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    guint type_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->type_dd));

    /* The list is stored oldest -> newest. Newest-first is presentation only: collect the
     * matches in stored order, then fill the grid from the last one back. The log is not
     * touched. Pointers stay valid because nothing appends during a refresh. */
    const Event **shown = g_new(const Event *, log->count > 0 ? log->count : 1);
    int n = 0;
    for (const EventNode *node = log->head; node != NULL; node = node->next)
        if (matches(&node->data, type_sel, flight_filter))
            shown[n++] = &node->data;

    for (int i = 0; i < n; i++) {
        const Event *e = shown[n - 1 - i];
        char seq[16], tick[16];
        snprintf(seq, sizeof seq, "%u", e->seq);
        snprintf(tick, sizeof tick, "%d", e->tick);
        const char *cells[COLUMNS] = {
            seq, tick, eventlog_type_name(e->type),
            e->flight_id[0] ? e->flight_id : "-", e->runway_id[0] ? e->runway_id : "-",
            e->details[0] ? e->details : "-"
        };
        for (int c = 0; c < COLUMNS; c++)
            add_cell(w->grid, c, i + 1, cells[c], "table-cell");
    }
    g_free(shown);

    if (n == 0) {
        GtkWidget *empty = gtk_label_new(log->count == 0 ? "No events recorded yet"
                                                         : "No events match the filters");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_grid_attach(GTK_GRID(w->grid), empty, 0, 1, COLUMNS, 1);
    }

    char status[112];
    int len = snprintf(status, sizeof status, "Total events: %d   Shown: %d", log->count, n);
    if (log->dropped > 0)
        snprintf(status + len, sizeof status - (size_t)len,
                 "   WARNING: %u event(s) could not be recorded", log->dropped);
    gtk_label_set_text(GTK_LABEL(w->count_label), status);
}

static void on_filter_changed(GObject *source, GParamSpec *pspec, gpointer user_data)
{
    (void)source;
    (void)pspec;
    page_eventlog_refresh(user_data);
}

GtkWidget *page_eventlog_new(AppCtx *ctx)
{
    EventLogWidgets *w = &ctx->eventlog_page;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Event Log");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkStringList *types = gtk_string_list_new(NULL);
    gtk_string_list_append(types, "All events");
    for (int t = 0; t < EVENT_TYPE_COUNT; t++)
        gtk_string_list_append(types, eventlog_type_name((EventType)t));
    w->type_dd = gtk_drop_down_new(G_LIST_MODEL(types), NULL);
    g_signal_connect(w->type_dd, "notify::selected", G_CALLBACK(on_filter_changed), ctx);
    gtk_box_append(GTK_BOX(bar), w->type_dd);

    w->flight_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->flight_entry), "Filter by flight ID");
    gtk_entry_set_max_length(GTK_ENTRY(w->flight_entry), ID_MAX_LEN);
    g_signal_connect(w->flight_entry, "notify::text", G_CALLBACK(on_filter_changed), ctx);
    gtk_box_append(GTK_BOX(bar), w->flight_entry);
    gtk_box_append(GTK_BOX(page), bar);

    w->count_label = gtk_label_new("");
    gtk_widget_set_halign(w->count_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), w->count_label);

    w->grid = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->grid);
    gtk_box_append(GTK_BOX(page), scroll);

    page_eventlog_refresh(ctx);
    return page;
}
