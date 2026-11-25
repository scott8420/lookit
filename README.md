# Lookit

A quick look at PDFs and pictures for GNOME, in the spirit of macOS Preview:
view, mark up, rearrange and sign PDFs; view and lightly edit images; and a
spacebar quick look, in Lookit and in Files.

**Status:** early. Opens and shows PDFs and pictures: paged or continuous,
zoom (keys, Ctrl+wheel, pinch), fit width / fit window, thumbnails, find,
recent files, drag-and-drop. Page tools, signatures and export (below).

## Build & run

```
./build.sh
./build/lookit
./build/lookit_selftest      # the GTK-free core, headless
./build/lookit_savecheck     # a saved signature lands where it was seen (headless)
```

Deps (Fedora): `gtkmm4.0-devel spdlog-devel poppler-glib-devel qpdf-devel cmake gcc-c++`.
Deps (Ubuntu/Debian): `libgtkmm-4.0-dev libspdlog-dev libpoppler-glib-dev libqpdf-dev cmake g++`.

## Page tools

Rotate (Ctrl+L / Ctrl+R), delete (Delete), duplicate (Ctrl+D), drag
thumbnails to rearrange, drop PDFs or pictures between thumbnails to insert
them, Insert Pages from File (Ctrl+I), Combine Files into One, Extract Pages
(Ctrl+E). Every change is undoable (Ctrl+Z / Ctrl+Shift+Z); Save (Ctrl+S)
writes the PDF with qpdf.

## Sign

Add Signature (the pen button, or Ctrl+Shift+I). The first time, sign on the
pad -- or Import Picture… of your signature on paper (white drops out).
Lookit keeps it (`~/.local/share/lookit/signatures`) and from then on puts it
straight on the page you're on. Click it to select; drag to move; drag its
corner to size it; Delete removes it; Ctrl+Z undoes any of it. Save writes it
into the PDF. Pages menu -> Signature… redraws or replaces it.

## Markup

Ctrl+Shift+A (or Pages menu -> Markup) opens the markup bar: Select,
Highlight (sweep over text -- it snaps to the lines), Note (click), Box,
Ellipse, Line, Arrow, Text (click, or drag the width), and six colours.
Select a mark to move it, drag its corner (a line: its ends) to size it,
pick a colour to recolour it, double-click a note or text to edit it, Delete
to remove it. Every change is one Ctrl+Z. Save draws the marks into the PDF;
notes become real PDF sticky notes other readers can open.

## Export

Ctrl+Shift+E (or File / Pages menu -> Export…): PDF pages as PNG, JPEG or
TIFF at 72-600 dpi (all, or "1-3, 7"), or the PDF's text as .txt; a picture as
PNG, JPEG, TIFF, BMP or PDF. Many pages go into a folder, one file each
(`name-p01.png`). Exports are what's on screen, unsaved changes included.

## Quick look in Files

`./install.sh` also installs `lookit-peek` and makes it Files' spacebar
previewer (in place of Sushi): Space on a file shows PDFs and pictures in
Lookit's page view, text as text, anything else as a card. Space / Esc close,
Enter opens in Lookit, arrows move the selection. `./install.sh --uninstall`
gives Space back to Sushi.

## Install (per user, no root)

```
./install.sh              # app, desktop entry, D-Bus service, quick-settings tile
./install.sh --uninstall  # take it all out (keeps recents + your signature)
./install.sh --help       # what each option does
```

Every installer (`install.sh`, `install-desktop.sh`, `install-extension.sh`)
takes `--help` and ends by printing how to use what it installed and the
command that removes it.

The quick-settings tile appears after you log out and back in.

## License

MIT -- see LICENSE.
