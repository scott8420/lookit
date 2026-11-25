#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-defaults.sh -- make Lookit the default app for PDFs, pictures, DjVu,
# comics and books, and put things back exactly as they were. (s013, road 11.)
#
# Careful and reversible, by construction:
#   * The file types come from data/io.github.scott8420.Lookit.desktop.in's
#     MimeType= line -- one list, so the desktop entry and the defaults can't
#     drift.
#   * Before the first change, each type's own line in YOUR mimeapps.list
#     ([Default Applications], ~/.config/mimeapps.list) is written down in
#     ~/.local/share/lookit/previous-defaults ("-" = you had none, so the
#     system default applied). Running this again never overwrites that record,
#     so it always holds what you had before Lookit.
#   * --restore puts each recorded line back (or removes ours, so the system
#     default applies again) -- but only where Lookit is still the default. A
#     type you've since pointed at another app yourself is left alone.
#   * --hide-others (optional) hides Papers, Evince, Loupe and Image Viewer
#     from the app grid with per-user copies of their desktop entries
#     (NoDisplay=true, marked X-Lookit-Hidden=true). Nothing is uninstalled;
#     --restore deletes only copies carrying that mark. A copy you made
#     yourself is never touched.
#   * Sushi (Files' spacebar preview) is already stepped aside by the quick-look
#     service install-desktop.sh writes; uninstalling Lookit brings it back.
#
# No root, no dnf: removal of other apps is only ever printed as a suggestion.
# ─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_ID="io.github.scott8420.Lookit"
DESKTOP="$APP_ID.desktop"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
MIMEAPPS="$CONFIG/mimeapps.list"
RECORD="$DATA/lookit/previous-defaults"
APPS="$DATA/applications"
# The viewers GNOME ships for these files (Flatpak or RPM: same desktop IDs).
OTHERS="org.gnome.Papers org.gnome.Evince org.gnome.Loupe org.gnome.eog"

usage() {
    cat <<HELP
Usage: ./install-defaults.sh [--hide-others] | --restore | --status | --help

  ./install-defaults.sh               make Lookit the default for every type it
                                      opens (PDF, DjVu, CBZ, EPUB, pictures);
                                      what you had is written down first
  ./install-defaults.sh --hide-others same, and also hide Papers / Evince /
                                      Loupe / Image Viewer from the app grid
                                      (nothing is uninstalled)
  ./install-defaults.sh --restore     put your previous defaults back and show
                                      the hidden apps again
  ./install-defaults.sh --status      which app opens each type right now
  ./install-defaults.sh --help        this text

  ./install.sh --default does the install and this in one go;
  ./install.sh --uninstall runs --restore for you.

Record of what you had:  $RECORD
Undo:                    $(pwd)/install-defaults.sh --restore
HELP
}

HIDE=0
ACTION=set
for a in "$@"; do
    case "$a" in
        -h|--help) usage; exit 0 ;;
        --hide-others) HIDE=1 ;;
        --restore) ACTION=restore ;;
        --status) ACTION=status ;;
        *) echo "Unknown option: $a" >&2; usage >&2; exit 2 ;;
    esac
done

command -v gio >/dev/null || { echo "This needs 'gio' (Fedora: glib2; Ubuntu: libglib2.0-bin)." >&2; exit 1; }

# The single list: the desktop entry's MimeType= line.
types() {
    sed -n 's/^MimeType=//p' "data/$DESKTOP.in" | tr ';' '\n' | sed '/^$/d'
}

# The value of TYPE's line in [Default Applications] of the user's
# mimeapps.list, or nothing.
user_default() {
    [ -f "$MIMEAPPS" ] || return 0
    awk -v t="$1" '
        /^\[/ { sec = $0; next }
        sec == "[Default Applications]" {
            i = index($0, "="); if (i && substr($0, 1, i - 1) == t) { print substr($0, i + 1); exit }
        }' "$MIMEAPPS"
}

# Drop TYPE's line from [Default Applications] (unless $2 = keep-default), and
# our ID from its [Added Associations] line (gio adds both when it sets a
# default; the added one is always ours to take back).
forget_type() {
    [ -f "$MIMEAPPS" ] || return 0
    awk -v t="$1" -v id="$DESKTOP" -v keep="${2:-}" '
        /^\[/ { sec = $0; print; next }
        {
            i = index($0, "=")
            if (i && substr($0, 1, i - 1) == t) {
                if (sec == "[Default Applications]" && keep == "") next
                if (sec == "[Added Associations]") {
                    n = split(substr($0, i + 1), v, ";"); out = ""
                    for (k = 1; k <= n; k++) if (v[k] != "" && v[k] != id) out = out v[k] ";"
                    if (out == "") next
                    print t "=" out; next
                }
            }
            print
        }' "$MIMEAPPS" > "$MIMEAPPS.lookit-tmp" && mv -f "$MIMEAPPS.lookit-tmp" "$MIMEAPPS"
}

# Is Lookit what the user's mimeapps.list names first for TYPE?
ours() { case "$(user_default "$1")" in "$DESKTOP"|"$DESKTOP;"*) return 0 ;; esac; return 1; }

# Who opens TYPE right now (as GNOME resolves it, system defaults included).
resolved() {
    LC_ALL=C gio mime "$1" 2>/dev/null | sed -n 's/^Default application for .*: //p'
}

find_desktop() {   # first $1.desktop on the XDG data path, skipping ours
    local d IFS=:
    for d in ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
        [ -f "$d/applications/$1.desktop" ] && { echo "$d/applications/$1.desktop"; return 0; }
    done
    return 1
}

hide_others() {
    mkdir -p "$APPS"
    local id src dst hidden=""
    for id in $OTHERS; do
        dst="$APPS/$id.desktop"
        src="$(find_desktop "$id")" || continue
        if [ -f "$dst" ]; then
            grep -q '^X-Lookit-Hidden=true' "$dst" || echo "  $id: you already have your own $dst -- left alone."
            continue
        fi
        # Same entry, hidden from the grid; it still opens files if asked to.
        awk '
            /^\[/ { if (main) { print "NoDisplay=true"; print "X-Lookit-Hidden=true" } main = ($0 == "[Desktop Entry]"); print; next }
            main && /^NoDisplay=/ { next }
            { print }
            END { if (main) { print "NoDisplay=true"; print "X-Lookit-Hidden=true" } }' "$src" > "$dst"
        hidden="$hidden $id"
    done
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
    if [ -n "$hidden" ]; then
        echo "Hidden from the app grid:$hidden"
        echo "(They are still installed. To remove the RPMs yourself, if you want:"
        echo "   sudo dnf remove papers evince loupe eog"
        echo " -- this script never runs that.)"
    else
        echo "No other viewers to hide."
    fi
}

show_others() {
    local f n=0
    for f in "$APPS"/*.desktop; do
        [ -f "$f" ] && grep -q '^X-Lookit-Hidden=true' "$f" && { rm -f "$f"; n=$((n + 1)); }
    done
    [ "$n" -gt 0 ] && echo "Showing $n hidden viewer(s) in the app grid again."
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
    return 0
}

case "$ACTION" in
status)
    printf '%-34s %s\n' "TYPE" "OPENS WITH"
    while read -r t; do
        r="$(resolved "$t")"; printf '%-34s %s\n' "$t" "${r:-(none)}"
    done < <(types)
    [ -f "$RECORD" ] && echo "" && echo "Previous defaults recorded in $RECORD (undo: ./install-defaults.sh --restore)."
    exit 0 ;;

restore)
    if [ -f "$RECORD" ]; then
        put=0 kept=0
        while IFS=$'\t' read -r t prev; do
            [ -n "$t" ] || continue
            if ! ours "$t"; then   # you changed it since: yours
                forget_type "$t" keep-default; kept=$((kept + 1)); continue
            fi
            forget_type "$t"
            if [ "$prev" != "-" ]; then
                # Put your own line back, word for word.
                printf '%s=%s\n' "$t" "$prev" > "$MIMEAPPS.lookit-line"
                awk -v f="$MIMEAPPS.lookit-line" '
                    { print } $0 == "[Default Applications]" { while ((getline l < f) > 0) print l; done = 1 }
                    END { if (!done) { print "[Default Applications]"; while ((getline l < f) > 0) print l } }' \
                    "$MIMEAPPS" > "$MIMEAPPS.lookit-tmp" && mv -f "$MIMEAPPS.lookit-tmp" "$MIMEAPPS"
                rm -f "$MIMEAPPS.lookit-line"
            fi
            put=$((put + 1))
        done < "$RECORD"
        rm -f "$RECORD"
        echo "Restored $put file type(s) to what you had before Lookit."
        [ "$kept" -gt 0 ] && echo "Left $kept type(s) alone: you've pointed them at another app since."
    else
        # No record (never set, or already restored): just make sure none of
        # our default lines linger.
        n=0
        while read -r t; do ours "$t" && { forget_type "$t"; n=$((n + 1)); }; done < <(types)
        [ "$n" -gt 0 ] && echo "Removed Lookit as the default for $n type(s)." || echo "Lookit isn't a default for anything -- nothing to restore."
    fi
    show_others
    exit 0 ;;
esac

# ── set ──────────────────────────────────────────────────────────────────────
if [ ! -f "$APPS/$DESKTOP" ]; then
    echo "Lookit's desktop entry isn't installed ($APPS/$DESKTOP)." >&2
    echo "Run ./install.sh first (or ./install.sh --default to do both)." >&2
    exit 1
fi
mkdir -p "$(dirname "$RECORD")" "$CONFIG"
touch "$RECORD"
n=0
while read -r t; do
    # Write down what you had -- only the first time, so the record always
    # holds the pre-Lookit state.
    if ! grep -q "^$t	" "$RECORD"; then
        prev="$(user_default "$t")"
        printf '%s\t%s\n' "$t" "${prev:--}" >> "$RECORD"
    fi
    if gio mime "$t" "$DESKTOP" >/dev/null 2>&1; then n=$((n + 1)); else echo "  (couldn't set $t)"; fi
done < <(types)
echo "Lookit is now the default for $n file type(s): PDF, DjVu, CBZ, EPUB and pictures."
echo "What you had is written down in $RECORD."
[ "$HIDE" = 1 ] && hide_others
echo ""
echo "See:   $(pwd)/install-defaults.sh --status"
echo "Undo:  $(pwd)/install-defaults.sh --restore"
