#include "ui_common.h"

GtkWidget *ui_page_new(const char *title)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(page, "page");

    GtkWidget *heading = gtk_label_new(title);
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), heading);
    return page;
}

GtkWidget *ui_section_title(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "section-title");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    return label;
}

GtkWidget *ui_form_row(void)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "form-row");
    return row;
}

GtkWidget *ui_form_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "form-label");
    return label;
}

GtkWidget *ui_button(const char *label, GCallback cb, gpointer data)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", cb, data);
    return button;
}

GtkWidget *ui_dev_section(const char *title, GtkWidget *child)
{
    GtkWidget *dev = gtk_expander_new(title);   /* collapsed by default */
    gtk_widget_add_css_class(dev, "dev-section");
    gtk_expander_set_child(GTK_EXPANDER(dev), child);
    return dev;
}

GtkWidget *ui_scroll_new(GtkWidget *child)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child);
    return scroll;
}

GtkWidget *ui_add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css,
                       int right)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), right ? 1.0f : 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
    return label;
}

void ui_reset_grid(GtkWidget *grid, const char *const *headers, int cols, unsigned numeric_cols)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(grid)) != NULL)
        gtk_grid_remove(GTK_GRID(grid), child);
    for (int c = 0; c < cols; c++)
        ui_add_cell(grid, c, 0, headers[c], "table-header", (numeric_cols >> c) & 1u);
}

void ui_add_empty(GtkWidget *grid, int cols, const char *text)
{
    GtkWidget *empty = gtk_label_new(text);
    gtk_widget_add_css_class(empty, "empty-state");
    gtk_widget_add_css_class(empty, "dim-label");
    gtk_widget_set_halign(empty, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), empty, 0, 1, cols, 1);
}
