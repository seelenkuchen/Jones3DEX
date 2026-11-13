// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Widget.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace babylon::ui
{

    // Identifies one pushed screen for the life of a Ui. Ids are unique,
    // never reused and never 0; 0 is the invalid id Ui::push returns for a
    // rejected screen. A popped screen's id no longer names a screen.
    using ScreenId = std::uint64_t;
    inline constexpr ScreenId kInvalidScreen = 0;

    // A game-defined result carried by a ScreenClosed event. The game
    // decides what each value means, exactly as the original blocking
    // screen wrapper decides what its return value means. The default for
    // closing with back is Screen::backResult.
    using ResultCode = std::int64_t;

    // One screen, declared in code: a title and a vertical list of widgets.
    // Ui lays it out automatically and owns a copy from push until close,
    // so the caller may destroy its Screen immediately after pushing.
    //
    // The panel is as wide as the widest row plus padding (clamped to the
    // canvas minus the skin's margins) and as tall as the title plus every
    // row. The skin's placement centers it or puts it in the lower left
    // corner. When the panel is taller than the canvas, rows scroll with
    // focus and the list stays clipped to the canvas.
    //
    // An empty widget list is valid: the title and an empty panel still
    // draw, and back closes the screen.
    struct Screen
    {
        // UTF-8, drawn in the title row. May be empty, in which case the
        // title row is still reserved so screens don't jump in height.
        std::string title;

        // The widgets from top to bottom. Focus skips labels, images and
        // disabled widgets. A list takes over up/down for its own selection
        // while it has focus; see List.
        std::vector<Widget> widgets;

        // The tab labels of a tabbed screen, in display order. Empty draws
        // no tab strip, which is the ordinary dialog look. When non-empty,
        // a strip of labels is drawn above the rows, the tab named by
        // activeTab is highlighted and underlined, and the previous-tab and
        // next-tab navigation inputs (or a click on a label) report a
        // TabChanged event with the new index. The game builds a new screen
        // for the chosen tab; the module only changes activeTab and reports
        // the request.
        std::vector<std::string> tabs;

        // The tab drawn as active and reported by a pointer click. Ignored
        // when tabs is empty; a value past the end is clamped to the last
        // tab.
        std::size_t activeTab = 0;

        // Optional help text for the right-hand column, parallel to widgets:
        // help[i] describes widgets[i]. An empty string, or a vector shorter
        // than widgets, means no help for those rows. The help column is
        // drawn only when at least one row has text and the skin reserves a
        // positive helpColumnWidth; only the focused row's text is shown. A
        // Top skin reserves the column even without text, so the rows keep
        // one width on every screen of a set.
        std::vector<std::string> help;

        // The result reported when the player presses back or a screen
        // has no explicit way to close. Ignored when a button or text entry
        // closes the screen explicitly.
        ResultCode backResult = 0;

        // The widget focused when the screen is pushed, or kNoWidget for
        // the first focusable widget. An index naming a missing, label,
        // image or disabled widget also falls back to the first focusable
        // widget, so a prompt that must default to a safe answer sets this
        // to that answer's button index.
        std::size_t initialFocus = kNoWidget;
    };

}
