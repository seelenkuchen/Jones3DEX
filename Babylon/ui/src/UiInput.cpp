// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Keyboard, mouse and gamepad handling for Ui::State.

#include "UiState.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

namespace babylon::ui
{

namespace
{
    // The magnitude a gamepad axis must reach to be reported by a binding capture. It is a fixed
    // rule, independent of NavigationMap::axisThreshold: capture accepts every gamepad axis, not
    // only the two the navigation map uses.
    constexpr float kCaptureAxisThreshold = 0.5f;
    constexpr double kAxisInitialDelay = 0.35;
    constexpr double kAxisRepeatRate = 0.10;
}

    void Ui::State::handleKey(const platform::KeyChanged& key)
    {
        const std::size_t index = static_cast<std::size_t>(key.key);
        if ( key.key < platform::Key::Count )
        {
            const bool wasDown = keysDown[index];
            keysDown[index] = key.down;
            if ( key.key == platform::Key::LeftShift || key.key == platform::Key::RightShift )
            {
                shiftDown = keysDown[static_cast<std::size_t>(platform::Key::LeftShift)]
                    || keysDown[static_cast<std::size_t>(platform::Key::RightShift)];
            }
            if ( key.down && wasDown && !key.repeat )
            {
                return;
            }
        }

        detail::ScreenState* state = top();
        if ( state == nullptr || !key.down )
        {
            return;
        }

        if ( state->capturing )
        {
            if ( key.key == navigation.back )
            {
                cancelCapture(*state);
                return;
            }
            if ( !key.repeat && key.key != platform::Key::Unknown )
            {
                setBinding(*state, Binding{ BindingKind::Key, key.key });
            }
            return;
        }

        if ( key.key == navigation.back && !key.repeat )
        {
            back(*state);
            return;
        }
        if ( key.key == navigation.accept && !key.repeat )
        {
            acceptFocused(*state);
            return;
        }
        if ( key.key == navigation.focusNext && !key.repeat )
        {
            moveFocus(*state, shiftDown ? -1 : 1);
            return;
        }
        if ( !state->screen.tabs.empty() )
        {
            if ( !key.repeat && key.key == navigation.previousTab )
            {
                switchTab(*state, -1);
                return;
            }
            if ( !key.repeat && key.key == navigation.nextTab )
            {
                switchTab(*state, 1);
                return;
            }
        }

        TextEntry* entry = state->focus < state->screen.widgets.size()
            ? detail::get<TextEntry>(state->screen.widgets[state->focus]) : nullptr;
        if ( entry != nullptr )
        {
            if ( key.key == platform::Key::Backspace )
            {
                std::string& text = std::get<std::string>(*state->local[state->focus]);
                if ( state->editCursor > 0 && state->editCursor <= text.size() )
                {
                    const std::size_t start =
                        detail::previousCodePoint(text, state->editCursor);
                    text.erase(start, state->editCursor - start);
                    state->editCursor = start;
                }
                return;
            }
            if ( key.key == platform::Key::Delete )
            {
                std::string& text = std::get<std::string>(*state->local[state->focus]);
                if ( state->editCursor < text.size() )
                {
                    const std::size_t end =
                        detail::nextCodePoint(text, state->editCursor);
                    text.erase(state->editCursor, end - state->editCursor);
                }
                return;
            }
            if ( key.key == platform::Key::Home )
            {
                state->editCursor = 0;
                return;
            }
            if ( key.key == platform::Key::End )
            {
                state->editCursor =
                    std::get<std::string>(*state->local[state->focus]).size();
                return;
            }
            if ( key.key == platform::Key::Left )
            {
                const std::string& text =
                    std::get<std::string>(*state->local[state->focus]);
                state->editCursor = detail::previousCodePoint(text, state->editCursor);
                return;
            }
            if ( key.key == platform::Key::Right )
            {
                const std::string& text =
                    std::get<std::string>(*state->local[state->focus]);
                state->editCursor = detail::nextCodePoint(text, state->editCursor);
                return;
            }
        }

        if ( key.key == navigation.up )
        {
            navigateVertical(*state, -1);
        }
        else if ( key.key == navigation.down )
        {
            navigateVertical(*state, 1);
        }
        else if ( key.key == navigation.left )
        {
            adjustHorizontal(*state, -1);
        }
        else if ( key.key == navigation.right )
        {
            adjustHorizontal(*state, 1);
        }
    }

    detail::Hit Ui::State::hitTest(const detail::ScreenState& state, Point point) const
    {
        const detail::Layout layout = computeLayout(state);
        const float scroll = effectiveScroll(state, layout);
        if ( !layout.rowsClip.contains(point) )
        {
            return {};
        }
        for ( auto it = layout.rows.rbegin(); it != layout.rows.rend(); ++it )
        {
            const detail::LayoutRow& row = *it;
            if ( !rowVisible(layout, row, scroll) )
            {
                continue;
            }
            const Rect rect{ row.rect.x, row.rect.y - scroll, row.rect.width, row.rect.height };
            if ( const List* list = detail::get<List>(state.screen.widgets[row.widget]) )
            {
                for ( std::size_t i = 0; i < list->items.size(); ++i )
                {
                    Rect itemRect = listItemRect(state, row, i);
                    itemRect.y -= scroll;
                    if ( itemRect.contains(point) )
                    {
                        return detail::Hit{ row.widget, i };
                    }
                }
                const Rect heading = listHeadingRect(state, row);
                if ( heading.width > 0.0f && Rect{ heading.x, heading.y - scroll,
                    heading.width, heading.height }.contains(point) )
                {
                    return detail::Hit{ row.widget, kNoWidget };
                }
                continue;
            }
            if ( rect.contains(point) )
            {
                return detail::Hit{ row.widget, kNoWidget };
            }
        }
        return {};
    }

    void Ui::State::handleMouseButton(const platform::MouseButtonChanged& button)
    {
        const std::size_t index = static_cast<std::size_t>(button.button);
        if ( index >= mouseButtons.size() )
        {
            return;
        }
        const bool wasDown = mouseButtons[index];
        mouseButtons[index] = button.down;

        detail::ScreenState* state = top();
        if ( state == nullptr )
        {
            return;
        }
        if ( button.down && state->capturing )
        {
            if ( !wasDown )
            {
                setBinding(*state, Binding{ BindingKind::MouseButton, platform::Key::Unknown,
                    button.button });
            }
            return;
        }
        if ( button.button != platform::MouseButton::Left || !button.down || wasDown )
        {
            return;
        }
        if ( viewportHeight <= 0 )
        {
            return;
        }

        const Point point = canvasPoint(mouseX, mouseY);
        if ( !state->screen.tabs.empty() )
        {
            const detail::Layout layout = computeLayout(*state);
            for ( const detail::LayoutTab& tab : layout.tabs )
            {
                if ( tab.rect.contains(point) )
                {
                    selectTab(*state, tab.index);
                    return;
                }
            }
        }
        const detail::Hit hit = hitTest(*state, point);
        if ( hit.widget == kNoWidget )
        {
            return;
        }
        if ( List* list = detail::get<List>(state->screen.widgets[hit.widget]) )
        {
            const std::size_t oldFocus = state->focus;
            setFocus(*state, hit.widget);
            if ( hit.item != kNoWidget && hit.item < list->items.size() )
            {
                const std::size_t old = list->selected;
                list->selected = hit.item;
                ensureListVisible(*state, hit.widget);
                if ( old != list->selected )
                {
                    events.emplace_back(SelectionChanged{ state->id, hit.widget,
                        list->selected, list->items[list->selected].id });
                    if ( skin.focusSound != 0 )
                    {
                        events.emplace_back(SoundCue{ skin.focusSound });
                    }
                }
            }
            else if ( oldFocus == hit.widget && navigation.clickAccepts )
            {
                acceptFocused(*state);
            }
            return;
        }
        if ( !isFocusable(*state, hit.widget) )
        {
            return;
        }
        if ( state->focus != hit.widget )
        {
            setFocus(*state, hit.widget);
        }
        else if ( navigation.clickAccepts )
        {
            acceptFocused(*state);
        }
    }

    void Ui::State::handleMouseWheel(const platform::MouseWheelMoved& wheel)
    {
        detail::ScreenState* state = top();
        if ( state == nullptr || viewportHeight <= 0 )
        {
            return;
        }
        const detail::Hit hit = hitTest(*state, canvasPoint(mouseX, mouseY));
        if ( hit.widget == kNoWidget )
        {
            return;
        }
        List* list = detail::get<List>(state->screen.widgets[hit.widget]);
        if ( list == nullptr || list->items.size() <= detail::kListVisibleRows )
        {
            return;
        }
        const std::size_t maxOffset = list->items.size() - detail::kListVisibleRows;
        std::size_t& offset = state->listOffsets[hit.widget];
        if ( wheel.dy > 0.0f )
        {
            offset = offset > 0 ? offset - 1 : 0;
        }
        else if ( wheel.dy < 0.0f )
        {
            offset = std::min(offset + 1, maxOffset);
        }
    }

    void Ui::State::handleGamepadButton(const platform::GamepadButtonChanged& button)
    {
        const std::size_t index = static_cast<std::size_t>(button.button);
        if ( index >= gamepadButtons.size() )
        {
            return;
        }
        const bool wasDown = gamepadButtons[index];
        gamepadButtons[index] = button.down;
        if ( !button.down )
        {
            return;
        }
        detail::ScreenState* state = top();
        if ( state == nullptr )
        {
            return;
        }
        if ( state->capturing )
        {
            if ( !wasDown )
            {
                setBinding(*state, Binding{ BindingKind::GamepadButton, platform::Key::Unknown,
                    platform::MouseButton::Left, button.button });
            }
            return;
        }
        if ( wasDown )
        {
            return;
        }
        if ( button.button == navigation.previousTabButton )
        {
            switchTab(*state, -1);
            return;
        }
        if ( button.button == navigation.nextTabButton )
        {
            switchTab(*state, 1);
            return;
        }
        if ( button.button == navigation.acceptButton )
        {
            acceptFocused(*state);
        }
        else if ( button.button == navigation.backButton )
        {
            back(*state);
        }
        else if ( button.button == navigation.upButton )
        {
            navigateVertical(*state, -1);
        }
        else if ( button.button == navigation.downButton )
        {
            navigateVertical(*state, 1);
        }
        else if ( button.button == navigation.leftButton )
        {
            adjustHorizontal(*state, -1);
        }
        else if ( button.button == navigation.rightButton )
        {
            adjustHorizontal(*state, 1);
        }
    }

    int Ui::State::axisDirection(platform::GamepadAxis axis, float value) const
    {
        if ( axis != navigation.horizontalAxis && axis != navigation.verticalAxis )
        {
            return 0;
        }
        if ( value >= navigation.axisThreshold )
        {
            return 1;
        }
        if ( value <= -navigation.axisThreshold )
        {
            return -1;
        }
        return 0;
    }

    // The capture rule for every axis: sticks report either half, the triggers rest at 0 and only
    // report their positive half. 0 is at rest or below the threshold.
    int Ui::State::captureAxisDirection(platform::GamepadAxis axis, float value)
    {
        if ( axis == platform::GamepadAxis::LeftTrigger
            || axis == platform::GamepadAxis::RightTrigger )
        {
            return value >= kCaptureAxisThreshold ? 1 : 0;
        }
        if ( value >= kCaptureAxisThreshold )
        {
            return 1;
        }
        if ( value <= -kCaptureAxisThreshold )
        {
            return -1;
        }
        return 0;
    }

    void Ui::State::fireAxis(bool horizontal, int direction)
    {
        detail::ScreenState* state = top();
        if ( state == nullptr )
        {
            return;
        }
        if ( horizontal )
        {
            adjustHorizontal(*state, direction);
        }
        else
        {
            navigateVertical(*state, direction);
        }
    }

    void Ui::State::handleAxis(const platform::GamepadAxisMoved& axis)
    {
        const std::size_t axisIndex = static_cast<std::size_t>(axis.axis);
        if ( axisIndex >= gamepadAxisDirections.size() )
        {
            return;
        }

        detail::ScreenState* screen = top();
        const int captureDirection = captureAxisDirection(axis.axis, axis.value);
        const int previousDirection = gamepadAxisDirections[axisIndex];
        gamepadAxisDirections[axisIndex] = captureDirection;

        if ( screen != nullptr && screen->capturing )
        {
            // Keep the navigation axes quiet once capture ends: a stick still held past the
            // capture threshold must not then navigate the screen.
            if ( axis.axis == navigation.horizontalAxis )
            {
                horizontalAxis.direction = captureDirection;
                horizontalAxis.held = 0.0;
                horizontalAxis.repeating = false;
            }
            else if ( axis.axis == navigation.verticalAxis )
            {
                verticalAxis.direction = captureDirection;
                verticalAxis.held = 0.0;
                verticalAxis.repeating = false;
            }

            if ( captureDirection != 0 && previousDirection == 0 )
            {
                setBinding(*screen, Binding{ BindingKind::GamepadAxis, platform::Key::Unknown,
                    platform::MouseButton::Left, platform::GamepadButton::South, axis.axis,
                    static_cast<float>(captureDirection) });
            }

            return;
        }

        // Outside capture only the navigation map's two axes move focus.
        if ( axis.axis != navigation.horizontalAxis
            && axis.axis != navigation.verticalAxis )
        {
            return;
        }
        detail::AxisState& state = axis.axis == navigation.horizontalAxis
            ? horizontalAxis : verticalAxis;
        const int direction = axisDirection(axis.axis, axis.value);

        if ( direction == state.direction )
        {
            return;
        }
        state.direction = direction;
        state.held = 0.0;
        state.repeating = false;
        if ( direction != 0 )
        {
            fireAxis(axis.axis == navigation.horizontalAxis, direction);
        }
    }

    void Ui::State::advanceAxis(detail::AxisState& axis, bool horizontal, double seconds)
    {
        if ( axis.direction == 0 )
        {
            return;
        }
        axis.held += seconds;
        if ( !axis.repeating )
        {
            if ( axis.held >= kAxisInitialDelay )
            {
                axis.repeating = true;
                axis.held = 0.0;
                fireAxis(horizontal, axis.direction);
            }
        }
        else if ( axis.held >= kAxisRepeatRate )
        {
            axis.held -= kAxisRepeatRate;
            fireAxis(horizontal, axis.direction);
        }
    }

    // Keeps the raw input state current when a screen's event callback consumed an event
    // before the default handlers could see it. Without this a consumed key-up would leave the
    // key down for the next screen, which then ignores its first press as a repeat.
    void Ui::State::trackConsumedEvent(const platform::Event& event)
    {
        std::visit([this](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr ( std::is_same_v<T, platform::KeyChanged> )
            {
                const std::size_t index = static_cast<std::size_t>(value.key);
                if ( value.key < platform::Key::Count )
                {
                    keysDown[index] = value.down;
                }
                shiftDown = keysDown[static_cast<std::size_t>(platform::Key::LeftShift)]
                    || keysDown[static_cast<std::size_t>(platform::Key::RightShift)];
            }
            else if constexpr ( std::is_same_v<T, platform::MouseMoved> )
            {
                mouseX = value.x;
                mouseY = value.y;
            }
            else if constexpr ( std::is_same_v<T, platform::MouseButtonChanged> )
            {
                const std::size_t index = static_cast<std::size_t>(value.button);
                if ( index < mouseButtons.size() )
                {
                    mouseButtons[index] = value.down;
                }
            }
            else if constexpr ( std::is_same_v<T, platform::GamepadButtonChanged> )
            {
                const std::size_t index = static_cast<std::size_t>(value.button);
                if ( index < gamepadButtons.size() )
                {
                    gamepadButtons[index] = value.down;
                }
            }
            else if constexpr ( std::is_same_v<T, platform::GamepadAxisMoved> )
            {
                const std::size_t index = static_cast<std::size_t>(value.axis);
                if ( index < gamepadAxisDirections.size() )
                {
                    gamepadAxisDirections[index] = captureAxisDirection(value.axis, value.value);
                }
            }
        }, event);
    }

    void Ui::State::handleEvent(const platform::Event& event)
    {
        std::visit([this](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr ( std::is_same_v<T, platform::KeyChanged> )
            {
                handleKey(value);
            }
            else if constexpr ( std::is_same_v<T, platform::MouseMoved> )
            {
                mouseX = value.x;
                mouseY = value.y;
            }
            else if constexpr ( std::is_same_v<T, platform::MouseButtonChanged> )
            {
                handleMouseButton(value);
            }
            else if constexpr ( std::is_same_v<T, platform::MouseWheelMoved> )
            {
                handleMouseWheel(value);
            }
            else if constexpr ( std::is_same_v<T, platform::TextInput> )
            {
                handleTextInput(value);
            }
            else if constexpr ( std::is_same_v<T, platform::GamepadButtonChanged> )
            {
                handleGamepadButton(value);
            }
            else if constexpr ( std::is_same_v<T, platform::GamepadAxisMoved> )
            {
                handleAxis(value);
            }
        }, event);
    }

}
