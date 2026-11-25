#!/usr/bin/env bash
# tools/test_defaults.sh -- install-defaults.sh round trip in a throwaway home
# (s013). Never touches your real ~/.config or ~/.local: HOME, XDG_CONFIG_HOME,
# XDG_DATA_HOME and XDG_DATA_DIRS all point into a temp dir with a fake
# "system" holding Papers, Loupe, Image Viewer and another app.
#
#   ./tools/test_defaults.sh        prints one line per check, exits 1 on a failure
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$(pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_DATA_HOME="$T/home/.local/share"
export XDG_DATA_DIRS="$T/sys" XDG_CONFIG_DIRS="$T/sysconf" LC_ALL=C
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/applications" "$T/sys/applications" "$T/sysconf"

pass=0 fail=0
check() { if eval "$2"; then echo "ok    $1"; pass=$((pass + 1)); else echo "FAIL  $1"; fail=$((fail + 1)); fi; }
opens() { gio mime "$1" 2>/dev/null | sed -n 's/^Default application for .*: //p'; }
app() {   # id mimetypes
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=/bin/true %%f\nMimeType=%s\n\n[Desktop Action new-window]\nName=New\nExec=/bin/true\n' \
        "$1" "$2" > "$T/sys/applications/$1.desktop"
}
app org.gnome.Papers  'application/pdf;'
app org.gnome.Loupe   'image/png;image/jpeg;image/gif;'
app org.gnome.eog     'image/png;'
app my.Other          'image/jpeg;image/gif;'
printf '[Default Applications]\napplication/pdf=org.gnome.Papers.desktop\nimage/png=org.gnome.Loupe.desktop\nimage/gif=org.gnome.Loupe.desktop\n' \
    > "$T/sys/applications/mimeapps.list"
# The user's own choices before Lookit: one default, one added association.
printf '[Added Associations]\nimage/jpeg=my.Other.desktop;\n\n[Default Applications]\nimage/jpeg=my.Other.desktop\n' \
    > "$XDG_CONFIG_HOME/mimeapps.list"
# A copy of Image Viewer's entry the user made themselves (must survive).
printf '[Desktop Entry]\nType=Application\nName=Mine\nExec=/bin/true\n' > "$XDG_DATA_HOME/applications/org.gnome.eog.desktop"
cp "$XDG_DATA_HOME/applications/org.gnome.eog.desktop" "$T/own-eog"
defaults_of() { awk '/^\[/{s=$0;next} s=="[Default Applications]" && /=/' "$1" | sort; }
defaults_of "$XDG_CONFIG_HOME/mimeapps.list" > "$T/before"

check "before: PDF opens with Papers"            '[ "$(opens application/pdf)" = org.gnome.Papers.desktop ]'
check "set refuses without the desktop entry"    '! ./install-defaults.sh >/dev/null 2>&1'

# Lookit's desktop entry, as install-desktop.sh writes it.
sed "s|@EXEC@|/bin/true|g" data/io.github.scott8420.Lookit.desktop.in > "$XDG_DATA_HOME/applications/io.github.scott8420.Lookit.desktop"
L=io.github.scott8420.Lookit.desktop
NTYPES="$(sed -n 's/^MimeType=//p' data/$L.in | tr ';' '\n' | sed '/^$/d' | wc -l)"

./install-defaults.sh --hide-others > "$T/out1" 2>&1
REC="$XDG_DATA_HOME/lookit/previous-defaults"
check "set: PDF opens with Lookit"               '[ "$(opens application/pdf)" = $L ]'
check "set: JPEG, EPUB, DjVu open with Lookit"   '[ "$(opens image/jpeg)" = $L ] && [ "$(opens application/epub+zip)" = $L ] && [ "$(opens image/vnd.djvu)" = $L ]'
check "set: one record line per type ($NTYPES)"  '[ "$(wc -l < "$REC")" = "$NTYPES" ]'
check "set: no-user-default recorded as -"       'grep -qx "application/pdf	-" "$REC"'
check "set: user default recorded word for word" 'grep -qx "image/jpeg	my.Other.desktop" "$REC"'
check "hide: Papers + Loupe copies, marked"      'grep -q "^NoDisplay=true" "$XDG_DATA_HOME/applications/org.gnome.Papers.desktop" && grep -q "^X-Lookit-Hidden=true" "$XDG_DATA_HOME/applications/org.gnome.Loupe.desktop"'
check "hide: NoDisplay only in [Desktop Entry]"  '[ "$(awk "/^\\[/{s=\$0} /^NoDisplay/{print s}" "$XDG_DATA_HOME/applications/org.gnome.Papers.desktop")" = "[Desktop Entry]" ]'
check "hide: the user's own eog copy untouched"  'cmp -s "$T/own-eog" "$XDG_DATA_HOME/applications/org.gnome.eog.desktop"'
check "hide: dnf only suggested, never run"      'grep -q "never runs that" "$T/out1"'


# The user points GIFs at another app after Lookit: restore must leave that be.
# (Edited in place, not with gio: some GLib versions tidy the whole file when
# they write it, which would hide a restore that forgets to tidy.)
sed -i 's/^image\/gif=io\.github\.scott8420\.Lookit\.desktop$/image\/gif=my.Other.desktop/' "$XDG_CONFIG_HOME/mimeapps.list"
check "user's later choice in place"             '[ "$(opens image/gif)" = my.Other.desktop ]'
./install-defaults.sh --status > "$T/status" 2>&1
check "status: lists every type"                 '[ "$(grep -c "/" "$T/status")" -ge "$NTYPES" ]'

./install-defaults.sh --restore > "$T/out2" 2>&1
check "restore: PDF opens with Papers again"     '[ "$(opens application/pdf)" = org.gnome.Papers.desktop ]'
check "restore: PNG opens with Loupe again"      '[ "$(opens image/png)" = org.gnome.Loupe.desktop ]'
check "restore: JPEG back to the user's choice"  '[ "$(opens image/jpeg)" = my.Other.desktop ]'
check "restore: the user's later GIF choice kept" '[ "$(opens image/gif)" = my.Other.desktop ] && grep -qx "image/gif=my.Other.desktop" "$XDG_CONFIG_HOME/mimeapps.list"'
check "restore: Lookit named nowhere as default" '! awk "/^\\[/{s=\$0;next} s==\"[Default Applications]\"" "$XDG_CONFIG_HOME/mimeapps.list" | grep -q Lookit'
check "restore: Lookit gone from added assocs"   '! grep -q Lookit "$XDG_CONFIG_HOME/mimeapps.list"'
defaults_of "$XDG_CONFIG_HOME/mimeapps.list" | grep -v '^image/gif=' > "$T/after"
check "restore: user defaults exactly as before" 'cmp -s "$T/before" "$T/after"'
check "restore: the record is gone"              '[ ! -f "$REC" ]'
check "restore: hidden viewers shown again"      '[ ! -f "$XDG_DATA_HOME/applications/org.gnome.Papers.desktop" ] && [ ! -f "$XDG_DATA_HOME/applications/org.gnome.Loupe.desktop" ]'
check "restore: the user's own eog copy kept"    'cmp -s "$T/own-eog" "$XDG_DATA_HOME/applications/org.gnome.eog.desktop"'

# Second round. (Last: some GLib versions drop the [Added Associations] lines
# when a default is set a second time, which would hide the tidy check above.)
./install-defaults.sh >/dev/null 2>&1
cp "$REC" "$T/rec1"
./install-defaults.sh >/dev/null 2>&1
check "set twice: the record is not overwritten" 'cmp -s "$T/rec1" "$REC"'
./install-defaults.sh --restore >/dev/null 2>&1
check "second round: PDF opens with Papers"      '[ "$(opens application/pdf)" = org.gnome.Papers.desktop ]'

./install-defaults.sh --restore > "$T/out3" 2>&1
check "restore twice: harmless"                  'grep -q "nothing to restore" "$T/out3"'
check "--help prints the undo command"           './install-defaults.sh --help | grep -q "install-defaults.sh --restore"'
check "unknown option refused"                   '! ./install-defaults.sh --bogus >/dev/null 2>&1'

echo ""
echo "test_defaults: $pass passed, $fail failed"
[ "$fail" = 0 ]
