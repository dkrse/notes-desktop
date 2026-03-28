# Architecture

## Overview

Notes Desktop is a single-window note-taking application built with GTK 4, libadwaita, and C17. It uses `AdwApplication` for proper runtime theme management and a custom `GtkTextView` subclass for current line highlighting.

The core workflow is **write-and-clear**: the user writes in a single buffer, presses Clear to save the note as a timestamped file and start fresh. Notes accumulate in a save directory and can be archived with Pack Notes.

## Source Files

```
src/
  main.c       — Application entry point, AdwApplication setup, quit action
  window.c     — Main window construction, theme/CSS management, line numbers,
                  current line highlight (NotesTextView subclass), auto-save
  window.h     — NotesWindow struct definition, public API
  settings.c   — Load/save settings from ~/.config/notes-desktop/settings.conf
  settings.h   — NotesSettings struct definition
  actions.c    — All GAction handlers (clear, save, open, zoom, pack, settings dialog)
  actions.h    — actions_setup() declaration
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

### NotesTextView (window.c)

A minimal GtkTextView subclass that overrides `snapshot()` to draw the current line highlight as a semi-transparent overlay after the parent renders all content. This approach:

- Works on **all themes** without hardcoded colors
- Works on **empty lines** (draws based on cursor position, not text content)
- Uses white overlay (alpha 0.06) for dark themes, black overlay for light themes

### Settings (settings.h)

Plain key=value config file at `~/.config/notes-desktop/settings.conf`. Parsed manually with `fgets()`/`strchr()` on load, written with `fprintf()` on save. No GSettings/dconf dependency.

Settings fields: font, font_size, font_intensity, line_spacing, theme, save_directory, archive_format, show_line_numbers, highlight_current_line, wrap_lines, delete_after_pack, last_file.

### Theme System (window.c)

Two layers working together:

1. **AdwStyleManager** — `adw_style_manager_set_color_scheme()` handles dark/light switching for all GTK/Adwaita widgets (headerbar, popover, window controls, scrollbars). Three modes:
   - `ADW_COLOR_SCHEME_DEFAULT` — follow system preference
   - `ADW_COLOR_SCHEME_FORCE_LIGHT` — force light for Light and light custom themes
   - `ADW_COLOR_SCHEME_FORCE_DARK` — force dark for Dark and dark custom themes

2. **GtkCssProvider** — per-theme CSS rules:
   - **System / Light / Dark**: minimal CSS — only textview colors and line numbers. All chrome (headerbar, popover, etc.) handled by libadwaita.
   - **Custom themes** (Monokai, Dracula, etc.): full CSS override for textview, headerbar, popover, window, status bar, and window controls.

### Font Intensity (window.c)

Uses a GtkTextTag (`intensity_tag`) with `foreground-rgba` applied to the entire buffer. The tag sets the theme's text color with a reduced alpha (0.3–1.0). At 1.0 the tag is removed entirely, letting native theme colors through. Application is deferred via `g_idle_add()` to coalesce multiple buffer changes into a single update.

### Line Numbers (window.c)

Implemented as a separate non-editable GtkTextView (not a GtkLabel) to ensure pixel-perfect alignment with the editor's line spacing. Placed in its own GtkScrolledWindow with `GTK_POLICY_EXTERNAL` vertical scroll, sharing the same GtkAdjustment as the main editor's scrolled window. Width is measured dynamically using Pango layout. Rebuilds are skipped when the line count hasn't changed (`cached_line_count`).

### Actions (actions.c)

All user actions are GActions on the window action map:

| Action             | Shortcut | Description                              |
|--------------------|----------|------------------------------------------|
| `win.clear`        | —        | Save current note with timestamp + clear buffer for new note |
| `win.save`         | Ctrl+S   | Save to current or new timestamped file  |
| `win.open-folder`  | Ctrl+O   | Open file (*.txt or all files filter)    |
| `win.zoom-in`      | Ctrl+=   | Increase font size                       |
| `win.zoom-out`     | Ctrl+-   | Decrease font size                       |
| `win.pack-notes`   | —        | Archive all .txt notes                   |
| `win.settings`     | —        | Open settings dialog                     |

### Pack Notes (actions.c)

Archives all `.txt` files in the save directory using `g_spawn_sync()` with zip/tar commands (no shell involved — arguments passed directly via argv to prevent command injection). If "Delete After Pack" is enabled, removes `.txt` files after successful archiving using `g_remove()`.

## Widget Hierarchy

```
GtkApplicationWindow (via AdwApplication)
 ├── GtkHeaderBar
 │    ├── [start] GtkButton "Clear"
 │    └── [end]   GtkMenuButton (hamburger)
 └── GtkBox (vertical)
      ├── GtkBox (horizontal) — editor area
      │    ├── GtkScrolledWindow — line numbers
      │    │    └── GtkTextView (non-editable, dimmed)
      │    └── GtkScrolledWindow — main editor
      │         └── NotesTextView (GtkTextView subclass)
      └── GtkBox (horizontal) — status bar
           ├── GtkLabel "UTF-8"
           └── GtkLabel "Ln X, Col Y"
```

## Data Flow

1. **Startup**: `main.c` creates AdwApplication → `on_activate` → `notes_window_new()`
2. **Settings**: Loaded once at window creation, saved on every change
3. **Writing**: User types in buffer, line numbers and cursor position update in real-time
4. **Clear**: `on_clear` → saves buffer to timestamped file → clears buffer → resets `current_file`
5. **Auto-save on close**: `on_close_request` → `auto_save_current()` → `settings_save()`
6. **Cleanup on destroy**: `on_destroy` → cancels idle callbacks → releases CSS provider → frees NotesWindow
7. **Restore on launch**: If `last_file` is set in config, loaded via `notes_window_load_file()`
8. **Theme change**: Settings dialog updates `win->settings.theme` → Apply → `notes_window_apply_settings()` → `apply_theme()` (AdwStyleManager) + `apply_css()` (GtkCssProvider) + `apply_highlight_color()` + `apply_font_intensity()`

## Memory Management

- `NotesWindow` is allocated with `g_new0()` and freed in the `destroy` signal handler
- `GtkCssProvider` is removed from the display and unref'd on destroy
- `GtkFileDialog` objects are unref'd in their async completion callbacks
- Pending `g_idle_add` sources are cancelled on window destroy to prevent use-after-free
