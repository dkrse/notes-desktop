#include "actions.h"
#include <adwaita.h>
#include <webkit/webkit.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* --- Theme list (must match window.c custom_themes + system/light/dark) --- */
static const char *theme_ids[] = {
    "system", "light", "dark",
    "solarized-light", "solarized-dark",
    "monokai",
    "gruvbox-light", "gruvbox-dark",
    "nord", "dracula", "tokyo-night",
    "catppuccin-latte", "catppuccin-mocha",
    NULL
};
static const char *theme_labels[] = {
    "System", "Light", "Dark",
    "Solarized Light", "Solarized Dark",
    "Monokai",
    "Gruvbox Light", "Gruvbox Dark",
    "Nord", "Dracula", "Tokyo Night",
    "Catppuccin Latte", "Catppuccin Mocha",
    NULL
};

static int theme_index_of(const char *id) {
    for (int i = 0; theme_ids[i]; i++)
        if (strcmp(theme_ids[i], id) == 0) return i;
    return 0;
}

/* --- Archive formats --- */
static const char *archive_ids[]    = {"zip", "tar.gz", "tar.xz", NULL};
static const char *archive_labels[] = {"ZIP", "tar.gz", "tar.xz", NULL};

/* --- Sort orders --- */
static const char *sort_ids[]    = {"newest", "oldest", "random", NULL};
static const char *sort_labels[] = {"Newest First", "Oldest First", "Random", NULL};

static int sort_index_of(const char *id) {
    for (int i = 0; sort_ids[i]; i++)
        if (strcmp(sort_ids[i], id) == 0) return i;
    return 0;
}

static int archive_index_of(const char *id) {
    for (int i = 0; archive_ids[i]; i++)
        if (strcmp(archive_ids[i], id) == 0) return i;
    return 0;
}

/* --- Actions --- */

static void on_new_note(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    /* Auto-save current note first */
    notes_window_update_index(win);

    /* Clear editor for new note */
    g_free(win->original_content);
    win->original_content = g_strdup("");
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    settings_save(&win->settings);

    /* Refresh sidebar */
    notes_window_refresh_sidebar(win);

    /* Focus editor */
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}

static void on_save(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->dirty) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') { g_free(text); return; }

    if (win->current_file[0] != '\0') {
        /* overwrite existing file */
        FILE *f = fopen(win->current_file, "w");
        if (f) { fputs(text, f); fclose(f); }
        if (win->db) notes_db_index_file(win->db, win->current_file);
    } else {
        /* create new timestamped file */
        g_mkdir_with_parents(win->settings.save_directory, 0755);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char filename[2048];
        snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.txt",
                 win->settings.save_directory,
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        FILE *f = fopen(filename, "w");
        if (f) { fputs(text, f); fclose(f); }
        snprintf(win->current_file, sizeof(win->current_file), "%s", filename);
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", filename);
        if (win->db) notes_db_index_file(win->db, filename);
        settings_save(&win->settings);
    }
    g_free(win->original_content);
    win->original_content = g_strdup(text);
    g_free(text);
    win->dirty = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");

    /* Refresh sidebar to show updated title/tags */
    notes_window_refresh_sidebar(win);
}

static void sync_preview_zoom(NotesWindow *win) {
    if (win->preview_webview) {
        double zoom = (double)win->settings.font_size / 14.0;
        if (zoom < 0.5) zoom = 0.5;
        if (zoom > 4.0) zoom = 4.0;
        webkit_web_view_set_zoom_level(
            WEBKIT_WEB_VIEW(win->preview_webview), zoom);
    }
}

static void on_zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size < 72) {
        win->settings.font_size += 2;
        notes_window_apply_settings(win);
        sync_preview_zoom(win);
        settings_save(&win->settings);
    }
}

static void on_zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size > 6) {
        win->settings.font_size -= 2;
        notes_window_apply_settings(win);
        sync_preview_zoom(win);
        settings_save(&win->settings);
    }
}

static void on_open_file_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            notes_window_load_file(win, path);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_open_folder(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open Note");
    GFile *folder = g_file_new_for_path(win->settings.save_directory);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);

    GtkFileFilter *txt_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(txt_filter, "Text files (*.txt)");
    gtk_file_filter_add_pattern(txt_filter, "*.txt");

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, txt_filter);
    g_list_store_append(filters, all_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(txt_filter);
    g_object_unref(all_filter);
    g_object_unref(filters);

    gtk_file_dialog_open(dialog, GTK_WINDOW(win->window), NULL, on_open_file_cb, win);
}

/* --- Confirmation dialog helper --- */
static void do_pack_notes(NotesWindow *win);
static void do_delete_note(NotesWindow *win);

static void on_confirm_response(AdwAlertDialog *dialog, const char *response, gpointer data) {
    (void)dialog;
    gpointer *ctx = data;
    void (*action_fn)(NotesWindow *) = ctx[0];
    NotesWindow *win = ctx[1];
    if (strcmp(response, "confirm") == 0)
        action_fn(win);
    g_free(ctx);
}

static void confirm_and_run(NotesWindow *win, const char *title, const char *body,
                             void (*action_fn)(NotesWindow *)) {
    if (!win->settings.confirm_dialogs) {
        action_fn(win);
        return;
    }
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(title, body));
    adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "confirm", "Confirm", NULL);
    adw_alert_dialog_set_response_appearance(dialog, "confirm", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    gpointer *ctx = g_new(gpointer, 2);
    ctx[0] = action_fn;
    ctx[1] = win;
    g_signal_connect(dialog, "response", G_CALLBACK(on_confirm_response), ctx);
    adw_alert_dialog_choose(dialog, GTK_WIDGET(win->window), NULL, NULL, NULL);
}

/* Pack notes to archive */
static void on_pack_notes(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    confirm_and_run(win, "Pack Notes", "Archive all notes? This cannot be undone.", do_pack_notes);
}

static void do_pack_notes(NotesWindow *win) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char archive_name[2048];
    const char *fmt = win->settings.archive_format;

    snprintf(archive_name, sizeof(archive_name), "%s/notes_%04d%02d%02d_%02d%02d%02d.%s",
             win->settings.save_directory,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec,
             fmt);

    gboolean pack_ok = FALSE;
    if (strcmp(fmt, "zip") == 0) {
        const char *argv[] = {"zip", "-j", archive_name, NULL};
        GDir *dir = g_dir_open(win->settings.save_directory, 0, NULL);
        if (dir) {
            GPtrArray *args = g_ptr_array_new();
            g_ptr_array_add(args, (gchar *)"zip");
            g_ptr_array_add(args, (gchar *)"-j");
            g_ptr_array_add(args, (gchar *)archive_name);
            const gchar *name;
            while ((name = g_dir_read_name(dir))) {
                if (g_str_has_suffix(name, ".txt")) {
                    char *full = g_build_filename(win->settings.save_directory, name, NULL);
                    g_ptr_array_add(args, full);
                }
            }
            g_dir_close(dir);
            g_ptr_array_add(args, NULL);
            gint exit_status = 0;
            pack_ok = g_spawn_sync(win->settings.save_directory,
                                   (gchar **)args->pdata, NULL,
                                   G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                   NULL, NULL, NULL, NULL, &exit_status, NULL)
                      && exit_status == 0;
            for (guint i = 3; i < args->len - 1; i++)
                g_free(g_ptr_array_index(args, i));
            g_ptr_array_free(args, TRUE);
        }
        (void)argv;
    } else {
        const char *flag = (strcmp(fmt, "tar.gz") == 0) ? "-czf" : "-cJf";
        GDir *dir = g_dir_open(win->settings.save_directory, 0, NULL);
        if (dir) {
            GPtrArray *args = g_ptr_array_new();
            g_ptr_array_add(args, (gchar *)"tar");
            g_ptr_array_add(args, (gchar *)flag);
            g_ptr_array_add(args, (gchar *)archive_name);
            g_ptr_array_add(args, (gchar *)"-C");
            g_ptr_array_add(args, (gchar *)win->settings.save_directory);
            const gchar *name;
            while ((name = g_dir_read_name(dir))) {
                if (g_str_has_suffix(name, ".txt"))
                    g_ptr_array_add(args, g_strdup(name));
            }
            g_dir_close(dir);
            g_ptr_array_add(args, NULL);
            gint exit_status = 0;
            pack_ok = g_spawn_sync(NULL,
                                   (gchar **)args->pdata, NULL,
                                   G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                   NULL, NULL, NULL, NULL, &exit_status, NULL)
                      && exit_status == 0;
            for (guint i = 5; i < args->len - 1; i++)
                g_free(g_ptr_array_index(args, i));
            g_ptr_array_free(args, TRUE);
        }
    }

    if (pack_ok && win->settings.delete_after_pack) {
        GDir *dir = g_dir_open(win->settings.save_directory, 0, NULL);
        if (dir) {
            const gchar *name;
            while ((name = g_dir_read_name(dir))) {
                if (g_str_has_suffix(name, ".txt")) {
                    char *full = g_build_filename(win->settings.save_directory, name, NULL);
                    g_remove(full);
                    g_free(full);
                }
            }
            g_dir_close(dir);
        }
        win->current_file[0] = '\0';
        win->settings.last_file[0] = '\0';
        gtk_text_buffer_set_text(win->buffer, "", -1);
        settings_save(&win->settings);

        /* Re-sync database after pack */
        if (win->db) {
            notes_db_sync(win->db, win->settings.save_directory);
            notes_window_refresh_sidebar(win);
        }
    }
}

/* --- Toggle sidebar --- */
static void on_toggle_sidebar(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    win->settings.show_sidebar = !win->settings.show_sidebar;
    gtk_widget_set_visible(win->sidebar_box, win->settings.show_sidebar);
    settings_save(&win->settings);
}

/* --- Toggle preview --- */
static void on_toggle_preview(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    /* Lazy-init WebKit on first use */
    if (!win->preview_webview)
        notes_window_init_preview(win);

    win->preview_visible = !win->preview_visible;
    gtk_widget_set_visible(win->preview_scrolled, win->preview_visible);
    if (win->preview_visible) {
        int width = gtk_widget_get_width(win->preview_paned);
        if (width > 0)
            gtk_paned_set_position(GTK_PANED(win->preview_paned), width / 2);
        notes_window_update_preview(win);
    }
}

/* --- Focus search --- */
static void on_focus_search(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->settings.show_sidebar) {
        win->settings.show_sidebar = TRUE;
        gtk_widget_set_visible(win->sidebar_box, TRUE);
        settings_save(&win->settings);
    }
    gtk_widget_grab_focus(win->search_entry);
}

/* --- Delete note --- */
static void on_delete_note(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->current_file[0] == '\0') return;
    confirm_and_run(win, "Delete Note", "Delete this note permanently?", do_delete_note);
}

static void do_delete_note(NotesWindow *win) {
    if (win->current_file[0] == '\0') return;

    g_remove(win->current_file);

    if (win->db)
        notes_db_remove_file(win->db, win->current_file);

    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    g_free(win->original_content);
    win->original_content = g_strdup("");
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
    settings_save(&win->settings);

    notes_window_refresh_sidebar(win);
}

/* --- Settings dialog callbacks --- */
static void on_settings_apply(GtkButton *button, gpointer data) {
    NotesWindow *win = data;
    settings_save(&win->settings);
    notes_window_apply_settings(win);
    notes_window_refresh_sidebar(win);
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_settings_cancel(GtkButton *button, gpointer data) {
    (void)data;
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_theme_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (theme_ids[idx])
        strncpy(win->settings.theme, theme_ids[idx], sizeof(win->settings.theme) - 1);
}

static void on_spacing_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    double spacings[] = {1.0, 1.2, 1.5, 2.0};
    if (idx < 4)
        win->settings.line_spacing = spacings[idx];
}

static void on_archive_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (archive_ids[idx])
        strncpy(win->settings.archive_format, archive_ids[idx], sizeof(win->settings.archive_format) - 1);
}

static void on_line_numbers_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.show_line_numbers = gtk_check_button_get_active(btn);
}

static void on_highlight_line_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.highlight_current_line = gtk_check_button_get_active(btn);
}

static void on_wrap_lines_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.wrap_lines = gtk_check_button_get_active(btn);
}

static void on_delete_after_pack_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.delete_after_pack = gtk_check_button_get_active(btn);
}

static void on_confirm_dialogs_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.confirm_dialogs = gtk_check_button_get_active(btn);
}

static void on_sort_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (sort_ids[idx])
        strncpy(win->settings.sort_order, sort_ids[idx], sizeof(win->settings.sort_order) - 1);
}

static void on_sidebar_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            strncpy(win->settings.sidebar_font, family, sizeof(win->settings.sidebar_font) - 1);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.sidebar_font_size = size / PANGO_SCALE;
    }
}

static void on_gui_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            strncpy(win->settings.gui_font, family, sizeof(win->settings.gui_font) - 1);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.gui_font_size = size / PANGO_SCALE;
    }
}

static void on_intensity_changed(GtkRange *range, gpointer data) {
    NotesWindow *win = data;
    win->settings.font_intensity = gtk_range_get_value(range);
}

static void on_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            strncpy(win->settings.font, family, sizeof(win->settings.font) - 1);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.font_size = size / PANGO_SCALE;
    }
}

static void on_dir_selected(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *folder = gtk_file_dialog_select_folder_finish(dialog, result, NULL);
    if (folder) {
        char *path = g_file_get_path(folder);
        if (path) {
            strncpy(win->settings.save_directory, path, sizeof(win->settings.save_directory) - 1);
            g_free(path);
        }
        g_object_unref(folder);
    }
    g_object_unref(dialog);
}

static void on_choose_dir(GtkButton *button, gpointer data) {
    (void)button;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Save Directory");
    GFile *current = g_file_new_for_path(win->settings.save_directory);
    gtk_file_dialog_set_initial_folder(dialog, current);
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(win->window), NULL, on_dir_selected, win);
    g_object_unref(current);
}

static void on_settings(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Settings");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(vbox), grid);

    int row = 0;

    /* Theme */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Theme:"), 0, row, 1, 1);
    GtkWidget *theme_dd = gtk_drop_down_new_from_strings(theme_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dd), (guint)theme_index_of(win->settings.theme));
    g_signal_connect(theme_dd, "notify::selected", G_CALLBACK(on_theme_changed), win);
    gtk_grid_attach(GTK_GRID(grid), theme_dd, 1, row++, 1, 1);

    /* Font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font:"), 0, row, 1, 1);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, win->settings.font);
    pango_font_description_set_size(desc, win->settings.font_size * PANGO_SCALE);
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font_btn = gtk_font_dialog_button_new(font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_btn), desc);
    pango_font_description_free(desc);
    g_signal_connect(font_btn, "notify::font-desc", G_CALLBACK(on_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), font_btn, 1, row++, 1, 1);

    /* Sidebar font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Sidebar Font:"), 0, row, 1, 1);
    PangoFontDescription *sb_desc = pango_font_description_new();
    pango_font_description_set_family(sb_desc, win->settings.sidebar_font);
    pango_font_description_set_size(sb_desc, win->settings.sidebar_font_size * PANGO_SCALE);
    GtkFontDialog *sb_font_dialog = gtk_font_dialog_new();
    GtkWidget *sb_font_btn = gtk_font_dialog_button_new(sb_font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(sb_font_btn), sb_desc);
    pango_font_description_free(sb_desc);
    g_signal_connect(sb_font_btn, "notify::font-desc", G_CALLBACK(on_sidebar_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), sb_font_btn, 1, row++, 1, 1);

    /* GUI font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("GUI Font:"), 0, row, 1, 1);
    PangoFontDescription *gui_desc = pango_font_description_new();
    pango_font_description_set_family(gui_desc, win->settings.gui_font);
    pango_font_description_set_size(gui_desc, win->settings.gui_font_size * PANGO_SCALE);
    GtkFontDialog *gui_font_dialog = gtk_font_dialog_new();
    GtkWidget *gui_font_btn = gtk_font_dialog_button_new(gui_font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(gui_font_btn), gui_desc);
    pango_font_description_free(gui_desc);
    g_signal_connect(gui_font_btn, "notify::font-desc", G_CALLBACK(on_gui_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), gui_font_btn, 1, row++, 1, 1);

    /* Font intensity */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font Intensity:"), 0, row, 1, 1);
    GtkWidget *intensity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.3, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(intensity_scale), win->settings.font_intensity);
    g_signal_connect(intensity_scale, "value-changed", G_CALLBACK(on_intensity_changed), win);
    gtk_grid_attach(GTK_GRID(grid), intensity_scale, 1, row++, 1, 1);

    /* Line spacing */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Spacing:"), 0, row, 1, 1);
    const char *spacings[] = {"1", "1.2", "1.5", "2", NULL};
    GtkWidget *sp_dd = gtk_drop_down_new_from_strings(spacings);
    guint sp_idx = 0;
    if (win->settings.line_spacing >= 1.9) sp_idx = 3;
    else if (win->settings.line_spacing >= 1.4) sp_idx = 2;
    else if (win->settings.line_spacing >= 1.1) sp_idx = 1;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sp_dd), sp_idx);
    g_signal_connect(sp_dd, "notify::selected", G_CALLBACK(on_spacing_changed), win);
    gtk_grid_attach(GTK_GRID(grid), sp_dd, 1, row++, 1, 1);

    /* Line numbers */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Numbers:"), 0, row, 1, 1);
    GtkWidget *ln_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ln_check), win->settings.show_line_numbers);
    g_signal_connect(ln_check, "toggled", G_CALLBACK(on_line_numbers_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), ln_check, 1, row++, 1, 1);

    /* Highlight current line */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Highlight Line:"), 0, row, 1, 1);
    GtkWidget *hl_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hl_check), win->settings.highlight_current_line);
    g_signal_connect(hl_check, "toggled", G_CALLBACK(on_highlight_line_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), hl_check, 1, row++, 1, 1);

    /* Wrap lines */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Wrap Lines:"), 0, row, 1, 1);
    GtkWidget *wrap_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap_check), win->settings.wrap_lines);
    g_signal_connect(wrap_check, "toggled", G_CALLBACK(on_wrap_lines_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), wrap_check, 1, row++, 1, 1);

    /* Archive format */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Archive Format:"), 0, row, 1, 1);
    GtkWidget *arch_dd = gtk_drop_down_new_from_strings(archive_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(arch_dd), (guint)archive_index_of(win->settings.archive_format));
    g_signal_connect(arch_dd, "notify::selected", G_CALLBACK(on_archive_changed), win);
    gtk_grid_attach(GTK_GRID(grid), arch_dd, 1, row++, 1, 1);

    /* Delete after pack */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Delete After Pack:"), 0, row, 1, 1);
    GtkWidget *dap_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dap_check), win->settings.delete_after_pack);
    g_signal_connect(dap_check, "toggled", G_CALLBACK(on_delete_after_pack_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), dap_check, 1, row++, 1, 1);

    /* Confirm dialogs */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Confirm Dialogs:"), 0, row, 1, 1);
    GtkWidget *cd_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cd_check), win->settings.confirm_dialogs);
    g_signal_connect(cd_check, "toggled", G_CALLBACK(on_confirm_dialogs_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), cd_check, 1, row++, 1, 1);

    /* Sort order */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Sort Order:"), 0, row, 1, 1);
    GtkWidget *sort_dd = gtk_drop_down_new_from_strings(sort_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sort_dd), (guint)sort_index_of(win->settings.sort_order));
    g_signal_connect(sort_dd, "notify::selected", G_CALLBACK(on_sort_changed), win);
    gtk_grid_attach(GTK_GRID(grid), sort_dd, 1, row++, 1, 1);

    /* Save directory */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Save Directory:"), 0, row, 1, 1);
    GtkWidget *dir_btn = gtk_button_new_with_label(win->settings.save_directory);
    g_signal_connect(dir_btn, "clicked", G_CALLBACK(on_choose_dir), win);
    gtk_grid_attach(GTK_GRID(grid), dir_btn, 1, row++, 1, 1);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *apply_btn = gtk_button_new_with_label("Apply");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_settings_cancel), win);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_settings_apply), win);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), apply_btn);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    gtk_window_present(GTK_WINDOW(dialog));
}

void actions_setup(NotesWindow *win, GtkApplication *app) {
    static const GActionEntry win_entries[] = {
        {"new-note",       on_new_note,       NULL, NULL, NULL, {0}},
        {"save",           on_save,           NULL, NULL, NULL, {0}},
        {"settings",       on_settings,       NULL, NULL, NULL, {0}},
        {"zoom-in",        on_zoom_in,        NULL, NULL, NULL, {0}},
        {"zoom-out",       on_zoom_out,       NULL, NULL, NULL, {0}},
        {"open-folder",    on_open_folder,    NULL, NULL, NULL, {0}},
        {"pack-notes",     on_pack_notes,     NULL, NULL, NULL, {0}},
        {"toggle-sidebar", on_toggle_sidebar, NULL, NULL, NULL, {0}},
        {"focus-search",   on_focus_search,   NULL, NULL, NULL, {0}},
        {"delete-note",    on_delete_note,    NULL, NULL, NULL, {0}},
        {"toggle-preview", on_toggle_preview, NULL, NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win->window),
                                   win_entries, G_N_ELEMENTS(win_entries), win);

    const char *zoom_in_accels[]  = {"<Control>plus", "<Control>equal", NULL};
    const char *zoom_out_accels[] = {"<Control>minus", NULL};
    const char *quit_accels[]     = {"<Control>q", NULL};
    const char *open_accels[]     = {"<Control>o", NULL};
    const char *save_accels[]     = {"<Control>s", NULL};
    const char *search_accels[]   = {"<Control>f", NULL};
    const char *sidebar_accels[]  = {"F9", NULL};
    const char *new_accels[]      = {"<Control>n", NULL};
    const char *delete_accels[]   = {"<Control>Delete", NULL};
    const char *preview_accels[]  = {"<Control>p", NULL};

    gtk_application_set_accels_for_action(app, "win.zoom-in",        zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out",       zoom_out_accels);
    gtk_application_set_accels_for_action(app, "app.quit",           quit_accels);
    gtk_application_set_accels_for_action(app, "win.open-folder",    open_accels);
    gtk_application_set_accels_for_action(app, "win.save",           save_accels);
    gtk_application_set_accels_for_action(app, "win.focus-search",   search_accels);
    gtk_application_set_accels_for_action(app, "win.toggle-sidebar", sidebar_accels);
    gtk_application_set_accels_for_action(app, "win.new-note",       new_accels);
    gtk_application_set_accels_for_action(app, "win.delete-note",    delete_accels);
    gtk_application_set_accels_for_action(app, "win.toggle-preview", preview_accels);
}
