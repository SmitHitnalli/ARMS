#ifndef UI_PAGE_PLACEHOLDER_H
#define UI_PAGE_PLACEHOLDER_H

#include <gtk/gtk.h>

/* A page with a title and a short note; used for screens that are not built yet. */
GtkWidget *page_placeholder_new(const char *title, const char *note);

#endif
