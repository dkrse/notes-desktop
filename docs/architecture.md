# Architecture

## Overview

Notes Desktop is a single-window note-taking application built with GTK 4, libadwaita, and C17. It uses `AdwApplication` for proper runtime theme management and a custom `GtkTextView` subclass for current line highlighting.

The core workflow is simple: the user writes in a single buffer, creates new notes with Ctrl+N (auto-saving the current one), and browses all notes via the sidebar. Notes accumulate in a save directory and can be archived with Pack Notes.

A **sidebar** with full-text search, tag filtering, and a scrollable note list provides fast navigation across all notes. The search backend is **SQLite FTS5**, bundled as an amalgamation.

## Source Files

```
src/
  main.c           — Application entry point, AdwApplication setup, quit action
  window.c         — Main window construction, sidebar, theme/CSS management, line numbers,
                      current line highlight (NotesTextView subclass), auto-save
  window.h         — NotesWindow struct definition, public API
  settings.c       — Load/save settings from ~/.config/notes-desktop/settings.conf
  settings.h       — NotesSettings struct definition
  actions.c        — All GAction handlers (new, save, delete, open, zoom, pack,
                      toggle-sidebar, focus-search, settings dialog, confirmation dialogs)
  actions.h        — actions_setup() declaration
  database.c       — SQLite FTS5 index: sync, search, tag extraction, CRUD
  database.h       — NotesDatabase, NoteInfo, NoteResults structs and API
  sqlite3/
    sqlite3.c      — SQLite amalgamation (compiled with -DSQLITE_ENABLE_FTS5)
    sqlite3.h      — SQLite header
```

## Key Components

### NotesWindow (window.h)

Central struct that holds all UI state:

| Field                  | Type                   | Purpose                                    |
|------------------------|------------------------|--------------------------------------------|
| `window`               | GtkApplicationWindow*  | Main application window                    |
| `text_view`            | GtkTextView*           | Editor (NotesTextView subclass)            |
| `buffer`               | GtkTextBuffer*         | Text buffer                                |
| `line_numbers`         | GtkTextView*           | Non-editable line number gutter            |
| `ln_scrolled`          | GtkWidget*             | Scroll container for line numbers          |
| `editor_box`           | GtkWidget*             | Horizontal box: line numbers + editor      |
| `highlight_line`       | int                    | Current line index for highlight           |
| `intensity_tag`        | GtkTextTag*            | Font intensity foreground alpha tag        |
| `status_encoding`      | GtkLabel*              | Encoding display in status bar             |
| `status_cursor`        | GtkLabel*              | Cursor position display                    |
| `settings`             | NotesSettings          | Current settings state                     |
| `css_provider`         | GtkCssProvider*        | Dynamic CSS for font/theme                 |
| `current_file`         | char[2048]             | Path to the currently open file            |
| `cached_line_count`    | int                    | Cached line count to avoid redundant rebuilds |
| `intensity_idle_id`    | guint                  | Pending idle source for font intensity     |
| `line_numbers_idle_id` | guint                  | Pending idle source for line numbers       |
| `db`                   | NotesDatabase*         | SQLite FTS5 search index                   |
| `sidebar_box`          | GtkWidget*             | Sidebar container (search + tags + list)   |
| `search_entry`         | GtkWidget*             | GtkSearchEntry for full-text search        |
| `tag_flow`             | GtkWidget*             | GtkFlowBox with tag filter chips           |
| `note_list`            | GtkWidget*             | GtkListBox with note rows                  |
| `paned`                | GtkWidget*             | GtkPaned splitting sidebar and editor      |
| `active_tag_filter`    | char*                  | Currently active tag filter (or NULL)      |
| `search_timeout_id`    | guint                  | Debounce timer for search-as-you-type      |
| `dirty`                | gboolean               | TRUE if buffer content differs from saved  |
| `original_content`     | char*                  | Snapshot of content at load/save for dirty comparison |

### NotesTextView (window.c)

A minimal GtkTextView subclass that overrides `snapshot()` to draw the current line highlight as a semi-transparent overlay after the parent renders all content. This approach:

- Works on **all themes** without hardcoded colors
- Works on **empty lines** (draws based on cursor position, not text content)
- Uses white overlay (alpha 0.06) for dark themes, black overlay for light themes

### NotesDatabase (database.h / database.c)

SQLite-based search index for all notes. The database is a **cache** — notes remain as `.txt` files on disk, and the index can be deleted and rebuilt at any time.

**Schema:**
- `notes` table — filepath (PK), title, content, mtime, tags (CSV)
- `notes_fts` virtual table — FTS5 full-text index over title, content, tags
- Triggers (`notes_ai`, `notes_ad`, `notes_au`) keep FTS in sync with the notes table

**Key operations:**
- `notes_db_sync()` — scans directory, compares mtime, re-indexes changed files, removes deleted
- `notes_db_index_file()` — reads file, extracts title (first line) and tags (#hashtags), upserts
- `notes_db_search()` — FTS5 MATCH with prefix matching and snippet generation
- `notes_db_filter_by_tag()` — filters by tag using LIKE on CSV tags field
- `notes_db_all_tags()` — collects all unique tags across all notes

**Tag extraction:** Scans content for `#` followed by `[a-zA-Z0-9_-]+`, normalizes to lowercase, deduplicates.

**Database location:** `~/.config/notes-desktop/notes_index.db`

### Settings (settings.h)

Plain key=value config file at `~/.config/notes-desktop/settings.conf`. Parsed manually with `fgets()`/`strchr()` on load, written with `fprintf()` on save. No GSettings/dconf dependency.

Settings fields: font, font_size, sidebar_font, sidebar_font_size, font_intensity, line_spacing, theme, save_directory, archive_format, show_line_numbers, highlight_current_line, wrap_lines, delete_after_pack, confirm_dialogs, sort_order, show_sidebar, last_file.

### Theme System (window.c)

Two layers working together:

1. **AdwStyleManager** — `adw_style_manager_set_color_scheme()` handles dark/light switching for all GTK/Adwaita widgets (headerbar, popover, window controls, scrollbars). Three modes:
   - `ADW_COLOR_SCHEME_DEFAULT` — follow system preference
   - `ADW_COLOR_SCHEME_FORCE_LIGHT` — force light for Light and light custom themes
   - `ADW_COLOR_SCHEME_FORCE_DARK` — force dark for Dark and dark custom themes

2. **GtkCssProvider** — per-theme CSS rules:
   - **System / Light / Dark**: minimal CSS — only textview colors, line numbers, and sidebar. All chrome (headerbar, popover, etc.) handled by libadwaita.
   - **Custom themes** (Monokai, Dracula, etc.): full CSS override for textview, headerbar, popover, window, status bar, window controls, and sidebar (search entry, note rows, tag chips).

### Font Intensity (window.c)

Uses a GtkTextTag (`intensity_tag`) with `foreground-rgba` applied to the entire buffer. The tag sets the theme's text color with a reduced alpha (0.3-1.0). At 1.0 the tag is removed entirely, letting native theme colors through. Application is deferred via `g_idle_add()` to coalesce multiple buffer changes into a single update.

### Line Numbers (window.c)

Implemented as a separate non-editable GtkTextView (not a GtkLabel) to ensure pixel-perfect alignment with the editor's line spacing. Placed in its own GtkScrolledWindow with `GTK_POLICY_EXTERNAL` vertical scroll, sharing the same GtkAdjustment as the main editor's scrolled window. Width is measured dynamically using Pango layout. Rebuilds are skipped when the line count hasn't changed (`cached_line_count`).

### Sidebar (window.c)

The sidebar is a vertical box containing:
1. **GtkSearchEntry** — search-as-you-type with 300ms debounce
2. **GtkFlowBox** — tag filter chips ("All" + one chip per unique tag)
3. **GtkSeparator**
4. **GtkListBox** in a GtkScrolledWindow — scrollable list of note rows

Each note row displays:
- **Title** (first line of note, bold, ellipsized)
- **Date** (formatted mtime)
- **Snippet** (FTS5 search result context, only during search)
- **Tags** (hashtags found in note content)

The sidebar is connected to the editor via `GtkPaned`, allowing the user to resize or hide it. Visibility is toggled with F9 and persisted in settings.

### Actions (actions.c)

All user actions are GActions on the window action map:

| Action               | Shortcut     | Description                                          |
|----------------------|--------------|------------------------------------------------------|
| `win.new-note`       | Ctrl+N       | Auto-save current, clear buffer for new note         |
| `win.save`           | Ctrl+S       | Save to current or new timestamped file              |
| `win.delete-note`    | Ctrl+Delete  | Delete current note (with confirmation dialog)       |
| `win.open-folder`    | Ctrl+O       | Open file (*.txt or all files filter)                |
| `win.zoom-in`        | Ctrl+=       | Increase font size                                   |
| `win.zoom-out`       | Ctrl+-       | Decrease font size                                   |
| `win.toggle-sidebar` | F9           | Show/hide sidebar                                    |
| `win.focus-search`   | Ctrl+F       | Focus search entry (opens sidebar if hidden)         |
| `win.pack-notes`     | —            | Archive all .txt notes (with confirmation dialog)    |
| `win.settings`       | —            | Open settings dialog                                 |

### Pack Notes (actions.c)

Shows a confirmation dialog (if enabled in settings) before archiving. Archives all `.txt` files in the save directory using `g_spawn_sync()` with zip/tar commands (no shell involved — arguments passed directly via argv to prevent command injection). If "Delete After Pack" is enabled, removes `.txt` files after successful archiving using `g_remove()`, then re-syncs the database index.

## Widget Hierarchy

```
GtkApplicationWindow (via AdwApplication)
 ├── GtkHeaderBar
 │    ├── [start] GtkToggleButton (sidebar toggle, view-list-symbolic)
 │    ├── [start] GtkButton (new note, document-new-symbolic)
 │    └── [end]   GtkMenuButton (hamburger)
 └── GtkPaned (horizontal)
      ├── GtkBox (vertical) — sidebar
      │    ├── GtkSearchEntry — full-text search
      │    ├── GtkFlowBox — tag filter chips
      │    ├── GtkSeparator
      │    └── GtkScrolledWindow
      │         └── GtkListBox — note list
      │              └── GtkListBoxRow (per note)
      │                   └── GtkBox (vertical)
      │                        ├── GtkLabel — title (bold, ellipsized)
      │                        ├── GtkLabel — date
      │                        ├── GtkLabel — snippet (search only)
      │                        └── GtkLabel — #tags
      └── GtkBox (vertical) — editor area
           ├── GtkBox (horizontal) — editor
           │    ├── GtkScrolledWindow — line numbers
           │    │    └── GtkTextView (non-editable, dimmed)
           │    └── GtkScrolledWindow — main editor
           │         └── NotesTextView (GtkTextView subclass)
           └── GtkBox (horizontal) — status bar
                ├── GtkLabel "UTF-8"
                └── GtkLabel "Ln X, Col Y"
```

## Data Flow

1. **Startup**: `main.c` creates AdwApplication -> `on_activate` -> `notes_window_new()`
2. **Settings**: Loaded once at window creation, saved on every change
3. **Database init**: `notes_db_open()` -> `notes_db_sync()` -> `notes_window_refresh_sidebar()`
4. **Writing**: User types in buffer, line numbers and cursor position update in real-time
5. **Save (Ctrl+S)**: Skipped if not dirty; writes buffer to file, updates index, updates original_content, refreshes sidebar
6. **New note (Ctrl+N)**: Auto-saves current (if dirty) -> clears buffer -> refreshes sidebar
7. **Delete (Ctrl+Del)**: Confirmation dialog (if enabled) -> removes file from disk -> removes from index -> clears buffer -> refreshes sidebar
9. **Search**: User types in search entry -> 300ms debounce -> `notes_db_search()` with FTS5 MATCH -> repopulate list
10. **Tag filter**: Click tag chip -> `notes_db_filter_by_tag()` -> repopulate list
11. **Note selection**: Click row in list -> auto-save current -> `notes_window_load_file()` on selected
12. **Auto-save on close**: `on_close_request` -> `auto_save_current()` -> `settings_save()`
13. **Cleanup on destroy**: `on_destroy` -> cancels timers -> releases CSS provider -> closes database -> frees NotesWindow
14. **Restore on launch**: If `last_file` is set in config, loaded via `notes_window_load_file()`
15. **Theme change**: Settings dialog updates `win->settings.theme` -> Apply -> `notes_window_apply_settings()` -> `apply_theme()` (AdwStyleManager) + `apply_css()` (GtkCssProvider with sidebar rules) + `apply_highlight_color()` + `apply_font_intensity()`

## Memory Management

- `NotesWindow` is allocated with `g_new0()` and freed in the `destroy` signal handler
- `NotesDatabase` is allocated in `notes_db_open()` and freed in `notes_db_close()` on destroy
- `GtkCssProvider` is removed from the display and unref'd on destroy
- `GtkFileDialog` objects are unref'd in their async completion callbacks
- Pending `g_idle_add` and `g_timeout_add` sources are cancelled on window destroy to prevent use-after-free
- Note list row filepaths are attached via `g_object_set_data_full()` with `g_free` destructor
- `NoteResults` and tag arrays are freed via dedicated `notes_db_results_free()` / `notes_db_tags_free()`
- `active_tag_filter` string is freed on destroy and on every filter change
- `original_content` string is freed on destroy and updated on every load/save
