#ifndef NOTES_DATABASE_H
#define NOTES_DATABASE_H

#include "sqlite3/sqlite3.h"
#include <glib.h>
#include <time.h>

typedef struct {
    sqlite3 *db;
    char db_path[2048];
} NotesDatabase;

typedef struct {
    char *filepath;
    char *title;       /* first line of note */
    char *snippet;     /* FTS5 snippet for search results */
    time_t mtime;
    char **tags;       /* NULL-terminated array of #tags */
    int tag_count;
} NoteInfo;

typedef struct {
    NoteInfo *items;
    int count;
} NoteResults;

/* Lifecycle */
NotesDatabase *notes_db_open(const char *notes_directory);
void           notes_db_close(NotesDatabase *db);

/* Index operations */
void notes_db_sync(NotesDatabase *db, const char *notes_directory);
void notes_db_index_file(NotesDatabase *db, const char *filepath);
void notes_db_remove_file(NotesDatabase *db, const char *filepath);

/* Queries */
NoteResults *notes_db_list_all(NotesDatabase *db, const char *sort_order);
NoteResults *notes_db_search(NotesDatabase *db, const char *query);
NoteResults *notes_db_filter_by_tag(NotesDatabase *db, const char *tag, const char *sort_order);
char       **notes_db_all_tags(NotesDatabase *db, int *count);

/* Free */
void notes_db_results_free(NoteResults *results);
void notes_db_tags_free(char **tags, int count);

#endif
