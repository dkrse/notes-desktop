#ifndef NOTES_WINDOW_H
#define NOTES_WINDOW_H

#include <gtk/gtk.h>
#include "settings.h"

typedef struct {
    GtkApplicationWindow *window;
    GtkTextView          *text_view;
    GtkTextBuffer        *buffer;
    GtkTextView          *line_numbers;
    GtkWidget            *ln_scrolled;
    GtkWidget            *editor_box;
    int                   highlight_line;
    GtkTextTag           *intensity_tag;
    GtkLabel             *status_encoding;
    GtkLabel             *status_cursor;
    NotesSettings         settings;
    GtkCssProvider       *css_provider;
    char                  current_file[2048];
    int                   cached_line_count;
    guint                 line_numbers_idle_id;
    guint                 intensity_idle_id;
} NotesWindow;

NotesWindow *notes_window_new(GtkApplication *app);
void         notes_window_apply_settings(NotesWindow *win);
void         notes_window_load_file(NotesWindow *win, const char *path);

#endif
