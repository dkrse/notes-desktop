#include "markdown.h"
#include "diagrams.h"
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <string.h>

/* ── Tag creation ──────────────────────────────────────────────── */

void markdown_create_tags(GtkTextBuffer *buffer) {
    gtk_text_buffer_create_tag(buffer, "md-h1",
        "weight", PANGO_WEIGHT_BOLD,
        "scale", 1.8,
        "pixels-above-lines", 12,
        "pixels-below-lines", 6,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-h2",
        "weight", PANGO_WEIGHT_BOLD,
        "scale", 1.5,
        "pixels-above-lines", 10,
        "pixels-below-lines", 4,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-h3",
        "weight", PANGO_WEIGHT_BOLD,
        "scale", 1.25,
        "pixels-above-lines", 8,
        "pixels-below-lines", 4,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-h4",
        "weight", PANGO_WEIGHT_BOLD,
        "scale", 1.1,
        "pixels-above-lines", 6,
        "pixels-below-lines", 2,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-h5",
        "weight", PANGO_WEIGHT_BOLD,
        "pixels-above-lines", 4,
        "pixels-below-lines", 2,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-h6",
        "weight", PANGO_WEIGHT_BOLD,
        "foreground", "#888888",
        "pixels-above-lines", 4,
        "pixels-below-lines", 2,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-bold",
        "weight", PANGO_WEIGHT_BOLD,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-italic",
        "style", PANGO_STYLE_ITALIC,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-code",
        "family", "Monospace",
        "background", "#e8e8e8",
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-code-block",
        "family", "Monospace",
        "paragraph-background", "#f4f4f4",
        "pixels-above-lines", 4,
        "pixels-below-lines", 4,
        "left-margin", 16,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-blockquote",
        "style", PANGO_STYLE_ITALIC,
        "foreground", "#666666",
        "left-margin", 24,
        "pixels-above-lines", 2,
        "pixels-below-lines", 2,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-link",
        "foreground", "#2a6496",
        "underline", PANGO_UNDERLINE_SINGLE,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-hr",
        "foreground", "#cccccc",
        "justification", GTK_JUSTIFY_CENTER,
        "pixels-above-lines", 8,
        "pixels-below-lines", 8,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-list-item",
        "left-margin", 24,
        "pixels-above-lines", 1,
        "pixels-below-lines", 1,
        NULL);
    gtk_text_buffer_create_tag(buffer, "md-strikethrough",
        "strikethrough", TRUE,
        NULL);
}

/* ── Rendering state ───────────────────────────────────────────── */

typedef struct {
    GtkTextBuffer *buffer;
    /* Stack of active tag names (max 16 deep) */
    const char *tag_stack[16];
    int tag_depth;
    /* Current list state */
    int list_item_number;
    gboolean in_ordered_list;
    int list_depth;
    /* Pending info */
    int heading_level;
    gboolean in_blockquote;
} RenderState;

static void push_tag(RenderState *s, const char *tag) {
    if (s->tag_depth < 16)
        s->tag_stack[s->tag_depth++] = tag;
}

static void pop_tag(RenderState *s) {
    if (s->tag_depth > 0)
        s->tag_depth--;
}

static void insert_text(RenderState *s, const char *text) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(s->buffer, &end);

    if (s->tag_depth == 0) {
        gtk_text_buffer_insert(s->buffer, &end, text, -1);
    } else {
        /* Get the offset before insert to apply tags after */
        int offset = gtk_text_iter_get_offset(&end);
        gtk_text_buffer_insert(s->buffer, &end, text, -1);

        GtkTextIter start;
        gtk_text_buffer_get_iter_at_offset(s->buffer, &start, offset);
        gtk_text_buffer_get_end_iter(s->buffer, &end);

        for (int i = 0; i < s->tag_depth; i++) {
            gtk_text_buffer_apply_tag_by_name(s->buffer,
                s->tag_stack[i], &start, &end);
        }
    }
}

static void insert_newline(RenderState *s) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(s->buffer, &end);
    gtk_text_buffer_insert(s->buffer, &end, "\n", -1);
}

static void ensure_newline(RenderState *s) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(s->buffer, &end);
    if (gtk_text_iter_get_offset(&end) == 0) return;
    gtk_text_iter_backward_char(&end);
    gunichar ch = gtk_text_iter_get_char(&end);
    if (ch != '\n')
        insert_newline(s);
}

/* ── Main render ───────────────────────────────────────────────── */

void markdown_render(GtkTextBuffer *buffer, const char *markdown_text) {
    if (!buffer || !markdown_text) return;

    /* Enable GFM extensions */
    cmark_gfm_core_extensions_ensure_registered();

    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);

    /* Attach GFM extensions */
    const char *ext_names[] = {"table", "strikethrough", "autolink", "tasklist", NULL};
    for (int i = 0; ext_names[i]; i++) {
        cmark_syntax_extension *ext = cmark_find_syntax_extension(ext_names[i]);
        if (ext)
            cmark_parser_attach_syntax_extension(parser, ext);
    }

    cmark_parser_feed(parser, markdown_text, strlen(markdown_text));
    cmark_node *doc = cmark_parser_finish(parser);

    /* Clear buffer */
    gtk_text_buffer_set_text(buffer, "", -1);

    RenderState state = {0};
    state.buffer = buffer;

    cmark_iter *iter = cmark_iter_new(doc);
    cmark_event_type ev;

    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (ev == CMARK_EVENT_ENTER) {
            switch (type) {
                case CMARK_NODE_HEADING: {
                    int level = cmark_node_get_heading_level(node);
                    state.heading_level = level;
                    ensure_newline(&state);
                    char tag[16];
                    int lv = level > 6 ? 6 : level;
                    snprintf(tag, sizeof(tag), "md-h%d", lv);
                    push_tag(&state, tag);
                    break;
                }
                case CMARK_NODE_PARAGRAPH:
                    ensure_newline(&state);
                    if (state.in_blockquote)
                        push_tag(&state, "md-blockquote");
                    break;
                case CMARK_NODE_STRONG:
                    push_tag(&state, "md-bold");
                    break;
                case CMARK_NODE_EMPH:
                    push_tag(&state, "md-italic");
                    break;
                case CMARK_NODE_CODE:
                    push_tag(&state, "md-code");
                    insert_text(&state, cmark_node_get_literal(node));
                    pop_tag(&state);
                    break;
                case CMARK_NODE_CODE_BLOCK: {
                    const char *info = cmark_node_get_fence_info(node);
                    const char *code = cmark_node_get_literal(node);

                    /* Check for diagram code blocks */
                    GdkTexture *tex = NULL;
                    if (info && (strcmp(info, "dot") == 0 ||
                                 strcmp(info, "graphviz") == 0)) {
                        tex = diagrams_render_dot(code, 600);
                    } else if (info && strcmp(info, "mermaid") == 0) {
                        tex = diagrams_render_mermaid(code, 600);
                    }

                    if (tex) {
                        ensure_newline(&state);
                        GdkPaintable *paintable = GDK_PAINTABLE(tex);
                        GtkTextIter end_iter;
                        gtk_text_buffer_get_end_iter(buffer, &end_iter);
                        gtk_text_buffer_insert_paintable(buffer,
                            &end_iter, paintable);
                        insert_newline(&state);
                        g_object_unref(tex);
                    } else if (info && (strcmp(info, "dot") == 0 ||
                               strcmp(info, "graphviz") == 0 ||
                               strcmp(info, "mermaid") == 0)) {
                        /* Diagram failed — show as code block */
                        ensure_newline(&state);
                        push_tag(&state, "md-code-block");
                        insert_text(&state, code ? code : "");
                        pop_tag(&state);
                    } else {
                        ensure_newline(&state);
                        push_tag(&state, "md-code-block");
                        insert_text(&state, code ? code : "");
                        pop_tag(&state);
                    }
                    break;
                }
                case CMARK_NODE_BLOCK_QUOTE:
                    state.in_blockquote = TRUE;
                    break;
                case CMARK_NODE_LIST:
                    state.list_depth++;
                    if (cmark_node_get_list_type(node) == CMARK_ORDERED_LIST) {
                        state.in_ordered_list = TRUE;
                        state.list_item_number = cmark_node_get_list_start(node);
                    } else {
                        state.in_ordered_list = FALSE;
                        state.list_item_number = 0;
                    }
                    break;
                case CMARK_NODE_ITEM:
                    ensure_newline(&state);
                    push_tag(&state, "md-list-item");
                    if (state.in_ordered_list) {
                        char prefix[16];
                        snprintf(prefix, sizeof(prefix), "%d. ",
                                 state.list_item_number++);
                        insert_text(&state, prefix);
                    } else {
                        insert_text(&state, "  • ");
                    }
                    break;
                case CMARK_NODE_LINK:
                    push_tag(&state, "md-link");
                    break;
                case CMARK_NODE_IMAGE:
                    /* Show alt text as placeholder */
                    push_tag(&state, "md-italic");
                    insert_text(&state, "[image: ");
                    break;
                case CMARK_NODE_THEMATIC_BREAK:
                    ensure_newline(&state);
                    push_tag(&state, "md-hr");
                    insert_text(&state, "────────────────────────────────");
                    pop_tag(&state);
                    insert_newline(&state);
                    break;
                case CMARK_NODE_TEXT:
                    insert_text(&state, cmark_node_get_literal(node));
                    break;
                case CMARK_NODE_SOFTBREAK:
                    insert_text(&state, " ");
                    break;
                case CMARK_NODE_LINEBREAK:
                    insert_newline(&state);
                    break;
                default:
                    break;
            }
        } else if (ev == CMARK_EVENT_EXIT) {
            switch (type) {
                case CMARK_NODE_HEADING:
                    pop_tag(&state);
                    insert_newline(&state);
                    break;
                case CMARK_NODE_PARAGRAPH:
                    if (state.in_blockquote)
                        pop_tag(&state);
                    insert_newline(&state);
                    break;
                case CMARK_NODE_STRONG:
                case CMARK_NODE_EMPH:
                case CMARK_NODE_LINK:
                    pop_tag(&state);
                    break;
                case CMARK_NODE_IMAGE:
                    insert_text(&state, "]");
                    pop_tag(&state);
                    break;
                case CMARK_NODE_BLOCK_QUOTE:
                    state.in_blockquote = FALSE;
                    break;
                case CMARK_NODE_LIST:
                    state.list_depth--;
                    break;
                case CMARK_NODE_ITEM:
                    pop_tag(&state);
                    break;
                default:
                    break;
            }
        }
    }

    cmark_iter_free(iter);
    cmark_node_free(doc);
    cmark_parser_free(parser);
}
