#include <adwaita.h>
#include "window.h"
#include "actions.h"
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

static GdkRGBA highlight_rgba;

static void notes_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    NotesTextView *self = NOTES_TEXT_VIEW(widget);
    NotesWindow *win = self->win;

    /* 1. Let GTK draw everything first (background, text, cursor) */
    GTK_WIDGET_CLASS(notes_text_view_parent_class)->snapshot(widget, snapshot);

    /* 2. Draw highlight overlay on top */
    if (win && win->settings.highlight_current_line) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_line(win->buffer, &iter, win->highlight_line);

        GdkRectangle rect;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(widget), &iter, &rect);

        int wx, wy;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(widget),
            GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y, &wx, &wy);

        int view_width = gtk_widget_get_width(widget);
        int h = rect.height > 0 ? rect.height : win->settings.font_size + 4;
        int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
        if (extra < 0) extra = 0;

        graphene_rect_t area = GRAPHENE_RECT_INIT(0, wy - extra, view_width, h + extra * 2);
        gtk_snapshot_append_color(snapshot, &highlight_rgba, &area);
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
        highlight_rgba = (GdkRGBA){1.0, 1.0, 1.0, 0.06};
    } else {
        highlight_rgba = (GdkRGBA){0.0, 0.0, 0.0, 0.06};
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
            ".sidebar { background-color: %s; }"
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
            bg,
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
            ".sidebar .note-row { padding: 8px 12px; }"
            ".sidebar .note-title { font-weight: bold; }"
            ".sidebar .note-date { opacity: 0.5; font-size: 10pt; }"
            ".sidebar .note-tags { opacity: 0.6; font-size: 9pt; }"
            ".sidebar .tag-chip { border-radius: 12px; padding: 2px 8px;"
            "  font-size: 9pt; opacity: 0.8; }"
            ".sidebar .tag-chip.active { opacity: 1.0; font-weight: bold; }",
            win->settings.font, win->settings.font_size, bg,
            bg, fg,
            bg, fg);
    }

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

/* Line numbers */
static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win);

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

static gboolean intensity_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->intensity_idle_id = 0;
    if (win->settings.font_intensity < 0.99)
        apply_font_intensity(win);
    return G_SOURCE_REMOVE;
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    NotesWindow *win = data;
    if (win->settings.show_line_numbers)
        update_line_numbers(buffer, win);
    update_cursor_position(win);
    update_line_highlights(win);
    if (win->settings.font_intensity < 0.99 && win->intensity_idle_id == 0)
        win->intensity_idle_id = g_idle_add(intensity_idle_cb, win);
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer data) {
    (void)buffer; (void)pspec;
    NotesWindow *win = data;
    update_cursor_position(win);
    update_line_highlights(win);
}

static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win) {
    int lines = gtk_text_buffer_get_line_count(buffer);

    /* Skip rebuild if line count hasn't changed */
    if (lines == win->cached_line_count)
        return;
    win->cached_line_count = lines;

    GString *str = g_string_sized_new((gsize)(lines * 4));
    for (int i = 1; i <= lines; i++) {
        if (i > 1) g_string_append_c(str, '\n');
        g_string_append_printf(str, "%d", i);
    }
    GtkTextBuffer *ln_buf = gtk_text_view_get_buffer(win->line_numbers);
    gtk_text_buffer_set_text(ln_buf, str->str, -1);
    g_string_free(str, TRUE);

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

    int width = pw + 12; /* left + right margin */
    gtk_widget_set_size_request(GTK_WIDGET(win->line_numbers), width, -1);
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
}

void notes_window_apply_settings(NotesWindow *win) {
    apply_theme(win);
    apply_css(win);

    int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
    if (extra < 0) extra = 0;
    gtk_text_view_set_pixels_above_lines(win->text_view, extra);
    gtk_text_view_set_pixels_below_lines(win->text_view, extra);
    gtk_text_view_set_pixels_above_lines(win->line_numbers, extra);
    gtk_text_view_set_pixels_below_lines(win->line_numbers, extra);

    gtk_widget_set_visible(win->ln_scrolled, win->settings.show_line_numbers);
    win->cached_line_count = 0; /* force rebuild */
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);

    gtk_text_view_set_wrap_mode(win->text_view,
        win->settings.wrap_lines ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);

    apply_highlight_color(win);
    update_line_highlights(win);
    apply_font_intensity(win);

    /* Sidebar visibility */
    if (win->sidebar_box)
        gtk_widget_set_visible(win->sidebar_box, win->settings.show_sidebar);
}

void notes_window_load_file(NotesWindow *win, const char *path) {
    if (!path || path[0] == '\0') return;
    char *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL)) {
        gtk_text_buffer_set_text(win->buffer, contents, (int)len);
        g_free(contents);
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
            /* Strip markup tags for plain display */
            gtk_label_set_markup(GTK_LABEL(snip_label), info->snippet);
            gtk_widget_add_css_class(snip_label, "note-date");
            gtk_label_set_ellipsize(GTK_LABEL(snip_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(snip_label), 30);
            gtk_label_set_xalign(GTK_LABEL(snip_label), 0);
            gtk_box_append(GTK_BOX(row_box), snip_label);
        }

        /* Tags */
        if (info->tag_count > 0) {
            GString *tag_str = g_string_new(NULL);
            for (int t2 = 0; t2 < info->tag_count; t2++) {
                if (t2 > 0) g_string_append(tag_str, "  ");
                g_string_append_c(tag_str, '#');
                g_string_append(tag_str, info->tags[t2]);
            }
            GtkWidget *tags_label = gtk_label_new(tag_str->str);
            gtk_widget_add_css_class(tags_label, "note-tags");
            gtk_label_set_ellipsize(GTK_LABEL(tags_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(tags_label), 30);
            gtk_label_set_xalign(GTK_LABEL(tags_label), 0);
            gtk_box_append(GTK_BOX(row_box), tags_label);
            g_string_free(tag_str, TRUE);
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

static void populate_tag_flow(NotesWindow *win) {
    /* Remove all children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(win->tag_flow)) != NULL)
        gtk_flow_box_remove(GTK_FLOW_BOX(win->tag_flow), child);

    int count = 0;
    char **tags = notes_db_all_tags(win->db, &count);
    if (!tags || count == 0) {
        gtk_widget_set_visible(win->tag_flow, FALSE);
        notes_db_tags_free(tags, count);
        return;
    }

    gtk_widget_set_visible(win->tag_flow, TRUE);

    /* "All" chip to clear filter */
    GtkWidget *all_btn = gtk_button_new_with_label("All");
    gtk_widget_add_css_class(all_btn, "tag-chip");
    if (!win->active_tag_filter)
        gtk_widget_add_css_class(all_btn, "active");
    gtk_flow_box_append(GTK_FLOW_BOX(win->tag_flow), all_btn);

    for (int i = 0; i < count; i++) {
        char label[128];
        snprintf(label, sizeof(label), "#%s", tags[i]);
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "tag-chip");
        g_object_set_data_full(G_OBJECT(btn), "tag", g_strdup(tags[i]), g_free);
        if (win->active_tag_filter && strcmp(win->active_tag_filter, tags[i]) == 0)
            gtk_widget_add_css_class(btn, "active");
        gtk_flow_box_append(GTK_FLOW_BOX(win->tag_flow), btn);
    }

    notes_db_tags_free(tags, count);
}

static void on_tag_chip_clicked(GtkFlowBox *flow, GtkFlowBoxChild *child, gpointer data);

void notes_window_refresh_sidebar(NotesWindow *win) {
    if (!win->db || !win->note_list) return;

    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    NoteResults *results = NULL;

    if (search_text && search_text[0] != '\0') {
        results = notes_db_search(win->db, search_text);
    } else if (win->active_tag_filter) {
        results = notes_db_filter_by_tag(win->db, win->active_tag_filter);
    } else {
        results = notes_db_list_all(win->db);
    }

    populate_note_list(win, results);
    populate_tag_flow(win);
    notes_db_results_free(results);
}

/* Save current buffer and update index */
void notes_window_update_index(NotesWindow *win) {
    if (!win->db) return;

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
        snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.txt",
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
    g_free(text);
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
static void on_tag_chip_clicked(GtkFlowBox *flow, GtkFlowBoxChild *child, gpointer data) {
    (void)flow;
    NotesWindow *win = data;

    /* Find the button inside the FlowBoxChild */
    GtkWidget *btn = gtk_flow_box_child_get_child(child);
    if (!btn || !GTK_IS_BUTTON(btn)) return;

    const char *tag = g_object_get_data(G_OBJECT(btn), "tag");

    g_free(win->active_tag_filter);
    if (tag && (!win->active_tag_filter || strcmp(win->active_tag_filter, tag) != 0)) {
        win->active_tag_filter = g_strdup(tag);
    } else {
        win->active_tag_filter = NULL;
    }

    /* Clear search when filtering by tag */
    gtk_editable_set_text(GTK_EDITABLE(win->search_entry), "");
    notes_window_refresh_sidebar(win);
}

static GtkWidget *build_menu_button(void) {
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Open Folder", "win.open-folder");
    g_menu_append(menu, "Pack Notes", "win.pack-notes");
    g_menu_append(menu, "Settings", "win.settings");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    return button;
}

static void auto_save_current(NotesWindow *win) {
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
        snprintf(filename, sizeof(filename), "%s/note_%04d%02d%02d_%02d%02d%02d.txt",
                 win->settings.save_directory,
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        FILE *f = fopen(filename, "w");
        if (f) { fputs(text, f); fclose(f); }
        snprintf(win->current_file, sizeof(win->current_file), "%s", filename);
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", filename);
        if (win->db) notes_db_index_file(win->db, filename);
    }
    g_free(text);
}

static void on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    NotesWindow *win = data;
    auto_save_current(win);
    win->settings.window_width = gtk_widget_get_width(GTK_WIDGET(win->window));
    win->settings.window_height = gtk_widget_get_height(GTK_WIDGET(win->window));
    settings_save(&win->settings);
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
    if (win->line_numbers_idle_id) {
        g_source_remove(win->line_numbers_idle_id);
        win->line_numbers_idle_id = 0;
    }

    gtk_style_context_remove_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider));
    g_object_unref(win->css_provider);

    notes_db_close(win->db);
    g_free(win->active_tag_filter);
    g_free(win);
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

    /* Tag flow */
    win->tag_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(win->tag_flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(win->tag_flow), 20);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(win->tag_flow), 1);
    gtk_widget_set_margin_start(win->tag_flow, 8);
    gtk_widget_set_margin_end(win->tag_flow, 8);
    gtk_widget_set_margin_bottom(win->tag_flow, 4);
    g_signal_connect(win->tag_flow, "child-activated", G_CALLBACK(on_tag_chip_clicked), win);
    gtk_box_append(GTK_BOX(win->sidebar_box), win->tag_flow);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(win->sidebar_box), sep);

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

    /* Clear button */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(clear_btn), "win.clear");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), clear_btn);

    /* New note button */
    GtkWidget *new_btn = gtk_button_new_from_icon_name("document-new-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(new_btn), "win.new-note");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_btn);

    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);

    /* Line numbers (non-editable text view for pixel-perfect spacing) */
    win->line_numbers = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(win->line_numbers, FALSE);
    gtk_text_view_set_cursor_visible(win->line_numbers, FALSE);
    gtk_widget_set_can_focus(GTK_WIDGET(win->line_numbers), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(win->line_numbers), "line-numbers");
    gtk_text_view_set_right_margin(win->line_numbers, 8);
    gtk_text_view_set_left_margin(win->line_numbers, 4);
    gtk_text_view_set_top_margin(win->line_numbers, 8);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(win->line_numbers), "1", -1);

    /* Text view (custom subclass for highlight drawing) */
    NotesTextView *ntv = g_object_new(NOTES_TYPE_TEXT_VIEW, NULL);
    ntv->win = win;
    win->text_view = GTK_TEXT_VIEW(ntv);
    win->buffer = gtk_text_view_get_buffer(win->text_view);
    gtk_text_view_set_wrap_mode(win->text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(win->text_view, 12);
    gtk_text_view_set_right_margin(win->text_view, 12);
    gtk_text_view_set_top_margin(win->text_view, 8);
    gtk_text_view_set_bottom_margin(win->text_view, 8);

    /* Font intensity tag */
    win->intensity_tag = gtk_text_buffer_create_tag(win->buffer, "intensity",
                                                     "foreground-rgba", NULL, NULL);

    g_signal_connect(win->buffer, "changed", G_CALLBACK(on_buffer_changed), win);
    g_signal_connect(win->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), win);

    /* Scrolled window for text view */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->text_view));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    /* Scrolled window for line numbers */
    win->ln_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(win->ln_scrolled), GTK_WIDGET(win->line_numbers));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(win->ln_scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
    gtk_widget_set_vexpand(win->ln_scrolled, TRUE);

    /* Sync vertical scroll adjustments */
    GtkAdjustment *main_vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(win->ln_scrolled), main_vadj);

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
    GtkWidget *editor_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(editor_vbox), win->editor_box);
    gtk_box_append(GTK_BOX(editor_vbox), status_bar);
    gtk_widget_set_hexpand(editor_vbox, TRUE);

    /* Build sidebar */
    GtkWidget *sidebar = build_sidebar(win);

    /* Paned: sidebar | editor */
    win->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(win->paned), sidebar);
    gtk_paned_set_end_child(GTK_PANED(win->paned), editor_vbox);
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

    /* Apply settings */
    notes_window_apply_settings(win);

    /* Populate sidebar */
    notes_window_refresh_sidebar(win);

    /* Restore last file */
    if (win->settings.last_file[0] != '\0')
        notes_window_load_file(win, win->settings.last_file);

    return win;
}
