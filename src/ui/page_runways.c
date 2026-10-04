#include "page_runways.h"
#include "page_dashboard.h"
#include "ui_common.h"

static const char *const CATEGORY_CHOICES[] = { "SMALL", "MEDIUM", "LARGE", NULL };
static const char *const HEADERS[] = { "Runway ID", "Status", "Max Category",
                                       "Current Flight", "Action" };
#define COLUMNS 5

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

    ui_reset_grid(grid, HEADERS, COLUMNS, 0);

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
            ui_add_cell(grid, c, i + 1, cells[c], "table-cell", 0);
        gtk_grid_attach(GTK_GRID(grid), action_widget(ctx, r), COLUMNS - 1, i + 1, 1, 1);
    }
    if (n == 0)
        ui_add_empty(grid, COLUMNS, "No runways yet");
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
    GtkWidget *page = ui_page_new("Runways");

    gtk_box_append(GTK_BOX(page), ui_section_title("Add runway"));
    GtkWidget *form = ui_form_row();
    w->id_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->id_entry), "Runway ID");
    w->category_dd = gtk_drop_down_new_from_strings(CATEGORY_CHOICES);
    GtkWidget *add = ui_button("Add Runway", G_CALLBACK(on_add_clicked), ctx);
    g_signal_connect_swapped(w->id_entry, "activate", G_CALLBACK(gtk_widget_activate), add);
    gtk_box_append(GTK_BOX(form), ui_form_label("Runway ID"));
    gtk_box_append(GTK_BOX(form), w->id_entry);
    gtk_box_append(GTK_BOX(form), ui_form_label("Max category"));
    gtk_box_append(GTK_BOX(form), w->category_dd);
    gtk_box_append(GTK_BOX(form), add);
    gtk_box_append(GTK_BOX(page), form);

    gtk_box_append(GTK_BOX(page), ui_section_title("All runways"));
    w->grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(page), ui_scroll_new(w->grid));

    page_runways_refresh(ctx);
    return page;
}
