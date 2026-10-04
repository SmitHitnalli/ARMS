#include "page_holding.h"
#include "ui_common.h"

static const char *const HEADERS[] = { "ID", "Type", "Emergency", "Fuel", "Request Seq", "Action" };
#define COLUMNS 6
#define NUMERIC (1u << 4)   /* Request Seq */

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

    ArmsStatus st = svc_hold_leave(&ctx->sys, id);
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

    /* svc_hold_enter also handles a flight that is currently in the scheduler heap. */
    ArmsStatus st = svc_hold_enter(&ctx->sys, id);
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
    ui_reset_grid(grid, HEADERS, COLUMNS, NUMERIC);

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
            ui_add_cell(grid, c, row, cells[c], "table-cell", (NUMERIC >> c) & 1u);

        GtkWidget *leave = gtk_button_new_with_label("Leave");
        gtk_widget_set_halign(leave, GTK_ALIGN_START);
        g_object_set_data_full(G_OBJECT(leave), "flight-id", g_strdup(f->id), g_free);
        g_signal_connect(leave, "clicked", G_CALLBACK(on_leave_clicked), ctx);
        gtk_grid_attach(GTK_GRID(grid), leave, COLUMNS - 1, row, 1, 1);
        row++;
    }
    if (row == 1)
        ui_add_empty(grid, COLUMNS, "No flights in holding");

    fill_enter_dropdown(ctx);
    fill_dev_label(ctx);
}

GtkWidget *page_holding_new(AppCtx *ctx)
{
    HoldingWidgets *w = &ctx->holding_page;
    GtkWidget *page = ui_page_new("Holding");

    gtk_box_append(GTK_BOX(page), ui_section_title("Hold a waiting flight"));
    GtkWidget *form = ui_form_row();
    w->enter_dd = gtk_drop_down_new(NULL, NULL);
    gtk_box_append(GTK_BOX(form), ui_form_label("Flight"));
    gtk_box_append(GTK_BOX(form), w->enter_dd);
    gtk_box_append(GTK_BOX(form), ui_button("Enter Holding", G_CALLBACK(on_enter_clicked), ctx));
    gtk_box_append(GTK_BOX(page), form);

    gtk_box_append(GTK_BOX(page), ui_section_title("Holding queue"));
    w->grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(page), ui_scroll_new(w->grid));

    w->dev_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->dev_label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(w->dev_label), TRUE);
    gtk_widget_add_css_class(w->dev_label, "monospace");
    gtk_label_set_wrap(GTK_LABEL(w->dev_label), TRUE);
    gtk_box_append(GTK_BOX(page), ui_dev_section("Developer: circular queue", w->dev_label));

    page_holding_refresh(ctx);
    return page;
}
