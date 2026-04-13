# Dependencies

## Runtime

| Dependency      | Version  | Purpose                                      |
|-----------------|----------|----------------------------------------------|
| GTK 4           | >= 4.0   | UI toolkit — widgets, text rendering, CSS     |
| libadwaita      | >= 1.0   | Theme management — AdwStyleManager for runtime dark/light switching |
| WebKitGTK       | 6.0      | Markdown preview — marked.js, KaTeX, Mermaid  |
| GLib / GIO      | >= 2.0   | Core utilities — file I/O, settings, memory   |
| Pango           | >= 1.0   | Font measurement for line number gutter width |
| zip (optional)  | any      | Pack notes to ZIP archive                     |
| tar (optional)  | any      | Pack notes to tar.gz / tar.xz archive         |

**Note:** SQLite is **bundled** as an amalgamation (`src/sqlite3/sqlite3.c`) compiled with `-DSQLITE_ENABLE_FTS5`. No system SQLite library is required.

## Build

| Dependency      | Version  | Purpose                          |
|-----------------|----------|----------------------------------|
| GCC             | C17      | Compiler                         |
| make            | any      | Build system                     |
| pkg-config      | any      | Locates library flags            |
| libadwaita-devel| >= 1.0   | Headers (pulls in gtk4-devel)    |
| webkitgtk6.0-devel | 6.0  | WebKitGTK headers                |

## Bundled

| Component       | Version    | Purpose                                  |
|-----------------|------------|------------------------------------------|
| SQLite          | 3.45.1     | Full-text search index (FTS5) for notes  |
| marked.js       | latest     | Markdown to HTML parser (in data/web/js) |
| KaTeX           | latest     | LaTeX math rendering (in data/web/js)    |
| Mermaid         | latest     | Diagram rendering (in data/web/js)       |

The SQLite amalgamation is compiled as a separate object with relaxed warnings (`-w`) and FTS5 enabled. It adds ~1MB to the binary size.

## Tested On

| OS              | Desktop         | GTK    | libadwaita |
|-----------------|-----------------|--------|------------|
| Fedora 43       | GNOME (Wayland) | 4.18   | 1.8        |

## Install Commands

### Fedora / RHEL

```sh
sudo dnf install libadwaita-devel webkitgtk6.0-devel gcc make pkg-config
```

### Ubuntu / Debian

```sh
sudo apt install libadwaita-1-dev libwebkitgtk-6.0-dev gcc make pkg-config
```

### Arch Linux

```sh
sudo pacman -S libadwaita webkitgtk-6.0 gcc make pkg-config
```

## Why libadwaita?

GTK 4 alone cannot reliably switch between dark and light themes at runtime. The `gtk-application-prefer-dark-theme` property does not trigger a full restyle of headerbar, popover, window controls, and other shell widgets when going from dark to light.

libadwaita's `AdwStyleManager` with `adw_style_manager_set_color_scheme()` solves this completely — it forces a proper theme reload across all widgets, including:

- Window decorations and controls (minimize, maximize, close)
- Header bar and its buttons
- Popover menus
- Scrollbars
- All standard GTK widgets

The application uses `AdwApplication` (a subclass of `GtkApplication`) as the entry point, which initializes the libadwaita style manager automatically.

## Why bundled SQLite?

Many Linux distribution packages of SQLite are compiled **without FTS5 support**. Bundling the amalgamation guarantees that FTS5 is always available regardless of system configuration. The amalgamation compiles as a single C file in ~5 seconds and is the [recommended approach](https://www.sqlite.org/amalgamation.html) for applications embedding SQLite.
