#include "page_holding.h"

static const char *const HEADERS[] = { "ID", "Type", "Emergency", "Fuel", "Request Seq", "Action" };
#define COLUMNS 6

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

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

static void on_leave_clicked(GtkButton *button, gpointer user_data)
{
    AppCtx *ctx = user_data;
    char id[ID_MAX_LEN + 1], msg[96];
    snprintf(id, sizeof id, "%s", (const char *)g_object_get_data(G_OBJECT(button), "flight-id"));

    ArmsStatus st = holding_leave(&ctx->sys.holding, &ctx->sys.requests, &ctx->sys.flights, id);
    snprintf(msg, sizeof msg, "Flight %s left holding and is WAITING at the rear of its queue.", id);
    finish(ctx, st, msg);   /* destroys this button: nothing above may be touched after it */
}

static void on_enter_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    char id[ID_MAX_LEN + 1], msg[96];
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(ctx->holding_page.enter_dd));
    if (item == NULL) {
        app_show_notice(ctx, 1, "No WAITING flight to hold.");
        return;
    }
    snprintf(id, sizeof id, "%s", gtk_string_object_get_string(GTK_STRING_OBJECT(item)));

    /* scheduler_hold also handles a flight that is currently in the scheduler heap. */
    ArmsStatus st = scheduler_hold(&ctx->sys.scheduler, &ctx->sys.holding, &ctx->sys.requests,
                                   &ctx->sys.flights, id);
    snprintf(msg, sizeof msg, "Flight %s is now HOLDING.", id);
    finish(ctx, st, msg);
}

/* Refills the Enter dropdown with WAITING flights, keeping the selection if still listed. */
static void fill_enter_dropdown(AppCtx *ctx)
{
    GtkWidget *dd = ctx->holding_page.enter_dd;
    char keep[ID_MAX_LEN + 1] = "";
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd));
    if (item != NULL)
        snprintf(keep, sizeof keep, "%s", gtk_string_object_get_string(GTK_STRING_OBJECT(item)));

    GtkStringList *list = gtk_string_list_new(NULL);
    guint pick = 0;
    for (int i = 0; i < flight_count(&ctx->sys.flights); i++) {
        const Flight *f = flight_at(&ctx->sys.flights, i);
        if (f->state != STATE_WAITING)
            continue;
        if (strcmp(f->id, keep) == 0)
            pick = g_list_model_get_n_items(G_LIST_MODEL(list));
        gtk_string_list_append(list, f->id);
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(dd), G_LIST_MODEL(list));
    g_object_unref(list);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), pick);
}

/* Compact text view of the circular queue: pointers, logical order and raw slot numbers. */
static void fill_dev_label(AppCtx *ctx)
{
    const CircularQueue *q = &ctx->sys.holding.queue;
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "front = %d   rear = %d   count = %d   capacity = %d\n",
                           q->front, q->rear, q->count, CQUEUE_CAPACITY);
    g_string_append(s, "logical order : ");
    for (int i = 0; i < q->count; i++)
        g_string_append_printf(s, "%s%s", i ? " -> " : "", cqueue_at(q, i));
    if (q->count == 0)
        g_string_append(s, "(empty)");
    g_string_append(s, "\nraw slots     : ");
    for (int i = 0; i < q->count; i++)
        g_string_append_printf(s, "%s%d", i ? ", " : "", (q->front + i) % CQUEUE_CAPACITY);
    if (q->count == 0)
        g_string_append(s, "(none)");
    gtk_label_set_text(GTK_LABEL(ctx->holding_page.dev_label), s->str);
    g_string_free(s, TRUE);
}

void page_holding_refresh(AppCtx *ctx)
{
    GtkWidget *grid = ctx->holding_page.grid;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), child);
    for (int c = 0; c < COLUMNS; c++)
        add_cell(grid, c, 0, HEADERS[c], "table-header");

    const CircularQueue *q = &ctx->sys.holding.queue;
    int row = 1;
    for (int i = 0; i < cqueue_count(q); i++) {
        const Flight *f = flight_find(&ctx->sys.flights, cqueue_at(q, i));
        if (f == NULL)
            continue;
        char seq[16];
        snprintf(seq, sizeof seq, "%u", f->request_seq);
        const char *cells[COLUMNS - 1] = {
            f->id, flight_type_name(f->type), flight_emergency_name(f->emergency),
            flight_fuel_name(f->fuel), seq
        };
        for (int c = 0; c < COLUMNS - 1; c++)
            add_cell(grid, c, row, cells[c], "table-cell");

        GtkWidget *leave = gtk_button_new_with_label("Leave");
        gtk_widget_set_halign(leave, GTK_ALIGN_START);
        g_object_set_data_full(G_OBJECT(leave), "flight-id", g_strdup(f->id), g_free);
        g_signal_connect(leave, "clicked", G_CALLBACK(on_leave_clicked), ctx);
        gtk_grid_attach(GTK_GRID(grid), leave, COLUMNS - 1, row, 1, 1);
        row++;
    }
    if (row == 1) {
        GtkWidget *empty = gtk_label_new("Holding queue is empty");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_widget_set_halign(empty, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), empty, 0, 1, COLUMNS, 1);
    }

    fill_enter_dropdown(ctx);
    fill_dev_label(ctx);
}

GtkWidget *page_holding_new(AppCtx *ctx)
{
    HoldingWidgets *w = &ctx->holding_page;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Holding");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(form, "form-row");
    w->enter_dd = gtk_drop_down_new(NULL, NULL);
    GtkWidget *enter = gtk_button_new_with_label("Enter Holding");
    g_signal_connect(enter, "clicked", G_CALLBACK(on_enter_clicked), ctx);
    gtk_box_append(GTK_BOX(form), w->enter_dd);
    gtk_box_append(GTK_BOX(form), enter);
    gtk_box_append(GTK_BOX(page), form);

    w->grid = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->grid);
    gtk_box_append(GTK_BOX(page), scroll);

    GtkWidget *dev = gtk_expander_new("Developer: circular queue");
    w->dev_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->dev_label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(w->dev_label), TRUE);
    gtk_widget_add_css_class(w->dev_label, "monospace");
    gtk_label_set_wrap(GTK_LABEL(w->dev_label), TRUE);
    gtk_expander_set_child(GTK_EXPANDER(dev), w->dev_label);
    gtk_box_append(GTK_BOX(page), dev);

    page_holding_refresh(ctx);
    return page;
}
