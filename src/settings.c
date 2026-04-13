#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>

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
    strncpy(s->sidebar_font, "Sans", sizeof(s->sidebar_font) - 1);
    s->sidebar_font_size = 10;
    strncpy(s->gui_font, "Sans", sizeof(s->gui_font) - 1);
    s->gui_font_size = 10;
    s->font_intensity = 1.0;
    s->line_spacing = 1.0;
    strncpy(s->theme, "system", sizeof(s->theme) - 1);
    snprintf(s->save_directory, sizeof(s->save_directory), "%s/Notes", g_get_home_dir());
    strncpy(s->archive_format, "zip", sizeof(s->archive_format) - 1);
    s->show_line_numbers = FALSE;
    s->highlight_current_line = TRUE;
    s->wrap_lines = TRUE;
    s->delete_after_pack = FALSE;
    s->confirm_dialogs = TRUE;
    strncpy(s->sort_order, "newest", sizeof(s->sort_order) - 1);
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

        #define SAFE_COPY(dst, src) do { \
            strncpy((dst), (src), sizeof(dst) - 1); \
            (dst)[sizeof(dst) - 1] = '\0'; \
        } while (0)

        /* Replace comma with dot for locale-safe float parsing */
        for (char *c = val; *c; c++) { if (*c == ',') *c = '.'; }

        if (strcmp(key, "font") == 0) {
            if (val[0]) SAFE_COPY(s->font, val);
        } else if (strcmp(key, "font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->font_size = v;
        } else if (strcmp(key, "sidebar_font") == 0) {
            if (val[0]) SAFE_COPY(s->sidebar_font, val);
        } else if (strcmp(key, "sidebar_font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->sidebar_font_size = v;
        } else if (strcmp(key, "gui_font") == 0) {
            if (val[0]) SAFE_COPY(s->gui_font, val);
        } else if (strcmp(key, "gui_font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->gui_font_size = v;
        } else if (strcmp(key, "font_intensity") == 0)
            s->font_intensity = CLAMP(g_ascii_strtod(val, NULL), 0.3, 1.0);
        else if (strcmp(key, "line_spacing") == 0) {
            double v = g_ascii_strtod(val, NULL); if (v >= 0.5 && v <= 5.0) s->line_spacing = v;
        }
        else if (strcmp(key, "theme") == 0) {
            if (val[0]) SAFE_COPY(s->theme, val);
        }
        else if (strcmp(key, "save_directory") == 0)
            SAFE_COPY(s->save_directory, val);
        else if (strcmp(key, "archive_format") == 0)
            SAFE_COPY(s->archive_format, val);
        else if (strcmp(key, "show_line_numbers") == 0)
            s->show_line_numbers = (strcmp(val, "1") == 0);
        else if (strcmp(key, "highlight_current_line") == 0)
            s->highlight_current_line = (strcmp(val, "1") == 0);
        else if (strcmp(key, "wrap_lines") == 0)
            s->wrap_lines = (strcmp(val, "1") == 0);
        else if (strcmp(key, "delete_after_pack") == 0)
            s->delete_after_pack = (strcmp(val, "1") == 0);
        else if (strcmp(key, "confirm_dialogs") == 0)
            s->confirm_dialogs = (strcmp(val, "1") == 0);
        else if (strcmp(key, "sort_order") == 0) {
            if (val[0]) SAFE_COPY(s->sort_order, val);
        }
        else if (strcmp(key, "show_sidebar") == 0)
            s->show_sidebar = (strcmp(val, "1") == 0);
        else if (strcmp(key, "window_width") == 0) {
            int v = atoi(val); if (v >= 200 && v <= 8192) s->window_width = v;
        } else if (strcmp(key, "window_height") == 0) {
            int v = atoi(val); if (v >= 200 && v <= 8192) s->window_height = v;
        }
        else if (strcmp(key, "last_file") == 0)
            SAFE_COPY(s->last_file, val);

        #undef SAFE_COPY
    }
    fclose(f);
}

void settings_save(const NotesSettings *s) {
    ensure_config_dir();
    char *path = settings_get_config_path();
    int fd = g_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return; }

    fprintf(f, "font=%s\n", s->font);
    fprintf(f, "font_size=%d\n", s->font_size);
    fprintf(f, "sidebar_font=%s\n", s->sidebar_font);
    fprintf(f, "sidebar_font_size=%d\n", s->sidebar_font_size);
    fprintf(f, "gui_font=%s\n", s->gui_font);
    fprintf(f, "gui_font_size=%d\n", s->gui_font_size);
    /* Use g_ascii_dtostr for locale-safe float output */
    char buf_intensity[32], buf_spacing[32];
    g_ascii_formatd(buf_intensity, sizeof(buf_intensity), "%.2f", s->font_intensity);
    g_ascii_formatd(buf_spacing, sizeof(buf_spacing), "%.1f", s->line_spacing);
    fprintf(f, "font_intensity=%s\n", buf_intensity);
    fprintf(f, "line_spacing=%s\n", buf_spacing);
    fprintf(f, "theme=%s\n", s->theme);
    fprintf(f, "save_directory=%s\n", s->save_directory);
    fprintf(f, "archive_format=%s\n", s->archive_format);
    fprintf(f, "show_line_numbers=%d\n", s->show_line_numbers);
    fprintf(f, "highlight_current_line=%d\n", s->highlight_current_line);
    fprintf(f, "wrap_lines=%d\n", s->wrap_lines);
    fprintf(f, "delete_after_pack=%d\n", s->delete_after_pack);
    fprintf(f, "confirm_dialogs=%d\n", s->confirm_dialogs);
    fprintf(f, "sort_order=%s\n", s->sort_order);
    fprintf(f, "show_sidebar=%d\n", s->show_sidebar);
    fprintf(f, "window_width=%d\n", s->window_width);
    fprintf(f, "window_height=%d\n", s->window_height);
    fprintf(f, "last_file=%s\n", s->last_file);
    fclose(f);
}
