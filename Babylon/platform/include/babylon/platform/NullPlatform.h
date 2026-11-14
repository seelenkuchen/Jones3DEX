// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Platform.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

        // The type connectGamepad() reports. Tests set it to model pads of
        // different families without building the event themselves.
        void setGamepadType(GamepadType type);
        GamepadType gamepadType() const;

        // Appends a GamepadConnected event for gamepad with the configured
        // type. A test then feeds button and axis events for the same id.
        void connectGamepad(GamepadId gamepad);

        // There is no native window system here, so the title and text go to
        // one babylon::log::warning line and nothing blocks.
        [[nodiscard]] bool showMessageBox(std::string_view title,
            std::string_view text) override;

        // There is no native choice box here. By default the title and text
        // go to one babylon::log::warning line and the answer is nullopt;
        // setChoiceAnswer() scripts a different answer, which is returned
        // unchanged (nullopt keeps the default). An empty or too long button
        // list is always refused with one warning, even when an answer is
        // scripted, so it is never reported as shown. Every call is recorded
        // for lastChoice(), including the refused ones.
        [[nodiscard]] std::optional<std::size_t> askChoice(
            std::string_view title, std::string_view text,
            std::span<const std::string_view> buttons) override;

        // The answer askChoice() returns next, or nullopt for the default.
        void setChoiceAnswer(std::optional<std::size_t> answer);

        // One askChoice() call as this backend recorded it.
        struct ChoiceRequest
        {
            std::string title;
            std::string text;
            std::vector<std::string> buttons;
        };

        // The last askChoice() call, or nullopt if none was made.
        std::optional<ChoiceRequest> lastChoice() const;

        // There is no native folder picker here. By default the title goes to
        // one babylon::log::warning line and the result is Unavailable;
        // setFolderAnswer() scripts a different answer, which is returned
        // unchanged. Every call is recorded for lastFolder(), including the
        // default one.
        [[nodiscard]] FolderChoice chooseFolder(std::string_view title,
            std::string_view defaultLocation) override;

        // The answer chooseFolder() returns next; the default is Unavailable
        // with an empty path.
        void setFolderAnswer(FolderChoice answer);

        // One chooseFolder() call as this backend recorded it.
        struct FolderRequest
        {
            std::string title;
            std::string defaultLocation;
        };

        // The last chooseFolder() call, or nullopt if none was made.
        std::optional<FolderRequest> lastFolder() const;

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
        GamepadType m_gamepadType = GamepadType::Unknown;
        std::optional<std::size_t> m_choiceAnswer;
        std::optional<FolderChoice> m_folderAnswer;
        std::optional<ChoiceRequest> m_lastChoice;
        std::optional<FolderRequest> m_lastFolder;
    };

}
