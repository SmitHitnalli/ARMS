#include "page_eventlog.h"

#include <string.h>

#include "ui_common.h"

static const char *const HEADERS[] = { "Seq", "Tick", "Event", "Flight", "Runway", "Details" };
#define COLUMNS 6
#define NUMERIC 3u   /* Seq, Tick */
#define ALL_TYPES 0   /* dropdown index 0 is "All"; index i + 1 is EventType i */

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

    ui_reset_grid(w->grid, HEADERS, COLUMNS, NUMERIC);

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
        for (int c = 0; c < COLUMNS; c++) {
            GtkWidget *cell = ui_add_cell(w->grid, c, i + 1, cells[c], "table-cell", (NUMERIC >> c) & 1u);
            if (c == COLUMNS - 1) {   /* details can be long: wrap, full text in the tooltip */
                gtk_label_set_wrap(GTK_LABEL(cell), TRUE);
                gtk_label_set_wrap_mode(GTK_LABEL(cell), PANGO_WRAP_WORD_CHAR);
                gtk_label_set_max_width_chars(GTK_LABEL(cell), 60);
                gtk_widget_set_tooltip_text(cell, cells[c]);
            }
        }
    }
    g_free(shown);

    if (n == 0)
        ui_add_empty(w->grid, COLUMNS, log->count == 0 ? "No events recorded yet"
                                                       : "No events match the filters");

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
    GtkWidget *page = ui_page_new("Event Log");

    GtkWidget *bar = ui_form_row();
    GtkStringList *types = gtk_string_list_new(NULL);
    gtk_string_list_append(types, "All events");
    for (int t = 0; t < EVENT_TYPE_COUNT; t++)
        gtk_string_list_append(types, eventlog_type_name((EventType)t));
    w->type_dd = gtk_drop_down_new(G_LIST_MODEL(types), NULL);
    g_signal_connect(w->type_dd, "notify::selected", G_CALLBACK(on_filter_changed), ctx);
    gtk_box_append(GTK_BOX(bar), ui_form_label("Event type"));
    gtk_box_append(GTK_BOX(bar), w->type_dd);

    w->flight_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->flight_entry), "Filter by flight ID");
    gtk_entry_set_max_length(GTK_ENTRY(w->flight_entry), ID_MAX_LEN);
    g_signal_connect(w->flight_entry, "notify::text", G_CALLBACK(on_filter_changed), ctx);
    gtk_box_append(GTK_BOX(bar), ui_form_label("Flight"));
    gtk_box_append(GTK_BOX(bar), w->flight_entry);
    gtk_box_append(GTK_BOX(page), bar);

    w->count_label = gtk_label_new("");
    gtk_widget_add_css_class(w->count_label, "status-line");
    gtk_widget_set_halign(w->count_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), w->count_label);

    w->grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(page), ui_scroll_new(w->grid));

    page_eventlog_refresh(ctx);
    return page;
}
