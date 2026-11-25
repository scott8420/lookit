cmake_minimum_required(VERSION 3.20)
project(lookit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
# PDFs: poppler-glib (Fedora: poppler-glib-devel; Ubuntu: libpoppler-glib-dev).
pkg_check_modules(POPPLER REQUIRED poppler-glib)
# Saving edited PDFs: qpdf (Fedora: qpdf-devel; Ubuntu: libqpdf-dev).
pkg_check_modules(QPDF REQUIRED libqpdf)
find_package(spdlog REQUIRED)

# ── lookit_core -- the GTK-free core (the model side of the one real seam) ──────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph and the
# core stays headless-testable. (docs/two-layer-seam.md.)
add_library(lookit_core STATIC
    src/core/Recents.cpp
    src/core/TextMap.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels
    # from it, ShortcutsDialog renders from it) so a key and its advertised row
    # can't drift. GTK-free, so it lives in core and the selftest guards it
    # headless -- a pure test proves no two chords collide.
    src/core/Shortcuts.cpp
    # The UNDO paradigm (s046): core/Undo.hpp is the journal (header-only, a
    # template over the app's store); Stones is the demo model + its verbs +
    # the verb table the selftest gate walks. docs/undo.md.
    src/core/Stones.cpp
    # The QUICK-SETTINGS-TILE stone: the app side of what the tile sends --
    # cleaned and capped on arrival, the C++ twin of the tile's lib.js.
    # docs/quick-settings-tile.md.
    src/core/TileText.cpp
    # Lookit: where pages sit + the zoom ladder; what a file is.
    src/core/Layout.cpp
    src/core/FileKind.cpp
    src/core/Search.cpp
    src/core/Pages.cpp
)
target_include_directories(lookit_core PUBLIC include)
# Vendored nlohmann/json (header-only, MIT), PRIVATE -- an implementation detail
# of the pump, no consumer inherits the include (CANON: surprise-free build).
target_include_directories(lookit_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── Headless selftest -- exercises the core with no display ────────────────────
add_executable(lookit_selftest src/selftest.cpp)
target_link_libraries(lookit_selftest PRIVATE lookit_core)

# ── App resources (gresource) -- the RESOURCE-PIPELINE paradigm ────────────────
# glib-compile-resources embeds resources/lookit.gresource.xml into a C source;
# App::on_activate registers it and points the icon theme at the bundle so
# set_icon_name("lookit-logo-symbolic") resolves out of the binary. The generated
# .c compiles as C (project enables C above) so lookit_resources_get_resource()
# has C linkage, matching the extern "C" prototype in App.cpp. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
# (docs/resources-and-dialog-lifetime.md)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(LOOKIT_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/lookit.gresource.xml")
set(LOOKIT_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/lookit-resources.c")
file(GLOB LOOKIT_RESOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg")
add_custom_command(
    OUTPUT  "${LOOKIT_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${LOOKIT_RESOURCES_C}"
            --generate-source
            --c-name lookit_resources
            lookit.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${LOOKIT_GRESOURCE_XML}" ${LOOKIT_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (lookit-resources.c)"
    VERBATIM)

# ── lookit -- the GTK app ───────────────────────────────────────────────────────
# The Shell is a SPLIT CLASS across TUs by category (see the index in
# include/Shell.hpp); all parts compile into the one lookit::Shell.
add_executable(lookit
    ${LOOKIT_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    # Follows the desktop's light/dark preference (GTK4 will not do it for us
    # and we do not link libadwaita). Needs no new dependency: the D-Bus and
    # GSettings calls are GIO, which gtkmm-4.0 already brings in.
    src/Appearance.cpp
    src/Document.cpp
    src/DocView.cpp
    src/ThumbStrip.cpp
    src/Save.cpp
    src/Shell.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/Shell_edit.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(lookit PRIVATE include ${GTKMM_INCLUDE_DIRS} ${POPPLER_INCLUDE_DIRS} ${QPDF_INCLUDE_DIRS})
target_link_libraries(lookit     PRIVATE ${GTKMM_LIBRARIES} ${POPPLER_LIBRARIES} ${QPDF_LIBRARIES} lookit_core spdlog::spdlog)
target_link_directories(lookit   PRIVATE ${GTKMM_LIBRARY_DIRS} ${POPPLER_LIBRARY_DIRS} ${QPDF_LIBRARY_DIRS})
target_compile_options(lookit    PRIVATE ${GTKMM_CFLAGS_OTHER})

# ── lookit-peek -- the spacebar quick look for Files (s005) ─────────────────────
# Its own small program: it owns org.gnome.NautilusPreviewer (Sushi's name) and
# answers Nautilus's org.gnome.NautilusPreviewer2 calls. Shares the page view,
# the documents and the skin with Lookit. See src/peek/main.cpp.
add_executable(lookit-peek
    src/peek/main.cpp
    src/Document.cpp
    src/DocView.cpp
    src/Log.cpp
)
target_include_directories(lookit-peek PRIVATE include ${GTKMM_INCLUDE_DIRS} ${POPPLER_INCLUDE_DIRS})
target_link_libraries(lookit-peek     PRIVATE ${GTKMM_LIBRARIES} ${POPPLER_LIBRARIES} lookit_core spdlog::spdlog)
target_link_directories(lookit-peek   PRIVATE ${GTKMM_LIBRARY_DIRS} ${POPPLER_LIBRARY_DIRS})
target_compile_options(lookit-peek    PRIVATE ${GTKMM_CFLAGS_OTHER})
