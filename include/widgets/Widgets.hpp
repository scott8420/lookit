#pragma once
#include "widgets/Named.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/overlay.h>
#include <gtkmm/revealer.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/togglebutton.h>

// The wrapper aliases. Widen on demand, not for symmetry (CANON). Each line is
// the whole cost of making a GTK widget name-mandatory in Lookit's app code.
namespace lookit::widgets {

using Box            = Named<Gtk::Box>;
using Button         = Named<Gtk::Button>;
using Entry          = Named<Gtk::Entry>;
using FlowBox        = Named<Gtk::FlowBox>;
using Image          = Named<Gtk::Image>;
using Label          = Named<Gtk::Label>;
using ListBox        = Named<Gtk::ListBox>;
using MenuButton     = Named<Gtk::MenuButton>;
using Overlay        = Named<Gtk::Overlay>;
using Revealer       = Named<Gtk::Revealer>;
using Scale          = Named<Gtk::Scale>;
using ScrolledWindow = Named<Gtk::ScrolledWindow>;
using Stack          = Named<Gtk::Stack>;
using StackSidebar   = Named<Gtk::StackSidebar>;
using ToggleButton   = Named<Gtk::ToggleButton>;

}  // namespace lookit::widgets
