#ifndef NOTES_WINDOW_H
#define NOTES_WINDOW_H

#include <gtk/gtk.h>
#include "settings.h"
#include "database.h"

typedef struct {
    GtkApplicationWindow *window;
    GtkTextView          *text_view;
    GtkTextBuffer        *buffer;
    GtkDrawingArea       *line_numbers;
    GtkWidget            *ln_scrolled;
    GtkWidget            *editor_box;
    int                   highlight_line;
    GdkRGBA               highlight_rgba;
    GtkTextTag           *intensity_tag;
    GtkLabel             *status_encoding;
    GtkLabel             *status_cursor;
    NotesSettings         settings;
    GtkCssProvider       *css_provider;
    char                  current_file[2048];
    int                   cached_line_count;
    guint                 line_numbers_idle_id;
    guint                 intensity_idle_id;
    guint                 scroll_idle_id;

    /* Sidebar */
    NotesDatabase        *db;
    GtkWidget            *sidebar_box;
    GtkWidget            *search_entry;
    GtkWidget            *tag_flow;
    GtkWidget            *note_list;
    GtkWidget            *paned;
    char                 *active_tag_filter;
    guint                 search_timeout_id;
    gboolean              dirty;
    char                 *original_content;

    /* Preview pane (WebKit) */
    GtkWidget            *preview_paned;
    GtkWidget            *preview_webview;
    GtkWidget            *preview_scrolled;
    gboolean              preview_visible;
    gboolean              preview_ready;
    char                 *preview_html;
    guint                 preview_timeout_id;
} NotesWindow;

NotesWindow *notes_window_new(GtkApplication *app);
void         notes_window_apply_settings(NotesWindow *win);
void         notes_window_load_file(NotesWindow *win, const char *path);
void         notes_window_refresh_sidebar(NotesWindow *win);
void         notes_window_update_index(NotesWindow *win);
void         notes_window_update_preview(NotesWindow *win);
void         notes_window_init_preview(NotesWindow *win);

#endif
