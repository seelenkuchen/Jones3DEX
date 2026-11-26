// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/cvars/Registry.h>
#include <babylon/log/CaptureSink.h>
#include <babylon/log/Sink.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace babylon::ui::test
{
    inline constexpr platform::WindowId kWindow = 1;

    inline platform::KeyChanged keyEvent(platform::Key key, bool down, bool repeat = false)
    {
        return platform::KeyChanged{ kWindow, key, down, repeat };
    }

    inline platform::MouseMoved mouseEvent(float x, float y)
    {
        return platform::MouseMoved{ kWindow, x, y, 0.0f, 0.0f };
    }

    inline platform::MouseButtonChanged mouseButtonEvent(platform::MouseButton button, bool down)
    {
        return platform::MouseButtonChanged{ kWindow, button, down };
    }

    inline platform::GamepadButtonChanged padButtonEvent(platform::GamepadButton button, bool down)
    {
        return platform::GamepadButtonChanged{ 1, button, down };
    }

    inline platform::GamepadAxisMoved padAxisEvent(platform::GamepadAxis axis, float value)
    {
        return platform::GamepadAxisMoved{ 1, axis, value };
    }

    inline void tap(Ui& ui, platform::Key key)
    {
        ui.handleEvent(platform::Event(keyEvent(key, true)));
        ui.handleEvent(platform::Event(keyEvent(key, false)));
    }

    inline void addCvar(cvars::Registry& registry, const std::string& name,
        cvars::Value value, cvars::Flag flags = cvars::Flag::None,
        std::optional<double> min = std::nullopt,
        std::optional<double> max = std::nullopt)
    {
        cvars::Desc desc;
        desc.name = name;
        desc.defaultValue = std::move(value);
        desc.flags = flags;
        desc.min = min;
        desc.max = max;
        REQUIRE(registry.add(desc));
    }

    inline std::vector<UiEvent> drain(Ui& ui)
    {
        std::vector<UiEvent> events;
        while ( std::optional<UiEvent> event = ui.pollEvent() )
        {
            events.push_back(std::move(*event));
        }
        return events;
    }

    template <class T>
    inline const T* findEvent(const std::vector<UiEvent>& events)
    {
        for ( const UiEvent& event : events )
        {
            if ( const T* found = std::get_if<T>(&event) )
            {
                return found;
            }
        }
        return nullptr;
    }

    inline Screen twoButtons(std::size_t initialFocus = kNoWidget)
    {
        Screen screen;
        screen.title = "Actions";
        screen.widgets = {
            Button{ "First", 1, true },
            Button{ "Second", 2, true },
            Button{ "Third", 3, true },
        };
        screen.initialFocus = initialFocus;
        return screen;
    }

    // Canvas point to window pixel for a 640x480 window: the scale is 1.
    inline platform::MouseMoved canvasMouse(float x, float y)
    {
        return mouseEvent(x, y);
    }
}
