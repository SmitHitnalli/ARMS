#include "page_placeholder.h"

GtkWidget *page_placeholder_new(const char *title, const char *note)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *heading = gtk_label_new(title);
    gtk_widget_add_css_class(heading, "page-title");
    GtkWidget *text = gtk_label_new(note);
    gtk_widget_add_css_class(text, "dim-label");

    gtk_box_append(GTK_BOX(box), heading);
    gtk_box_append(GTK_BOX(box), text);
    return box;
}
