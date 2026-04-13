#include "diagrams.h"
#include <graphviz/gvc.h>
#include <graphviz/cgraph.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

/* ── Graphviz rendering ────────────────────────────────────────── */

GdkTexture *diagrams_render_dot(const char *dot_source, int max_width) {
    if (!dot_source || dot_source[0] == '\0') return NULL;
    (void)max_width;

    GVC_t *gvc = gvContext();
    if (!gvc) return NULL;

    Agraph_t *graph = agmemread(dot_source);
    if (!graph) {
        gvFreeContext(gvc);
        return NULL;
    }

    gvLayout(gvc, graph, "dot");

    char *data = NULL;
    unsigned int length = 0;
    gvRenderData(gvc, graph, "png", &data, &length);

    GdkTexture *texture = NULL;
    if (data && length > 0) {
        GBytes *bytes = g_bytes_new(data, length);
        texture = gdk_texture_new_from_bytes(bytes, NULL);
        g_bytes_unref(bytes);
    }

    if (data) gvFreeRenderData(data);
    gvFreeLayout(gvc, graph);
    agclose(graph);
    gvFreeContext(gvc);

    return texture;
}

/* ── Mermaid → Graphviz translation ────────────────────────────── */

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static gboolean starts_with_ci(const char *line, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*line) != tolower((unsigned char)*prefix))
            return FALSE;
        line++; prefix++;
    }
    return TRUE;
}

/* Escape for graphviz label inside double quotes */
static void append_escaped(GString *out, const char *s, int len) {
    for (int i = 0; i < len; i++) {
        if (s[i] == '"' || s[i] == '\\')
            g_string_append_c(out, '\\');
        g_string_append_c(out, s[i]);
    }
}

/* Generate a safe graphviz node id from arbitrary text */
static char *make_node_id(const char *raw) {
    GString *id = g_string_new("n_");
    for (const char *p = raw; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '_')
            g_string_append_c(id, *p);
        else
            g_string_append_printf(id, "_%02x", (unsigned char)*p);
    }
    return g_string_free(id, FALSE);
}

typedef struct {
    char *id;        /* graphviz-safe id */
    char *label;     /* display label */
    char *shape;     /* graphviz shape */
} MermaidNode;

typedef struct {
    char *from;      /* node id */
    char *to;        /* node id */
    char *label;     /* edge label or NULL */
    char *style;     /* "solid", "dashed", "bold" */
    gboolean bidir;  /* bidirectional */
} MermaidEdge;

typedef struct {
    GPtrArray *nodes;     /* MermaidNode* */
    GPtrArray *edges;     /* MermaidEdge* */
    GHashTable *node_map; /* raw_name -> MermaidNode* */
    const char *rankdir;
    gboolean is_directed;
    /* Subgraph stack */
    GPtrArray *subgraph_stack; /* GString* for subgraph names */
    int subgraph_id;
} MermaidCtx;

static MermaidNode *ctx_get_or_create_node(MermaidCtx *ctx, const char *raw_name) {
    MermaidNode *n = g_hash_table_lookup(ctx->node_map, raw_name);
    if (n) return n;

    n = g_new0(MermaidNode, 1);
    n->id = make_node_id(raw_name);
    n->label = g_strdup(raw_name);
    n->shape = g_strdup("box");
    g_ptr_array_add(ctx->nodes, n);
    g_hash_table_insert(ctx->node_map, g_strdup(raw_name), n);
    return n;
}

static void node_free(gpointer p) {
    MermaidNode *n = p;
    g_free(n->id); g_free(n->label); g_free(n->shape); g_free(n);
}

static void edge_free(gpointer p) {
    MermaidEdge *e = p;
    g_free(e->from); g_free(e->to); g_free(e->label); g_free(e->style); g_free(e);
}

/*
 * Parse a node reference: ID optionally followed by shape+label
 * Shapes: [label] = box, (label) = ellipse, {label} = diamond,
 *         ((label)) = doublecircle, [[label]] = box3d, [/label/] = parallelogram,
 *         [\label\] = inv parallelogram, [(label)] = cylinder,
 *         >label] = house, ({label}) = hexagon (approx)
 * Returns chars consumed, 0 on failure.
 */
static int parse_node_ref(MermaidCtx *ctx, const char *s, MermaidNode **out) {
    const char *p = s;

    /* Parse node id: alphanumeric, underscore, can't start with digit? Actually mermaid allows it */
    const char *id_start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
        p++;

    if (p == id_start) return 0;

    char *raw_name = g_strndup(id_start, (gsize)(p - id_start));
    MermaidNode *node = ctx_get_or_create_node(ctx, raw_name);

    /* Check for shape+label */
    const char *shape = NULL;
    char open_char = *p;
    char close_seq[4] = {0};
    int open_len = 1;

    switch (open_char) {
        case '[':
            if (p[1] == '[') { shape = "box3d"; open_len = 2; close_seq[0] = ']'; close_seq[1] = ']'; }
            else if (p[1] == '(') { shape = "cylinder"; open_len = 2; close_seq[0] = ')'; close_seq[1] = ']'; }
            else if (p[1] == '/') { shape = "parallelogram"; open_len = 2; close_seq[0] = '/'; close_seq[1] = ']'; }
            else if (p[1] == '\\') { shape = "parallelogram"; open_len = 2; close_seq[0] = '\\'; close_seq[1] = ']'; }
            else { shape = "box"; close_seq[0] = ']'; }
            break;
        case '(':
            if (p[1] == '(') { shape = "doublecircle"; open_len = 2; close_seq[0] = ')'; close_seq[1] = ')'; }
            else if (p[1] == '[') { shape = "cylinder"; open_len = 2; close_seq[0] = ']'; close_seq[1] = ')'; }
            else { shape = "ellipse"; close_seq[0] = ')'; }
            break;
        case '{':
            if (p[1] == '{') { shape = "hexagon"; open_len = 2; close_seq[0] = '}'; close_seq[1] = '}'; }
            else { shape = "diamond"; close_seq[0] = '}'; }
            break;
        case '>':
            shape = "house"; close_seq[0] = ']';
            break;
        default:
            /* No shape, just bare id */
            *out = node;
            g_free(raw_name);
            return (int)(p - s);
    }

    p += open_len;
    const char *label_start = p;

    /* Find closing sequence */
    int close_len = (int)strlen(close_seq);
    const char *label_end = NULL;
    while (*p) {
        if (strncmp(p, close_seq, (size_t)close_len) == 0) {
            label_end = p;
            break;
        }
        p++;
    }

    if (label_end) {
        g_free(node->label);
        node->label = g_strndup(label_start, (gsize)(label_end - label_start));
        g_free(node->shape);
        node->shape = g_strdup(shape);
        p += close_len;
    }

    *out = node;
    g_free(raw_name);
    return (int)(p - s);
}

/*
 * Parse arrow: -->, --->, -...->, ====>, <-->, -.->
 * Returns chars consumed. Sets edge style, label, direction.
 */
static int parse_arrow(const char *s, char **label, char **style, gboolean *bidir) {
    const char *p = s;
    *label = NULL;
    *style = NULL;
    *bidir = FALSE;

    /* Leading < for bidirectional */
    if (*p == '<') { *bidir = TRUE; p++; }

    /* Must start with - or = */
    if (*p != '-' && *p != '=') return 0;
    char arrow_char = *p;

    /* Determine style */
    if (arrow_char == '=') *style = g_strdup("bold");

    /* Consume body of arrow */
    gboolean has_dot = FALSE;
    const char *body_start = p;
    while (*p == '-' || *p == '=' || *p == '.') {
        if (*p == '.') has_dot = TRUE;
        p++;
    }

    if (has_dot && !*style) *style = g_strdup("dashed");
    if (!*style) *style = g_strdup("solid");

    /* Check for |label| */
    if (*p == '|') {
        p++;
        const char *lstart = p;
        while (*p && *p != '|') p++;
        if (*p == '|') {
            *label = g_strndup(lstart, (gsize)(p - lstart));
            p++;
        }
    }

    /* More arrow body after label */
    while (*p == '-' || *p == '=' || *p == '.') p++;

    /* Arrow head */
    if (*p == '>') p++;
    else if (*p == 'x' || *p == 'o') p++;

    int consumed = (int)(p - s);
    /* Need at least 2 chars for smallest arrow -- */
    if (consumed < 2) {
        g_free(*label); *label = NULL;
        g_free(*style); *style = NULL;
        return 0;
    }

    /* Also check we had at least -- or == */
    if ((int)(p - body_start) < 2 && !*bidir) {
        g_free(*label); *label = NULL;
        g_free(*style); *style = NULL;
        return 0;
    }

    return consumed;
}

/* Parse a single line, possibly containing chained edges: A --> B --> C */
static void parse_line(MermaidCtx *ctx, const char *line) {
    const char *p = skip_ws(line);

    /* Skip empty, comments, directives */
    if (*p == '\0' || *p == '%') return;
    if (*p == ';') return;

    /* Skip known directives */
    if (starts_with_ci(p, "style ") || starts_with_ci(p, "class ") ||
        starts_with_ci(p, "click ") || starts_with_ci(p, "linkstyle ") ||
        starts_with_ci(p, "classDef ") || starts_with_ci(p, "direction "))
        return;

    /* Handle subgraph */
    if (starts_with_ci(p, "subgraph")) {
        p += 8;
        p = skip_ws(p);
        /* Rest is subgraph label (we store it but keep rendering flat) */
        ctx->subgraph_id++;
        return;
    }
    if (starts_with_ci(p, "end")) {
        if (p[3] == '\0' || p[3] == ' ' || p[3] == '\t' || p[3] == ';')
            return;
    }

    /* Try to parse chain: NodeA --> NodeB --> NodeC */
    MermaidNode *prev_node = NULL;

    while (*p) {
        p = skip_ws(p);
        if (*p == '\0' || *p == ';' || *p == '%') break;

        /* Parse node reference */
        MermaidNode *node = NULL;
        int n = parse_node_ref(ctx, p, &node);
        if (n == 0) break;
        p += n;

        if (prev_node && node) {
            /* We got here through a chain without arrow?
             * Actually this shouldn't happen, the arrow parse below handles it */
        }

        if (!prev_node) {
            prev_node = node;
        }

        p = skip_ws(p);
        if (*p == '\0' || *p == ';' || *p == '%') break;

        /* Try to parse arrow */
        char *edge_label = NULL, *edge_style = NULL;
        gboolean bidir = FALSE;
        int arrow_len = parse_arrow(p, &edge_label, &edge_style, &bidir);
        if (arrow_len == 0) break;
        p += arrow_len;

        p = skip_ws(p);

        /* Parse target node */
        MermaidNode *target = NULL;
        n = parse_node_ref(ctx, p, &target);
        if (n == 0) {
            g_free(edge_label); g_free(edge_style);
            break;
        }
        p += n;

        /* Create edge */
        MermaidEdge *edge = g_new0(MermaidEdge, 1);
        edge->from = g_strdup(prev_node->id);
        edge->to = g_strdup(target->id);
        edge->label = edge_label;
        edge->style = edge_style;
        edge->bidir = bidir;
        g_ptr_array_add(ctx->edges, edge);

        prev_node = target;

        p = skip_ws(p);
        /* Check for & (parallel edges: A --> B & C) */
        if (*p == '&') {
            p++;
            continue; /* Will parse next node and create edge from same prev */
        }
    }
}

/* Convert parsed context to dot string */
static char *ctx_to_dot(MermaidCtx *ctx) {
    GString *dot = g_string_new(NULL);

    g_string_append_printf(dot, "%s G {\n",
        ctx->is_directed ? "digraph" : "graph");
    g_string_append_printf(dot, "  rankdir=%s;\n", ctx->rankdir);
    g_string_append(dot,
        "  node [fontname=\"Sans\", fontsize=11, style=rounded];\n"
        "  edge [fontname=\"Sans\", fontsize=10];\n"
        "  bgcolor=transparent;\n");

    /* Emit nodes */
    for (guint i = 0; i < ctx->nodes->len; i++) {
        MermaidNode *n = g_ptr_array_index(ctx->nodes, i);
        g_string_append_printf(dot, "  %s [label=\"", n->id);
        append_escaped(dot, n->label, (int)strlen(n->label));
        g_string_append_printf(dot, "\", shape=%s];\n", n->shape);
    }

    /* Emit edges */
    const char *edge_op = ctx->is_directed ? " -> " : " -- ";
    for (guint i = 0; i < ctx->edges->len; i++) {
        MermaidEdge *e = g_ptr_array_index(ctx->edges, i);
        g_string_append_printf(dot, "  %s%s%s", e->from, edge_op, e->to);

        /* Edge attributes */
        gboolean has_attr = FALSE;
        GString *attrs = g_string_new(NULL);

        if (e->label && e->label[0]) {
            g_string_append(attrs, "label=\"");
            append_escaped(attrs, e->label, (int)strlen(e->label));
            g_string_append_c(attrs, '"');
            has_attr = TRUE;
        }
        if (e->style && strcmp(e->style, "dashed") == 0) {
            if (has_attr) g_string_append(attrs, ", ");
            g_string_append(attrs, "style=dashed");
            has_attr = TRUE;
        } else if (e->style && strcmp(e->style, "bold") == 0) {
            if (has_attr) g_string_append(attrs, ", ");
            g_string_append(attrs, "penwidth=2.0");
            has_attr = TRUE;
        }
        if (e->bidir && ctx->is_directed) {
            if (has_attr) g_string_append(attrs, ", ");
            g_string_append(attrs, "dir=both");
            has_attr = TRUE;
        }

        if (has_attr)
            g_string_append_printf(dot, " [%s]", attrs->str);

        g_string_append(dot, ";\n");
        g_string_free(attrs, TRUE);
    }

    g_string_append(dot, "}\n");
    return g_string_free(dot, FALSE);
}

static char *mermaid_to_dot(const char *source) {
    MermaidCtx ctx = {0};
    ctx.nodes = g_ptr_array_new_with_free_func(node_free);
    ctx.edges = g_ptr_array_new_with_free_func(edge_free);
    ctx.node_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx.rankdir = "TB";
    ctx.is_directed = TRUE;

    gchar **lines = g_strsplit(source, "\n", -1);

    for (int i = 0; lines[i]; i++) {
        const char *line = skip_ws(lines[i]);
        if (line[0] == '\0') continue;

        /* First non-empty line: header */
        if (i == 0 || (starts_with_ci(line, "graph") || starts_with_ci(line, "flowchart"))) {
            if (starts_with_ci(line, "graph") || starts_with_ci(line, "flowchart")) {
                /* Detect direction */
                if (strstr(line, " LR") || strstr(line, " lr")) ctx.rankdir = "LR";
                else if (strstr(line, " RL") || strstr(line, " rl")) ctx.rankdir = "RL";
                else if (strstr(line, " BT") || strstr(line, " bt")) ctx.rankdir = "BT";
                else ctx.rankdir = "TB";

                /* graph (undirected) vs flowchart/digraph */
                if (starts_with_ci(line, "graph"))
                    ctx.is_directed = TRUE; /* mermaid graph is actually directed */
                continue;
            }
        }

        /* Handle lines that may contain ; separated statements */
        gchar **stmts = g_strsplit(line, ";", -1);
        for (int j = 0; stmts[j]; j++) {
            parse_line(&ctx, stmts[j]);
        }
        g_strfreev(stmts);
    }

    g_strfreev(lines);

    char *dot = ctx_to_dot(&ctx);

    g_ptr_array_free(ctx.nodes, TRUE);
    g_ptr_array_free(ctx.edges, TRUE);
    g_hash_table_destroy(ctx.node_map);

    return dot;
}

/* ── Sequence diagram → dot ────────────────────────────────────── */

typedef struct {
    GPtrArray *participants; /* char* names, ordered */
    GHashTable *part_set;   /* name -> index (dedup) */
    GPtrArray *messages;    /* "from|to|label|type" strings */
} SeqCtx;

static void seq_ensure_participant(SeqCtx *ctx, const char *name) {
    if (g_hash_table_contains(ctx->part_set, name)) return;
    g_hash_table_insert(ctx->part_set, g_strdup(name),
                        GINT_TO_POINTER((gint)ctx->participants->len));
    g_ptr_array_add(ctx->participants, g_strdup(name));
}

static char *sequence_to_dot(const char *source) {
    SeqCtx ctx = {0};
    ctx.participants = g_ptr_array_new_with_free_func(g_free);
    ctx.part_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx.messages = g_ptr_array_new_with_free_func(g_free);

    gchar **lines = g_strsplit(source, "\n", -1);

    for (int i = 0; lines[i]; i++) {
        const char *line = skip_ws(lines[i]);
        if (line[0] == '\0' || line[0] == '%') continue;
        if (starts_with_ci(line, "sequencediagram")) continue;
        if (starts_with_ci(line, "participant ") || starts_with_ci(line, "actor ")) {
            const char *name = skip_ws(line + (starts_with_ci(line, "actor") ? 5 : 11));
            /* Trim trailing whitespace */
            char *n = g_strstrip(g_strdup(name));
            /* Handle "as" alias */
            char *as = strstr(n, " as ");
            if (as) *as = '\0';
            seq_ensure_participant(&ctx, n);
            g_free(n);
            continue;
        }

        /* Parse message: A ->> B: text  or  A -->> B: text  etc. */
        const char *arrow = strstr(line, "->>");
        if (!arrow) arrow = strstr(line, "-->>");
        if (!arrow) arrow = strstr(line, "-->");
        if (!arrow) arrow = strstr(line, "->");
        if (!arrow) arrow = strstr(line, "-)");
        if (!arrow) continue;

        /* Extract from */
        char *from = g_strndup(line, (gsize)(arrow - line));
        g_strstrip(from);

        /* Skip arrow */
        const char *after = arrow;
        while (*after == '-' || *after == '>' || *after == ')') after++;
        after = skip_ws(after);

        /* Find colon separator */
        const char *colon = strchr(after, ':');
        char *to = NULL;
        char *msg = NULL;
        if (colon) {
            to = g_strndup(after, (gsize)(colon - after));
            g_strstrip(to);
            msg = g_strstrip(g_strdup(colon + 1));
        } else {
            to = g_strstrip(g_strdup(after));
            msg = g_strdup("");
        }

        seq_ensure_participant(&ctx, from);
        seq_ensure_participant(&ctx, to);

        char *entry = g_strdup_printf("%s|%s|%s", from, to, msg);
        g_ptr_array_add(ctx.messages, entry);

        g_free(from); g_free(to); g_free(msg);
    }
    g_strfreev(lines);

    /* Build dot: use a record-based left-to-right graph */
    GString *dot = g_string_new("digraph seq {\n");
    g_string_append(dot, "  rankdir=LR;\n");
    g_string_append(dot, "  node [shape=box, fontname=\"Sans\", fontsize=11, style=rounded];\n");
    g_string_append(dot, "  edge [fontname=\"Sans\", fontsize=10];\n");

    /* Emit participants in order with rank constraint */
    g_string_append(dot, "  { rank=same;\n");
    for (guint i = 0; i < ctx.participants->len; i++) {
        const char *name = g_ptr_array_index(ctx.participants, i);
        char *id = make_node_id(name);
        g_string_append_printf(dot, "    %s [label=\"", id);
        append_escaped(dot, name, (int)strlen(name));
        g_string_append(dot, "\"];\n");
        g_free(id);
    }
    g_string_append(dot, "  }\n");

    /* Hidden edges to enforce participant order */
    for (guint i = 0; i + 1 < ctx.participants->len; i++) {
        char *id1 = make_node_id(g_ptr_array_index(ctx.participants, i));
        char *id2 = make_node_id(g_ptr_array_index(ctx.participants, i + 1));
        g_string_append_printf(dot, "  %s -> %s [style=invis];\n", id1, id2);
        g_free(id1); g_free(id2);
    }

    /* Emit messages as edges */
    for (guint i = 0; i < ctx.messages->len; i++) {
        const char *entry = g_ptr_array_index(ctx.messages, i);
        gchar **parts = g_strsplit(entry, "|", 3);
        if (parts[0] && parts[1]) {
            char *fid = make_node_id(parts[0]);
            char *tid = make_node_id(parts[1]);
            g_string_append_printf(dot, "  %s -> %s", fid, tid);
            if (parts[2] && parts[2][0]) {
                g_string_append(dot, " [label=\"");
                append_escaped(dot, parts[2], (int)strlen(parts[2]));
                g_string_append(dot, "\"]");
            }
            g_string_append(dot, ";\n");
            g_free(fid); g_free(tid);
        }
        g_strfreev(parts);
    }

    g_string_append(dot, "}\n");

    g_ptr_array_free(ctx.participants, TRUE);
    g_hash_table_destroy(ctx.part_set);
    g_ptr_array_free(ctx.messages, TRUE);

    return g_string_free(dot, FALSE);
}

/* ── Public API ────────────────────────────────────────────────── */

GdkTexture *diagrams_render_mermaid(const char *mermaid_source, int max_width) {
    if (!mermaid_source || mermaid_source[0] == '\0') return NULL;

    const char *s = skip_ws(mermaid_source);
    char *dot = NULL;

    if (starts_with_ci(s, "graph") || starts_with_ci(s, "flowchart")) {
        dot = mermaid_to_dot(mermaid_source);
    } else if (starts_with_ci(s, "sequencediagram") || starts_with_ci(s, "sequence")) {
        dot = sequence_to_dot(mermaid_source);
    } else {
        /* Unsupported diagram type — try as flowchart anyway */
        dot = mermaid_to_dot(mermaid_source);
    }

    if (!dot) return NULL;

    GdkTexture *tex = diagrams_render_dot(dot, max_width);
    g_free(dot);
    return tex;
}
