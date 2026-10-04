#include "page_flights.h"
#include "page_dashboard.h"

static const char *const TYPE_CHOICES[]     = { "ARRIVAL", "DEPARTURE", NULL };
static const char *const CATEGORY_CHOICES[] = { "SMALL", "MEDIUM", "LARGE", NULL };
static const char *const HEADERS[] = { "Flight ID", "Type", "Category", "State",
                                       "Emergency", "Fuel", "Runway" };
#define COLUMNS 7

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

void page_flights_refresh(AppCtx *ctx)
{
    GtkWidget *grid = ctx->flights_page.grid;
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), child);

    for (int c = 0; c < COLUMNS; c++)
        add_cell(grid, c, 0, HEADERS[c], "table-header");

    int n = flight_count(&ctx->sys.flights);
    for (int i = 0; i < n; i++) {
        const Flight *f = flight_at(&ctx->sys.flights, i);
        const char *cells[COLUMNS] = {
            f->id,
            flight_type_name(f->type),
            aircraft_category_name(f->category),
            flight_state_name(f->state),
            flight_emergency_name(f->emergency),
            flight_fuel_name(f->fuel),
            f->runway_id[0] != '\0' ? f->runway_id : "-"
        };
        for (int c = 0; c < COLUMNS; c++)
            add_cell(grid, c, i + 1, cells[c], "table-cell");
    }
    if (n == 0) {
        GtkWidget *empty = gtk_label_new("No flights yet");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), empty, 0, 1, COLUMNS, 1);
    }
}

static void on_add_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    FlightsWidgets *w = &ctx->flights_page;
    const char *id = gtk_editable_get_text(GTK_EDITABLE(w->id_entry));
    FlightType type = (FlightType)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->type_dd));
    AircraftCategory cat =
        (AircraftCategory)gtk_drop_down_get_selected(GTK_DROP_DOWN(w->category_dd));

    ArmsStatus st = flight_add(&ctx->sys.flights, id, type, cat);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }

    char msg[96];
    snprintf(msg, sizeof msg, "Flight %s added.", id);
    gtk_editable_set_text(GTK_EDITABLE(w->id_entry), "");
    page_flights_refresh(ctx);
    page_dashboard_refresh(ctx);
    app_show_notice(ctx, 0, msg);
}

GtkWidget *page_flights_new(AppCtx *ctx)
{
    FlightsWidgets *w = &ctx->flights_page;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Flights");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(form, "form-row");
    w->id_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->id_entry), "Flight ID");
    w->type_dd = gtk_drop_down_new_from_strings(TYPE_CHOICES);
    w->category_dd = gtk_drop_down_new_from_strings(CATEGORY_CHOICES);
    GtkWidget *add = gtk_button_new_with_label("Add Flight");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_clicked), ctx);
    g_signal_connect_swapped(w->id_entry, "activate", G_CALLBACK(gtk_widget_activate), add);
    gtk_box_append(GTK_BOX(form), w->id_entry);
    gtk_box_append(GTK_BOX(form), w->type_dd);
    gtk_box_append(GTK_BOX(form), w->category_dd);
    gtk_box_append(GTK_BOX(form), add);
    gtk_box_append(GTK_BOX(page), form);

    w->grid = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->grid);
    gtk_box_append(GTK_BOX(page), scroll);

    page_flights_refresh(ctx);
    return page;
}
