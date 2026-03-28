# Notes Desktop

A fast, lightweight notes application built with GTK 4, libadwaita, and C17.

## Features

- **Simple text editor** — no tabs, no distractions, just writing
- **13 color themes** — System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha
- **Proper dark/light switching** — via libadwaita AdwStyleManager, all widgets update instantly
- **Current line highlight** — semi-transparent overlay that works on all themes including empty lines
- **Line numbers** — optional, with synchronized scrolling and dynamic width
- **Font intensity** — adjustable text opacity (0.3–1.0)
- **Word wrap** — toggle line wrapping on/off
- **Configurable line spacing** — 1, 1.2, 1.5, 2
- **Font picker** — choose any system font and size
- **Zoom** — Ctrl+Plus / Ctrl+Minus to adjust font size
- **Auto-save** — buffer is saved on close and restored on next launch
- **Clear** — saves current note with timestamp, clears the editor for a fresh page
- **Pack notes** — archive all notes to ZIP, tar.gz, or tar.xz with optional cleanup
- **Status bar** — shows text encoding (UTF-8) and cursor position (Ln/Col)

## Requirements

- GTK 4 (>= 4.0)
- libadwaita (>= 1.0)
- GCC with C17 support
- `pkg-config`

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
| Ctrl+S         | Save current file             |
| Ctrl+O         | Open a file (*.txt or all)    |
| Ctrl+Plus      | Zoom in                       |
| Ctrl+Minus     | Zoom out                      |
| Ctrl+Q         | Quit                          |

## Configuration

Settings are stored in `~/.config/notes-desktop/settings.conf`.
Notes are saved to `~/Notes/` by default (configurable in Settings).

## Documentation

- [Architecture](docs/architecture.md) — code structure, components, data flow
- [Diagrams](docs/diagrams.md) — Mermaid diagrams (widget tree, theme flow, lifecycle)
- [Dependencies](docs/dependencies.md) — runtime and build dependencies
- [Changelog](docs/changelog.md) — version history

## Author

krse

## License

MIT
