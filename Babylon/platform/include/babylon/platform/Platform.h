// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Event.h>
#include <babylon/platform/Window.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace babylon::platform
{

    // What chooseFolder() learned: the user picked a folder, dismissed the
    // picker, or the backend could not show one at all.
    enum class FolderChoiceResult
    {
        Chosen,
        Cancelled,
        Unavailable
    };

    // One answer from chooseFolder(). path is set only for Chosen and is then
    // UTF-8 with '/' separators and no trailing separator unless the picker
    // returned one. Cancelled and Unavailable leave path empty.
    struct FolderChoice
    {
        FolderChoiceResult result = FolderChoiceResult::Unavailable;
        std::string path;
    };

    // The most buttons askChoice() accepts; a longer list is refused rather
    // than shown, so every backend has the same limit.
    inline constexpr std::size_t kMaxChoiceButtons = 8;

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

        // Shows a native choice box with 1..kMaxChoiceButtons buttons, the
        // given title and text, parented to no window. The return key chooses
        // the first button and Escape, or closing the box without choosing,
        // chooses the last button: make the last button the cancel choice.
        // Returns the index of the button chosen, or nullopt when no box
        // could be shown or the button list is empty or longer than
        // kMaxChoiceButtons. In that case the backend logs one warning with
        // the title and text, as showMessageBox does. May block until the
        // user answers it, so call it only where a blocking answer is
        // acceptable, such as before rendering works.
        [[nodiscard]] virtual std::optional<std::size_t> askChoice(
            std::string_view title, std::string_view text,
            std::span<const std::string_view> buttons) = 0;

        // Shows a native folder picker, parented to no window, starting at
        // defaultLocation (UTF-8 with '/' separators, or empty for the
        // picker's own default). Returns the chosen folder, Cancelled when the
        // user dismissed the picker, or Unavailable when no picker could be
        // shown; the last case logs one warning with the title. Blocking
        // until the user answers, like askChoice(), and usable before any
        // window exists.
        [[nodiscard]] virtual FolderChoice chooseFolder(std::string_view title,
            std::string_view defaultLocation) = 0;

        // Starts a rumble effect on one gamepad. lowFrequency and highFrequency
        // are the two motors, each clamped to 0..1 (0 stops it); durationMs is
        // capped at kMaxRumbleDurationMs. Returns false when the gamepad is
        // unknown, is not open, or cannot rumble; a backend that cannot rumble
        // at all returns false without logging. Rumble is output only: it never
        // changes the game state, and the call does not block.
        virtual bool rumbleGamepad(GamepadId gamepad, float lowFrequency,
            float highFrequency, std::uint32_t durationMs) = 0;
    };

    // The longest rumble a caller may ask for; longer durations are capped.
    inline constexpr std::uint32_t kMaxRumbleDurationMs = 5000;

    // Clamps one rumble motor's intensity to the 0..1 range the interface
    // expects. Backends apply it before handing the value to their device.
    inline float clampRumbleIntensity(float intensity)
    {
        if ( intensity < 0.0f )
        {
            return 0.0f;
        }
        if ( intensity > 1.0f )
        {
            return 1.0f;
        }
        return intensity;
    }

}
