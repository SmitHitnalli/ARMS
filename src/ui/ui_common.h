#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <gtk/gtk.h>

/* Presentation helpers shared by the pages. No core calls and no application state. */

/* Vertical page box (.page) with the page title already added. */
GtkWidget *ui_page_new(const char *title);

GtkWidget *ui_section_title(const char *text);
GtkWidget *ui_form_row(void);
GtkWidget *ui_form_label(const char *text);
GtkWidget *ui_button(const char *label, GCallback cb, gpointer data);

/* Collapsed expander styled as a developer/DSA view. */
GtkWidget *ui_dev_section(const char *title, GtkWidget *child);

/* Scrolled window that fills the free height around a table. */
GtkWidget *ui_scroll_new(GtkWidget *child);

/* One table cell. css is "table-header" or "table-cell"; right aligns numeric columns. */
GtkWidget *ui_add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css,
                       int right);

/* Empties the grid and writes the header row. Bit c of numeric_cols right-aligns column c. */
void ui_reset_grid(GtkWidget *grid, const char *const *headers, int cols, unsigned numeric_cols);

/* Empty-state text spanning the table's columns, directly under the header. */
void ui_add_empty(GtkWidget *grid, int cols, const char *text);

#endif
