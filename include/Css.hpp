#pragma once

namespace lookit {

// The app stylesheet, installed once in App::ensure_shell.
//
// THE ROUSTER SKIN, as Fowler wears it: always dark (App asks GTK for its dark
// variant too, so menus, popovers and the file chooser match), a deep gradient
// per page picked by the window's .page-<name> class, a frosted sidebar of
// icon + label rows, content on translucent cards. The colour literals ARE the
// design -- the skin never follows the theme. Plain GTK4 CSS: no var().
//
// Lookit's tint is teal (the logo's glass). Home is the bright one; View is
// darker and quieter, because the page is what you look at there.
//
// The drawer tabs are invisible-until-hover ENTIRELY IN CSS (opacity 0 with a
// :hover override) -- the reliable idiom, from Folio.
inline const char* LOOKIT_CSS = R"CSS(
    window.lookit { background-color: #0f2229; color: #eef8fa; }
    window.lookit headerbar {
        background: transparent; box-shadow: none; border: none;
        min-height: 40px; color: #eef8fa;
    }
    window.lookit headerbar button, window.lookit headerbar menubutton > button {
        color: rgba(238,248,250,0.85); background: transparent; box-shadow: none; border: none;
    }
    window.lookit headerbar menubutton > button:checked { background-color: rgba(255,255,255,0.14); }
    window.lookit headerbar button:hover { background-color: rgba(255,255,255,0.10); }
    window.lookit .title { font-weight: 600; color: rgba(238,248,250,0.75); }

    /* Per page tint. Home bright teal; View deep slate with a teal glow. */
    window.lookit.page-home {
        background-image: radial-gradient(circle at 75% 15%, rgba(102,217,232,0.45), transparent 55%),
                          linear-gradient(160deg, #0c4a57 0%, #0b6274 55%, #072f39 100%);
    }
    window.lookit.page-view {
        background-image: radial-gradient(circle at 80% 0%, rgba(21,170,191,0.22), transparent 50%),
                          linear-gradient(170deg, #142a31 0%, #10252c 60%, #0b1b20 100%);
    }

    /* Sidebar: frosted, icon + label rows, a soft pill for the selection. */
    .sidebar-pane { background-color: rgba(0,0,0,0.22); border-right: 1px solid rgba(255,255,255,0.06); }
    .sidebar-pane list { background: transparent; }
    .sidebar-pane row {
        border-radius: 10px; margin: 2px 10px; padding: 8px 10px;
        color: rgba(238,248,250,0.80); background: transparent;
    }
    .sidebar-pane row:hover    { background-color: rgba(255,255,255,0.07); }
    .sidebar-pane row:selected { background-color: rgba(255,255,255,0.16); color: #ffffff; }
    .sidebar-pane row:focus-visible { outline: 2px solid rgba(255,255,255,0.4); outline-offset: -2px; }
    .sidebar-brand { font-size: 1.15em; font-weight: 800; margin: 6px 20px 8px 20px; }
    .sidebar-foot  { margin: 8px 20px 14px 20px; font-size: 0.85em; color: rgba(238,248,250,0.50); }

    /* Home: the hero. */
    .hero-title { font-size: 2.4em; font-weight: 800; }
    .hero-sub   { font-size: 1.05em; color: rgba(238,248,250,0.72); }
    .hero-button {
        padding: 10px 26px; border-radius: 999px; font-weight: 800; font-size: 1.05em;
        color: #053039; background-image: none; background-color: #99e9f2; box-shadow: none; border: none;
    }
    .hero-button:hover { background-color: #c5f6fa; }
    .section-head { font-weight: 800; font-size: 0.85em; letter-spacing: 0.08em;
                    color: rgba(238,248,250,0.60); margin: 6px 4px; }
    .drop-hint { color: rgba(238,248,250,0.55); font-size: 0.92em; }
    window.lookit.dropping .hero-card { border-color: #99e9f2; background-color: rgba(153,233,242,0.12); }

    /* Content on translucent cards; scrollers let the gradient through. */
    window.lookit scrolledwindow, window.lookit viewport { background: transparent; }
    .pane-card, .hero-card {
        background-color: rgba(0,0,0,0.24); border-radius: 16px;
        border: 1px solid rgba(255,255,255,0.08);
    }
    .hero-card { padding: 34px 40px; border-width: 2px; border-style: dashed;
                 border-color: rgba(255,255,255,0.14); }

    /* Recent files: cards. */
    flowbox.recents { background: transparent; }
    flowbox.recents > flowboxchild { padding: 0; background: transparent; border-radius: 14px; }
    .recent-card {
        padding: 10px 14px; border-radius: 14px; min-width: 200px;
        background-image: none; background-color: rgba(255,255,255,0.08);
        border: 1px solid rgba(255,255,255,0.08); box-shadow: none; color: #eef8fa;
    }
    .recent-card:hover { background-color: rgba(255,255,255,0.14); }
    .recent-name { font-weight: 700; }
    .recent-where { font-size: 0.85em; color: rgba(238,248,250,0.55); }
    .recent-kind  { font-size: 0.75em; font-weight: 800; letter-spacing: 0.06em; color: #99e9f2; }

    /* View: the toolbar is a floating pill; the pages sit on the gradient. */
    .view-bar {
        padding: 4px 8px; border-radius: 999px; margin: 6px 14px 4px 14px;
        background-color: rgba(0,0,0,0.30); border: 1px solid rgba(255,255,255,0.08);
    }
    .view-bar button { min-height: 28px; min-width: 28px; padding: 2px 6px; border-radius: 999px;
                       background: transparent; box-shadow: none; border: none; color: #eef8fa; }
    .view-bar button:hover   { background-color: rgba(255,255,255,0.12); }
    .view-bar button:checked { background-color: rgba(153,233,242,0.25); }
    .view-bar .doc-name { font-weight: 700; margin: 0 8px; }
    .view-bar .zoom-label, .view-bar .page-label { font-feature-settings: "tnum"; min-width: 54px;
                                                   color: rgba(238,248,250,0.80); }
    .view-bar entry.page-entry { min-width: 0; min-height: 24px; padding: 0 6px; border-radius: 8px;
                                 background-color: rgba(0,0,0,0.25); color: #ffffff; }
    .view-empty { color: rgba(238,248,250,0.55); }
    .find-bar { margin-top: 0; }
    .find-bar entry { min-height: 26px; border-radius: 999px; }

    /* Thumbnails: a quiet strip; the current page gets the teal ring. */
    .thumbs { background-color: rgba(0,0,0,0.16); border-right: 1px solid rgba(255,255,255,0.06); }
    listview.thumb-list { background: transparent; padding: 8px 6px; }
    listview.thumb-list > row { padding: 6px 4px; border-radius: 10px; background: transparent; }
    listview.thumb-list > row:hover { background-color: rgba(255,255,255,0.06); }
    listview.thumb-list > row:selected { background-color: rgba(153,233,242,0.16); color: #ffffff; }
    listview.thumb-list > row:selected .thumb-pic { outline: 2px solid #99e9f2; outline-offset: 1px; }
    .thumb-pic { box-shadow: 0 2px 6px rgba(0,0,0,0.45); }
    .thumb.dragging { opacity: 0.4; }
    .thumb.drop-before { border-top: 3px solid #99e9f2; }
    .thumb.drop-after  { border-bottom: 3px solid #99e9f2; }
    .thumb-num { font-size: 0.8em; color: rgba(238,248,250,0.65); font-feature-settings: "tnum"; }
    lookit-canvas:focus-visible { outline: none; }

    /* Messages (a file that wouldn't open). */
    .toast {
        padding: 8px 16px; border-radius: 999px; margin: 14px;
        background-color: rgba(10,30,36,0.92); border: 1px solid rgba(255,255,255,0.12);
    }
    .toast.toast-error { border-color: #ff8787; color: #ffe3e3; }

    window.lookit entry { background-color: rgba(0,0,0,0.25); color: #ffffff; border-radius: 8px; }

    /* The drawers sit on the skin, not the theme's grey. */
    window.lookit .drawer-panel { background-color: #12313a; }
    .drawer-tab, .drawer-tab-right {
        background-image: none;
        background-color: alpha(currentColor, 0.10);
        font-size: 16px;
        border: 1px solid alpha(currentColor, 0.20);
        min-width: 22px;
        min-height: 160px;
        padding: 0;
        box-shadow: none;
        opacity: 0;
        transition: opacity 160ms ease;
    }
    .drawer-tab       { border-left: none;  border-radius: 0 10px 10px 0; }
    .drawer-tab-right { border-right: none; border-radius: 10px 0 0 10px; }
    .drawer-tab:hover, .drawer-tab-right:hover {
        background-color: alpha(currentColor, 0.20);
        opacity: 1;
    }
    .drawer-panel { padding: 14px; }
    .drawer-panel.left  { border-right: 1px solid alpha(currentColor, 0.15);
                          box-shadow: 2px 0 10px alpha(black, 0.25); }
    .drawer-panel.right { border-left: 1px solid alpha(currentColor, 0.15);
                          box-shadow: -2px 0 10px alpha(black, 0.25); }
    .drawer-scrim { background-color: transparent; }
    .info-key { color: rgba(238,248,250,0.55); font-size: 0.85em; }
    .info-val { font-weight: 600; }
)CSS";

}  // namespace lookit
