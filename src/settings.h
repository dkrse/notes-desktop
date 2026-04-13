#ifndef NOTES_SETTINGS_H
#define NOTES_SETTINGS_H

#include <gtk/gtk.h>

typedef struct {
    char font[256];
    double line_spacing;
    int font_size;
    char sidebar_font[256];
    int sidebar_font_size;
    char gui_font[256];
    int gui_font_size;
    double font_intensity;  /* 0.5 .. 1.0 */
    char theme[64];
    char save_directory[1024];
    char archive_format[16];   /* "zip", "tar.gz", "tar.xz" */
    gboolean show_line_numbers;
    gboolean highlight_current_line;
    gboolean wrap_lines;
    gboolean delete_after_pack;
    gboolean confirm_dialogs;
    char sort_order[16];       /* "newest", "oldest", "random" */
    gboolean show_sidebar;
    int window_width;
    int window_height;
    char last_file[2048];
} NotesSettings;

void     settings_load(NotesSettings *s);
void     settings_save(const NotesSettings *s);
char    *settings_get_config_path(void);

#endif
