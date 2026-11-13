// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Keys.h>

namespace babylon::ui
{

    // Translates platform events into UI navigation. The game owns one and
    // may change it at any time; Ui copies it. The same map serves every
    // screen; binding capture deliberately uses a fixed rule and is not
    // affected by it.
    //
    // Accept and back are edge-triggered: one press causes one action.
    // Up/down/left/right repeat while held, using the operating system's
    // key repeat for keys and a fixed initial delay for gamepad axes. Shift
    // held with focusNext focuses the previous widget; otherwise focusNext
    // focuses the next one.
    struct NavigationMap
    {
        // Keyboard navigation.
        platform::Key up = platform::Key::Up;
        platform::Key down = platform::Key::Down;
        platform::Key left = platform::Key::Left;
        platform::Key right = platform::Key::Right;
        platform::Key accept = platform::Key::Enter;
        platform::Key back = platform::Key::Escape;
        platform::Key focusNext = platform::Key::Tab;

        // Previous and next tab of a tabbed screen, edge-triggered like
        // accept and back. They do nothing on a screen without tabs.
        platform::Key previousTab = platform::Key::Q;
        platform::Key nextTab = platform::Key::E;

        // Gamepad d-pad and face buttons. South is the conventional A and
        // East the conventional B position, independent of their labels.
        platform::GamepadButton upButton = platform::GamepadButton::DpadUp;
        platform::GamepadButton downButton = platform::GamepadButton::DpadDown;
        platform::GamepadButton leftButton = platform::GamepadButton::DpadLeft;
        platform::GamepadButton rightButton = platform::GamepadButton::DpadRight;
        platform::GamepadButton acceptButton = platform::GamepadButton::South;
        platform::GamepadButton backButton = platform::GamepadButton::East;

        // Previous and next tab of a tabbed screen.
        platform::GamepadButton previousTabButton = platform::GamepadButton::LeftShoulder;
        platform::GamepadButton nextTabButton = platform::GamepadButton::RightShoulder;

        // The left stick by default. Values with an absolute magnitude of
        // at least axisThreshold count as a direction; the axis is then
        // treated like the matching d-pad button until it returns below the
        // threshold.
        platform::GamepadAxis horizontalAxis = platform::GamepadAxis::LeftX;
        platform::GamepadAxis verticalAxis = platform::GamepadAxis::LeftY;
        float axisThreshold = 0.5f;

        // When true, a click on the focused widget accepts it; when false, a
        // click only moves focus. A click on another widget always moves
        // focus there, and a click inside a list selects the row under the
        // pointer either way. Keyboard and gamepad acceptance are not
        // affected.
        bool clickAccepts = true;
    };

}
