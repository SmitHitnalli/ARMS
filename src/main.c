#include "ui/app.h"

int main(int argc, char **argv)
{
    AppCtx ctx;
    GtkApplication *app = gtk_application_new("edu.arms.app", G_APPLICATION_DEFAULT_FLAGS);

    app_ctx_init(&ctx, app);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), &ctx);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    app_ctx_free(&ctx);
    return status;
}
