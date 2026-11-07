// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Window.h>

#include <memory>
#include <optional>
#include <string_view>

namespace babylon::platform
{

    // The operating system layer the game sees: its windows and the one
    // queue of OS events. Exactly one Platform exists at a time; backends may
    // refuse a second one. The composition root builds it and passes it on;
    // nothing here is a singleton. Not thread-safe: call from the thread that
    // created it.
    class Platform
    {
    public:
        virtual ~Platform() = default;

        // Creates a window. Returns nullptr and logs one warning if the size
        // is invalid (isValidWindowSize()) or the operating system refuses.
        // The caller owns the window and must destroy it before this
        // Platform; nothing throws.
        [[nodiscard]] virtual std::unique_ptr<Window> createWindow(
            const WindowDesc& desc) = 0;

        // Returns the next pending event, or nullopt when none is pending.
        // Never blocks and never calls back into the game: the game drains
        // the queue once per frame, so the order of events is the order the
        // operating system produced them.
        virtual std::optional<Event> pollEvent() = 0;

        // Shows a native error message box with one OK button, the given
        // title and text, parented to no window. Returns true when a box was
        // shown and dismissed. Returns false when the backend cannot show
        // one, in which case it logs one warning with the title and text so
        // the message is not lost; a backend whose box fails logs its own
        // warning. May block until the user dismisses the box, so call it
        // only where a blocking answer is acceptable, such as errors before
        // rendering works.
        [[nodiscard]] virtual bool showMessageBox(std::string_view title,
            std::string_view text) = 0;
    };

}
