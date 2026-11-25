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

Deps (Fedora): `gtkmm4.0-devel spdlog-devel poppler-glib-devel qpdf-devel djvulibre-devel cmake gcc-c++`.
Deps (Ubuntu/Debian): `libgtkmm-4.0-dev libspdlog-dev libpoppler-glib-dev libqpdf-dev libdjvulibre-dev cmake g++`.
(s012 added djvulibre, for DjVu. `lookit_savecheck` also makes a DjVu with djvulibre's
tools -- `djvulibre` / `djvulibre-bin` -- and skips that part without them.)

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

## Forms and passwords

A PDF form's fields are tinted pale blue. Click one to fill it: a check box
ticks, a round choice is picked, a text field or a drop-down / list opens a
small box over it (Enter keeps what you typed, Ctrl+Enter in a box of several
lines, Tab / Shift+Tab go on to the next / previous field, Esc leaves it as
it was). Each fill is one Ctrl+Z, in the same undo list as the page tools.
Ctrl+S writes the values into the PDF -- still a form, readable and editable
in any other reader.

A PDF locked with a password asks for it when you open it (and again if it
was wrong); Lookit keeps it only until you quit. Saving a locked PDF keeps
its lock and password. Pages menu -> Password... adds a password (AES-256),
changes it, or removes it; the change is one Ctrl+Z and is written by the
next Save. The Info drawer (Shift+F9) says whether the PDF has a password
and how many fields its form has.

## DjVu, comics and books

DjVu files open like PDFs: pages, thumbnails, Book view, zoom, export, and
Ctrl+F when the file has a text layer (scanned books often do). Comics (CBZ)
open as their pictures, one per page, in name order ("page2" before
"page10"). EPUB books show their cover, then the chapters' text laid out on
6 x 9 in pages (headings and paragraphs -- a look inside, not a reading app:
no styles or pictures inside chapters yet); Ctrl+F finds words in them.
These are for reading: the page tools, markup and Save are for PDFs. Space in
Files shows all three too.

## Pictures

Open a picture and the picture bar appears under the toolbar. Left / Right
(or its arrows) walk the pictures in the same folder, in name order ("img2"
before "img10"). Rotate left / right (Ctrl+L / Ctrl+R), flip left-right or
upside down (Ctrl+Shift+H / V), crop (Ctrl+Shift+X: drag a frame or its
corners, Enter crops, Esc cancels), resize (Ctrl+Shift+R: width x height,
in proportion, or 25 / 50 / 75%), light (Ctrl+Shift+L: brightness, contrast,
saturation -- one Ctrl+Z undoes a whole visit to the sliders), revert to the
original. Every edit is one Ctrl+Z; nothing touches the file until Ctrl+S,
which writes over it in its own format (PNG, JPEG, TIFF, BMP). Ctrl+Shift+S
saves as any of those or a PDF, by the name you give. A kind Lookit can't
write (WebP, GIF, SVG, HEIC...) is offered as PNG. Leaving an edited picture
asks first. Saving drops the file's metadata (EXIF) -- backlog.

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
