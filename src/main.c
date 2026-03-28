#include <adwaita.h>
#include "window.h"

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    NotesWindow *win = notes_window_new(app);
    gtk_window_present(GTK_WINDOW(win->window));
}

static void on_quit(GSimpleAction *action, GVariant *param, gpointer app) {
    (void)action; (void)param;
    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char *argv[]) {
    AdwApplication *app = adw_application_new("com.notes.desktop",
                                               G_APPLICATION_DEFAULT_FLAGS);

    static const GActionEntry app_entries[] = {
        {"quit", on_quit, NULL, NULL, NULL, {0}},
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries,
                                   G_N_ELEMENTS(app_entries), app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
