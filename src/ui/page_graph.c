#include "page_graph.h"

static const char *const HEADERS[] = { "Index", "ID", "Label", "Connected To" };
#define COLUMNS 4

static void add_cell(GtkWidget *grid, int col, int row, const char *text, const char *css)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
}

/* Appends "ID" or "ID (Label)" (label omitted when empty). */
static void append_node(GString *s, const GraphNode *n)
{
    g_string_append(s, n->id);
    if (n->label[0] != '\0')
        g_string_append_printf(s, " (%s)", n->label);
}

/* The ID selected in a node dropdown, or NULL when the list is empty. Valid until it is refilled. */
static const char *selected_id(GtkWidget *dd)
{
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd));
    return item == NULL ? NULL : gtk_string_object_get_string(GTK_STRING_OBJECT(item));
}

/* Refills a dropdown with every node ID, keeping the selection if the node still exists. */
static void fill_dropdown(GtkWidget *dd, const AirportGraph *g)
{
    char keep[ID_MAX_LEN + 1] = "";
    const char *cur = selected_id(dd);
    if (cur != NULL)
        snprintf(keep, sizeof keep, "%s", cur);

    GtkStringList *list = gtk_string_list_new(NULL);
    guint pick = 0;
    for (int i = 0; i < graph_node_count(g); i++) {
        const GraphNode *n = graph_node_at(g, i);
        if (strcmp(n->id, keep) == 0)
            pick = (guint)i;
        gtk_string_list_append(list, n->id);
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(dd), G_LIST_MODEL(list));
    g_object_unref(list);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), pick);
}

/* Adjacency matrix as monospace text, with node IDs as row and column headers. */
static void fill_matrix(AppCtx *ctx)
{
    const AirportGraph *g = &ctx->sys.graph;
    GString *s = g_string_new(NULL);
    int n = graph_node_count(g);

    if (n == 0) {
        g_string_append(s, "(no nodes)");
    } else {
        g_string_append_printf(s, "%-*s", ID_MAX_LEN + 1, "");
        for (int j = 0; j < n; j++)
            g_string_append_printf(s, " %-*s", ID_MAX_LEN, graph_node_at(g, j)->id);
        for (int i = 0; i < n; i++) {
            const GraphNode *a = graph_node_at(g, i);
            g_string_append_printf(s, "\n%-*s", ID_MAX_LEN + 1, a->id);
            for (int j = 0; j < n; j++)
                g_string_append_printf(s, " %-*d", ID_MAX_LEN,
                                       graph_adjacent(g, a->id, graph_node_at(g, j)->id));
        }
    }
    gtk_label_set_text(GTK_LABEL(ctx->graph_page.matrix_label), s->str);
    g_string_free(s, TRUE);
}

void page_graph_refresh(AppCtx *ctx)
{
    GraphWidgets *w = &ctx->graph_page;
    const AirportGraph *g = &ctx->sys.graph;
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(w->nodes_grid)) != NULL)
        gtk_grid_remove(GTK_GRID(w->nodes_grid), child);

    for (int c = 0; c < COLUMNS; c++)
        add_cell(w->nodes_grid, c, 0, HEADERS[c], "table-header");

    int n = graph_node_count(g);
    for (int i = 0; i < n; i++) {
        const GraphNode *a = graph_node_at(g, i);
        char index[16];
        snprintf(index, sizeof index, "%d", i);

        GString *links = g_string_new(NULL);
        for (int j = 0; j < n; j++) {
            const GraphNode *b = graph_node_at(g, j);
            if (!graph_adjacent(g, a->id, b->id))
                continue;
            if (links->len > 0)
                g_string_append(links, ", ");
            append_node(links, b);
        }
        const char *cells[COLUMNS] = { index, a->id, a->label[0] != '\0' ? a->label : "-",
                                       links->len > 0 ? links->str : "-" };
        for (int c = 0; c < COLUMNS; c++)
            add_cell(w->nodes_grid, c, i + 1, cells[c], "table-cell");
        g_string_free(links, TRUE);
    }
    if (n == 0) {
        GtkWidget *empty = gtk_label_new("No nodes yet");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_grid_attach(GTK_GRID(w->nodes_grid), empty, 0, 1, COLUMNS, 1);
    }

    fill_dropdown(w->edge_a_dd, g);
    fill_dropdown(w->edge_b_dd, g);
    fill_dropdown(w->trav_dd, g);
    fill_dropdown(w->reach_from_dd, g);
    fill_dropdown(w->reach_to_dd, g);
    fill_matrix(ctx);
    gtk_label_set_text(GTK_LABEL(w->trav_label), "");
    gtk_label_set_text(GTK_LABEL(w->reach_label), "");
}

static void on_add_node(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    GraphWidgets *w = &ctx->graph_page;
    const char *id = gtk_editable_get_text(GTK_EDITABLE(w->id_entry));
    const char *label = gtk_editable_get_text(GTK_EDITABLE(w->label_entry));

    ArmsStatus st = graph_add_node(&ctx->sys.graph, id, label);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "Node %s added.", id);
    gtk_editable_set_text(GTK_EDITABLE(w->id_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(w->label_entry), "");
    page_graph_refresh(ctx);
    app_show_notice(ctx, 0, msg);
}

static void on_add_edge(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    GraphWidgets *w = &ctx->graph_page;
    const char *a = selected_id(w->edge_a_dd);
    const char *b = selected_id(w->edge_b_dd);

    if (a == NULL || b == NULL) {
        app_show_notice(ctx, 1, "Add nodes first, then pick both ends of the edge.");
        return;
    }
    ArmsStatus st = graph_add_edge(&ctx->sys.graph, a, b);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "Edge %s - %s added.", a, b);   /* before the refill below */
    page_graph_refresh(ctx);
    app_show_notice(ctx, 0, msg);
}

/* One handler for both buttons; the "dfs" flag on the button picks the algorithm. */
static void on_traverse(GtkButton *button, gpointer user_data)
{
    AppCtx *ctx = user_data;
    GraphWidgets *w = &ctx->graph_page;
    int dfs = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "dfs"));
    const char *start = selected_id(w->trav_dd);

    if (start == NULL) {
        app_show_notice(ctx, 1, "Add a node first.");
        return;
    }
    int order[MAX_NODES], n = 0;
    ArmsStatus st = dfs ? graph_dfs(&ctx->sys.graph, start, order, MAX_NODES, &n)
                        : graph_bfs(&ctx->sys.graph, start, order, MAX_NODES, &n);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }

    GString *s = g_string_new(dfs ? "DFS: " : "BFS: ");
    for (int i = 0; i < n; i++) {
        if (i > 0)
            g_string_append(s, " -> ");
        append_node(s, graph_node_at(&ctx->sys.graph, order[i]));
    }
    gtk_label_set_text(GTK_LABEL(w->trav_label), s->str);
    g_string_free(s, TRUE);
}

static void on_reach(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppCtx *ctx = user_data;
    GraphWidgets *w = &ctx->graph_page;
    const char *from = selected_id(w->reach_from_dd);
    const char *to = selected_id(w->reach_to_dd);

    if (from == NULL || to == NULL) {
        app_show_notice(ctx, 1, "Add nodes first.");
        return;
    }
    int reachable = 0;
    ArmsStatus st = graph_is_reachable(&ctx->sys.graph, from, to, &reachable);
    if (st != ARMS_OK) {
        app_show_notice(ctx, 1, arms_status_str(st));
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "%s: %s is %sreachable from %s", reachable ? "Reachable" : "Not reachable",
             to, reachable ? "" : "not ", from);
    gtk_label_set_text(GTK_LABEL(w->reach_label), msg);
}

static GtkWidget *section_title(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "card-title");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    return label;
}

static GtkWidget *form_row(void)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "form-row");
    return row;
}

static GtkWidget *button_new(const char *text, GCallback cb, AppCtx *ctx)
{
    GtkWidget *b = gtk_button_new_with_label(text);
    g_signal_connect(b, "clicked", cb, ctx);
    return b;
}

static GtkWidget *result_label(void)
{
    GtkWidget *label = gtk_label_new("");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    return label;
}

GtkWidget *page_graph_new(AppCtx *ctx)
{
    GraphWidgets *w = &ctx->graph_page;
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(content, "page");

    GtkWidget *heading = gtk_label_new("Airport Graph");
    gtk_widget_add_css_class(heading, "page-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), heading);

    /* edit */
    GtkWidget *node_form = form_row();
    w->id_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->id_entry), "Node ID");
    w->label_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->label_entry), "Label (optional)");
    GtkWidget *add_node = button_new("Add Node", G_CALLBACK(on_add_node), ctx);
    g_signal_connect_swapped(w->label_entry, "activate", G_CALLBACK(gtk_widget_activate), add_node);
    gtk_box_append(GTK_BOX(node_form), w->id_entry);
    gtk_box_append(GTK_BOX(node_form), w->label_entry);
    gtk_box_append(GTK_BOX(node_form), add_node);
    gtk_box_append(GTK_BOX(content), node_form);

    GtkWidget *edge_form = form_row();
    w->edge_a_dd = gtk_drop_down_new(NULL, NULL);
    w->edge_b_dd = gtk_drop_down_new(NULL, NULL);
    gtk_box_append(GTK_BOX(edge_form), w->edge_a_dd);
    gtk_box_append(GTK_BOX(edge_form), gtk_label_new("-"));
    gtk_box_append(GTK_BOX(edge_form), w->edge_b_dd);
    gtk_box_append(GTK_BOX(edge_form), button_new("Add Edge", G_CALLBACK(on_add_edge), ctx));
    gtk_box_append(GTK_BOX(content), edge_form);

    /* nodes and connections */
    w->nodes_grid = gtk_grid_new();
    gtk_box_append(GTK_BOX(content), w->nodes_grid);

    /* traversal */
    gtk_box_append(GTK_BOX(content), section_title("Traversal"));
    GtkWidget *trav_form = form_row();
    w->trav_dd = gtk_drop_down_new(NULL, NULL);
    GtkWidget *bfs = button_new("BFS", G_CALLBACK(on_traverse), ctx);
    GtkWidget *dfs = button_new("DFS", G_CALLBACK(on_traverse), ctx);
    g_object_set_data(G_OBJECT(dfs), "dfs", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(trav_form), gtk_label_new("Start:"));
    gtk_box_append(GTK_BOX(trav_form), w->trav_dd);
    gtk_box_append(GTK_BOX(trav_form), bfs);
    gtk_box_append(GTK_BOX(trav_form), dfs);
    gtk_box_append(GTK_BOX(content), trav_form);
    w->trav_label = result_label();
    gtk_box_append(GTK_BOX(content), w->trav_label);

    /* reachability */
    gtk_box_append(GTK_BOX(content), section_title("Reachability"));
    GtkWidget *reach_form = form_row();
    w->reach_from_dd = gtk_drop_down_new(NULL, NULL);
    w->reach_to_dd = gtk_drop_down_new(NULL, NULL);
    gtk_box_append(GTK_BOX(reach_form), gtk_label_new("From:"));
    gtk_box_append(GTK_BOX(reach_form), w->reach_from_dd);
    gtk_box_append(GTK_BOX(reach_form), gtk_label_new("To:"));
    gtk_box_append(GTK_BOX(reach_form), w->reach_to_dd);
    gtk_box_append(GTK_BOX(reach_form),
                   button_new("Check Reachability", G_CALLBACK(on_reach), ctx));
    gtk_box_append(GTK_BOX(content), reach_form);
    w->reach_label = result_label();
    gtk_box_append(GTK_BOX(content), w->reach_label);

    /* developer view */
    GtkWidget *dev = gtk_expander_new("Developer: adjacency matrix");
    w->matrix_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->matrix_label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(w->matrix_label), TRUE);
    gtk_widget_add_css_class(w->matrix_label, "monospace");
    gtk_expander_set_child(GTK_EXPANDER(dev), w->matrix_label);
    gtk_box_append(GTK_BOX(content), dev);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);

    page_graph_refresh(ctx);
    return scroll;
}
