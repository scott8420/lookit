#pragma once
#include "core/Layout.hpp"
#include "core/Pages.hpp"

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Forms -- filling in a PDF's form (s011). GTK-free.
//
// A FIELD is one widget on one page as the file describes it: where it sits
// (the page's UNTURNED frame, points, top-left origin -- the frame marks use),
// what kind it is, and its value. Poppler reads them (Document::fields); this
// file only knows their shape.
//
// A field's VALUE is what the person chose, as text:
//   text            the words
//   check / radio   "1" on, "0" off (each radio widget is its own field; a
//                   group is the widgets sharing a name in one source)
//   combo / list    "#0,2" -- the chosen items by number; a combo you can
//                   type into may hold "=words" instead
// The values the person set live in ONE record -- the document's (Pages::kDoc,
// PageRec::fields), keyed by key(src, id) -- so filling a field is one step in
// the page tools' journal, and Ctrl+Z walks page edits and form edits in the
// order they were made. A field with no entry there shows what the file says.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

enum class FieldKind { Text, Check, Radio, Combo, List, Push, Signature, Unknown };

struct Field {
    int          src = 0;          // the source (Pages::sources) it belongs to
    int          id = 0;           // poppler's id for it in that source
    std::string  name;             // radios of one group share it
    FieldKind    kind = FieldKind::Unknown;
    layout::Rect box;              // unturned page frame, points
    std::string  value;            // see above
    std::vector<std::string> options;   // combo / list items
    bool multiline = false, password = false, read_only = false;
    bool multi = false;            // a list that takes several picks
    bool editable = false;         // a combo you can type into
    int  max_len = -1;             // text: -1 = no limit
};

namespace forms {

std::string key(int src, int id);                 // "0:65536"
const char* noun(FieldKind k);                    // "text field", "check box", ...
bool fillable(const Field& f);                    // not read-only, a kind Lookit fills

std::string on(bool yes);                         // "1" / "0"
bool is_on(const std::string& v);
std::string picks(const std::vector<int>& items);         // {0, 2} -> "#0,2"
std::vector<int> picked(const std::string& v);            // "#0,2" -> {0, 2}; anything else -> {}
std::string typed(const std::string& words);              // "=words"
bool is_typed(const std::string& v);
std::string typed_words(const std::string& v);
// What a person reads for a value: the words, "on" / "off", the chosen items.
std::string shown(const Field& f, const std::string& v);

// The field under (x, y) -- the smallest box that holds it (a field drawn
// inside another wins) -- or -1.
int hit(const std::vector<Field>& fields, double x, double y);

// Reading order for Tab: by page, then top to bottom, then left to right.
// `fields` are (page position, field) pairs; returns indices.
std::vector<int> tab_order(const std::vector<std::pair<int, Field>>& fields);

// The verb: `f` takes `value`. `group`: every field of the document's sources
// that shares f's source and name (a radio group; a text field shown twice) --
// they all take it (a radio: the chosen one "1", the rest "0"). One step,
// labelled "Fill in “name”" / "Tick “agree”" / "Choose “size”". False (no
// step) when nothing would change or the field can't be filled.
bool set(PageJournal& j, Pages& p, const Field& f, const std::vector<Field>& group, const std::string& value);

// The values the record holds for `f`'s group put back to the file's own (one
// step, "Clear “name”"); false when it already shows the file's value.
bool reset(PageJournal& j, Pages& p, const Field& f, const std::vector<Field>& group);

// Fields the person changed (keys present in the record).
int changed(const Pages& p);

}  // namespace forms
}  // namespace lookit::core
