// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Keys.h>

namespace babylon::ui
{

    // What kind of input a Binding names.
    enum class BindingKind
    {
        None,           // no binding; no other field is meaningful
        Key,            // a platform::Key
        MouseButton,    // a platform::MouseButton
        GamepadButton,  // a platform::GamepadButton
        GamepadAxis     // a platform::GamepadAxis and a direction
    };

    // One key, mouse button, gamepad button or gamepad axis direction
    // captured by a BindingCapture widget. It is a plain value: the game
    // copies it into its own configuration and the ui module keeps no
    // reference. The fields are meaningful only for the kind named by kind.
    //
    // A captured gamepad axis names the axis and the half that crossed the
    // capture threshold (half of full deflection): a stick reports -1 or +1,
    // while a trigger, which rests at 0 and moves to 1, reports only +1.
    // Capture accepts every gamepad axis, not only the two the navigation
    // map uses.
    struct Binding
    {
        BindingKind kind = BindingKind::None;

        platform::Key key = platform::Key::Unknown;                 // kind == Key
        platform::MouseButton mouseButton = platform::MouseButton::Left;
        platform::GamepadButton button = platform::GamepadButton::South;
        platform::GamepadAxis axis = platform::GamepadAxis::LeftX;

        // The axis direction: -1 for the negative half, +1 for the positive
        // half, and 0 when the axis is at rest or below the capture
        // threshold. A trigger never reports -1.
        float axisDirection = 0.0f;

        bool operator==(const Binding&) const = default;
    };

}
