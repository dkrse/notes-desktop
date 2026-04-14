# Architecture

## Overview

Notes Desktop is a single-window note-taking application built with GTK 4, libadwaita, and C17. It uses `AdwApplication` for proper runtime theme management and a custom `GtkTextView` subclass for current line highlighting.

The core workflow is markdown-first: notes are saved as `.md` files and open in rendered preview by default. The user presses Ctrl+E to switch to the editor (with markdown syntax highlighting), writes or edits content, then returns to preview. New notes are created with Ctrl+N (auto-saving the current one). Notes accumulate in a save directory and can be archived with Pack Notes.

A **sidebar** with full-text search and a scrollable note list provides fast navigation across all notes. The search backend is **SQLite FTS5**, bundled as an amalgamation.

A **markdown preview pane** renders notes using WebKitGTK with marked.js, KaTeX math, and Mermaid diagrams. The preview is initialized at startup for instant rendering when notes are opened.

## Source Files

```
src/
  main.c           — Application entry point, AdwApplication setup, quit action
  window.c         — Main window construction, sidebar, theme/CSS management, line numbers,
                      current line highlight (NotesTextView subclass), auto-save
  window.h         — NotesWindow struct definition, public API
  settings.c       — Load/save settings from ~/.config/notes-desktop/settings.conf
  settings.h       — NotesSettings struct definition
  actions.c        — All GAction handlers (new, save, delete, open, zoom, pack, export-pdf,
                      toggle-sidebar, toggle-edit, focus-search, settings dialog, confirmation dialogs)
  actions.h        — actions_setup() declaration
  highlight.c      — Regex-based syntax highlighting for markdown and programming languages
  highlight.h      — HighlightLanguage enum, highlight_apply() API
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
| `line_numbers`         | GtkDrawingArea*        | Line number gutter (custom draw function)  |
| `ln_scrolled`          | GtkWidget*             | Container for line numbers                 |
| `editor_box`           | GtkWidget*             | Horizontal box: line numbers + editor      |
| `highlight_line`       | int                    | Current line index for highlight           |
| `highlight_rgba`       | GdkRGBA                | Per-window highlight overlay color         |
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
| `sidebar_box`          | GtkWidget*             | Sidebar container (search + list)          |
| `search_entry`         | GtkWidget*             | GtkSearchEntry for full-text search        |
| `note_list`            | GtkWidget*             | GtkListBox with note rows                  |
| `paned`                | GtkWidget*             | GtkPaned splitting sidebar and editor      |
| `search_timeout_id`    | guint                  | Debounce timer for search-as-you-type      |
| `dirty`                | gboolean               | TRUE if buffer content differs from saved  |
| `original_content`     | char*                  | Snapshot of content at load/save for dirty comparison |
| `preview_paned`        | GtkWidget*             | GtkPaned splitting editor and preview      |
| `preview_webview`      | GtkWidget*             | WebKitWebView for markdown preview         |
| `preview_scrolled`     | GtkWidget*             | Scrolled container for preview             |
| `editor_vbox`          | GtkWidget*             | Editor area container (hidden in preview)  |
| `preview_visible`      | gboolean               | TRUE if preview pane is shown              |
| `preview_ready`        | gboolean               | TRUE after JS libraries are injected       |
| `editing`              | gboolean               | TRUE if in edit mode (editor visible)      |
| `preview_html`         | char*                  | Concatenated JS blob (marked+KaTeX+Mermaid)|
| `preview_timeout_id`   | guint                  | Debounce timer (300ms) for preview update  |
| `highlight_idle_id`    | guint                  | Debounce timer (300ms) for syntax highlight|

### NotesTextView (window.c)

A minimal GtkTextView subclass that overrides `snapshot()` to draw the current line highlight as a semi-transparent overlay after the parent renders all content. This approach:

- Works on **all themes** without hardcoded colors
- Works on **empty lines** (draws based on cursor position, not text content)
- Covers **all wrapped display lines** of the current buffer line using `gtk_text_view_get_line_yrange()`
- Uses white overlay (alpha 0.06) for dark themes, black overlay for light themes
- Highlight color is stored per-window (not global) to support multiple instances

### NotesDatabase (database.h / database.c)

SQLite-based search index for all notes. The database is a **cache** — notes remain as `.md` files on disk, and the index can be deleted and rebuilt at any time.

**Schema:**
- `notes` table — filepath (PK), title, content, mtime, tags (CSV)
- `notes_fts` virtual table — FTS5 full-text index over title, content, tags
- Triggers (`notes_ai`, `notes_ad`, `notes_au`) keep FTS in sync with the notes table

**Key operations:**
- `notes_db_sync()` — scans directory, compares mtime, re-indexes changed files, removes deleted
- `notes_db_index_file()` — reads file, extracts title (first line) and tags (#hashtags), upserts
- `notes_db_search()` — FTS5 MATCH with prefix matching and snippet generation

**Database location:** `~/.config/notes-desktop/notes_index.db`

### Markdown Preview (window.c)

A WebKitGTK-based preview pane, initialized at application startup for instant rendering. Notes open in preview by default; the editor is shown via Ctrl+E toggle. Architecture:

1. **HTML shell** — minimal HTML with CSS variables for theming, loaded into WebKitWebView
2. **JS injection** — on load-finished, a concatenated blob of marked.js + KaTeX + Mermaid is evaluated
3. **`updatePreview(src)`** — JavaScript function called from C via `webkit_web_view_evaluate_javascript()`, parses markdown, renders math and diagrams
4. **Debounced updates** — preview updates are debounced at 300ms via `g_timeout_add()` on buffer changes; file loads and JS injection trigger immediate updates
5. **Theme sync** — `applyTheme(fg, bg, dark)` JS function updates CSS variables and re-initializes Mermaid theme

**Safety limits:**
- Preview content capped at 100KB to prevent WebKit memory exhaustion
- Maximum 20 Mermaid diagrams per update to prevent DOM accumulation
- Mermaid `securityLevel: loose` for full diagram feature support
- `_mmPending` flag prevents concurrent Mermaid render operations (reset before each render cycle)
- `_lastSrc` deduplication skips identical content
- Mermaid element IDs use a globally incrementing counter to prevent DOM ID collisions across updates

### Settings (settings.h)

Plain key=value config file at `~/.config/notes-desktop/settings.conf`. Parsed manually with `fgets()`/`strchr()` on load, written with `fprintf()` on save. No GSettings/dconf dependency.

Settings fields: font, font_size, sidebar_font, sidebar_font_size, gui_font, gui_font_size, font_intensity, line_spacing, theme, save_directory, archive_format, show_line_numbers, highlight_current_line, wrap_lines, delete_after_pack, confirm_dialogs, sort_order, show_sidebar, last_file, pdf_margin_top, pdf_margin_bottom, pdf_margin_left, pdf_margin_right, pdf_landscape, pdf_page_numbers.

**Validation on load:**
- Font sizes clamped to 6–72pt (prevents Pango crashes from corrupt values)
- Window dimensions clamped to 200–8192px
- Line spacing clamped to 0.5–5.0
- Empty strings for theme/sort_order are ignored (defaults preserved)
- Locale-safe float parsing: commas replaced with dots, `g_ascii_strtod()` used instead of `atof()`
- Locale-safe float output: `g_ascii_formatd()` used instead of `fprintf("%.2f")`

### Theme System (window.c)

Two layers working together:

1. **AdwStyleManager** — `adw_style_manager_set_color_scheme()` handles dark/light switching for all GTK/Adwaita widgets (headerbar, popover, window controls, scrollbars). Three modes:
   - `ADW_COLOR_SCHEME_DEFAULT` — follow system preference
   - `ADW_COLOR_SCHEME_FORCE_LIGHT` — force light for Light and light custom themes
   - `ADW_COLOR_SCHEME_FORCE_DARK` — force dark for Dark and dark custom themes

2. **GtkCssProvider** — per-theme CSS rules:
   - **System / Light / Dark**: minimal CSS — only textview colors, line numbers, and sidebar. All chrome (headerbar, popover, etc.) handled by libadwaita.
   - **Custom themes** (Monokai, Dracula, etc.): full CSS override for textview, headerbar, popover, window, status bar, window controls, and sidebar (search entry, note rows).

### Font Intensity (window.c)

Uses a GtkTextTag (`intensity_tag`) with `foreground-rgba` applied to the entire buffer. The tag sets the theme's text color with a reduced alpha (0.3-1.0). At 1.0 the tag is removed entirely, letting native theme colors through. The intensity tag is kept at the lowest priority (0) so that syntax highlight colors take precedence. Application is deferred via `g_idle_add()` to coalesce multiple buffer changes into a single update.

### Line Numbers (window.c)

Implemented as a `GtkDrawingArea` with a custom draw function (`draw_line_numbers`). For each visible buffer line, the function queries the main text view's line positions using the standard GTK4 approach:

1. `gtk_text_view_get_visible_rect()` — gets the visible area in buffer coordinates
2. `gtk_text_view_get_iter_at_location()` — finds the first visible line
3. `gtk_text_view_get_line_yrange()` — gets Y position and full height (including wrapped lines)
4. `gtk_text_view_buffer_to_window_coords()` — converts buffer Y to widget Y (handles scroll automatically)
5. `gtk_text_iter_forward_line()` — advances to the next buffer line

This correctly handles word wrap — each line number aligns with the first visual line of the corresponding buffer line, regardless of how many display lines it wraps into. Width is measured dynamically using Pango layout. Redraws are deferred via `g_idle_add()` to ensure the text view layout is up to date after buffer changes. The drawing area is also redrawn on every scroll via a signal on the main scroll adjustment.

### Sidebar (window.c)

The sidebar is a vertical box containing:
1. **GtkSearchEntry** — search-as-you-type with 300ms debounce
2. **GtkListBox** in a GtkScrolledWindow — scrollable list of note rows

Each note row displays:
- **Title** (first line of note, bold, ellipsized)
- **Date** (formatted mtime)
- **Snippet** (FTS5 search result context, only during search)

The sidebar is connected to the editor via `GtkPaned`, allowing the user to resize or hide it. Visibility is toggled with F9 and persisted in settings.

### Actions (actions.c)

All user actions are GActions on the window action map:

| Action               | Shortcut     | Description                                          |
|----------------------|--------------|------------------------------------------------------|
| `win.new-note`       | Ctrl+N       | Auto-save current, clear buffer for new note         |
| `win.save`           | Ctrl+S       | Save to current or new timestamped file              |
| `win.delete-note`    | Ctrl+Delete  | Delete current note (with confirmation dialog)       |
| `win.open-folder`    | Ctrl+O       | Open file (*.md or all files filter)                 |
| `win.zoom-in`        | Ctrl+=       | Increase font size                                   |
| `win.zoom-out`       | Ctrl+-       | Decrease font size                                   |
| `win.toggle-edit`    | Ctrl+E       | Toggle edit mode (show editor / show preview)        |
| `win.toggle-preview` | Ctrl+P       | Show/hide markdown preview pane                      |
| `win.toggle-sidebar` | F9           | Show/hide sidebar                                    |
| `win.focus-search`   | Ctrl+F       | Focus search entry (opens sidebar if hidden)         |
| `win.export-pdf`     | —            | Export current note to PDF (file save dialog)        |
| `win.pack-notes`     | —            | Archive all .md notes (with confirmation dialog)     |
| `win.settings`       | —            | Open settings dialog (General + PDF tabs)            |

### Pack Notes (actions.c)

Shows a confirmation dialog (if enabled in settings) before archiving. Archives all `.md` files in the save directory using `g_spawn_sync()` with zip/tar commands (no shell involved — arguments passed directly via argv to prevent command injection). If "Delete After Pack" is enabled, removes `.md` files after successful archiving using `g_remove()`, then re-syncs the database index.

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
      │    └── GtkScrolledWindow
      │         └── GtkListBox — note list
      │              └── GtkListBoxRow (per note)
      │                   └── GtkBox (vertical)
      │                        ├── GtkLabel — title (bold, ellipsized)
      │                        ├── GtkLabel — date
      │                        └── GtkLabel — snippet (search only)
      └── GtkPaned (horizontal) — editor | preview
           ├── GtkBox (vertical) — editor area
           │    ├── GtkBox (horizontal) — editor
           │    │    ├── GtkDrawingArea — line numbers (custom draw)
           │    │    └── GtkScrolledWindow — main editor
           │    │         └── NotesTextView (GtkTextView subclass)
           │    └── GtkBox (horizontal) — status bar
           │         ├── GtkLabel "UTF-8"
           │         └── GtkLabel "Ln X, Col Y"
           └── WebKitWebView — markdown preview (lazy-initialized)
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
10. **Note selection**: Click row in list -> auto-save current -> `notes_window_load_file()` on selected -> preview shown (editor hidden)
12. **Edit toggle (Ctrl+E)**: Show editor with syntax highlighting (hide preview, cancel pending preview timeouts), or return to preview (hide editor, sync zoom level)
13. **Auto-save on close**: `on_close_request` -> `auto_save_current()` -> `settings_save()`
13. **Cleanup on destroy**: `on_destroy` -> cancels timers -> releases CSS provider -> closes database -> frees NotesWindow
14. **Restore on launch**: If `last_file` is set in config, loaded via `notes_window_load_file()`
15. **Preview**: WebKit initialized at startup -> inject JS blob -> notes open in preview by default
16. **Preview update**: `on_buffer_changed` -> `notes_window_update_preview()` (debounced 300ms) -> `preview_timeout_cb` -> js_escape text (capped at 100KB) -> `webkit_web_view_evaluate_javascript("updatePreview('...')")`
17. **Syntax highlighting**: `on_buffer_changed` -> `schedule_highlight()` (debounced 300ms) -> `highlight_apply(buffer, LANG_MARKDOWN)` -> regex-based tag application
15. **Theme change**: Settings dialog updates `win->settings.theme` -> Apply -> `notes_window_apply_settings()` -> `apply_theme()` (AdwStyleManager) + `apply_css()` (GtkCssProvider with sidebar rules) + `apply_highlight_color()` + `apply_font_intensity()`

## Memory Management

- `NotesWindow` is allocated with `g_new0()` and freed in the `destroy` signal handler
- `NotesDatabase` is allocated in `notes_db_open()` and freed in `notes_db_close()` on destroy
- `GtkCssProvider` is removed from the display and unref'd on destroy
- `GtkFileDialog` objects are unref'd in their async completion callbacks
- Pending `g_idle_add` and `g_timeout_add` sources are cancelled on window destroy to prevent use-after-free
- Note list row filepaths are attached via `g_object_set_data_full()` with `g_free` destructor
- `NoteResults` are freed via `notes_db_results_free()`
- `original_content` string is freed on destroy and updated on every load/save
