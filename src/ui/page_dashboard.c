#include "page_dashboard.h"

/* One summary card: a small title above a big value. Returns the value label. */
static GtkWidget *add_card(GtkWidget *row, const char *title)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_hexpand(card, TRUE);

    GtkWidget *name = gtk_label_new(title);
    gtk_widget_add_css_class(name, "card-title");
    gtk_widget_set_halign(name, GTK_ALIGN_START);

    GtkWidget *value = gtk_label_new("0");
    gtk_widget_add_css_class(value, "card-value");
    gtk_widget_set_halign(value, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(card), name);
    gtk_box_append(GTK_BOX(card), value);
    gtk_box_append(GTK_BOX(row), card);
    return value;
}

static void set_number(GtkWidget *label, int n)
{
    char text[16];
    snprintf(text, sizeof text, "%d", n);
    gtk_label_set_text(GTK_LABEL(label), text);
}

void page_dashboard_refresh(AppCtx *ctx)
{
    int waiting = 0, emergencies = 0, available = 0;

    for (int i = 0; i < flight_count(&ctx->sys.flights); i++) {
        const Flight *f = flight_at(&ctx->sys.flights, i);
        if (f->state == STATE_WAITING)
            waiting++;
        /* A finished flight is no longer an emergency, even if its flag is still set. */
        if (f->emergency == EMERGENCY_ACTIVE &&
            f->state != STATE_COMPLETED && f->state != STATE_CANCELLED)
            emergencies++;
    }
    for (int i = 0; i < runway_count(&ctx->sys.runways); i++)
        if (runway_at(&ctx->sys.runways, i)->status == RUNWAY_AVAILABLE)
            available++;

    set_number(ctx->dashboard.total, flight_count(&ctx->sys.flights));
    set_number(ctx->dashboard.waiting, waiting);
    set_number(ctx->dashboard.emergencies, emergencies);
    set_number(ctx->dashboard.runways, available);
}

GtkWidget *page_dashboard_new(AppCtx *ctx)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new("Dashboard");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    ctx->dashboard.total       = add_card(row, "Total flights");
    ctx->dashboard.waiting     = add_card(row, "Waiting flights");
    ctx->dashboard.emergencies = add_card(row, "Active emergencies");
    ctx->dashboard.runways     = add_card(row, "Available runways");
    gtk_box_append(GTK_BOX(page), row);

    page_dashboard_refresh(ctx);
    return page;
}
