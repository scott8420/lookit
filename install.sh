#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install.sh -- install Lookit for this user: the app, its desktop entry and
# D-Bus service, and the quick-settings tile. No root; all under ~/.local.
# (Fowler's shape.)
#
#   ~/.local/lib/lookit/lookit   the binary (+ lookit-peek, Files' spacebar preview)
#   ~/.local/bin/lookit         a link to it, for the command line
#   ~/.local/share/...         desktop entry, D-Bus service, icons (install-desktop.sh)
#   ~/.local/share/gnome-shell/extensions/lookit-tile@...   the tile (install-extension.sh)
#
# Builds first (./build.sh is incremental). Re-run after pulling changes.
# Safe while Lookit runs: the binary is replaced, not overwritten; quit and
# reopen Lookit to use the new one. A new tile needs a log out / in.
#
# ./install.sh --uninstall  takes all of it out. Recents stay
# (~/.local/share/lookit).
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

LIB="$HOME/.local/lib/lookit"
BIN="$HOME/.local/bin"

usage() {
    cat <<'HELP'
Usage: ./install.sh [--uninstall | --help]

  ./install.sh              build, then install Lookit for this user (~/.local)
  ./install.sh --uninstall  take it all out again (app, desktop entry, quick
                            look, tile); your recents and signature stay in
                            ~/.local/share/lookit
  ./install.sh --help       this text
HELP
}

# What to do with it, printed at the end of every install.
how_to_use() {
    cat <<HELP
----------------------------------------------------------------------
Lookit is installed. How to use it:

  lookit                    open the window (or find Lookit in the app grid)
  lookit FILE.pdf           open a PDF or a picture
  Space in Files            quick look (Lookit's preview instead of Sushi)
  Ctrl+? in the window      every keyboard shortcut
  Pages menu (pencil icon)  rotate, delete, insert, combine, extract,
                            Password..., Add Signature, Markup, Export
  A PDF form                click a field to fill it (Tab: next field);
                            Ctrl+S saves the values into the PDF
  A locked PDF              Lookit asks for its password; Pages menu ->
                            Password... adds, changes or removes one
  DjVu, CBZ, EPUB           open like PDFs (read-only): pages, thumbnails,
                            Book view (Ctrl+B), find (Ctrl+F) where there's text
  Ctrl+Shift+A              markup: highlight, note, box, ellipse, line,
                            arrow, text
  A picture                 Left / Right walk its folder; the picture bar
                            rotates, flips, crops, resizes, adjusts light;
                            Ctrl+S saves, Ctrl+Shift+S saves as PNG / JPEG /
                            TIFF / BMP / PDF
  lookit --help             command-line options

Uninstall:  $(pwd)/install.sh --uninstall
----------------------------------------------------------------------
HELP
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    ""|--uninstall) ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
esac

if [ "${1:-}" = "--uninstall" ]; then
    ./install-extension.sh --uninstall
    ./install-desktop.sh --uninstall
    rm -f "$BIN/lookit"
    rm -rf "$LIB"
    echo ""
    echo "Lookit uninstalled. Kept: ~/.local/share/lookit (recents, your signature)."
    echo "To remove those too:  rm -rf ~/.local/share/lookit"
    echo "To install again:     $(pwd)/install.sh"
    exit 0
fi

./build.sh
for f in build/lookit build/lookit-peek; do
    [ -f "$f" ] || { echo "Missing $f after the build." >&2; exit 1; }
done

mkdir -p "$LIB" "$BIN"
# Copy beside, then rename: a running Lookit keeps its old file, nothing is
# half-written ("Text file busy" never happens).
install -m 755 build/lookit "$LIB/.lookit.new" && mv -f "$LIB/.lookit.new" "$LIB/lookit"
install -m 755 build/lookit-peek "$LIB/.lookit-peek.new" && mv -f "$LIB/.lookit-peek.new" "$LIB/lookit-peek"
ln -sfn "$LIB/lookit" "$BIN/lookit"
echo "Installed: $LIB/lookit, linked from $BIN/lookit"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "Note: $BIN is not on your PATH -- the tile and app grid don't need it." ;; esac
echo ""

APP_EXEC="$LIB/lookit" LOOKIT_QUIET_HELP=1 ./install-desktop.sh
echo ""
LOOKIT_QUIET_HELP=1 ./install-extension.sh
echo ""
how_to_use
