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

Deps (Fedora): `gtkmm4.0-devel spdlog-devel poppler-glib-devel qpdf-devel cmake gcc-c++`.
Deps (Ubuntu/Debian): `libgtkmm-4.0-dev libspdlog-dev libpoppler-glib-dev libqpdf-dev cmake g++`.

## Page tools

Rotate (Ctrl+L / Ctrl+R), delete (Delete), duplicate (Ctrl+D), drag
thumbnails to rearrange, drop PDFs or pictures between thumbnails to insert
them, Insert Pages from File (Ctrl+I), Combine Files into One, Extract Pages
(Ctrl+E). Every change is undoable (Ctrl+Z / Ctrl+Shift+Z); Save (Ctrl+S)
writes the PDF with qpdf.

## Quick look in Files

`./install.sh` also installs `lookit-peek` and makes it Files' spacebar
previewer (in place of Sushi): Space on a file shows PDFs and pictures in
Lookit's page view, text as text, anything else as a card. Space / Esc close,
Enter opens in Lookit, arrows move the selection. `./install.sh --uninstall`
gives Space back to Sushi.

## Install (per user, no root)

```
./install.sh              # app, desktop entry, D-Bus service, quick-settings tile
./install.sh --uninstall
```

The quick-settings tile appears after you log out and back in.

## License

MIT -- see LICENSE.
