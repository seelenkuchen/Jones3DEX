// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Painter.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace babylon::ui
{

    // An action the game attaches to a button or list. The value is the
    // game's to define; the module only reports it back in an ActionPressed
    // event. 0 means "no action": the widget is still navigable and drawn,
    // but accepting it reports nothing.
    using ActionId = std::uint32_t;
    inline constexpr ActionId kNoAction = 0;

    // The index of no widget in a screen. Returned by Ui::focusedWidget()
    // when nothing has focus, and the default of Screen::initialFocus.
    inline constexpr std::size_t kNoWidget =
        (std::numeric_limits<std::size_t>::max)();

    // The value a value-carrying widget exposes through ValueChanged and
    // Ui::widgetValue(): bool for Toggle, double for Slider (even when the
    // cvar is an int), std::int64_t for the selected Choice index, and
    // std::string for TextEntry. List reports SelectionChanged instead and
    // never uses this type.
    using WidgetValue = std::variant<bool, double, std::int64_t, std::string>;

    // A read-only line of text. When value is non-empty it is drawn right
    // aligned in the same row, with text left aligned; this is the two
    // column row used by statistics and totals.
    struct Label
    {
        std::string text;    // UTF-8 label, drawn as one unwrapped line
        std::string value;   // UTF-8 right-aligned value; empty for none
    };

    // A non-interactive image of a fixed canvas size, for a save preview or
    // a store icon. image is an ImageId the game owns; 0 draws nothing. size
    // is the row's space in canvas units; a zero width or height draws
    // nothing and reserves no row space. align places the image
    // horizontally inside that space, and it is vertically centered in it.
    struct Image
    {
        ImageId image = 0;
        Size size;
        TextAlign align = TextAlign::Center;
    };

    // A text row that reports its action when accepted. A disabled row is
    // drawn with the disabled text color, is skipped by focus and does not
    // respond to pointer input.
    struct Button
    {
        std::string text;              // UTF-8, drawn as one unwrapped line
        ActionId action = kNoAction;   // reported on accept
        bool enabled = true;           // false draws and navigates as disabled
    };

    // An on/off row. When cvar is non-empty it is bound to a bool cvar:
    // the row shows the cvar's value and writes the opposite value on
    // accept. When cvar is empty the row keeps its value in the screen,
    // starting from value.
    // Every user change reports ValueChanged with a bool; a disabled row is
    // skipped by focus and does not respond to pointer input.
    struct Toggle
    {
        std::string text;      // UTF-8 label, drawn left of the value
        std::string cvar;      // cvar name, empty for an unbound toggle
        bool enabled = true;   // false draws and navigates as disabled
        bool value = false;    // initial value, used when cvar is empty
    };

    // A numeric row adjusted with left/right, a click on either half, or
    // the gamepad stick/d-pad. When cvar is non-empty it binds to an int or
    // float cvar: the row reads the cvar, writes a change and treats the
    // cvar's range as the outer limit. When cvar is empty the row keeps its
    // value in the screen.
    //
    // min and max are inclusive and in the cvar's units. The effective
    // range is the intersection of [min, max] and the cvar's own range, so
    // a value outside the cvar's range is never written. step is the
    // increment of one key or click; 0 means one hundredth of the effective
    // range (and at least 1 for an int cvar). A negative step or min > max
    // makes Ui::push reject the screen. When cvar is empty the row starts
    // from value, clamped to min..max.
    //
    // Every user change reports ValueChanged with a double, even when the
    // bound cvar is an int. A disabled row is skipped by focus and does not
    // respond to pointer input.
    struct Slider
    {
        std::string text;      // UTF-8 label
        std::string cvar;      // cvar name, empty for an unbound slider
        double min = 0.0;      // inclusive lower bound, widget units
        double max = 1.0;      // inclusive upper bound, widget units
        double step = 0.0;     // increment per change, 0 = automatic
        bool enabled = true;   // false draws and navigates as disabled
        double value = 0.0;    // initial value, clamped to min..max
    };

    // One of a fixed list of options, adjusted with left/right. When cvar
    // is non-empty it binds to an int cvar (the option index) or a string
    // cvar (the option text); the row reads the cvar and writes the new
    // selection. An int cvar's range, when present, must contain every
    // option index; Ui::push rejects a screen whose cvar range does not.
    // When cvar is empty the row keeps its index in the screen, starting
    // from selected. Every user change reports ValueChanged with the new
    // index as std::int64_t, whatever the cvar type. A disabled row is
    // skipped by focus and does not respond to pointer input.
    struct Choice
    {
        std::string text;                  // UTF-8 label
        std::string cvar;                  // cvar name, empty when unbound
        std::vector<std::string> options;  // at least one non-empty label
        bool enabled = true;               // false draws and navigates as disabled
        std::size_t selected = 0;          // initial index, clamped to options
    };

    // One row of a List.
    struct ListItem
    {
        std::string text;              // UTF-8, drawn as one unwrapped line
        ImageId image = 0;             // optional thumbnail, 0 for none
        std::int64_t id = 0;           // game-defined id reported on accept

        bool operator==(const ListItem&) const = default;
    };

    // A scrolling selection list. When focused, up/down move the selection
    // and accept reports action together with the selected row's id in an
    // ActionPressed event. The mouse wheel scrolls without moving the
    // selection, and a click selects the row under the pointer. Every user
    // change of the selected row reports SelectionChanged. A list reserves
    // an image column when any item has a non-zero image id, leaving room
    // for a later file list with thumbnails. items may be empty; accepting
    // an empty list reports nothing. A disabled list is skipped by focus
    // and does not respond to pointer input.
    struct List
    {
        std::string text;                // optional heading, may be empty
        std::vector<ListItem> items;     // rows, in display order
        std::size_t selected = 0;        // initial selected index; clamped
        ActionId action = kNoAction;     // reported on accept
        bool enabled = true;             // false draws and navigates as disabled
    };

    // A row that, when accepted, enters capture mode. The next key press,
    // gamepad button press, gamepad axis that crosses the capture threshold
    // (half of full deflection), or mouse button press is reported once as a
    // BindingCaptured event and capture ends. Every gamepad axis is captured,
    // not only the two the navigation map uses: a stick reports the positive
    // or negative half, and a trigger, which rests at 0, reports its positive
    // half only. An axis already past the threshold when capture starts is
    // ignored until it returns below it. The back key cancels capture without
    // reporting; keys and buttons already down when capture starts are
    // ignored. The row shows text until capture starts, then a prompt the
    // module supplies. A disabled row is skipped by focus and does not
    // respond to pointer input.
    struct BindingCapture
    {
        std::string text;      // UTF-8 label
        bool enabled = true;   // false draws and navigates as disabled
    };

    // A single line of editable text with an initial value. When cvar is
    // non-empty it binds to a string cvar: the row reads the cvar, writes
    // every accepted value and falls back to defaultText (never written)
    // when the cvar is empty or fails to load. When cvar is empty the row
    // keeps its text in the screen. Accepting the row is possible without
    // typing and reports the current text in a TextSubmitted event; the
    // widget's index in that event identifies the row. Back cancels editing
    // and restores the text the row had when editing started.
    //
    // The module implements a small line editor: printable keys insert,
    // Backspace deletes, left/right move the cursor, Home/End jump; the
    // text is at most 1024 bytes. It has no IME, clipboard or selection.
    // Live edits do not report ValueChanged; accepting reports
    // TextSubmitted. A disabled row is skipped by focus and does not respond
    // to pointer input.
    struct TextEntry
    {
        std::string text;          // UTF-8 label
        std::string cvar;          // cvar name, empty when unbound
        std::string defaultText;   // initial value, also used when unbound
        bool enabled = true;       // false draws and navigates as disabled
    };

    // One declarative widget of a screen. The alternatives are the widget
    // kinds; Ui copies them when the screen is pushed and owns them until
    // it closes. Add a new alternative, never a field that only one kind
    // uses, so each declaration stays small.
    using Widget = std::variant<Label, Image, Button, Toggle, Slider, Choice,
        List, BindingCapture, TextEntry>;

}
