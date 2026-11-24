// Lookit Tile -- a tile in GNOME's quick settings panel: type or paste a line,
// press Enter, and Lookit comes up with it. The QUICK-SETTINGS-TILE stone,
// distilled from jot's capture tile and Fowler's check tile.
// (docs/quick-settings-tile.md)
//
// The tile does no work of the app's. It hands the text over D-Bus:
//   org.freedesktop.Application.ActivateAction("receive-text", [text])
// -- the same app action `lookit --send TEXT` uses, so the tile and the command
// line can't disagree. Lookit closed -> D-Bus starts it (the service file
// install-desktop.sh writes), then delivers the text.
//
// The shell reads the clipboard / selection itself (St.Clipboard), so nothing
// depends on Lookit having focus. Then the shell raises Lookit's window: a
// window raised from the shell isn't held back by focus-stealing prevention.
//
// RENAMING for a seeded app: APP_ID, OBJECT_PATH, APP_NAME, ACTION, ICON and the
// labels below; the folder + metadata.json uuid; the css class prefix.

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as QuickSettings from 'resource:///org/gnome/shell/ui/quickSettings.js';

import {sendRequest} from './lib.js';

const APP_ID = 'io.github.scott8420.Lookit';
const OBJECT_PATH = '/io/github/scott8420/Lookit';
const APP_NAME = 'Lookit';
const ACTION = 'receive-text';          // App.cpp: add_action_with_parameter
const ICON = 'io.github.scott8420.Lookit-symbolic.svg'; // copied in by install-extension.sh
const HINT = 'Enter opens it. Esc closes.';

function theApp() {
    return Shell.AppSystem.get_default().lookup_app(`${APP_ID}.desktop`);
}

// The app's window: through the app (desktop entry), else by its app id.
function theWindow() {
    const app = theApp();
    const wins = app ? app.get_windows() : [];
    if (wins.length > 0)
        return wins[0];
    for (const actor of global.get_window_actors()) {
        const w = actor.get_meta_window();
        if (w && (w.get_gtk_application_id?.() === APP_ID || w.get_wm_class() === APP_ID))
            return w;
    }
    return null;
}

const LookitTileToggle = GObject.registerClass(
class LookitTileToggle extends QuickSettings.QuickMenuToggle {
    constructor(extension) {
        const gicon = Gio.icon_new_for_string(
            `${extension.path}/icons/${ICON}`);
        super({
            title: APP_NAME,
            subtitle: 'Open',
            gicon,
            toggleMode: false,
        });

        this._busy = false;
        this._clearId = 0;
        this._raiseId = 0;

        this.menu.setHeader(gicon, APP_NAME, 'Open a file by its path');

        // ── the line ───────────────────────────────────────────────────────
        const lineItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false,
            style_class: 'lookit-tile-item',
        });
        this._entry = new St.Entry({
            hint_text: 'Paste a file path or file:// link…',
            can_focus: true,
            x_expand: true,
            style_class: 'lookit-tile-entry',
        });
        this._entry.clutter_text.connect('activate', () => this._fromLine());
        lineItem.add_child(this._entry);
        this.menu.addMenuItem(lineItem);

        // ── what happened ──────────────────────────────────────────────────
        this._status = new St.Label({
            text: HINT,
            style_class: 'lookit-tile-status',
            x_expand: true,
        });
        const statusItem = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false});
        statusItem.add_child(this._status);
        this.menu.addMenuItem(statusItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // ── without pasting ────────────────────────────────────────────────
        this.menu.addAction('Open path on the clipboard',
            () => this._fromClipboard(St.ClipboardType.CLIPBOARD, 'clipboard'));
        this.menu.addAction('Open selected path',
            () => this._fromClipboard(St.ClipboardType.PRIMARY, 'selection'));
        this.menu.addAction(`Open ${APP_NAME}`, () => this._openApp());

        // The tile's body opens the line too: there is nothing to toggle.
        this.connect('clicked', () => this.menu.open());

        // The cursor is in the line whenever the menu opens.
        this.menu.connect('open-state-changed', (_menu, open) => {
            if (!open)
                return;
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                this._entry.grab_key_focus();
                return GLib.SOURCE_REMOVE;
            });
        });
    }

    _say(text, error = false) {
        this._status.text = text;
        if (error)
            this._status.add_style_class_name('lookit-tile-error');
        else
            this._status.remove_style_class_name('lookit-tile-error');
        if (this._clearId)
            GLib.source_remove(this._clearId);
        this._clearId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 6, () => {
            this._clearId = 0;
            this._sayQuiet(HINT);
            return GLib.SOURCE_REMOVE;
        });
    }

    _sayQuiet(text) {
        this._status.text = text;
        this._status.remove_style_class_name('lookit-tile-error');
    }

    _fromLine() {
        this._send(sendRequest(this._entry.get_text(), 'line'), true);
    }

    _fromClipboard(type, where) {
        St.Clipboard.get_default().get_text(type, (_clip, text) => {
            this._send(sendRequest(text, where), false);
        });
    }

    // req: {text} | {error} | {} (nothing). fromLine: clear the line once the app took it.
    _send(req, fromLine) {
        if (this._busy)
            return;
        if (req.error) {
            this._say(req.error, true);
            return;
        }
        if (!req.text)
            return;

        this._busy = true;
        this._entry.reactive = false;
        this._say(`Sending to ${APP_NAME}…`);
        Gio.DBus.session.call(
            APP_ID, OBJECT_PATH, 'org.freedesktop.Application', 'ActivateAction',
            new GLib.Variant('(sava{sv})', [ACTION, [new GLib.Variant('s', req.text)], {}]),
            null, Gio.DBusCallFlags.NONE, 30000, null,
            (conn, res) => {
                let error = null;
                try {
                    conn.call_finish(res);
                } catch (e) {
                    error = e;
                }
                this._busy = false;
                this._entry.reactive = true;
                if (error) {
                    const unknown = Gio.DBusError.is_remote_error(error) &&
                        Gio.DBusError.get_remote_error(error) ===
                        'org.freedesktop.DBus.Error.ServiceUnknown';
                    this._say(unknown ? `${APP_NAME} is not installed -- run ./install.sh`
                        : `${APP_NAME} didn't take it: ${error.message}`, true);
                    console.log(`lookit-tile: failed -- ${error.message}`);
                    return;
                }
                if (fromLine)
                    this._entry.set_text('');
                console.log(`lookit-tile: sent ${req.text.length} chars`);
                Main.panel.closeQuickSettings?.();
                this.menu.close();
                this._raise(40);   // the app may be starting: look for ~6 s
            });
    }

    // Bring the app's window to the front from the shell. An app that was just
    // started by D-Bus has no window yet, so look again for a while.
    _raise(tries) {
        if (this._raiseId) {
            GLib.source_remove(this._raiseId);
            this._raiseId = 0;
        }
        const win = theWindow();
        if (win) {
            Main.activateWindow(win);
            return;
        }
        if (tries <= 0) {
            console.log(`lookit-tile: no ${APP_NAME} window to raise`);
            return;
        }
        this._raiseId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 150, () => {
            this._raiseId = 0;
            this._raise(tries - 1);
            return GLib.SOURCE_REMOVE;
        });
    }

    _openApp() {
        const app = theApp();
        if (!app) {
            this._say(`${APP_NAME} is not installed -- run ./install.sh`, true);
            return;
        }
        app.activate();
        Main.panel.closeQuickSettings?.();
    }

    destroy() {
        if (this._clearId) {
            GLib.source_remove(this._clearId);
            this._clearId = 0;
        }
        if (this._raiseId) {
            GLib.source_remove(this._raiseId);
            this._raiseId = 0;
        }
        super.destroy();
    }
});

const LookitTileIndicator = GObject.registerClass(
class LookitTileIndicator extends QuickSettings.SystemIndicator {
    constructor(extension) {
        super();
        this.quickSettingsItems.push(new LookitTileToggle(extension));
    }

    destroy() {
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

export default class LookitTileExtension extends Extension {
    enable() {
        this._indicator = new LookitTileIndicator(this);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
    }
}
