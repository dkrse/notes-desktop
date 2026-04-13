# Notes Desktop

A fast, lightweight notes application built with GTK 4, libadwaita, and C17.

## How It Works

Notes Desktop is designed around a simple workflow — with a powerful sidebar for managing all your notes.

1. **Launch** — the app opens with your last note (or a blank page) and a sidebar listing all notes
2. **Write** — just start typing, no file dialogs or save prompts
3. **New note** — press Ctrl+N to auto-save the current note and start fresh
4. **Browse** — use the sidebar to navigate between notes, search content, or filter by tags
5. **Close** — closing the window auto-saves whatever is in the buffer

All your notes accumulate in `~/Notes/` (configurable). Use **#hashtags** anywhere in your notes to organize them. When you want to archive them, use **Pack Notes** from the menu to bundle everything into a ZIP, tar.gz, or tar.xz archive.

## Features

### Editor
- **Distraction-free editor** — single buffer with optional sidebar
- **Smart save** — only saves when content actually changed; title shows `*` when modified
- **Auto-save** — buffer is saved on close and restored on next launch
- **Note accumulation** — notes are stored as timestamped files in your save directory
- **13 color themes** — System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha
- **Current line highlight** — semi-transparent overlay that works on all themes, including empty lines and wrapped lines
- **Line numbers** — optional, drawn via GtkDrawingArea with correct positioning even with word wrap
- **Font intensity** — adjustable text opacity (0.3-1.0)
- **Word wrap** — toggle line wrapping on/off
- **Configurable line spacing** — 1, 1.2, 1.5, 2
- **Font picker** — choose any system font and size for editor, sidebar, and GUI independently
- **Zoom** — Ctrl+Plus / Ctrl+Minus to adjust font size
- **Status bar** — shows text encoding (UTF-8) and cursor position (Ln/Col)

### Note Management
- **Sidebar with note list** — browse all notes with title (first line), date, and tags
- **Full-text search** — search-as-you-type across all note content using SQLite FTS5
- **Tag system** — use `#hashtags` in your notes, filter by clicking tag chips in the sidebar
- **New note** — Ctrl+N starts a fresh note (auto-saves current)
- **Delete note** — Ctrl+Delete or via hamburger menu, with confirmation dialog
- **Pack notes** — archive all notes to ZIP, tar.gz, or tar.xz with confirmation dialog and optional cleanup
- **Configurable sort order** — newest first, oldest first, or random
- **Confirmation dialogs** — optional confirmation before destructive actions (can be disabled in Settings)

## Requirements

- GTK 4 (>= 4.0)
- libadwaita (>= 1.0)
- GCC with C17 support
- `pkg-config`

SQLite is bundled as an amalgamation — no system SQLite dependency needed.

### Fedora / RHEL

```sh
sudo dnf install libadwaita-devel gcc make pkg-config
```

### Ubuntu / Debian

```sh
sudo apt install libadwaita-1-dev gcc make pkg-config
```

### Arch Linux

```sh
sudo pacman -S libadwaita gcc make pkg-config
```

## Build

```sh
make
```

Binary is output to `build/notes-desktop`.

## Run

```sh
./build/notes-desktop
```

## Keyboard Shortcuts

| Shortcut       | Action                        |
|----------------|-------------------------------|
| Ctrl+N         | New note                      |
| Ctrl+S         | Save current file             |
| Ctrl+O         | Open a file (*.txt or all)    |
| Ctrl+F         | Focus search in sidebar       |
| F9             | Toggle sidebar                |
| Ctrl+Delete    | Delete current note           |
| Ctrl+Plus      | Zoom in                       |
| Ctrl+Minus     | Zoom out                      |
| Ctrl+Q         | Quit                          |

## Configuration

Settings are stored in `~/.config/notes-desktop/settings.conf`.
Notes are saved to `~/Notes/` by default (configurable in Settings).
The search index is stored at `~/.config/notes-desktop/notes_index.db` (rebuilt automatically if deleted).

## Documentation

- [Architecture](docs/architecture.md) — code structure, components, data flow
- [Diagrams](docs/diagrams.md) — Mermaid diagrams (widget tree, theme flow, lifecycle)
- [Dependencies](docs/dependencies.md) — runtime and build dependencies
- [Changelog](docs/changelog.md) — version history

## Author

krse

## License

MIT
