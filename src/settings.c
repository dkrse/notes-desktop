#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void ensure_config_dir(void) {
    char path[1024];
    const char *config = g_get_user_config_dir();
    snprintf(path, sizeof(path), "%s/notes-desktop", config);
    g_mkdir_with_parents(path, 0755);
}

char *settings_get_config_path(void) {
    static char path[1024];
    const char *config = g_get_user_config_dir();
    snprintf(path, sizeof(path), "%s/notes-desktop/settings.conf", config);
    return path;
}

void settings_load(NotesSettings *s) {
    /* defaults */
    strncpy(s->font, "Monospace", sizeof(s->font) - 1);
    s->font_size = 14;
    s->font_intensity = 1.0;
    s->line_spacing = 1.0;
    strncpy(s->theme, "system", sizeof(s->theme) - 1);
    snprintf(s->save_directory, sizeof(s->save_directory), "%s/Notes", g_get_home_dir());
    strncpy(s->archive_format, "zip", sizeof(s->archive_format) - 1);
    s->show_line_numbers = FALSE;
    s->highlight_current_line = TRUE;
    s->wrap_lines = TRUE;
    s->delete_after_pack = FALSE;
    s->show_sidebar = TRUE;
    s->window_width = 700;
    s->window_height = 500;
    s->last_file[0] = '\0';

    char *path = settings_get_config_path();
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = g_strstrip(line);
        char *val = g_strstrip(eq + 1);

        if (strcmp(key, "font") == 0)
            strncpy(s->font, val, sizeof(s->font) - 1);
        else if (strcmp(key, "font_size") == 0)
            s->font_size = atoi(val);
        else if (strcmp(key, "font_intensity") == 0)
            s->font_intensity = CLAMP(atof(val), 0.3, 1.0);
        else if (strcmp(key, "line_spacing") == 0)
            s->line_spacing = atof(val);
        else if (strcmp(key, "theme") == 0)
            strncpy(s->theme, val, sizeof(s->theme) - 1);
        else if (strcmp(key, "save_directory") == 0)
            strncpy(s->save_directory, val, sizeof(s->save_directory) - 1);
        else if (strcmp(key, "archive_format") == 0)
            strncpy(s->archive_format, val, sizeof(s->archive_format) - 1);
        else if (strcmp(key, "show_line_numbers") == 0)
            s->show_line_numbers = (strcmp(val, "1") == 0);
        else if (strcmp(key, "highlight_current_line") == 0)
            s->highlight_current_line = (strcmp(val, "1") == 0);
        else if (strcmp(key, "wrap_lines") == 0)
            s->wrap_lines = (strcmp(val, "1") == 0);
        else if (strcmp(key, "delete_after_pack") == 0)
            s->delete_after_pack = (strcmp(val, "1") == 0);
        else if (strcmp(key, "show_sidebar") == 0)
            s->show_sidebar = (strcmp(val, "1") == 0);
        else if (strcmp(key, "window_width") == 0)
            s->window_width = atoi(val);
        else if (strcmp(key, "window_height") == 0)
            s->window_height = atoi(val);
        else if (strcmp(key, "last_file") == 0)
            strncpy(s->last_file, val, sizeof(s->last_file) - 1);
    }
    fclose(f);
}

void settings_save(const NotesSettings *s) {
    ensure_config_dir();
    char *path = settings_get_config_path();
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "font=%s\n", s->font);
    fprintf(f, "font_size=%d\n", s->font_size);
    fprintf(f, "font_intensity=%.2f\n", s->font_intensity);
    fprintf(f, "line_spacing=%.1f\n", s->line_spacing);
    fprintf(f, "theme=%s\n", s->theme);
    fprintf(f, "save_directory=%s\n", s->save_directory);
    fprintf(f, "archive_format=%s\n", s->archive_format);
    fprintf(f, "show_line_numbers=%d\n", s->show_line_numbers);
    fprintf(f, "highlight_current_line=%d\n", s->highlight_current_line);
    fprintf(f, "wrap_lines=%d\n", s->wrap_lines);
    fprintf(f, "delete_after_pack=%d\n", s->delete_after_pack);
    fprintf(f, "show_sidebar=%d\n", s->show_sidebar);
    fprintf(f, "window_width=%d\n", s->window_width);
    fprintf(f, "window_height=%d\n", s->window_height);
    fprintf(f, "last_file=%s\n", s->last_file);
    fclose(f);
}
