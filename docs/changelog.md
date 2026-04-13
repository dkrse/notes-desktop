# Changelog

## 2.3.0 — 2026-04-13

### Stability

- **Fixed critical settings corruption** — `gui_font_size` could grow to billions (1751607660) due to missing validation, causing Pango to request negative font sizes and GTK to allocate widgets with extreme dimensions (958669px headerbar), leading to memory exhaustion and OS-level freezes/restarts
- **Locale-safe float handling** — settings file now uses `g_ascii_strtod()` / `g_ascii_formatd()` for parsing/writing floats, and commas are replaced with dots on read. Previously, Slovak/Czech locale wrote `0,30` which `atof()` in C locale parsed as `0`
- **Settings validation** — all numeric settings are now range-checked on load:
  - Font sizes: 6–72pt (was unbounded `atoi()`)
  - Window dimensions: 200–8192px (was unbounded)
  - Line spacing: 0.5–5.0 (was unbounded)
  - Empty strings for theme/sort_order are ignored (default preserved)

### Preview (WebKit)

- **Preview content limit** — markdown preview is capped at 100KB to prevent WebKit memory exhaustion on large notes
- **Mermaid diagram limit** — maximum 5 mermaid diagrams rendered per preview update to prevent DOM accumulation
- **Mermaid security hardened** — `securityLevel` changed from `loose` to `strict`, preventing arbitrary HTML/JS execution in diagrams

### File Loading

- **File size limit** — files larger than 10MB are rejected on load to prevent editor and system unresponsiveness

---

## 2.2.0 — 2026-04-13

### Performance

- **Optimized dirty tracking** — buffer content is no longer compared via `strcmp()` on every keystroke; dirty flag is set immediately on first change
- **Optimized search results** — `results_from_stmt()` uses `GArray` directly instead of double-allocating via `GPtrArray` then copying
- **Deferred line numbers redraw** — line numbers update via `g_idle_add()` so text view layout is complete before drawing, preventing stale positions when switching notes

### Security

- **Fixed markup injection in search snippets** — user content is now escaped before rendering as Pango markup; SQLite FTS5 uses safe delimiters (0x02/0x03) instead of HTML tags
- **Fixed FTS5 syntax injection** — search tokens are quoted to prevent FTS5 operators (NOT, OR, NEAR) from causing query errors
- **Safe string copies** — all `strncpy()` calls in settings now explicitly null-terminate via `SAFE_COPY` macro
- **Config file permissions** — `settings.conf` is created with mode 0600 instead of default 0644

### Memory Management

- **Fixed use-after-free in tag filter** — `active_tag_filter` pointer is now saved before freeing and comparison uses the saved copy
- **Per-window highlight color** — `highlight_rgba` moved from global variable to `NotesWindow` struct to support multiple instances

### Line Numbers

- **Rewritten as GtkDrawingArea** — line numbers are now drawn via a custom draw function using `gtk_text_view_get_line_yrange()` and `gtk_text_view_buffer_to_window_coords()`, correctly aligning with wrapped lines when word wrap is enabled
- **Scroll-synced redraw** — line numbers redraw on every scroll event via signal on the main scroll adjustment

### Current Line Highlight

- **Full wrapped line highlight** — highlight overlay now covers all display lines of a wrapped buffer line using `gtk_text_view_get_line_yrange()` instead of `gtk_text_view_get_iter_location()`

### UI

- **Cursor starts at top** — loading a file now places the cursor at the start instead of scrolling to the bottom
- **Focus editor on note selection** — clicking a note in the sidebar moves focus to the text editor
- **GUI font setting** — new independent font for headerbar, popover menus, and status bar (default: Sans 10pt)

### Settings

- Added `gui_font` setting (default: "Sans")
- Added `gui_font_size` setting (default: 10)

---

## 2.1.0 — 2026-04-10

### Removed

- **Removed Clear button** — redundant with New Note (Ctrl+N) which auto-saves and clears the editor

### Added

- **Confirmation dialogs** — Pack Notes and Delete Note now show an AdwAlertDialog before executing. Can be disabled in Settings via "Confirm Dialogs" toggle (default: enabled)
- **Delete Note in hamburger menu** — Delete Note action is now accessible from the hamburger menu (in addition to Ctrl+Delete)
- **Sort order setting** — notes in the sidebar can be sorted by "Newest First" (default), "Oldest First", or "Random". Configurable in Settings, applied immediately on Apply
- **Sidebar font setting** — independent font family and size for the sidebar (default: Sans 10pt), configurable via font picker in Settings
- **Smart dirty tracking** — saves are skipped when content hasn't changed from what's on disk. If you type something and undo it back to original, the note is no longer considered modified
- **Modified indicator** — window title shows `Notes *` when the current note has unsaved changes, reverts to `Notes` when content matches the saved version

### Settings

- Added `confirm_dialogs` setting (default: true)
- Added `sort_order` setting (default: "newest") — values: "newest", "oldest", "random"
- Added `sidebar_font` setting (default: "Sans")
- Added `sidebar_font_size` setting (default: 10)

---

## 2.0.0 — 2026-04-10

### Note Management

- **Sidebar with note list** — browse all notes with title (first line), date, and tags. Resizable via GtkPaned. Toggle with F9 or sidebar button in header bar. Visibility persisted in settings.
- **Full-text search** — search-as-you-type across all note content using SQLite FTS5. 300ms debounce for responsive filtering. Prefix matching (typing "meet" finds "meeting"). Search results show context snippets.
- **Tag system** — write `#hashtags` anywhere in your notes. Tags are automatically extracted, normalized to lowercase, and deduplicated. Tag chips appear in the sidebar for one-click filtering. "All" chip clears the filter.
- **SQLite FTS5 index** — bundled SQLite amalgamation compiled with FTS5 support. Database at `~/.config/notes-desktop/notes_index.db`. Syncs on startup (compares file mtime), updates in real-time on save/clear/delete. Can be safely deleted — rebuilds automatically.
- **New note action** (Ctrl+N) — auto-saves current note, clears buffer for a fresh note
- **Delete note action** (Ctrl+Delete) — removes current note from disk and search index

### New Files

- `src/database.h` / `src/database.c` — SQLite FTS5 search index layer
- `src/sqlite3/sqlite3.c` / `sqlite3.h` — SQLite 3.45.1 amalgamation

### UI Changes

- Header bar now includes: sidebar toggle button (view-list-symbolic), new note button (document-new-symbolic), hamburger menu
- Main layout changed from simple vertical box to GtkPaned (sidebar | editor)
- Sidebar CSS rules added for all 13 themes (search entry, note rows, tag chips, hover/active states)

### New Keyboard Shortcuts

| Shortcut     | Action              |
|--------------|---------------------|
| Ctrl+N       | New note            |
| Ctrl+F       | Focus search        |
| F9           | Toggle sidebar      |
| Ctrl+Delete  | Delete current note |

### Settings

- Added `show_sidebar` setting (default: true)

### Build

- Makefile updated to compile SQLite amalgamation as separate object with `-DSQLITE_ENABLE_FTS5 -w`
- Added `-lpthread -ldl -lm` to linker flags for SQLite

---

## 1.1.0 — 2026-03-28

### Security

- **Fixed command injection** in Pack Notes — replaced `system()` shell calls with `g_spawn_sync()` using direct argv (no shell interpretation of file paths)
- **Fixed shell injection in file deletion** — replaced `find -delete` shell command with `g_remove()` loop

### Performance

- **Line numbers cache** — line number gutter is only rebuilt when the line count actually changes, not on every keystroke
- **Deferred font intensity** — `apply_font_intensity()` is now scheduled via `g_idle_add()` to coalesce multiple rapid buffer changes into a single full-buffer tag application
- **Pre-allocated GString** — line number string builder uses `g_string_sized_new()` with estimated size

### Memory Management

- **Fixed NotesWindow leak** — struct is now freed in the `destroy` signal handler
- **Fixed GtkCssProvider leak** — provider is removed from display and unref'd on window destroy
- **Fixed GtkFileDialog leaks** — file dialog objects are unref'd in async completion callbacks (open file and select directory)
- **Idle source cleanup** — pending `g_idle_add` callbacks are cancelled on window destroy to prevent use-after-free

### UI

- **Fixed popover rendering on custom themes** — hamburger menu no longer shows a rectangular background behind the rounded popover

---

## 1.0.0 — 2026-03-28

### Initial Release

#### Editor
- Simple single-file text editor with GtkTextView
- Write-and-clear workflow — press Clear to save current note with timestamp and start fresh
- All notes accumulate as timestamped files in the save directory
- Ctrl+S to save (overwrites existing file or creates new timestamped file)
- Ctrl+O to open a file from the save directory (*.txt or all files filter)
- Ctrl+Plus / Ctrl+Minus to zoom in/out (persisted to config)
- Ctrl+Q to quit
- Auto-save on window close, restore last file on launch
- Font intensity control (0.3-1.0) via GtkTextTag foreground alpha
- Word wrap toggle (on/off)

#### Themes
- 13 built-in themes: System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha
- Runtime dark/light switching via libadwaita AdwStyleManager
- Custom CSS for custom themes (full override: textview, headerbar, popover, window)
- Minimal CSS for system/light/dark (textview only, GTK handles chrome)
- System theme follows OS dark/light preference

#### Current Line Highlight
- NotesTextView subclass with snapshot overlay
- Semi-transparent overlay drawn after parent render (white for dark themes, black for light)
- Works on all themes without hardcoded colors
- Works on empty lines
- Toggle in Settings

#### Line Numbers
- Non-editable GtkTextView gutter for pixel-perfect spacing alignment
- Own GtkScrolledWindow with synced vertical adjustment
- Dynamic width via Pango measurement (grows with digit count)
- Dimmed appearance (alpha 0.3 of theme text color)
- Toggle in Settings

#### Line Spacing
- Options: 1, 1.2, 1.5, 2
- Applied via pixels-above/below-lines on both editor and line numbers

#### Status Bar
- Text encoding display (UTF-8)
- Real-time cursor position (Ln, Col)

#### Pack Notes
- Archive all .txt notes to ZIP, tar.gz, or tar.xz
- Configurable archive format in Settings
- Optional delete-after-pack

#### Settings
- Plain key=value config file at `~/.config/notes-desktop/settings.conf`
- Settings dialog with: theme, font, font intensity, line spacing, line numbers, highlight line, wrap lines, archive format, delete after pack, save directory
- Apply/Cancel buttons

#### Build
- Makefile with GCC, C17, -Wall -Wextra -O2
- Output to `build/` directory
- Dependencies: GTK 4, libadwaita
- Tested on Fedora 43 (GNOME / Wayland)
