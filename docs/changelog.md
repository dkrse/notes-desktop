# Changelog

## 1.0.0 — 2026-03-28

### Initial Release

#### Editor
- Simple single-file text editor with GtkTextView
- Word-char wrapping, configurable margins
- Ctrl+S to save (overwrites existing file or creates new timestamped file)
- Ctrl+O to open a file from the save directory (*.txt or all files filter)
- Ctrl+Plus / Ctrl+Minus to zoom in/out (persisted to config)
- Ctrl+Q to quit
- Auto-save on window close, restore last file on launch
- Clear button in header bar — saves current note with timestamp, clears editor
- Font intensity control (0.3–1.0) via GtkTextTag foreground alpha
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
