#include "page_history.h"

static const char *const HEADERS[] = { "Flight ID", "Type", "Category", "Runway",
                                       "Request Tick", "Request Seq", "Completed Tick" };
#define COLUMNS 7

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

void page_history_refresh(AppCtx *ctx)
{
    HistoryWidgets *w = &ctx->history_page;
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(w->grid)) != NULL)
        gtk_grid_remove(GTK_GRID(w->grid), child);

    for (int c = 0; c < COLUMNS; c++)
        add_cell(w->grid, c, 0, HEADERS[c], "table-header");

    /* Walk the nodes directly: history_at(i) would rescan from the head for every row. */
    int row = 1;
    for (const MovementNode *n = ctx->sys.history.head; n != NULL; n = n->next, row++) {
        const Movement *m = &n->data;
        char tick[16], seq[16], done[16];
        snprintf(tick, sizeof tick, "%d", m->request_tick);
        snprintf(seq, sizeof seq, "%u", m->request_seq);
        snprintf(done, sizeof done, "%d", m->completed_tick);
        const char *cells[COLUMNS] = {
            m->flight_id, flight_type_name(m->type), aircraft_category_name(m->category),
            m->runway_id, tick, seq, done
        };
        for (int c = 0; c < COLUMNS; c++)
            add_cell(w->grid, c, row, cells[c], "table-cell");
    }
    if (row == 1) {
        GtkWidget *empty = gtk_label_new("No completed movements yet");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_grid_attach(GTK_GRID(w->grid), empty, 0, 1, COLUMNS, 1);
    }

    char count[48];
    snprintf(count, sizeof count, "Completed movements: %d", history_count(&ctx->sys.history));
    gtk_label_set_text(GTK_LABEL(w->count_label), count);
}

GtkWidget *page_history_new(AppCtx *ctx)
{
    HistoryWidgets *w = &ctx->history_page;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Movement History");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    w->count_label = gtk_label_new("");
    gtk_widget_set_halign(w->count_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), w->count_label);

    w->grid = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->grid);
    gtk_box_append(GTK_BOX(page), scroll);

    page_history_refresh(ctx);
    return page;
}
