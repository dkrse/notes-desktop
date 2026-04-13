#ifndef NOTES_DIAGRAMS_H
#define NOTES_DIAGRAMS_H

#include <gtk/gtk.h>

/* Render a Graphviz dot source string to a GdkTexture.
 * Returns NULL on failure. Caller owns the returned texture. */
GdkTexture *diagrams_render_dot(const char *dot_source, int max_width);

/* Render a Mermaid diagram source to a GdkTexture by translating
 * supported diagram types (flowchart/graph) to Graphviz dot.
 * Returns NULL if the diagram type is unsupported or rendering fails. */
GdkTexture *diagrams_render_mermaid(const char *mermaid_source, int max_width);

#endif
