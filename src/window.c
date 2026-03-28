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
    char css[4096];

    /* find custom theme if any */
    const ThemeDef *td = NULL;
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            td = &custom_themes[i];
            break;
        }
    }

    if (td) {
        /*
         * Custom theme: we must override ALL widget colors because GTK's
         * built-in light/dark variants don't match these palettes.
         */
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
            "popover, popover.menu, popover > contents, popover > arrow {"
            "  background-color: %s; color: %s; }"
            "popover modelbutton { color: %s; }"
            "popover modelbutton:hover { background-color: alpha(%s, 0.15); }"
            "windowcontrols button { color: %s; }",
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
            fg);
    } else {
        /*
         * System / Light / Dark: set explicit textview colors but let GTK's
         * own theme handle headerbar, popover, window controls, etc.
         */
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
            ".statusbar { font-size: 10pt; padding: 2px 4px; opacity: 0.7; }",
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

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    NotesWindow *win = data;
    if (win->settings.show_line_numbers)
        update_line_numbers(buffer, win);
    update_cursor_position(win);
    update_line_highlights(win);
    if (win->settings.font_intensity < 0.99)
        apply_font_intensity(win);
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer data) {
    (void)buffer; (void)pspec;
    NotesWindow *win = data;
    update_cursor_position(win);
    update_line_highlights(win);
}

static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win) {
    int lines = gtk_text_buffer_get_line_count(buffer);
    GString *str = g_string_new(NULL);
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
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);

    gtk_text_view_set_wrap_mode(win->text_view,
        win->settings.wrap_lines ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);

    apply_highlight_color(win);
    update_line_highlights(win);
    apply_font_intensity(win);
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
    }
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
    }
    g_free(text);
}

static void on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    NotesWindow *win = data;
    auto_save_current(win);
    settings_save(&win->settings);
}

NotesWindow *notes_window_new(GtkApplication *app) {
    NotesWindow *win = g_new0(NotesWindow, 1);
    settings_load(&win->settings);

    win->window = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes");
    gtk_window_set_default_size(GTK_WINDOW(win->window), 700, 500);

    g_signal_connect(win->window, "close-request", G_CALLBACK(on_close_request), win);

    /* Header bar */
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *menu_btn = build_menu_button();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn);

    /* Clear button on the left */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(clear_btn), "win.clear");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), clear_btn);

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

    /* Font intensity tag — applied to entire buffer */
    win->intensity_tag = gtk_text_buffer_create_tag(win->buffer, "intensity",
                                                     "foreground-rgba", NULL, NULL);

    g_signal_connect(win->buffer, "changed", G_CALLBACK(on_buffer_changed), win);
    g_signal_connect(win->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), win);

    /* Scrolled window for text view (must be direct child for auto-scroll) */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->text_view));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    /* Scrolled window for line numbers (synced with main) */
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

    /* Main vbox */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(main_box), win->editor_box);
    gtk_box_append(GTK_BOX(main_box), status_bar);
    gtk_window_set_child(GTK_WINDOW(win->window), main_box);

    /* CSS provider */
    win->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Actions & shortcuts */
    actions_setup(win, app);

    /* Apply settings */
    notes_window_apply_settings(win);

    /* Restore last file */
    if (win->settings.last_file[0] != '\0')
        notes_window_load_file(win, win->settings.last_file);

    return win;
}
