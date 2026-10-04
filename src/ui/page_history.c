#include "page_history.h"
#include "ui_common.h"

static const char *const HEADERS[] = { "Flight ID", "Type", "Category", "Runway",
                                       "Request Tick", "Request Seq", "Completed Tick" };
#define COLUMNS 7
#define NUMERIC (7u << 4)   /* Request Tick, Request Seq, Completed Tick */

void page_history_refresh(AppCtx *ctx)
{
    HistoryWidgets *w = &ctx->history_page;

    ui_reset_grid(w->grid, HEADERS, COLUMNS, NUMERIC);

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
            ui_add_cell(w->grid, c, row, cells[c], "table-cell", (NUMERIC >> c) & 1u);
    }
    if (row == 1)
        ui_add_empty(w->grid, COLUMNS, "No completed movements yet");

    char count[48];
    snprintf(count, sizeof count, "Completed movements: %d", history_count(&ctx->sys.history));
    gtk_label_set_text(GTK_LABEL(w->count_label), count);
}

GtkWidget *page_history_new(AppCtx *ctx)
{
    HistoryWidgets *w = &ctx->history_page;
    GtkWidget *page = ui_page_new("Movement History");

    w->count_label = gtk_label_new("");
    gtk_widget_add_css_class(w->count_label, "status-line");
    gtk_widget_set_halign(w->count_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), w->count_label);

    w->grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(page), ui_scroll_new(w->grid));

    page_history_refresh(ctx);
    return page;
}
