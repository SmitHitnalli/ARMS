#include "page_flights.h"
#include "page_dashboard.h"
#include "ui_common.h"

static const char *const TYPE_CHOICES[]     = { "ARRIVAL", "DEPARTURE", NULL };
static const char *const CATEGORY_CHOICES[] = { "SMALL", "MEDIUM", "LARGE", NULL };
static const char *const HEADERS[] = { "Flight ID", "Type", "Category", "State",
                                       "Emergency", "Fuel", "Runway" };
#define COLUMNS 7

void page_flights_refresh(AppCtx *ctx)
{
    GtkWidget *grid = ctx->flights_page.grid;

    ui_reset_grid(grid, HEADERS, COLUMNS, 0);

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
            ui_add_cell(grid, c, i + 1, cells[c], "table-cell", 0);
    }
    if (n == 0)
        ui_add_empty(grid, COLUMNS, "No flights yet");
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

    ArmsStatus st = svc_flight_add(&ctx->sys, id, type, cat);
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
    GtkWidget *page = ui_page_new("Flights");

    gtk_box_append(GTK_BOX(page), ui_section_title("Add flight"));
    GtkWidget *form = ui_form_row();
    w->id_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->id_entry), "Flight ID");
    w->type_dd = gtk_drop_down_new_from_strings(TYPE_CHOICES);
    w->category_dd = gtk_drop_down_new_from_strings(CATEGORY_CHOICES);
    GtkWidget *add = ui_button("Add Flight", G_CALLBACK(on_add_clicked), ctx);
    g_signal_connect_swapped(w->id_entry, "activate", G_CALLBACK(gtk_widget_activate), add);
    gtk_box_append(GTK_BOX(form), ui_form_label("Flight ID"));
    gtk_box_append(GTK_BOX(form), w->id_entry);
    gtk_box_append(GTK_BOX(form), ui_form_label("Type"));
    gtk_box_append(GTK_BOX(form), w->type_dd);
    gtk_box_append(GTK_BOX(form), ui_form_label("Category"));
    gtk_box_append(GTK_BOX(form), w->category_dd);
    gtk_box_append(GTK_BOX(form), add);
    gtk_box_append(GTK_BOX(page), form);

    gtk_box_append(GTK_BOX(page), ui_section_title("All flights"));
    w->grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(page), ui_scroll_new(w->grid));

    page_flights_refresh(ctx);
    return page;
}
