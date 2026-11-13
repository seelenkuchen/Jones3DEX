// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Platform.h>

#include <deque>
#include <memory>
#include <optional>
#include <string_view>

namespace babylon::platform
{

    // Platform that touches no operating system at all, for tests and
    // headless runs. Windows are records in memory: ids start at 1 and count
    // up, resize() always succeeds once the size passed the shared check, the
    // title and size are exactly what was asked for, and relative mouse,
    // text input and fullscreen modes are recorded per window.
    //
    // Nothing ever arrives in the event queue by itself: only pushEvent(),
    // from a test or from the headless runner, puts events in it. They come
    // out again in that order.
    class NullPlatform final : public Platform
    {
    public:
        std::unique_ptr<Window> createWindow(const WindowDesc& desc) override;
        std::optional<Event> pollEvent() override;

        // Appends an event to the queue (FIFO). pushEvent() followed by
        // pollEvent() hands back a copy of the event.
        void pushEvent(const Event& event);

        // There is no native window system here, so the title and text go to
        // one babylon::log::warning line and nothing blocks.
        [[nodiscard]] bool showMessageBox(std::string_view title,
            std::string_view text) override;

        // There is no device to vibrate: the call is recorded (with its
        // intensities clamped and its duration capped, as the interface
        // promises) and always returns false.
        bool rumbleGamepad(GamepadId gamepad, float lowFrequency,
            float highFrequency, std::uint32_t durationMs) override;

        // One rumble request as this backend recorded it.
        struct RumbleCall
        {
            GamepadId gamepad;
            float lowFrequency;
            float highFrequency;
            std::uint32_t durationMs;
        };

        // The last rumble request, or nullopt if none was made.
        std::optional<RumbleCall> lastRumble() const;

    private:
        // The id the next created window gets. 0 is never handed out, as the
        // interface requires.
        WindowId m_nextId = 1;
        std::deque<Event> m_events;
        std::optional<RumbleCall> m_lastRumble;
    };

}
