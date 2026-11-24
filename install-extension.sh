#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-extension.sh -- put the Lookit tile in GNOME's quick settings panel.
# The quick-settings-tile stone's second half (jot's / Fowler's installer).
#
# The tile hands text to Lookit over D-Bus (ActivateAction "receive-text"),
# which needs the desktop entry + D-Bus service install-desktop.sh writes --
# so that runs first if it has not.
#
# Copies extension/lookit-tile@scott8420.github.io (plus the logo, from
# resources/ -- one copy of the icon in the tree) into
# ~/.local/share/gnome-shell/extensions and enables it. On Wayland GNOME Shell
# only sees a NEW extension after you log out and back in; after that,
# re-running this to update it ALSO needs a log out / in: the shell caches the
# old code.
#
# `--uninstall` disables it and takes it back out.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Lookit"
UUID="lookit-tile@scott8420.github.io"
LOGO="resources/icons/scalable/apps/lookit-logo-symbolic.svg"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/gnome-shell/extensions/$UUID"

if [ "${1:-}" = "--uninstall" ]; then
    gnome-extensions disable "$UUID" 2>/dev/null || true
    rm -rf "$DEST"
    echo "Removed the Lookit tile. Log out and in to clear it from the shell."
    exit 0
fi

if [ ! -f "$DATA/applications/$APP_ID.desktop" ]; then
    echo "Lookit's desktop entry is not installed -- running install-desktop.sh first."
    ./install-desktop.sh
    echo ""
fi

NEW=1
[ -d "$DEST" ] && NEW=0
mkdir -p "$DEST/icons"
cp -r "extension/$UUID/." "$DEST/"
cp "$LOGO" "$DEST/icons/"
echo "Installed: $DEST"

# Enabled by gsettings, not `gnome-extensions enable`: on Wayland the shell
# does not know a brand-new extension until the next login, and the CLI refuses
# one it does not know. The key is what the shell reads at login.
enable_at_login() {
    command -v gsettings >/dev/null || return 0
    gsettings list-schemas 2>/dev/null | grep -qx org.gnome.shell || { echo "(No GNOME Shell settings here -- enable it by hand: gnome-extensions enable $UUID)"; return 0; }
    local cur
    cur="$(gsettings get org.gnome.shell enabled-extensions)"
    case "$cur" in *"'$UUID'"*) return 0 ;; esac
    if [ "$cur" = "@as []" ] || [ "$cur" = "[]" ]; then
        gsettings set org.gnome.shell enabled-extensions "['$UUID']"
    else
        gsettings set org.gnome.shell enabled-extensions "${cur%]}, '$UUID']"
    fi
}
enable_at_login

if [ "$NEW" = "1" ]; then
    echo ""
    echo "First install: log out and back in, then the Lookit tile is in quick settings."
    echo "(If it is not, run: gnome-extensions enable $UUID)"
else
    echo "Updated. Log out and back in to load the new code."
fi
