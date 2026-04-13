#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <gtk/gtk.h>

typedef enum {
    LANG_PLAIN_TEXT = 0,
    LANG_MARKDOWN,
    LANG_C,
    LANG_PYTHON,
    LANG_JAVASCRIPT,
    LANG_JSON,
    LANG_COUNT
} HighlightLanguage;

static const char *lang_names[] = {
    "Plain Text", "Markdown", "C", "Python", "JavaScript", "JSON", NULL
};

void highlight_create_tags(GtkTextBuffer *buffer);
void highlight_apply(GtkTextBuffer *buffer, HighlightLanguage lang);
HighlightLanguage highlight_detect(const char *path);

#endif
