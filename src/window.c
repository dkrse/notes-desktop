#define _GNU_SOURCE
#include <adwaita.h>
#include "window.h"
#include "actions.h"
#include "highlight.h"
#include <webkit/webkit.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Custom CSS themes */
typedef struct {
    const char *name;
    const char *fg;   /* text foreground color */
    const char *bg;   /* text background color */
    const char *css;
} ThemeDef;

static const ThemeDef custom_themes[] = {
    {"solarized-light", "#657b83", "#fdf6e3",
     "textview text { background-color: #fdf6e3; color: #657b83; }"
     "textview { background-color: #fdf6e3; }"},
    {"solarized-dark", "#839496", "#002b36",
     "textview text { background-color: #002b36; color: #839496; }"
     "textview { background-color: #002b36; }"},
    {"monokai", "#f8f8f2", "#272822",
     "textview text { background-color: #272822; color: #f8f8f2; }"
     "textview { background-color: #272822; }"},
    {"gruvbox-light", "#3c3836", "#fbf1c7",
     "textview text { background-color: #fbf1c7; color: #3c3836; }"
     "textview { background-color: #fbf1c7; }"},
    {"gruvbox-dark", "#ebdbb2", "#282828",
     "textview text { background-color: #282828; color: #ebdbb2; }"
     "textview { background-color: #282828; }"},
    {"nord", "#d8dee9", "#2e3440",
     "textview text { background-color: #2e3440; color: #d8dee9; }"
     "textview { background-color: #2e3440; }"},
    {"dracula", "#f8f8f2", "#282a36",
     "textview text { background-color: #282a36; color: #f8f8f2; }"
     "textview { background-color: #282a36; }"},
    {"tokyo-night", "#a9b1d6", "#1a1b26",
     "textview text { background-color: #1a1b26; color: #a9b1d6; }"
     "textview { background-color: #1a1b26; }"},
    {"catppuccin-latte", "#4c4f69", "#eff1f5",
     "textview text { background-color: #eff1f5; color: #4c4f69; }"
     "textview { background-color: #eff1f5; }"},
    {"catppuccin-mocha", "#cdd6f4", "#1e1e2e",
     "textview text { background-color: #1e1e2e; color: #cdd6f4; }"
     "textview { background-color: #1e1e2e; }"},
    {NULL, NULL, NULL, NULL}
};

static gboolean is_dark_theme(const char *theme);

/*
 * Highlight current line: overlay approach (like VS Code, Sublime).
 * Draw a semi-transparent rectangle AFTER the parent renders everything.
 * Dark themes get a white overlay, light themes get a black overlay.
 */

#define NOTES_TYPE_TEXT_VIEW (notes_text_view_get_type())
G_DECLARE_FINAL_TYPE(NotesTextView, notes_text_view, NOTES, TEXT_VIEW, GtkTextView)

struct _NotesTextView {
    GtkTextView parent;
    NotesWindow *win;
};

G_DEFINE_TYPE(NotesTextView, notes_text_view, GTK_TYPE_TEXT_VIEW)

/* highlight_rgba is now per-window in NotesWindow struct */

static void notes_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    NotesTextView *self = NOTES_TEXT_VIEW(widget);
    NotesWindow *win = self->win;

    /* 1. Let GTK draw everything first (background, text, cursor) */
    GTK_WIDGET_CLASS(notes_text_view_parent_class)->snapshot(widget, snapshot);

    /* 2. Draw highlight overlay on top */
    if (win && win->settings.highlight_current_line) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_line(win->buffer, &iter, win->highlight_line);

        /* get_line_yrange returns the full height of the buffer line,
           including all wrapped display lines */
        int buf_y, line_height;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(widget), &iter,
                                      &buf_y, &line_height);

        int wx, wy;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(widget),
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &wx, &wy);

        int view_width = gtk_widget_get_width(widget);
        int h = line_height > 0 ? line_height : win->settings.font_size + 4;
        int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
        if (extra < 0) extra = 0;

        graphene_rect_t area = GRAPHENE_RECT_INIT(0, wy - extra, view_width, h + extra * 2);
        gtk_snapshot_append_color(snapshot, &win->highlight_rgba, &area);
    }
}

static void notes_text_view_class_init(NotesTextViewClass *klass) {
    GTK_WIDGET_CLASS(klass)->snapshot = notes_text_view_snapshot;
}

static void notes_text_view_init(NotesTextView *self) {
    (void)self;
}

static void update_line_highlights(NotesWindow *win) {
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cursor, mark);
    win->highlight_line = gtk_text_iter_get_line(&cursor);
    gtk_widget_queue_draw(GTK_WIDGET(win->text_view));
}

static void apply_highlight_color(NotesWindow *win) {
    gboolean dark = is_dark_theme(win->settings.theme);
    /* dark themes: white overlay; light themes: black overlay */
    if (dark) {
        win->highlight_rgba = (GdkRGBA){1.0, 1.0, 1.0, 0.06};
    } else {
        win->highlight_rgba = (GdkRGBA){0.0, 0.0, 0.0, 0.06};
    }
}

static void apply_css(NotesWindow *win) {
    char css[8192];

    /* find custom theme if any */
    const ThemeDef *td = NULL;
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            td = &custom_themes[i];
            break;
        }
    }

    if (td) {
        const char *bg = td->bg;
        const char *fg = td->fg;

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s; color: %s; }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".titlebar, headerbar {"
            "  background: %s; color: %s; box-shadow: none; }"
            "headerbar button, headerbar menubutton button,"
            "headerbar menubutton { color: %s; background: transparent; }"
            "headerbar button:hover, headerbar menubutton button:hover {"
            "  background: alpha(%s, 0.1); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px;"
            "  color: alpha(%s, 0.6); background-color: %s; }"
            "window, window.background { background-color: %s; }"
            "popover, popover.menu {"
            "  background: transparent; box-shadow: none; border: none; }"
            "popover > contents, popover.menu > contents {"
            "  background-color: %s; color: %s;"
            "  border-radius: 12px; border: none; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
            "popover > arrow, popover.menu > arrow { background: transparent; border: none; }"
            "popover modelbutton { color: %s; }"
            "popover modelbutton:hover { background-color: alpha(%s, 0.15); }"
            "windowcontrols button { color: %s; }"
            /* Sidebar styling for custom themes */
            ".sidebar { background-color: %s; font-family: %s; font-size: %dpt; }"
            ".sidebar entry { background-color: alpha(%s, 0.08); color: %s;"
            "  border: 1px solid alpha(%s, 0.15); border-radius: 8px; }"
            ".sidebar entry:focus-within { border-color: alpha(%s, 0.3); }"
            ".sidebar .note-row { padding: 8px 12px; border-bottom: 1px solid alpha(%s, 0.08); }"
            ".sidebar .note-row:hover { background-color: alpha(%s, 0.06); }"
            ".sidebar .note-title { color: %s; font-weight: bold; }"
            ".sidebar .note-date { color: alpha(%s, 0.5); font-size: 10pt; }"
            ".sidebar .note-tags { color: alpha(%s, 0.6); font-size: 9pt; }"
            ".sidebar .tag-chip { background: alpha(%s, 0.1); color: alpha(%s, 0.7);"
            "  border-radius: 12px; padding: 2px 8px; font-size: 9pt;"
            "  border: 1px solid alpha(%s, 0.15); }"
            ".sidebar .tag-chip:hover { background: alpha(%s, 0.2); }"
            ".sidebar .tag-chip.active { background: alpha(%s, 0.25);"
            "  border-color: alpha(%s, 0.3); }",
            win->settings.font, win->settings.font_size, bg,
            bg, fg,
            bg, fg,
            bg, fg,
            fg,
            fg,
            fg, bg,
            bg,
            bg, fg,
            fg,
            fg,
            fg,
            /* sidebar */
            bg, win->settings.sidebar_font, win->settings.sidebar_font_size,
            fg, fg,
            fg,
            fg,
            fg,
            fg,
            fg,
            fg,
            fg,
            fg, fg,
            fg,
            fg,
            fg,
            fg);
    } else {
        const char *bg, *fg;
        if (is_dark_theme(win->settings.theme)) {
            bg = "#1e1e1e"; fg = "#d4d4d4";
        } else {
            bg = "#ffffff"; fg = "#1e1e1e";
        }

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s; color: %s; }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px; opacity: 0.7; }"
            /* Sidebar styling for system themes */
            ".sidebar { font-family: %s; font-size: %dpt; }"
            ".sidebar .note-row { padding: 8px 12px; }"
            ".sidebar .note-title { font-weight: bold; }"
            ".sidebar .note-date { opacity: 0.5; font-size: 10pt; }"
            ".sidebar .note-tags { opacity: 0.6; font-size: 9pt; }"
            ".sidebar .tag-chip { border-radius: 12px; padding: 2px 8px;"
            "  font-size: 9pt; opacity: 0.8; }"
            ".sidebar .tag-chip.active { opacity: 1.0; font-weight: bold; }",
            win->settings.font, win->settings.font_size, bg,
            bg, fg,
            bg, fg,
            win->settings.sidebar_font, win->settings.sidebar_font_size);
    }

    /* Append GUI font rule — exclude sidebar which has its own font */
    char gui_css[512];
    snprintf(gui_css, sizeof(gui_css),
        "headerbar, headerbar button, headerbar label,"
        "popover, popover.menu, popover label, popover button,"
        ".statusbar, .statusbar label {"
        "  font-family: %s; font-size: %dpt; }",
        win->settings.gui_font, win->settings.gui_font_size);
    strncat(css, gui_css, sizeof(css) - strlen(css) - 1);

    gtk_css_provider_load_from_string(win->css_provider, css);
}

static gboolean is_dark_theme(const char *theme) {
    return strcmp(theme, "dark") == 0 ||
           strcmp(theme, "solarized-dark") == 0 ||
           strcmp(theme, "monokai") == 0 ||
           strcmp(theme, "gruvbox-dark") == 0 ||
           strcmp(theme, "nord") == 0 ||
           strcmp(theme, "dracula") == 0 ||
           strcmp(theme, "tokyo-night") == 0 ||
           strcmp(theme, "catppuccin-mocha") == 0;
}

static void apply_theme(NotesWindow *win) {
    AdwStyleManager *sm = adw_style_manager_get_default();
    gboolean dark = is_dark_theme(win->settings.theme);

    /* For custom themes, determine dark from bg luminance */
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            GdkRGBA c;
            gdk_rgba_parse(&c, custom_themes[i].bg);
            dark = (0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) < 0.5;
            break;
        }
    }

    if (strcmp(win->settings.theme, "system") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
    else if (dark)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
}

/* Apply theme colors to preview webview */
static void apply_preview_theme(NotesWindow *win) {
    if (!win->preview_visible || !win->preview_webview || !win->preview_ready)
        return;

    const char *fg = NULL, *bg = NULL;
    gboolean dark = is_dark_theme(win->settings.theme);

    /* Find custom theme colors */
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            fg = custom_themes[i].fg;
            bg = custom_themes[i].bg;
            GdkRGBA c;
            gdk_rgba_parse(&c, bg);
            dark = (0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) < 0.5;
            break;
        }
    }

    /* Fallback for built-in themes */
    if (!fg) {
        if (dark) { fg = "#d4d4d4"; bg = "#1e1e1e"; }
        else { fg = "#1f2328"; bg = "#ffffff"; }
    }

    char js[512];
    snprintf(js, sizeof(js),
        "if(typeof applyTheme==='function'){applyTheme('%s','%s',%s);}"
        "else{document.documentElement.style.setProperty('--fg','%s');"
        "document.documentElement.style.setProperty('--bg','%s');}",
        fg, bg, dark ? "true" : "false", fg, bg);
    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(win->preview_webview),
        js, -1, NULL, NULL, NULL, NULL, NULL);
}

/* Line numbers */
static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win);
void notes_window_update_preview(NotesWindow *win);
static gboolean preview_timeout_cb(gpointer data);
static void notes_window_update_preview_now(NotesWindow *win);

static void update_cursor_position(NotesWindow *win) {
    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_buffer_get_iter_at_mark(win->buffer, &iter, mark);
    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;
    char buf[64];
    snprintf(buf, sizeof(buf), "Ln %d, Col %d", line, col);
    gtk_label_set_text(win->status_cursor, buf);
}

static void apply_font_intensity(NotesWindow *win);

static gboolean scroll_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->scroll_idle_id = 0;
    GtkTextMark *insert = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_view_scroll_to_mark(win->text_view, insert, 0.05, FALSE, 0, 0);
    return G_SOURCE_REMOVE;
}

static gboolean intensity_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->intensity_idle_id = 0;
    if (win->settings.font_intensity < 0.99)
        apply_font_intensity(win);
    return G_SOURCE_REMOVE;
}

static void update_dirty_state(NotesWindow *win) {
    if (!win->dirty) {
        win->dirty = TRUE;
        gtk_window_set_title(GTK_WINDOW(win->window), "Notes *");
    }
}

static gboolean highlight_timeout_cb(gpointer data) {
    NotesWindow *win = data;
    win->highlight_idle_id = 0;
    highlight_apply(win->buffer, LANG_MARKDOWN);
    /* Ensure intensity tag stays at lowest priority so highlight colors win */
    gtk_text_tag_set_priority(win->intensity_tag, 0);
    return G_SOURCE_REMOVE;
}

static void schedule_highlight(NotesWindow *win) {
    if (win->highlight_idle_id)
        g_source_remove(win->highlight_idle_id);
    win->highlight_idle_id = g_timeout_add(300, highlight_timeout_cb, win);
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    NotesWindow *win = data;
    update_dirty_state(win);
    if (win->settings.show_line_numbers)
        update_line_numbers(buffer, win);
    update_cursor_position(win);
    update_line_highlights(win);
    if (win->settings.font_intensity < 0.99 && win->intensity_idle_id == 0)
        win->intensity_idle_id = g_idle_add(intensity_idle_cb, win);
    if (win->scroll_idle_id == 0)
        win->scroll_idle_id = g_idle_add(scroll_idle_cb, win);
    /* Update markdown preview (debounced) */
    notes_window_update_preview(win);
    /* Update syntax highlighting (debounced) */
    schedule_highlight(win);
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer data) {
    (void)buffer; (void)pspec;
    NotesWindow *win = data;
    update_cursor_position(win);
    update_line_highlights(win);
}

static void draw_line_numbers(GtkDrawingArea *area, cairo_t *cr,
                              int width, int height, gpointer data) {
    (void)area; (void)height;
    NotesWindow *win = data;
    if (!win->settings.show_line_numbers) return;

    /* Get font description */
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    pango_layout_set_width(layout, (width - 12) * PANGO_SCALE);

    /* Get line number color from theme */
    GdkRGBA color;
    const char *theme_fg = NULL;
    for (int t = 0; custom_themes[t].name; t++) {
        if (strcmp(win->settings.theme, custom_themes[t].name) == 0) {
            theme_fg = custom_themes[t].fg;
            break;
        }
    }
    if (theme_fg) {
        gdk_rgba_parse(&color, theme_fg);
    } else if (is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){0.85, 0.85, 0.85, 1.0};
    } else {
        color = (GdkRGBA){0.12, 0.12, 0.12, 1.0};
    }
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.3);

    /* Get visible area in buffer coordinates */
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(win->text_view, &visible);

    /* Get first visible line iter */
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(win->text_view, &iter,
                                       visible.x, visible.y);
    gtk_text_iter_set_line_offset(&iter, 0);

    /* Walk visible lines */
    while (TRUE) {
        int buf_y, line_height;
        gtk_text_view_get_line_yrange(win->text_view, &iter, &buf_y, &line_height);

        /* Stop if past visible area */
        if (buf_y > visible.y + visible.height) break;

        /* Convert buffer coords to widget (window) coords */
        int win_x, win_y;
        gtk_text_view_buffer_to_window_coords(win->text_view,
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &win_x, &win_y);

        char num[16];
        snprintf(num, sizeof(num), "%d", gtk_text_iter_get_line(&iter) + 1);
        pango_layout_set_text(layout, num, -1);
        cairo_move_to(cr, 4, win_y);
        pango_cairo_show_layout(cr, layout);

        /* Move to next buffer line (skip wrapped display lines) */
        if (!gtk_text_iter_forward_line(&iter)) break;
    }

    g_object_unref(layout);
    pango_font_description_free(fd);
}

static gboolean line_numbers_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->line_numbers_idle_id = 0;
    if (!win->settings.show_line_numbers) return G_SOURCE_REMOVE;

    int lines = gtk_text_buffer_get_line_count(win->buffer);

    /* measure actual width needed using Pango */
    int digits = 1, n = lines;
    while (n >= 10) { digits++; n /= 10; }
    if (digits < 2) digits = 2;

    char sample[16];
    memset(sample, '9', (size_t)digits);
    sample[digits] = '\0';

    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(win->line_numbers), sample);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    (void)ph;
    pango_font_description_free(fd);
    g_object_unref(layout);

    int width = pw + 12;
    gtk_widget_set_size_request(GTK_WIDGET(win->line_numbers), width, -1);

    /* Trigger redraw */
    gtk_widget_queue_draw(GTK_WIDGET(win->line_numbers));
    return G_SOURCE_REMOVE;
}

static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win) {
    (void)buffer;
    if (!win->settings.show_line_numbers) return;
    /* Schedule redraw in idle so text view layout is up to date */
    if (win->line_numbers_idle_id == 0)
        win->line_numbers_idle_id = g_idle_add(line_numbers_idle_cb, win);
}

static void apply_font_intensity(NotesWindow *win) {
    double alpha = win->settings.font_intensity;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);

    if (alpha >= 0.99) {
        gtk_text_buffer_remove_tag(win->buffer, win->intensity_tag, &start, &end);
        return;
    }

    /* get text foreground color from theme */
    const char *fg = NULL;
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            fg = custom_themes[i].fg;
            break;
        }
    }

    GdkRGBA color;
    if (fg) {
        gdk_rgba_parse(&color, fg);
    } else if (is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
    } else {
        color = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
    }
    color.alpha = alpha;

    g_object_set(win->intensity_tag, "foreground-rgba", &color, NULL);
    gtk_text_buffer_apply_tag(win->buffer, win->intensity_tag, &start, &end);
    /* Keep intensity at lowest priority so highlight colors take precedence */
    gtk_text_tag_set_priority(win->intensity_tag, 0);
}

void notes_window_apply_settings(NotesWindow *win) {
    apply_theme(win);
    apply_css(win);

    int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
    if (extra < 0) extra = 0;
    gtk_text_view_set_pixels_above_lines(win->text_view, extra);
    gtk_text_view_set_pixels_below_lines(win->text_view, extra);
    /* Line numbers are drawn via GtkDrawingArea using text view positions,
       so they automatically reflect the main text view's line spacing */

    gtk_widget_set_visible(win->ln_scrolled, win->settings.show_line_numbers);
    win->cached_line_count = 0; /* force rebuild */
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);

    gtk_text_view_set_wrap_mode(win->text_view,
        win->settings.wrap_lines ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);

    apply_highlight_color(win);
    update_line_highlights(win);
    apply_font_intensity(win);
    apply_preview_theme(win);

    /* Sidebar visibility */
    if (win->sidebar_box)
        gtk_widget_set_visible(win->sidebar_box, win->settings.show_sidebar);
}

void notes_window_load_file(NotesWindow *win, const char *path) {
    if (!path || path[0] == '\0') return;
    char *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && len <= 10 * 1024 * 1024) {
        g_free(win->original_content);
        win->original_content = g_strdup(contents);
        gtk_text_buffer_set_text(win->buffer, contents, (int)len);
        g_free(contents);

        /* Move cursor to the start so the view doesn't scroll to the bottom */
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(win->buffer, &start);
        gtk_text_buffer_place_cursor(win->buffer, &start);

        win->dirty = FALSE;
        gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
        strncpy(win->current_file, path, sizeof(win->current_file) - 1);
        strncpy(win->settings.last_file, path, sizeof(win->settings.last_file) - 1);
        settings_save(&win->settings);

        /* Highlight the selected row in sidebar */
        if (win->note_list) {
            GtkWidget *child = gtk_widget_get_first_child(win->note_list);
            while (child) {
                if (GTK_IS_LIST_BOX_ROW(child)) {
                    const char *fp = g_object_get_data(G_OBJECT(child), "filepath");
                    if (fp && strcmp(fp, path) == 0) {
                        gtk_list_box_select_row(GTK_LIST_BOX(win->note_list),
                                                GTK_LIST_BOX_ROW(child));
                        break;
                    }
                }
                child = gtk_widget_get_next_sibling(child);
            }
        }

        /* Apply syntax highlighting */
        highlight_apply(win->buffer, LANG_MARKDOWN);
        gtk_text_tag_set_priority(win->intensity_tag, 0);

        /* Show preview by default, hide editor */
        if (!win->preview_webview)
            notes_window_init_preview(win);
        win->preview_visible = TRUE;
        win->editing = FALSE;
        gtk_widget_set_visible(win->preview_scrolled, TRUE);
        gtk_widget_set_visible(win->editor_vbox, FALSE);
        if (win->preview_ready)
            notes_window_update_preview_now(win);
        else
            notes_window_update_preview(win);
    }
}

/* ── Sidebar ────────────────────────────────────────────────────── */

static void on_note_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
    (void)list;
    NotesWindow *win = data;
    if (!row) return;

    const char *filepath = g_object_get_data(G_OBJECT(row), "filepath");
    if (!filepath) return;

    /* Auto-save current before switching */
    notes_window_update_index(win);
    notes_window_load_file(win, filepath);
}

static void populate_note_list(NotesWindow *win, NoteResults *results) {
    /* Remove all children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(win->note_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(win->note_list), child);

    if (!results) return;

    for (int i = 0; i < results->count; i++) {
        NoteInfo *info = &results->items[i];

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_add_css_class(row_box, "note-row");

        /* Title */
        GtkWidget *title_label = gtk_label_new(info->title);
        gtk_widget_add_css_class(title_label, "note-title");
        gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(title_label), 30);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0);
        gtk_box_append(GTK_BOX(row_box), title_label);

        /* Date */
        char date_str[64];
        struct tm *t = localtime(&info->mtime);
        if (t)
            strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", t);
        else
            snprintf(date_str, sizeof(date_str), "---");

        GtkWidget *date_label = gtk_label_new(date_str);
        gtk_widget_add_css_class(date_label, "note-date");
        gtk_label_set_xalign(GTK_LABEL(date_label), 0);
        gtk_box_append(GTK_BOX(row_box), date_label);

        /* Snippet (if search result) */
        if (info->snippet && info->snippet[0]) {
            GtkWidget *snip_label = gtk_label_new(NULL);
            /* Escape user content, then restore highlight markers as markup */
            char *escaped = g_markup_escape_text(info->snippet, -1);
            gchar **parts1 = g_strsplit(escaped, "\x02", -1);
            char *tmp1 = g_strjoinv("<b>", parts1);
            g_strfreev(parts1);
            gchar **parts2 = g_strsplit(tmp1, "\x03", -1);
            char *safe_markup = g_strjoinv("</b>", parts2);
            g_strfreev(parts2);
            gtk_label_set_markup(GTK_LABEL(snip_label), safe_markup);
            g_free(escaped);
            g_free(tmp1);
            g_free(safe_markup);
            gtk_widget_add_css_class(snip_label, "note-date");
            gtk_label_set_ellipsize(GTK_LABEL(snip_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(snip_label), 30);
            gtk_label_set_xalign(GTK_LABEL(snip_label), 0);
            gtk_box_append(GTK_BOX(row_box), snip_label);
        }

        /* Add row to list box */
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        g_object_set_data_full(G_OBJECT(row), "filepath",
                               g_strdup(info->filepath), g_free);
        gtk_list_box_append(GTK_LIST_BOX(win->note_list), row);

        /* Select current file */
        if (win->current_file[0] && strcmp(info->filepath, win->current_file) == 0)
            gtk_list_box_select_row(GTK_LIST_BOX(win->note_list), GTK_LIST_BOX_ROW(row));
    }
}

void notes_window_refresh_sidebar(NotesWindow *win) {
    if (!win->db || !win->note_list) return;

    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    NoteResults *results = NULL;

    if (search_text && search_text[0] != '\0') {
        results = notes_db_search(win->db, search_text);
    } else {
        results = notes_db_list_all(win->db, win->settings.sort_order);
    }

    populate_note_list(win, results);
    notes_db_results_free(results);
}

/* Save current buffer and update index */
void notes_window_update_index(NotesWindow *win) {
    if (!win->db || !win->dirty) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') {
        g_free(text);
        return;
    }

    /* if we have a current file, overwrite it */
    if (win->current_file[0] != '\0') {
        FILE *f = fopen(win->current_file, "w");
        if (f) { fputs(text, f); fclose(f); }
        notes_db_index_file(win->db, win->current_file);
    } else {
        /* save to new timestamped file */
        g_mkdir_with_parents(win->settings.save_directory, 0755);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char filename[2048];
        snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.md",
                 win->settings.save_directory,
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        FILE *f = fopen(filename, "w");
        if (f) { fputs(text, f); fclose(f); }
        snprintf(win->current_file, sizeof(win->current_file), "%s", filename);
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", filename);
        notes_db_index_file(win->db, filename);
        settings_save(&win->settings);
    }
    g_free(win->original_content);
    win->original_content = text;
    win->dirty = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
}

/* ── Preview ───────────────────────────────────────────────────── */

/* Escape a string for JavaScript single-quoted string literal */
char *js_escape(const char *src) {
    GString *out = g_string_sized_new(strlen(src) + 32);
    for (const char *p = src; *p; p++) {
        switch (*p) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '\'': g_string_append(out, "\\'"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    g_string_append_printf(out, "\\x%02x", (unsigned char)*p);
                } else {
                    g_string_append_c(out, *p);
                }
        }
    }
    return g_string_free(out, FALSE);
}

static gboolean preview_timeout_cb(gpointer data) {
    NotesWindow *win = data;
    win->preview_timeout_id = 0;
    if (!win->preview_visible || !win->preview_webview || !win->preview_ready)
        return G_SOURCE_REMOVE;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

    /* Limit preview content to 100KB to prevent WebKit memory exhaustion */
    gsize text_len = strlen(text);
    if (text_len > 100 * 1024) {
        text[100 * 1024] = '\0';
    }

    char *escaped = js_escape(text);
    g_free(text);

    char *js = g_strdup_printf("updatePreview('%s');", escaped);
    g_free(escaped);

    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(win->preview_webview),
        js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
    return G_SOURCE_REMOVE;
}

void notes_window_update_preview(NotesWindow *win) {
    if (!win->preview_visible) return;
    if (win->preview_timeout_id)
        g_source_remove(win->preview_timeout_id);
    win->preview_timeout_id = g_timeout_add(300, preview_timeout_cb, win);
}

/* Update preview immediately without debounce */
static void notes_window_update_preview_now(NotesWindow *win) {
    if (!win->preview_visible) return;
    if (win->preview_timeout_id) {
        g_source_remove(win->preview_timeout_id);
        win->preview_timeout_id = 0;
    }
    preview_timeout_cb(win);
}

/* Search debounce */
static gboolean search_timeout_cb(gpointer data) {
    NotesWindow *win = data;
    win->search_timeout_id = 0;
    notes_window_refresh_sidebar(win);
    return G_SOURCE_REMOVE;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    (void)entry;
    NotesWindow *win = data;
    if (win->search_timeout_id)
        g_source_remove(win->search_timeout_id);
    win->search_timeout_id = g_timeout_add(300, search_timeout_cb, win);
}

/* Tag chip click handling */
static GtkWidget *build_menu_button(void) {
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Edit (Ctrl+E)", "win.toggle-edit");
    g_menu_append(menu, "Export PDF", "win.export-pdf");
    g_menu_append(menu, "Open Folder", "win.open-folder");
    g_menu_append(menu, "Delete Note", "win.delete-note");
    g_menu_append(menu, "Pack Notes", "win.pack-notes");
    g_menu_append(menu, "Settings", "win.settings");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    return button;
}

static void auto_save_current(NotesWindow *win) {
    if (!win->dirty) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') {
        g_free(text);
        win->settings.last_file[0] = '\0';
        return;
    }

    /* if we have a current file, overwrite it */
    if (win->current_file[0] != '\0') {
        FILE *f = fopen(win->current_file, "w");
        if (f) { fputs(text, f); fclose(f); }
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", win->current_file);
        if (win->db) notes_db_index_file(win->db, win->current_file);
    } else {
        /* save to new timestamped file */
        g_mkdir_with_parents(win->settings.save_directory, 0755);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char filename[2048];
        snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.md",
                 win->settings.save_directory,
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        FILE *f = fopen(filename, "w");
        if (f) { fputs(text, f); fclose(f); }
        snprintf(win->current_file, sizeof(win->current_file), "%s", filename);
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", filename);
        if (win->db) notes_db_index_file(win->db, filename);
    }
    g_free(win->original_content);
    win->original_content = text;
    win->dirty = FALSE;
}

static gboolean on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    NotesWindow *win = data;
    auto_save_current(win);
    win->settings.window_width = gtk_widget_get_width(GTK_WIDGET(win->window));
    win->settings.window_height = gtk_widget_get_height(GTK_WIDGET(win->window));
    settings_save(&win->settings);
    return FALSE;
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;

    if (win->search_timeout_id) {
        g_source_remove(win->search_timeout_id);
        win->search_timeout_id = 0;
    }
    if (win->intensity_idle_id) {
        g_source_remove(win->intensity_idle_id);
        win->intensity_idle_id = 0;
    }
    if (win->scroll_idle_id) {
        g_source_remove(win->scroll_idle_id);
        win->scroll_idle_id = 0;
    }
    if (win->line_numbers_idle_id) {
        g_source_remove(win->line_numbers_idle_id);
        win->line_numbers_idle_id = 0;
    }
    if (win->preview_timeout_id) {
        g_source_remove(win->preview_timeout_id);
        win->preview_timeout_id = 0;
    }
    if (win->highlight_idle_id) {
        g_source_remove(win->highlight_idle_id);
        win->highlight_idle_id = 0;
    }
    gtk_style_context_remove_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider));
    g_object_unref(win->css_provider);

    notes_db_close(win->db);
    g_free(win->original_content);
    g_free(win->preview_html);
    g_free(win);
}

static const char *html_shell =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<style>"
    ":root{--fg:#1f2328;--bg:#fff;--link:#0969da;--code-bg:#f6f8fa;"
    "--border:#d0d7de;--pre-border:#d0d7de;--quote-border:#d0d7de;"
    "--quote-fg:#656d76;--stripe-bg:#f6f8fa;--hr-bg:#d0d7de}"
    "body{font-family:-apple-system,'Segoe UI','Noto Sans',Helvetica,Arial,sans-serif;"
    "font-size:15px;line-height:1.6;color:var(--fg);background:var(--bg);"
    "padding:16px 24px;margin:0;word-wrap:break-word}"
    "h1{font-size:2em;border-bottom:1px solid var(--border);padding-bottom:.3em}"
    "h2{font-size:1.5em;border-bottom:1px solid var(--border);padding-bottom:.3em}"
    "h3{font-size:1.25em}h1,h2,h3,h4,h5,h6{margin-top:24px;margin-bottom:16px}"
    "p{margin-top:0;margin-bottom:16px}"
    "code{font-family:Consolas,'Liberation Mono',Menlo,monospace;"
    "font-size:85%;padding:.2em .4em;background:var(--code-bg);border-radius:6px}"
    "pre{padding:16px;overflow:auto;font-size:85%;line-height:1.45;"
    "background:var(--code-bg);border-radius:6px;border:1px solid var(--pre-border)}"
    "pre code{padding:0;background:transparent;font-size:100%}"
    "blockquote{margin:0 0 16px 0;padding:0 1em;color:var(--quote-fg);"
    "border-left:.25em solid var(--quote-border)}"
    "table{border-spacing:0;border-collapse:collapse;margin-bottom:16px}"
    "table th,table td{padding:6px 13px;border:1px solid var(--border)}"
    "table tr:nth-child(2n){background:var(--stripe-bg)}"
    "table th{font-weight:600}img{max-width:100%}"
    "a{color:var(--link)}hr{height:.25em;margin:24px 0;background:var(--hr-bg);border:0}"
    "ul,ol{padding-left:2em;margin-bottom:16px}"
    ".katex-display{overflow-x:auto;padding:4px 0}"
    ".mermaid{text-align:center;margin:16px 0}.mermaid svg{max-width:100%}"
    "</style></head><body><div id='content'></div></body></html>";

static void on_preview_load_changed(WebKitWebView *webview, WebKitLoadEvent event, gpointer data);

/* Lazy-initialize the WebKit preview pane on first use */
void notes_window_init_preview(NotesWindow *win) {
    if (win->preview_webview) return; /* already initialized */

    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitSettings *wk_settings = webkit_web_view_get_settings(wv);
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_allow_file_access_from_file_urls(wk_settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(wk_settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(wk_settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    win->preview_webview = GTK_WIDGET(wv);
    win->preview_scrolled = win->preview_webview;
    gtk_widget_set_vexpand(win->preview_webview, TRUE);
    gtk_widget_set_hexpand(win->preview_webview, TRUE);

    win->preview_ready = FALSE;
    g_signal_connect(wv, "load-changed",
        G_CALLBACK(on_preview_load_changed), win);

    /* Build JS blob: libraries + glue code */
    char exe_path[2048];
    ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (elen > 0) {
        exe_path[elen] = '\0';
        char *exe_dir = g_path_get_dirname(exe_path);
        char *web_dir = g_build_filename(exe_dir, "..", "data", "web", NULL);

        char *marked_js = NULL, *katex_js = NULL, *mermaid_js = NULL;
        char *p;
        p = g_build_filename(web_dir, "js", "marked.min.js", NULL);
        g_file_get_contents(p, &marked_js, NULL, NULL); g_free(p);
        p = g_build_filename(web_dir, "js", "katex.min.js", NULL);
        g_file_get_contents(p, &katex_js, NULL, NULL); g_free(p);
        p = g_build_filename(web_dir, "js", "mermaid.min.js", NULL);
        g_file_get_contents(p, &mermaid_js, NULL, NULL); g_free(p);

        GString *js = g_string_new(NULL);
        if (marked_js) { g_string_append(js, marked_js); g_string_append_c(js, '\n'); }
        if (katex_js) { g_string_append(js, katex_js); g_string_append_c(js, '\n'); }
        if (mermaid_js) { g_string_append(js, mermaid_js); g_string_append_c(js, '\n'); }

        g_string_append(js,
            "var _dark=false;\n"
            "window.applyTheme=function(fg,bg,dark){\n"
            "_dark=dark;var r=document.documentElement.style;\n"
            "r.setProperty('--fg',fg);r.setProperty('--bg',bg);\n"
            "if(dark){r.setProperty('--link','#58a6ff');"
            "r.setProperty('--code-bg','rgba(255,255,255,0.08)');"
            "r.setProperty('--border','rgba(255,255,255,0.15)');"
            "r.setProperty('--pre-border','rgba(255,255,255,0.15)');"
            "r.setProperty('--quote-border','rgba(255,255,255,0.2)');"
            "r.setProperty('--quote-fg','rgba(255,255,255,0.5)');"
            "r.setProperty('--stripe-bg','rgba(255,255,255,0.04)');"
            "r.setProperty('--hr-bg','rgba(255,255,255,0.15)');"
            "}else{r.setProperty('--link','#0969da');"
            "r.setProperty('--code-bg','rgba(0,0,0,0.04)');"
            "r.setProperty('--border','#d0d7de');"
            "r.setProperty('--pre-border','#d0d7de');"
            "r.setProperty('--quote-border','#d0d7de');"
            "r.setProperty('--quote-fg','rgba(0,0,0,0.5)');"
            "r.setProperty('--stripe-bg','rgba(0,0,0,0.03)');"
            "r.setProperty('--hr-bg','#d0d7de');}"
            "mermaid.initialize({startOnLoad:false,theme:dark?'dark':'default',securityLevel:'loose'});"
            "};\n"
            "mermaid.initialize({startOnLoad:false,"
            "theme:'default',"
            "securityLevel:'strict'});\n"
            "var mmId=0;\n"
            "var _lastSrc='';\n"
            "var _mmPending=false;\n"
            "window.updatePreview=function(src){\n"
            "if(src===_lastSrc)return;\n"
            "_lastSrc=src;\n"
            "mmId=0;var dm=[],im=[];\n"
            "src=src.replace(/\\$\\$([\\s\\S]*?)\\$\\$/g,function(m){dm.push(m);return'%%D'+(dm.length-1)+'%%';});\n"
            "src=src.replace(/\\$([^\\$\\n]+?)\\$/g,function(m){im.push(m);return'%%I'+(im.length-1)+'%%';});\n"
            "var r=new marked.Renderer();\n"
            "r.code=function(o){\n"
            "var t=typeof o==='string'?o:(o.text||''),l=typeof o==='object'&&o.lang?o.lang.trim().toLowerCase():'';\n"
            "if(l==='mermaid'){var id='mm-'+(mmId++);return'<div class=\"mermaid\" id=\"'+id+'\">'+t.replace(/</g,'&lt;')+'</div>';}\n"
            "return'<pre><code>'+t.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</code></pre>';};\n"
            "var h=marked.parse(src,{renderer:r,gfm:true});\n"
            "for(var i=0;i<dm.length;i++){var m=dm[i].replace(/^\\$\\$/,'').replace(/\\$\\$$/,'').trim();\n"
            "try{h=h.replace('%%D'+i+'%%',katex.renderToString(m,{displayMode:true,throwOnError:false}));}catch(e){}}\n"
            "for(var i=0;i<im.length;i++){var m=im[i].replace(/^\\$/,'').replace(/\\$$/,'').trim();\n"
            "try{h=h.replace('%%I'+i+'%%',katex.renderToString(m,{displayMode:false,throwOnError:false}));}catch(e){}}\n"
            "document.getElementById('content').innerHTML=h;\n"
            /* Clean up mermaid temp SVGs before rendering new ones */
            "document.querySelectorAll('[id^=\"dsvg-\"]').forEach(function(e){e.remove();});\n"
            "if(_mmPending)return;\n"
            "var mmEls=document.querySelectorAll('.mermaid');\n"
            "if(mmEls.length===0)return;\n"
            "if(mmEls.length>5)mmEls=Array.prototype.slice.call(mmEls,0,5);\n"
            "_mmPending=true;\n"
            "var done=0,total=mmEls.length;\n"
            "mmEls.forEach(function(el){\n"
            "var c=el.textContent,id='svg-'+el.id;\n"
            "try{mermaid.render(id,c).then(function(r){"
            "el.innerHTML=r.svg;done++;if(done>=total)_mmPending=false;"
            "}).catch(function(){done++;if(done>=total)_mmPending=false;});"
            "}catch(e){done++;if(done>=total)_mmPending=false;}});\n"
            "};\n");

        win->preview_html = g_string_free(js, FALSE);
        g_free(marked_js);
        g_free(katex_js);
        g_free(mermaid_js);
        g_free(web_dir);
        g_free(exe_dir);
    }

    /* Add to the paned and load HTML */
    gtk_paned_set_end_child(GTK_PANED(win->preview_paned), win->preview_scrolled);
    gtk_paned_set_shrink_end_child(GTK_PANED(win->preview_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(win->preview_paned), FALSE);

    webkit_web_view_load_html(WEBKIT_WEB_VIEW(win->preview_webview),
                               html_shell, NULL);
}

static void on_js_injected(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src; (void)res;
    NotesWindow *win = data;
    win->preview_ready = TRUE;
    apply_preview_theme(win);
    notes_window_update_preview_now(win);
}

static void on_preview_load_changed(WebKitWebView *webview, WebKitLoadEvent event, gpointer data) {
    NotesWindow *win = data;
    if (event == WEBKIT_LOAD_FINISHED) {
        /* Inject JS libraries after HTML shell is loaded */
        if (win->preview_html) {
            webkit_web_view_evaluate_javascript(webview,
                win->preview_html, -1, NULL, NULL, NULL,
                on_js_injected, win);
        } else {
            win->preview_ready = TRUE;
            apply_preview_theme(win);
            notes_window_update_preview(win);
        }
    }
}

static GtkWidget *build_sidebar(NotesWindow *win) {
    win->sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(win->sidebar_box, "sidebar");
    gtk_widget_set_size_request(win->sidebar_box, 250, -1);

    /* Search entry */
    win->search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_start(win->search_entry, 8);
    gtk_widget_set_margin_end(win->search_entry, 8);
    gtk_widget_set_margin_top(win->search_entry, 8);
    gtk_widget_set_margin_bottom(win->search_entry, 4);
    g_signal_connect(win->search_entry, "search-changed", G_CALLBACK(on_search_changed), win);
    gtk_box_append(GTK_BOX(win->sidebar_box), win->search_entry);

    /* Note list */
    win->note_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(win->note_list), GTK_SELECTION_SINGLE);
    g_signal_connect(win->note_list, "row-activated", G_CALLBACK(on_note_row_activated), win);

    GtkWidget *list_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scrolled), win->note_list);
    gtk_widget_set_vexpand(list_scrolled, TRUE);
    gtk_box_append(GTK_BOX(win->sidebar_box), list_scrolled);

    return win->sidebar_box;
}

NotesWindow *notes_window_new(GtkApplication *app) {
    NotesWindow *win = g_new0(NotesWindow, 1);
    settings_load(&win->settings);

    win->window = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
    gtk_window_set_default_size(GTK_WINDOW(win->window),
                                win->settings.window_width,
                                win->settings.window_height);

    g_signal_connect(win->window, "close-request", G_CALLBACK(on_close_request), win);
    g_signal_connect(win->window, "destroy", G_CALLBACK(on_destroy), win);

    /* Header bar */
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *menu_btn = build_menu_button();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn);

    /* Sidebar toggle button */
    GtkWidget *sidebar_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(sidebar_btn), "view-list-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(sidebar_btn), "win.toggle-sidebar");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), sidebar_btn);

    /* New note button */
    GtkWidget *new_btn = gtk_button_new_from_icon_name("document-new-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(new_btn), "win.new-note");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_btn);

    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);

    /* Line numbers (drawing area that queries main text view positions) */
    win->line_numbers = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_can_focus(GTK_WIDGET(win->line_numbers), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(win->line_numbers), "line-numbers");
    gtk_drawing_area_set_draw_func(win->line_numbers, draw_line_numbers, win, NULL);

    /* Text view (custom subclass for highlight drawing) */
    NotesTextView *ntv = g_object_new(NOTES_TYPE_TEXT_VIEW, NULL);
    ntv->win = win;
    win->text_view = GTK_TEXT_VIEW(ntv);
    win->buffer = gtk_text_view_get_buffer(win->text_view);
    highlight_create_tags(win->buffer);
    gtk_text_view_set_wrap_mode(win->text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(win->text_view, 12);
    gtk_text_view_set_right_margin(win->text_view, 12);
    gtk_text_view_set_top_margin(win->text_view, 8);
    gtk_text_view_set_bottom_margin(win->text_view, 8);

    /* Font intensity tag — lowest priority so highlight colors take precedence */
    win->intensity_tag = gtk_text_buffer_create_tag(win->buffer, "intensity",
                                                     "foreground-rgba", NULL, NULL);
    gtk_text_tag_set_priority(win->intensity_tag, 0);

    g_signal_connect(win->buffer, "changed", G_CALLBACK(on_buffer_changed), win);
    g_signal_connect(win->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), win);

    /* Scrolled window for text view */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->text_view));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    /* Line numbers container */
    win->ln_scrolled = GTK_WIDGET(win->line_numbers);
    gtk_widget_set_vexpand(win->ln_scrolled, TRUE);

    /* Redraw line numbers when the main text view scrolls */
    GtkAdjustment *main_vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    g_signal_connect_swapped(main_vadj, "value-changed",
                             G_CALLBACK(gtk_widget_queue_draw), win->line_numbers);

    /* HBox: line numbers + text view */
    win->editor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(win->editor_box), win->ln_scrolled);
    gtk_box_append(GTK_BOX(win->editor_box), scrolled);

    /* Status bar */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_bar, "statusbar");
    gtk_widget_set_margin_start(status_bar, 8);
    gtk_widget_set_margin_end(status_bar, 8);
    gtk_widget_set_margin_top(status_bar, 2);
    gtk_widget_set_margin_bottom(status_bar, 2);

    win->status_encoding = GTK_LABEL(gtk_label_new("UTF-8"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_encoding), GTK_ALIGN_START);
    gtk_widget_set_hexpand(GTK_WIDGET(win->status_encoding), TRUE);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_encoding));

    win->status_cursor = GTK_LABEL(gtk_label_new("Ln 1, Col 1"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_cursor), GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_cursor));

    /* Editor + statusbar vbox */
    win->editor_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *editor_vbox = win->editor_vbox;
    gtk_widget_set_vexpand(win->editor_box, TRUE);
    gtk_box_append(GTK_BOX(editor_vbox), win->editor_box);
    gtk_box_append(GTK_BOX(editor_vbox), status_bar);
    gtk_widget_set_hexpand(editor_vbox, TRUE);
    gtk_widget_set_vexpand(editor_vbox, TRUE);

    /* Preview pane */
    win->preview_webview = NULL;
    win->preview_scrolled = NULL;
    win->preview_visible = FALSE;
    win->preview_ready = FALSE;

    /* Paned: editor | preview */
    win->preview_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(win->preview_paned, TRUE);
    gtk_widget_set_hexpand(win->preview_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(win->preview_paned), editor_vbox);
    gtk_paned_set_shrink_start_child(GTK_PANED(win->preview_paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(win->preview_paned), TRUE);

    /* Build sidebar */
    GtkWidget *sidebar = build_sidebar(win);

    /* Paned: sidebar | (editor | preview) */
    win->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(win->paned), sidebar);
    gtk_paned_set_end_child(GTK_PANED(win->paned), win->preview_paned);
    gtk_paned_set_position(GTK_PANED(win->paned), 260);
    gtk_paned_set_shrink_start_child(GTK_PANED(win->paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(win->paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(win->paned), FALSE);

    gtk_window_set_child(GTK_WINDOW(win->window), win->paned);

    /* CSS provider */
    win->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Actions & shortcuts */
    actions_setup(win, app);

    /* Open database and sync index */
    win->db = notes_db_open(win->settings.save_directory);
    if (win->db)
        notes_db_sync(win->db, win->settings.save_directory);

    /* Initialize WebKit preview early so it's ready when files are opened */
    notes_window_init_preview(win);

    /* Apply settings */
    notes_window_apply_settings(win);

    /* Populate sidebar */
    notes_window_refresh_sidebar(win);

    /* Restore last file */
    if (win->settings.last_file[0] != '\0')
        notes_window_load_file(win, win->settings.last_file);

    return win;
}
