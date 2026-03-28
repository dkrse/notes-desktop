#include "actions.h"
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

static int archive_index_of(const char *id) {
    for (int i = 0; archive_ids[i]; i++)
        if (strcmp(archive_ids[i], id) == 0) return i;
    return 0;
}

/* --- Save buffer with timestamp --- */
static char *save_buffer_to_file(NotesWindow *win) {
    g_mkdir_with_parents(win->settings.save_directory, 0755);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    static char filename[2048];
    snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.txt",
             win->settings.save_directory,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

    if (text && text[0] != '\0') {
        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(text, f);
            fclose(f);
        }
    }
    g_free(text);
    return filename;
}

/* --- Actions --- */
static void on_clear(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    char *saved = save_buffer_to_file(win);
    snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", saved);
    snprintf(win->current_file, sizeof(win->current_file), "%s", saved);
    gtk_text_buffer_set_text(win->buffer, "", -1);
    /* after clear, last_file becomes empty (blank page) */
    win->settings.last_file[0] = '\0';
    win->current_file[0] = '\0';
    settings_save(&win->settings);
}

static void on_save(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') { g_free(text); return; }

    if (win->current_file[0] != '\0') {
        /* overwrite existing file */
        FILE *f = fopen(win->current_file, "w");
        if (f) { fputs(text, f); fclose(f); }
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
        settings_save(&win->settings);
    }
    g_free(text);
}

static void on_zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size < 72) {
        win->settings.font_size += 2;
        notes_window_apply_settings(win);
        settings_save(&win->settings);
    }
}

static void on_zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size > 6) {
        win->settings.font_size -= 2;
        notes_window_apply_settings(win);
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

/* Pack notes to archive */
static void on_pack_notes(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char archive_name[2048];
    const char *fmt = win->settings.archive_format;

    snprintf(archive_name, sizeof(archive_name), "%s/notes_%04d%02d%02d_%02d%02d%02d.%s",
             win->settings.save_directory,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec,
             fmt);

    char cmd[4096];
    if (strcmp(fmt, "zip") == 0) {
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && zip -j \"%s\" *.txt 2>/dev/null",
                 win->settings.save_directory, archive_name);
    } else if (strcmp(fmt, "tar.gz") == 0) {
        snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" --include='*.txt' . 2>/dev/null",
                 archive_name, win->settings.save_directory);
    } else {
        snprintf(cmd, sizeof(cmd), "tar -cJf \"%s\" -C \"%s\" --include='*.txt' . 2>/dev/null",
                 archive_name, win->settings.save_directory);
    }

    int ret = system(cmd);
    if (ret == 0 && win->settings.delete_after_pack) {
        char rm_cmd[4096];
        snprintf(rm_cmd, sizeof(rm_cmd), "find \"%s\" -maxdepth 1 -name '*.txt' -type f -delete",
                 win->settings.save_directory);
        system(rm_cmd);
        /* clear current state since files are gone */
        win->current_file[0] = '\0';
        win->settings.last_file[0] = '\0';
        gtk_text_buffer_set_text(win->buffer, "", -1);
        settings_save(&win->settings);
    }
}

/* --- Settings dialog callbacks --- */
static void on_settings_apply(GtkButton *button, gpointer data) {
    NotesWindow *win = data;
    settings_save(&win->settings);
    notes_window_apply_settings(win);
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
        {"clear",       on_clear,       NULL, NULL, NULL, {0}},
        {"save",        on_save,        NULL, NULL, NULL, {0}},
        {"settings",    on_settings,    NULL, NULL, NULL, {0}},
        {"zoom-in",     on_zoom_in,     NULL, NULL, NULL, {0}},
        {"zoom-out",    on_zoom_out,    NULL, NULL, NULL, {0}},
        {"open-folder", on_open_folder, NULL, NULL, NULL, {0}},
        {"pack-notes",  on_pack_notes,  NULL, NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win->window),
                                   win_entries, G_N_ELEMENTS(win_entries), win);

    const char *zoom_in_accels[]  = {"<Control>plus", "<Control>equal", NULL};
    const char *zoom_out_accels[] = {"<Control>minus", NULL};
    const char *quit_accels[]     = {"<Control>q", NULL};
    const char *open_accels[]     = {"<Control>o", NULL};
    const char *save_accels[]     = {"<Control>s", NULL};

    gtk_application_set_accels_for_action(app, "win.zoom-in",     zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out",    zoom_out_accels);
    gtk_application_set_accels_for_action(app, "app.quit",        quit_accels);
    gtk_application_set_accels_for_action(app, "win.open-folder", open_accels);
    gtk_application_set_accels_for_action(app, "win.save",        save_accels);
}
