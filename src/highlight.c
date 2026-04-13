#include "highlight.h"
#include <string.h>

/* --- Tag definitions --- */

typedef struct {
    const char *name;
    const char *fg;
    gboolean    bold;
    gboolean    italic;
    double      scale;
} TagDef;

static const TagDef tag_defs[] = {
    {"hl-heading1",   "#e06c75", TRUE,  FALSE, 1.6},
    {"hl-heading2",   "#e06c75", TRUE,  FALSE, 1.4},
    {"hl-heading3",   "#e06c75", TRUE,  FALSE, 1.2},
    {"hl-heading",    "#e06c75", TRUE,  FALSE, 1.0},
    {"hl-bold",       NULL,      TRUE,  FALSE, 0},
    {"hl-italic",     NULL,      FALSE, TRUE,  0},
    {"hl-code",       "#98c379", FALSE, FALSE, 0},
    {"hl-link",       "#61afef", FALSE, FALSE, 0},
    {"hl-link-url",   "#5c6370", FALSE, FALSE, 0},
    {"hl-list",       "#d19a66", TRUE,  FALSE, 0},
    {"hl-blockquote", "#5c6370", FALSE, TRUE,  0},
    {"hl-keyword",    "#c678dd", TRUE,  FALSE, 0},
    {"hl-type",       "#e5c07b", FALSE, FALSE, 0},
    {"hl-string",     "#98c379", FALSE, FALSE, 0},
    {"hl-number",     "#d19a66", FALSE, FALSE, 0},
    {"hl-comment",    "#5c6370", FALSE, TRUE,  0},
    {"hl-preproc",    "#c678dd", FALSE, FALSE, 0},
    {"hl-function",   "#61afef", FALSE, FALSE, 0},
    {"hl-operator",   "#56b6c2", FALSE, FALSE, 0},
    {"hl-punct",      "#abb2bf", FALSE, FALSE, 0},
    {"hl-key",        "#e06c75", FALSE, FALSE, 0},
    {NULL, NULL, FALSE, FALSE, 0}
};

void highlight_create_tags(GtkTextBuffer *buffer) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    for (int i = 0; tag_defs[i].name; i++) {
        if (gtk_text_tag_table_lookup(table, tag_defs[i].name))
            continue;
        GtkTextTag *tag = gtk_text_buffer_create_tag(buffer, tag_defs[i].name, NULL);
        if (tag_defs[i].fg)
            g_object_set(tag, "foreground", tag_defs[i].fg, NULL);
        if (tag_defs[i].bold)
            g_object_set(tag, "weight", PANGO_WEIGHT_BOLD, NULL);
        if (tag_defs[i].italic)
            g_object_set(tag, "style", PANGO_STYLE_ITALIC, NULL);
        if (tag_defs[i].scale > 0 && tag_defs[i].scale != 1.0)
            g_object_set(tag, "scale", tag_defs[i].scale, NULL);
    }
}

/* --- Rule-based highlighting --- */

typedef struct {
    const char *tag_name;
    const char *pattern;
    int         group;
} Rule;

/* Markdown rules */
static const Rule md_rules[] = {
    {"hl-heading1",   "^# .+$",           0},
    {"hl-heading2",   "^## .+$",          0},
    {"hl-heading3",   "^### .+$",         0},
    {"hl-heading",    "^#{4,6} .+$",      0},
    {"hl-bold",       "\\*\\*[^*]+\\*\\*",  0},
    {"hl-bold",       "__[^_]+__",          0},
    {"hl-italic",     "(?<![*])\\*(?![*])[^*\n]+\\*(?![*])", 0},
    {"hl-italic",     "(?<!_)_(?!_)[^_\n]+_(?!_)", 0},
    {"hl-code",       "```[^`]*```",         0},
    {"hl-code",       "`[^`\n]+`",          0},
    {"hl-link",       "\\[[^\\]]+\\]",      0},
    {"hl-link-url",   "\\]\\([^)]+\\)",     0},
    {"hl-list",       "^\\s*[-*+] ",        0},
    {"hl-list",       "^\\s*\\d+\\. ",      0},
    {"hl-blockquote", "^>.*$",              0},
    {NULL, NULL, 0}
};

/* C rules */
static const Rule c_rules[] = {
    {"hl-preproc",  "^\\s*#\\s*\\w+",   0},
    {"hl-keyword",  "\\b(auto|break|case|const|continue|default|do|else|enum|extern|for|goto|if|inline|register|restrict|return|sizeof|static|struct|switch|typedef|union|volatile|while|_Alignas|_Alignof|_Atomic|_Generic|_Noreturn|_Static_assert|_Thread_local)\\b", 0},
    {"hl-type",     "\\b(void|char|short|int|long|float|double|signed|unsigned|bool|size_t|ssize_t|int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|gboolean|gchar|gint|guint|gpointer|GtkWidget|GtkTextBuffer|GtkTextView|GtkTextIter|GtkTextTag|GtkTextMark|GtkLabel|GtkBox|GtkButton|GdkRGBA|GString|GRegex|GMatchInfo)\\b", 0},
    {"hl-number",   "\\b(0[xX][0-9a-fA-F]+|0[bB][01]+|[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?[fFlLuU]*)\\b", 0},
    {"hl-string",   "\"([^\"\\\\]|\\\\.)*\"", 0},
    {"hl-string",   "'([^'\\\\]|\\\\.)*'",    0},
    {"hl-comment",  "//.*$",               0},
    {"hl-comment",  "/\\*[\\s\\S]*?\\*/",  0},
    {NULL, NULL, 0}
};

/* Python rules */
static const Rule py_rules[] = {
    {"hl-keyword",  "\\b(and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\\b", 0},
    {"hl-type",     "\\b(True|False|None|int|float|str|list|dict|tuple|set|bool|bytes|type|object)\\b", 0},
    {"hl-function", "\\b(print|len|range|enumerate|zip|map|filter|sorted|reversed|open|isinstance|hasattr|getattr|setattr|super)\\b", 0},
    {"hl-number",   "\\b(0[xX][0-9a-fA-F]+|0[oO][0-7]+|0[bB][01]+|[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?j?)\\b", 0},
    {"hl-string",   "\"\"\"[\\s\\S]*?\"\"\"", 0},
    {"hl-string",   "'''[\\s\\S]*?'''",       0},
    {"hl-string",   "\"([^\"\\\\]|\\\\.)*\"", 0},
    {"hl-string",   "'([^'\\\\]|\\\\.)*'",    0},
    {"hl-comment",  "#.*$",                    0},
    {NULL, NULL, 0}
};

/* JavaScript rules */
static const Rule js_rules[] = {
    {"hl-keyword",  "\\b(async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|export|extends|finally|for|from|function|if|import|in|instanceof|let|new|of|return|super|switch|this|throw|try|typeof|var|void|while|with|yield)\\b", 0},
    {"hl-type",     "\\b(true|false|null|undefined|NaN|Infinity|Array|Object|String|Number|Boolean|Map|Set|Promise|Symbol|BigInt)\\b", 0},
    {"hl-function", "\\b(console|document|window|Math|JSON|parseInt|parseFloat|setTimeout|setInterval|fetch|require)\\b", 0},
    {"hl-number",   "\\b(0[xX][0-9a-fA-F]+|0[oO][0-7]+|0[bB][01]+|[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?n?)\\b", 0},
    {"hl-string",   "`[^`]*`",                0},
    {"hl-string",   "\"([^\"\\\\]|\\\\.)*\"", 0},
    {"hl-string",   "'([^'\\\\]|\\\\.)*'",    0},
    {"hl-comment",  "//.*$",                   0},
    {"hl-comment",  "/\\*[\\s\\S]*?\\*/",      0},
    {NULL, NULL, 0}
};

/* JSON rules */
static const Rule json_rules[] = {
    {"hl-key",      "\"[^\"]*\"\\s*:",    0},
    {"hl-string",   ":\\s*\"([^\"\\\\]|\\\\.)*\"", 0},
    {"hl-number",   "\\b-?[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?\\b", 0},
    {"hl-type",     "\\b(true|false|null)\\b", 0},
    {NULL, NULL, 0}
};

static const Rule *lang_rules[] = {
    [LANG_PLAIN_TEXT]  = NULL,
    [LANG_MARKDOWN]    = md_rules,
    [LANG_C]           = c_rules,
    [LANG_PYTHON]      = py_rules,
    [LANG_JAVASCRIPT]  = js_rules,
    [LANG_JSON]        = json_rules,
};

/* Cached compiled regexes */
static GRegex *compiled[LANG_COUNT][32];
static gboolean compiled_init[LANG_COUNT];

static void ensure_compiled(HighlightLanguage lang) {
    if (compiled_init[lang]) return;
    compiled_init[lang] = TRUE;
    const Rule *rules = lang_rules[lang];
    if (!rules) return;
    for (int i = 0; rules[i].pattern; i++) {
        GRegexCompileFlags flags = G_REGEX_MULTILINE;
        compiled[lang][i] = g_regex_new(rules[i].pattern, flags, 0, NULL);
    }
}

/* Remove all highlight tags */
static void remove_hl_tags(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    for (int i = 0; tag_defs[i].name; i++) {
        GtkTextTag *tag = gtk_text_tag_table_lookup(table, tag_defs[i].name);
        if (tag)
            gtk_text_buffer_remove_tag(buffer, tag, start, end);
    }
}

void highlight_apply(GtkTextBuffer *buffer, HighlightLanguage lang) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    remove_hl_tags(buffer, &start, &end);

    if (lang == LANG_PLAIN_TEXT || lang >= LANG_COUNT)
        return;

    const Rule *rules = lang_rules[lang];
    if (!rules) return;

    ensure_compiled(lang);

    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    int text_len = strlen(text);
    if (text_len == 0) { g_free(text); return; }

    /* Build byte-to-char offset map */
    int *byte_to_char = g_malloc(sizeof(int) * (text_len + 1));
    int ci = 0;
    for (int bi = 0; bi <= text_len; ) {
        byte_to_char[bi] = ci;
        if (bi == text_len) break;
        gunichar ch = g_utf8_get_char(text + bi);
        int clen = g_unichar_to_utf8(ch, NULL);
        for (int j = 1; j < clen && bi + j <= text_len; j++)
            byte_to_char[bi + j] = ci;
        bi += clen;
        ci++;
    }

    for (int i = 0; rules[i].pattern; i++) {
        GRegex *re = compiled[lang][i];
        if (!re) continue;
        GMatchInfo *mi = NULL;
        g_regex_match(re, text, 0, &mi);
        while (g_match_info_matches(mi)) {
            int s_byte, e_byte;
            if (!g_match_info_fetch_pos(mi, rules[i].group, &s_byte, &e_byte)) {
                g_match_info_next(mi, NULL);
                continue;
            }
            int s_char = byte_to_char[s_byte];
            int e_char = byte_to_char[e_byte];
            GtkTextIter si, ei;
            gtk_text_buffer_get_iter_at_offset(buffer, &si, s_char);
            gtk_text_buffer_get_iter_at_offset(buffer, &ei, e_char);
            gtk_text_buffer_apply_tag_by_name(buffer, rules[i].tag_name, &si, &ei);
            g_match_info_next(mi, NULL);
        }
        g_match_info_free(mi);
    }

    g_free(byte_to_char);
    g_free(text);
}

HighlightLanguage highlight_detect(const char *path) {
    if (!path || !*path) return LANG_PLAIN_TEXT;
    if (g_str_has_suffix(path, ".md") || g_str_has_suffix(path, ".markdown") ||
        g_str_has_suffix(path, ".mdx"))
        return LANG_MARKDOWN;
    if (g_str_has_suffix(path, ".c") || g_str_has_suffix(path, ".h") ||
        g_str_has_suffix(path, ".cpp") || g_str_has_suffix(path, ".hpp") ||
        g_str_has_suffix(path, ".cc"))
        return LANG_C;
    if (g_str_has_suffix(path, ".py") || g_str_has_suffix(path, ".pyw"))
        return LANG_PYTHON;
    if (g_str_has_suffix(path, ".js") || g_str_has_suffix(path, ".jsx") ||
        g_str_has_suffix(path, ".ts") || g_str_has_suffix(path, ".tsx") ||
        g_str_has_suffix(path, ".mjs"))
        return LANG_JAVASCRIPT;
    if (g_str_has_suffix(path, ".json") || g_str_has_suffix(path, ".jsonc"))
        return LANG_JSON;
    return LANG_PLAIN_TEXT;
}
