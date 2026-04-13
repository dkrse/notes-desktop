#ifndef NOTES_MARKDOWN_H
#define NOTES_MARKDOWN_H

#include <gtk/gtk.h>

/* Create standard markdown text tags in the buffer */
void markdown_create_tags(GtkTextBuffer *buffer);

/* Render markdown source text into a GtkTextBuffer with formatting tags.
 * The buffer is cleared and repopulated. Graphviz code blocks are rendered
 * as inline images via the diagrams module. */
void markdown_render(GtkTextBuffer *buffer, const char *markdown_text);

#endif
