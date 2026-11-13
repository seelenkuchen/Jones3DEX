// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Binding.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Widget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace babylon::ui
{

    // A button or list row was accepted. item is the selected ListItem::id
    // for a list and 0 for a button.
    struct ActionPressed
    {
        ScreenId screen = kInvalidScreen;   // the screen that reported it
        ActionId action = kNoAction;        // the widget's ActionId
        std::int64_t item = 0;              // ListItem::id, or 0
    };

    // A screen left the stack, either because it was popped, replaced, or
    // closed by back. Whoever opened it compares screen with the id it got
    // and reads result, which is the value the caller or the screen asked
    // for. Queued before any event produced by the screen below it.
    struct ScreenClosed
    {
        ScreenId screen = kInvalidScreen;
        ResultCode result = 0;
    };

    // The player asked for another tab of a tabbed screen. tab is the new
    // index in Screen::tabs; Ui has already stored it in the open screen's
    // activeTab. The game builds the screen for that tab and replaces the
    // current one.
    struct TabChanged
    {
        ScreenId screen = kInvalidScreen;
        std::size_t tab = 0;
    };

    // A Toggle, Slider or Choice changed because of user input. widget is
    // the widget's index in Screen::widgets. The event is reported for both
    // bound and unbound widgets, after the cvar write for a bound one:
    // bool for Toggle, double for Slider (also for an int cvar), and
    // std::int64_t for the selected Choice index. TextEntry changes do not
    // report this event; accepting one reports TextSubmitted.
    struct ValueChanged
    {
        ScreenId screen = kInvalidScreen;
        std::size_t widget = kNoWidget;
        WidgetValue value;
    };

    // A List changed its selected row because of user input. widget is the
    // widget's index in Screen::widgets, row the new index in List::items,
    // and id that item's ListItem::id. Moving the wheel changes no
    // selection and reports nothing.
    struct SelectionChanged
    {
        ScreenId screen = kInvalidScreen;
        std::size_t widget = kNoWidget;
        std::size_t row = 0;
        std::int64_t id = 0;
    };

    // A BindingCapture widget captured input. widget is the widget's index
    // in Screen::widgets. Capture has already ended; the widget is focused
    // and shows its label again.
    struct BindingCaptured
    {
        ScreenId screen = kInvalidScreen;
        std::size_t widget = kNoWidget;
        Binding binding;
    };

    // A TextEntry was accepted. widget is the widget's index in
    // Screen::widgets and text is the accepted value. A bound string cvar
    // has already been written before this event is queued.
    struct TextSubmitted
    {
        ScreenId screen = kInvalidScreen;
        std::size_t widget = kNoWidget;
        std::string text;
    };

    // The top screen started (on == true) or stopped (on == false) editing
    // a TextEntry, so the game should start or stop the platform's text
    // input for its window. The ui deliberately knows no window; the game
    // turns this into Window::setTextInput(). It is reported only when the
    // state changes, so a game may call setTextInput() from it directly.
    struct TextInputWanted
    {
        bool on = false;
    };

    // A sound cue the skin asked for: focus moved, accepted, or back. cue is
    // one of Skin::focusSound, acceptSound or backSound, never 0 (a zero
    // sound id reports nothing). The module doesn't play anything.
    struct SoundCue
    {
        std::uint32_t cue = 0;
    };

    // Everything the ui module reports to the game, in the order it
    // happened. Ui::pollEvent() returns one at a time; the game drains the
    // queue once per frame. Cvar writes happen before their corresponding
    // event is queued.
    using UiEvent = std::variant<ActionPressed, ScreenClosed, TabChanged, ValueChanged,
        SelectionChanged, BindingCaptured, TextSubmitted, TextInputWanted, SoundCue>;

}
