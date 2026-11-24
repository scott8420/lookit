# Lookit

A quick look at PDFs and pictures for GNOME, in the spirit of macOS Preview:
view, mark up, rearrange and sign PDFs; view and lightly edit images; and a
spacebar quick look, in Lookit and in Files.

**Status:** early. Opens and shows PDFs and pictures: continuous scroll, zoom
(keys, Ctrl+wheel, pinch), fit width / fit page, recent files, drag-and-drop.

## Build & run

```
./build.sh
./build/lookit
./build/lookit_selftest      # the GTK-free core, headless
```

Deps (Fedora): `gtkmm4.0-devel spdlog-devel poppler-glib-devel cmake gcc-c++`.
Deps (Ubuntu/Debian): `libgtkmm-4.0-dev libspdlog-dev libpoppler-glib-dev cmake g++`.

## Install (per user, no root)

```
./install.sh              # app, desktop entry, D-Bus service, quick-settings tile
./install.sh --uninstall
```

The quick-settings tile appears after you log out and back in.

## License

MIT -- see LICENSE.
