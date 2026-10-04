#include "page_runways.h"
#include "page_dashboard.h"

static const char *const CATEGORY_CHOICES[] = { "SMALL", "MEDIUM", "LARGE", NULL };
static const char *const HEADERS[] = { "Runway ID", "Status", "Max Category",
                                       "Current Flight", "Action" };
#define COLUMNS 5

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

/* The button carries the runway ID and the status to set; the core validates the change. */
static void on_status_clicked(GtkButton *button, gpointer user_data)
{
    AppCtx *ctx = user_data;
    char id[ID_MAX_LEN + 1];
    snprintf(id, sizeof id, "%s", (const char *)g_object_get_data(G_OBJECT(button), "runway-id"));
    RunwayStatus to = (RunwayStatus)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "to"));

    ArmsStatus st = svc_runway_set_status(&ctx->sys, id, to);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }

    char msg[96];
    snprintf(msg, sizeof msg, "Runway %s %s.", id,
             to == RUNWAY_CLOSED ? "closed" : "reopened");
    page_runways_refresh(ctx);   /* destroys this button: nothing may touch it after this */
    page_dashboard_refresh(ctx);
    app_show_notice(ctx, 0, msg);
}

static GtkWidget *action_widget(AppCtx *ctx, const Runway *r)
{
    if (r->status == RUNWAY_OCCUPIED) {
        GtkWidget *label = gtk_label_new("Occupied");
        gtk_widget_add_css_class(label, "dim-label");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        return label;
    }

    int closing = (r->status == RUNWAY_AVAILABLE);
    GtkWidget *button = gtk_button_new_with_label(closing ? "Close" : "Reopen");
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(button), "runway-id", g_strdup(r->id), g_free);
    g_object_set_data(G_OBJECT(button), "to",
                      GINT_TO_POINTER(closing ? RUNWAY_CLOSED : RUNWAY_AVAILABLE));
    g_signal_connect(button, "clicked", G_CALLBACK(on_status_clicked), ctx);
    return button;
}

void page_runways_refresh(AppCtx *ctx)
{
    GtkWidget *grid = ctx->runways_page.grid;
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), child);

    for (int c = 0; c < COLUMNS; c++)
        add_cell(grid, c, 0, HEADERS[c], "table-header");

    int n = runway_count(&ctx->sys.runways);
    for (int i = 0; i < n; i++) {
        const Runway *r = runway_at(&ctx->sys.runways, i);
        const char *cells[COLUMNS - 1] = {
            r->id,
            runway_status_name(r->status),
            aircraft_category_name(r->max_category),
            r->current_flight_id[0] != '\0' ? r->current_flight_id : "-"
        };
        for (int c = 0; c < COLUMNS - 1; c++)
            add_cell(grid, c, i + 1, cells[c], "table-cell");
        gtk_grid_attach(GTK_GRID(grid), action_widget(ctx, r), COLUMNS - 1, i + 1, 1, 1);
    }
    if (n == 0) {
        GtkWidget *empty = gtk_label_new("No runways yet");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), empty, 0, 1, COLUMNS, 1);
    }
}

static void on_add_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    RunwaysWidgets *w = &ctx->runways_page;
    const char *id = gtk_editable_get_text(GTK_EDITABLE(w->id_entry));
    AircraftCategory cat =
        (AircraftCategory)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->category_dd));

    ArmsStatus st = svc_runway_add(&ctx->sys, id, cat);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }

    char msg[96];
    snprintf(msg, sizeof msg, "Runway %s added.", id);
    gtk_editable_set_text(GTK_EDITABLE(w->id_entry), "");
    page_runways_refresh(ctx);
    page_dashboard_refresh(ctx);
    app_show_notice(ctx, 0, msg);
}

GtkWidget *page_runways_new(AppCtx *ctx)
{
    RunwaysWidgets *w = &ctx->runways_page;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Runways");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(form, "form-row");
    w->id_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->id_entry), "Runway ID");
    w->category_dd = gtk_drop_down_new_from_strings(CATEGORY_CHOICES);
    GtkWidget *add = gtk_button_new_with_label("Add Runway");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_clicked), ctx);
    g_signal_connect_swapped(w->id_entry, "activate", G_CALLBACK(gtk_widget_activate), add);
    gtk_box_append(GTK_BOX(form), w->id_entry);
    gtk_box_append(GTK_BOX(form), w->category_dd);
    gtk_box_append(GTK_BOX(form), add);
    gtk_box_append(GTK_BOX(page), form);

    w->grid = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->grid);
    gtk_box_append(GTK_BOX(page), scroll);

    page_runways_refresh(ctx);
    return page;
}
