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

if [ "${1:-}" = "--uninstall" ]; then
    ./install-extension.sh --uninstall
    ./install-desktop.sh --uninstall
    rm -f "$BIN/lookit"
    rm -rf "$LIB"
    echo ""
    echo "Lookit uninstalled. Kept: ~/.local/share/lookit (recents)."
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

APP_EXEC="$LIB/lookit" ./install-desktop.sh
echo ""
./install-extension.sh
