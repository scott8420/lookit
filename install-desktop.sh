#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-desktop.sh -- make Lookit findable by GNOME. The quick-settings-tile
# stone's first half (jot's / Fowler's shape). Run by install.sh; alone, it
# points at the build tree's binary.
#
#   the .desktop entry  -- Lookit in the app grid, and "Send clipboard" on its
#                          right-click menu (dock / app grid).
#   the D-Bus service   -- so the tile (and "Send clipboard") can START Lookit
#                          when it isn't running, then deliver the text.
#                          DBusActivatable without it is worse than neither:
#                          GNOME stops falling back to Exec.
#   the icon            -- the logo is compiled into the binary; the shell can't
#                          see that, so the app grid needs a file on disk.
#
# Everything under ~/.local/share; no root. `--uninstall` takes it all out.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Lookit"
LOGO="resources/icons/scalable/apps/io.github.scott8420.Lookit-symbolic.svg"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/scalable/apps"
SYMBOLIC="$DATA/icons/hicolor/symbolic/apps"
SERVICES="$DATA/dbus-1/services"

# ── Quick look (s005): lookit-peek answers Files' spacebar instead of Sushi ──
# A per-user D-Bus service file for Sushi's name wins over the system one.
PEEK_NAME="org.gnome.NautilusPreviewer"

# Stop whoever owns the previewer name right now (Sushi, or an older
# lookit-peek), so the next Space starts the one the service file names.
stop_previewer() {
    command -v gdbus >/dev/null || return 0
    local pid
    pid="$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
           --method org.freedesktop.DBus.GetConnectionUnixProcessID "$PEEK_NAME" 2>/dev/null \
           | grep -o '[0-9]\+' | head -1)"
    [ -n "$pid" ] && kill "$pid" 2>/dev/null && echo "Stopped the running previewer (pid $pid)."
    return 0
}

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$APPS/$APP_ID.desktop" "$ICONS/$APP_ID.svg" "$SYMBOLIC/$APP_ID-symbolic.svg" \
          "$SERVICES/$APP_ID.service" "$SERVICES/$PEEK_NAME.service"
    stop_previewer   # ours goes; Files starts Sushi again on the next Space
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
    echo "Removed Lookit's desktop entry, icons and D-Bus service."
    exit 0
fi

# install.sh passes the installed copy; run alone, the build tree's binary.
APP_EXEC="${APP_EXEC:-$(pwd)/build/lookit}"
if [ ! -x "$APP_EXEC" ]; then
    echo "No binary at $APP_EXEC -- run ./build.sh (or ./install.sh) first." >&2
    exit 1
fi

mkdir -p "$APPS" "$ICONS" "$SYMBOLIC" "$SERVICES"
sed "s|@EXEC@|$APP_EXEC|g" "data/$APP_ID.desktop.in" > "$APPS/$APP_ID.desktop"
sed "s|@EXEC@|$APP_EXEC|g" "data/$APP_ID.service.in" > "$SERVICES/$APP_ID.service"
# The glassy colour logo (tools/lookit_logo.py makes both).
cp "resources/icons/scalable/apps/$APP_ID.svg" "$ICONS/$APP_ID.svg"
cp "$LOGO" "$SYMBOLIC/$APP_ID-symbolic.svg"

# Quick look: the previewer sits next to the app. IS_SERVICE programs take no
# --gapplication-service (GApplication rejects it): plain Exec.
PEEK_EXEC="$(dirname "$APP_EXEC")/lookit-peek"
if [ -x "$PEEK_EXEC" ]; then
    printf '[D-BUS Service]\nName=%s\nExec=%s\n' "$PEEK_NAME" "$PEEK_EXEC" > "$SERVICES/$PEEK_NAME.service"
    stop_previewer
    echo "Quick look: Space in Files now opens Lookit's preview ($PEEK_EXEC)."
else
    echo "(No lookit-peek next to $APP_EXEC -- Files keeps its own previewer.)"
fi

command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" || true
command -v gtk4-update-icon-cache  >/dev/null && gtk4-update-icon-cache -qtf "$DATA/icons/hicolor" || true

echo "Installed:"
echo "  $APPS/$APP_ID.desktop"
echo "  $SERVICES/$APP_ID.service"
echo "  $ICONS/$APP_ID.svg"
echo ""
echo "Exec points at $APP_EXEC -- re-run this if it moves."
echo "Try: $APP_EXEC --send \"hello from the command line\""
