#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <glib.h>
#include <glib/gstdio.h>

/* ── Schema ─────────────────────────────────────────────────────── */

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS notes ("
    "  filepath TEXT PRIMARY KEY,"
    "  title TEXT,"
    "  content TEXT,"
    "  mtime INTEGER,"
    "  tags TEXT"  /* comma-separated #tags */
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5("
    "  title, content, tags,"
    "  content='notes',"
    "  content_rowid='rowid'"
    ");"
    /* Triggers to keep FTS in sync */
    "CREATE TRIGGER IF NOT EXISTS notes_ai AFTER INSERT ON notes BEGIN"
    "  INSERT INTO notes_fts(rowid, title, content, tags)"
    "  VALUES (new.rowid, new.title, new.content, new.tags);"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS notes_ad AFTER DELETE ON notes BEGIN"
    "  INSERT INTO notes_fts(notes_fts, rowid, title, content, tags)"
    "  VALUES ('delete', old.rowid, old.title, old.content, old.tags);"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS notes_au AFTER UPDATE ON notes BEGIN"
    "  INSERT INTO notes_fts(notes_fts, rowid, title, content, tags)"
    "  VALUES ('delete', old.rowid, old.title, old.content, old.tags);"
    "  INSERT INTO notes_fts(rowid, title, content, tags)"
    "  VALUES (new.rowid, new.title, new.content, new.tags);"
    "END;";

/* ── Tag extraction ─────────────────────────────────────────────── */

static char *extract_tags_csv(const char *content) {
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GString *csv = g_string_new(NULL);
    const char *p = content;

    while ((p = strchr(p, '#')) != NULL) {
        /* must be at start of text or preceded by whitespace */
        if (p != content && !isspace((unsigned char)*(p - 1))) {
            p++;
            continue;
        }
        p++; /* skip '#' */
        const char *start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
            p++;
        int len = (int)(p - start);
        if (len == 0) continue;

        char *tag = g_strndup(start, len);
        /* lowercase for normalization */
        for (char *c = tag; *c; c++) *c = tolower((unsigned char)*c);

        if (!g_hash_table_contains(seen, tag)) {
            g_hash_table_add(seen, g_strdup(tag));
            if (csv->len > 0) g_string_append_c(csv, ',');
            g_string_append(csv, tag);
        }
        g_free(tag);
    }

    g_hash_table_destroy(seen);
    return g_string_free(csv, FALSE);
}

static char **split_tags(const char *csv, int *count) {
    *count = 0;
    if (!csv || csv[0] == '\0') return NULL;

    gchar **parts = g_strsplit(csv, ",", -1);
    int n = 0;
    while (parts[n]) n++;

    char **tags = g_new(char *, n + 1);
    for (int i = 0; i < n; i++)
        tags[i] = g_strdup(parts[i]);
    tags[n] = NULL;
    *count = n;
    g_strfreev(parts);
    return tags;
}

/* ── Title extraction ───────────────────────────────────────────── */

static char *extract_title(const char *content) {
    if (!content || content[0] == '\0')
        return g_strdup("(empty)");

    const char *end = strchr(content, '\n');
    int len = end ? (int)(end - content) : (int)strlen(content);
    if (len > 120) len = 120;
    if (len == 0) return g_strdup("(empty)");

    return g_strndup(content, len);
}

/* ── Database lifecycle ─────────────────────────────────────────── */

NotesDatabase *notes_db_open(const char *notes_directory) {
    (void)notes_directory;
    NotesDatabase *db = g_new0(NotesDatabase, 1);

    const char *config = g_get_user_config_dir();
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/notes-desktop", config);
    g_mkdir_with_parents(dir, 0755);
    snprintf(db->db_path, sizeof(db->db_path), "%s/notes_index.db", dir);

    int rc = sqlite3_open(db->db_path, &db->db);
    if (rc != SQLITE_OK) {
        g_warning("Failed to open database: %s", sqlite3_errmsg(db->db));
        g_free(db);
        return NULL;
    }

    /* Performance pragmas */
    sqlite3_exec(db->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    /* Create schema */
    char *err = NULL;
    rc = sqlite3_exec(db->db, SCHEMA_SQL, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        g_warning("Schema creation failed: %s", err);
        sqlite3_free(err);
    }

    return db;
}

void notes_db_close(NotesDatabase *db) {
    if (!db) return;
    if (db->db) sqlite3_close(db->db);
    g_free(db);
}

/* ── Index a single file ────────────────────────────────────────── */

void notes_db_index_file(NotesDatabase *db, const char *filepath) {
    if (!db || !filepath || filepath[0] == '\0') return;

    char *content = NULL;
    gsize len = 0;
    if (!g_file_get_contents(filepath, &content, &len, NULL)) return;

    struct stat st;
    if (stat(filepath, &st) != 0) { g_free(content); return; }

    char *title = extract_title(content);
    char *tags = extract_tags_csv(content);

    /* Upsert */
    const char *sql =
        "INSERT INTO notes (filepath, title, content, mtime, tags) "
        "VALUES (?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(filepath) DO UPDATE SET "
        "title=?2, content=?3, mtime=?4, tags=?5;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)st.st_mtime);
        sqlite3_bind_text(stmt, 5, tags, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    g_free(title);
    g_free(tags);
    g_free(content);
}

void notes_db_remove_file(NotesDatabase *db, const char *filepath) {
    if (!db || !filepath) return;
    const char *sql = "DELETE FROM notes WHERE filepath=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

/* ── Sync directory ─────────────────────────────────────────────── */

void notes_db_sync(NotesDatabase *db, const char *notes_directory) {
    if (!db || !notes_directory) return;

    sqlite3_exec(db->db, "BEGIN;", NULL, NULL, NULL);

    /* Collect existing filepaths from DB */
    GHashTable *db_files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    {
        const char *sql = "SELECT filepath, mtime FROM notes;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *fp = (const char *)sqlite3_column_text(stmt, 0);
                sqlite3_int64 mt = sqlite3_column_int64(stmt, 1);
                g_hash_table_insert(db_files, g_strdup(fp), GINT_TO_POINTER((gint)mt));
            }
            sqlite3_finalize(stmt);
        }
    }

    /* Scan directory */
    GHashTable *disk_files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GDir *dir = g_dir_open(notes_directory, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(name, ".txt")) continue;
            char *full = g_build_filename(notes_directory, name, NULL);
            g_hash_table_add(disk_files, g_strdup(full));

            struct stat st;
            if (stat(full, &st) == 0) {
                gpointer val = g_hash_table_lookup(db_files, full);
                time_t db_mtime = val ? (time_t)GPOINTER_TO_INT(val) : 0;
                if (!val || db_mtime != st.st_mtime) {
                    notes_db_index_file(db, full);
                }
            }
            g_free(full);
        }
        g_dir_close(dir);
    }

    /* Remove entries for deleted files */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, db_files);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (!g_hash_table_contains(disk_files, key)) {
            notes_db_remove_file(db, key);
        }
    }

    sqlite3_exec(db->db, "COMMIT;", NULL, NULL, NULL);

    g_hash_table_destroy(db_files);
    g_hash_table_destroy(disk_files);
}

/* ── Query helpers ──────────────────────────────────────────────── */

static NoteResults *results_from_stmt(sqlite3_stmt *stmt, gboolean has_snippet) {
    NoteResults *res = g_new0(NoteResults, 1);
    GPtrArray *arr = g_ptr_array_new();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NoteInfo *info = g_new0(NoteInfo, 1);
        info->filepath = g_strdup((const char *)sqlite3_column_text(stmt, 0));
        info->title    = g_strdup((const char *)sqlite3_column_text(stmt, 1));
        info->mtime    = (time_t)sqlite3_column_int64(stmt, 2);

        const char *tags_csv = (const char *)sqlite3_column_text(stmt, 3);
        info->tags = split_tags(tags_csv, &info->tag_count);

        if (has_snippet) {
            const char *snip = (const char *)sqlite3_column_text(stmt, 4);
            info->snippet = snip ? g_strdup(snip) : NULL;
        }
        g_ptr_array_add(arr, info);
    }

    res->count = arr->len;
    res->items = g_new0(NoteInfo, res->count);
    for (int i = 0; i < res->count; i++)
        res->items[i] = *(NoteInfo *)g_ptr_array_index(arr, i);

    /* free the temp NoteInfo pointers (data copied) */
    for (guint i = 0; i < arr->len; i++)
        g_free(g_ptr_array_index(arr, i));
    g_ptr_array_free(arr, TRUE);

    return res;
}

NoteResults *notes_db_list_all(NotesDatabase *db) {
    if (!db) return NULL;
    const char *sql =
        "SELECT filepath, title, mtime, tags FROM notes ORDER BY mtime DESC;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;
    NoteResults *res = results_from_stmt(stmt, FALSE);
    sqlite3_finalize(stmt);
    return res;
}

NoteResults *notes_db_search(NotesDatabase *db, const char *query) {
    if (!db || !query || query[0] == '\0') return notes_db_list_all(db);

    /* Build FTS5 query: add * to last token for prefix matching */
    GString *fts_query = g_string_new(NULL);
    gchar **tokens = g_strsplit(query, " ", -1);
    for (int i = 0; tokens[i]; i++) {
        if (tokens[i][0] == '\0') continue;
        if (fts_query->len > 0) g_string_append_c(fts_query, ' ');
        g_string_append(fts_query, tokens[i]);
        if (!tokens[i + 1]) g_string_append_c(fts_query, '*');
    }
    g_strfreev(tokens);

    const char *sql =
        "SELECT n.filepath, n.title, n.mtime, n.tags, "
        "snippet(notes_fts, 1, '<b>', '</b>', '...', 32) "
        "FROM notes n JOIN notes_fts f ON n.rowid = f.rowid "
        "WHERE notes_fts MATCH ?1 "
        "ORDER BY rank;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        g_string_free(fts_query, TRUE);
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, fts_query->str, -1, SQLITE_TRANSIENT);
    NoteResults *res = results_from_stmt(stmt, TRUE);
    sqlite3_finalize(stmt);
    g_string_free(fts_query, TRUE);
    return res;
}

NoteResults *notes_db_filter_by_tag(NotesDatabase *db, const char *tag) {
    if (!db || !tag || tag[0] == '\0') return notes_db_list_all(db);

    /* Use LIKE for comma-separated tags field */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%%%s%%", tag);

    const char *sql =
        "SELECT filepath, title, mtime, tags FROM notes "
        "WHERE tags LIKE ?1 ORDER BY mtime DESC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    NoteResults *res = results_from_stmt(stmt, FALSE);
    sqlite3_finalize(stmt);
    return res;
}

char **notes_db_all_tags(NotesDatabase *db, int *count) {
    *count = 0;
    if (!db) return NULL;

    GHashTable *tag_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *sql = "SELECT tags FROM notes WHERE tags != '' AND tags IS NOT NULL;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *csv = (const char *)sqlite3_column_text(stmt, 0);
            if (!csv) continue;
            gchar **parts = g_strsplit(csv, ",", -1);
            for (int i = 0; parts[i]; i++) {
                g_strstrip(parts[i]);
                if (parts[i][0] && !g_hash_table_contains(tag_set, parts[i]))
                    g_hash_table_add(tag_set, g_strdup(parts[i]));
            }
            g_strfreev(parts);
        }
        sqlite3_finalize(stmt);
    }

    int n = g_hash_table_size(tag_set);
    char **tags = g_new(char *, n + 1);
    GHashTableIter iter;
    gpointer key;
    int i = 0;
    g_hash_table_iter_init(&iter, tag_set);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        tags[i++] = g_strdup(key);
    tags[n] = NULL;
    *count = n;

    /* Sort alphabetically */
    for (int a = 0; a < n - 1; a++)
        for (int b = a + 1; b < n; b++)
            if (strcmp(tags[a], tags[b]) > 0) {
                char *tmp = tags[a]; tags[a] = tags[b]; tags[b] = tmp;
            }

    g_hash_table_destroy(tag_set);
    return tags;
}

/* ── Free ───────────────────────────────────────────────────────── */

void notes_db_results_free(NoteResults *results) {
    if (!results) return;
    for (int i = 0; i < results->count; i++) {
        g_free(results->items[i].filepath);
        g_free(results->items[i].title);
        g_free(results->items[i].snippet);
        if (results->items[i].tags) {
            for (int t = 0; t < results->items[i].tag_count; t++)
                g_free(results->items[i].tags[t]);
            g_free(results->items[i].tags);
        }
    }
    g_free(results->items);
    g_free(results);
}

void notes_db_tags_free(char **tags, int count) {
    if (!tags) return;
    for (int i = 0; i < count; i++)
        g_free(tags[i]);
    g_free(tags);
}
