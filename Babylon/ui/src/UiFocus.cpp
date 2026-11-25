// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Focus, value changes and binding capture for Ui::State.

#include "UiState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace babylon::ui
{

namespace
{
    double clampDouble(double value, double low, double high)
    {
        return std::max(low, std::min(value, high));
    }

    bool enabledFlag(const Widget& widget)
    {
        return std::visit([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr ( std::is_same_v<T, Button> || std::is_same_v<T, Toggle>
                || std::is_same_v<T, Slider> || std::is_same_v<T, Choice>
                || std::is_same_v<T, List> || std::is_same_v<T, BindingCapture>
                || std::is_same_v<T, TextEntry> )
            {
                return value.enabled;
            }
            else
            {
                return true;
            }
        }, widget);
    }
}

    bool Ui::State::isReadOnly(std::string_view name) const
    {
        const std::optional<cvars::Flag> flags = cvars.flags(name);
        return flags.has_value() && cvars::hasFlag(*flags, cvars::Flag::ReadOnly);
    }

    bool Ui::State::isWidgetDisabled(const detail::ScreenState& state, std::size_t index) const
    {
        const Widget& widget = state.screen.widgets[index];
        if ( !enabledFlag(widget) )
        {
            return true;
        }
        const std::string_view name = detail::cvarName(widget);
        return !name.empty() && isReadOnly(name);
    }

    bool Ui::State::isFocusable(const detail::ScreenState& state, std::size_t index) const
    {
        return index < state.screen.widgets.size() && detail::focusableKind(state.screen.widgets[index])
            && !isWidgetDisabled(state, index);
    }

    std::size_t Ui::State::firstFocusable(const detail::ScreenState& state) const
    {
        for ( std::size_t i = 0; i < state.screen.widgets.size(); ++i )
        {
            if ( isFocusable(state, i) )
            {
                return i;
            }
        }
        return kNoWidget;
    }

    std::size_t Ui::State::nextFocusable(const detail::ScreenState& state, std::size_t from, int direction) const
    {
        const std::size_t count = state.screen.widgets.size();
        if ( count == 0 )
        {
            return kNoWidget;
        }
        long start = from == kNoWidget ? (direction > 0 ? -1L : 0L)
            : static_cast<long>(from);
        for ( std::size_t step = 1; step <= count; ++step )
        {
            const long delta = direction > 0 ? static_cast<long>(step)
                : -static_cast<long>(step);
            const long index = ((start + delta) % static_cast<long>(count)
                + static_cast<long>(count)) % static_cast<long>(count);
            if ( isFocusable(state, static_cast<std::size_t>(index)) )
            {
                return static_cast<std::size_t>(index);
            }
        }
        return kNoWidget;
    }

    bool Ui::State::currentToggle(const detail::ScreenState& state, std::size_t index) const
    {
        const Toggle* toggle = detail::get<Toggle>(state.screen.widgets[index]);
        if ( toggle == nullptr )
        {
            return false;
        }
        if ( !toggle->cvar.empty() )
        {
            return cvars.getBool(toggle->cvar).value_or(false);
        }
        if ( index < state.local.size() && state.local[index].has_value() )
        {
            return std::get<bool>(*state.local[index]);
        }
        return false;
    }

    bool Ui::State::sliderIsInt(const Slider& slider) const
    {
        return !slider.cvar.empty() && cvars.getInt(slider.cvar).has_value();
    }

    double Ui::State::currentSlider(const detail::ScreenState& state, std::size_t index) const
    {
        const Slider* slider = detail::get<Slider>(state.screen.widgets[index]);
        if ( slider == nullptr )
        {
            return 0.0;
        }
        if ( !slider->cvar.empty() )
        {
            if ( const std::optional<std::int64_t> value = cvars.getInt(slider->cvar) )
            {
                return static_cast<double>(*value);
            }
            if ( const std::optional<double> value = cvars.getFloat(slider->cvar) )
            {
                return *value;
            }
        }
        if ( index < state.local.size() && state.local[index].has_value() )
        {
            return std::get<double>(*state.local[index]);
        }
        return slider->min;
    }

    std::size_t Ui::State::currentChoiceIndex(const Choice& choice, const detail::ScreenState& state,
        std::size_t index) const
    {
        std::size_t selected = 0;
        if ( !choice.cvar.empty() )
        {
            if ( const std::optional<std::int64_t> intValue = cvars.getInt(choice.cvar) )
            {
                selected = static_cast<std::size_t>(std::max<std::int64_t>(0, *intValue));
            }
            else if ( const std::optional<std::string> stringValue = cvars.getString(choice.cvar) )
            {
                const auto found = std::find(choice.options.begin(), choice.options.end(), *stringValue);
                selected = found == choice.options.end() ? 0
                    : static_cast<std::size_t>(found - choice.options.begin());
            }
        }
        else if ( index < state.local.size() && state.local[index].has_value() )
        {
            selected = static_cast<std::size_t>(
                std::max<std::int64_t>(0, std::get<std::int64_t>(*state.local[index])));
        }
        if ( choice.options.empty() )
        {
            return 0;
        }
        return std::min(selected, choice.options.size() - 1);
    }

    void Ui::State::setFocus(detail::ScreenState& state, std::size_t index)
    {
        if ( state.focus == index )
        {
            return;
        }
        const bool enteringTextEntry = index < state.screen.widgets.size()
            && detail::get<TextEntry>(state.screen.widgets[index]) != nullptr;
        std::string enteringText;
        if ( enteringTextEntry )
        {
            enteringText = displayText(state, index);
        }
        state.focus = index;
        if ( enteringTextEntry )
        {
            setLocalText(state, index, enteringText);
            state.editStartText = enteringText;
            state.editCursor = enteringText.size();
        }
        if ( index != kNoWidget )
        {
            ensureFocusVisible(state);
            if ( skin.focusSound != 0 )
            {
                events.emplace_back(SoundCue{ skin.focusSound });
            }
        }
    }

    void Ui::State::resetFocus(detail::ScreenState& state)
    {
        state.focus = state.screen.initialFocus;
        if ( state.focus >= state.screen.widgets.size()
            || !isFocusable(state, state.focus) )
        {
            state.focus = firstFocusable(state);
        }
        if ( state.focus != kNoWidget
            && detail::get<TextEntry>(state.screen.widgets[state.focus]) != nullptr )
        {
            const std::string text = displayText(state, state.focus);
            setLocalText(state, state.focus, text);
            state.editStartText = text;
            state.editCursor = text.size();
        }
        ensureFocusVisible(state);
    }

    void Ui::State::moveFocus(detail::ScreenState& state, int direction)
    {
        const std::size_t next = nextFocusable(state, state.focus, direction);
        setFocus(state, next);
    }

    // Moves a focused list's selection one row; false when there is no row in that direction.
    bool Ui::State::moveSelection(detail::ScreenState& state, int direction)
    {
        if ( state.focus == kNoWidget )
        {
            return false;
        }
        List* list = detail::get<List>(state.screen.widgets[state.focus]);
        if ( list == nullptr || list->items.empty() )
        {
            return false;
        }
        const long current = static_cast<long>(std::min(list->selected,
            list->items.size() - 1));
        const long next = std::clamp(current + direction, 0L,
            static_cast<long>(list->items.size()) - 1);
        if ( next != current )
        {
            list->selected = static_cast<std::size_t>(next);
            ensureListVisible(state, state.focus);
            const ListItem& item = list->items[list->selected];
            events.emplace_back(SelectionChanged{ state.id, state.focus,
                list->selected, item.id });
            if ( skin.focusSound != 0 )
            {
                events.emplace_back(SoundCue{ skin.focusSound });
            }
            return true;
        }
        return false;
    }

    // Up and down move a focused list's selection; past its first or last row they leave the list
    // like any other widget, so a pad without Tab reaches the widgets above and below it.
    void Ui::State::navigateVertical(detail::ScreenState& state, int direction)
    {
        if ( state.focus != kNoWidget
            && detail::get<List>(state.screen.widgets[state.focus]) != nullptr
            && moveSelection(state, direction) )
        {
            return;
        }
        const std::size_t next = nextFocusable(state, state.focus, direction);
        if ( next != state.focus )
        {
            setFocus(state, next);
        }
    }

    void Ui::State::adjustSlider(detail::ScreenState& state, std::size_t index, int direction)
    {
        Slider* slider = detail::get<Slider>(state.screen.widgets[index]);
        if ( slider == nullptr )
        {
            return;
        }
        double low = slider->min;
        double high = slider->max;
        if ( !slider->cvar.empty() )
        {
            if ( const cvars::Desc* desc = cvars.desc(slider->cvar) )
            {
                if ( desc->min.has_value() )
                {
                    low = std::max(low, *desc->min);
                }
                if ( desc->max.has_value() )
                {
                    high = std::min(high, *desc->max);
                }
            }
        }
        if ( low > high )
        {
            return;
        }
        const bool isInt = sliderIsInt(*slider);
        const double current = clampDouble(currentSlider(state, index), low, high);
        double stored = current;
        std::int64_t storedInt = 0;

        if ( isInt )
        {
            // An int slider moves in whole steps that are at least one, so
            // a fractional step such as 0.4 still advances by one instead
            // of rounding back to the current value. The automatic step is
            // one hundredth of the effective range, but never below one.
            const double step = slider->step > 0.0
                ? slider->step
                : std::max(1.0, std::round((high - low) / 100.0));
            const long long delta = std::max(1LL, static_cast<long long>(std::llround(step)));
            const long long currentInt = static_cast<long long>(std::llround(current));
            const long long lowInt = static_cast<long long>(std::ceil(low));
            const long long highInt = static_cast<long long>(std::floor(high));
            if ( lowInt > highInt )
            {
                return;
            }
            const long long target = std::clamp(currentInt
                + static_cast<long long>(direction) * delta, lowInt, highInt);
            if ( target == currentInt )
            {
                return;
            }
            storedInt = static_cast<std::int64_t>(target);
            stored = static_cast<double>(storedInt);
        }
        else
        {
            const double step = slider->step > 0.0
                ? slider->step
                : std::max((high - low) / 100.0, 0.000001);
            stored = clampDouble(current + static_cast<double>(direction) * step,
                low, high);
            if ( stored == current )
            {
                return;
            }
        }

        bool applied = true;
        if ( !slider->cvar.empty() )
        {
            const cvars::SetResult result = isInt
                ? cvars.set(slider->cvar, cvars::Value{ storedInt },
                    cvars::Source::User)
                : cvars.set(slider->cvar, cvars::Value{ stored }, cvars::Source::User);
            applied = result == cvars::SetResult::Ok;
        }
        if ( applied )
        {
            if ( slider->cvar.empty() )
            {
                state.local[index] = stored;
            }
            events.emplace_back(ValueChanged{ state.id, index, stored });
        }
    }

    void Ui::State::adjustChoice(detail::ScreenState& state, std::size_t index, int direction)
    {
        Choice* choice = detail::get<Choice>(state.screen.widgets[index]);
        if ( choice == nullptr || choice->options.empty() )
        {
            return;
        }
        const std::size_t current = currentChoiceIndex(*choice, state, index);
        const long next = std::clamp(static_cast<long>(current) + direction, 0L,
            static_cast<long>(choice->options.size()) - 1);
        if ( next == static_cast<long>(current) )
        {
            return;
        }

        bool applied = true;
        if ( !choice->cvar.empty() )
        {
            const cvars::SetResult result = cvars.getInt(choice->cvar).has_value()
                ? cvars.set(choice->cvar, cvars::Value{ static_cast<std::int64_t>(next) },
                    cvars::Source::User)
                : cvars.set(choice->cvar,
                    cvars::Value{ choice->options[static_cast<std::size_t>(next)] },
                    cvars::Source::User);
            applied = result == cvars::SetResult::Ok;
        }
        if ( applied )
        {
            if ( choice->cvar.empty() )
            {
                state.local[index] = static_cast<std::int64_t>(next);
            }
            events.emplace_back(ValueChanged{ state.id, index,
                static_cast<std::int64_t>(next) });
        }
    }

    void Ui::State::adjustHorizontal(detail::ScreenState& state, int direction)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        Widget& widget = state.screen.widgets[state.focus];
        if ( detail::get<Slider>(widget) != nullptr )
        {
            adjustSlider(state, state.focus, direction);
        }
        else if ( detail::get<Choice>(widget) != nullptr )
        {
            adjustChoice(state, state.focus, direction);
        }
        else if ( detail::get<TextEntry>(widget) != nullptr )
        {
            const std::string& text = std::get<std::string>(*state.local[state.focus]);
            state.editCursor = direction < 0
                ? detail::previousCodePoint(text, state.editCursor)
                : detail::nextCodePoint(text, state.editCursor);
        }
    }

    void Ui::State::acceptFocused(detail::ScreenState& state)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        Widget& widget = state.screen.widgets[state.focus];

        if ( Button* button = detail::get<Button>(widget) )
        {
            if ( button->action != kNoAction )
            {
                events.emplace_back(ActionPressed{ state.id, button->action, 0 });
            }
            if ( skin.acceptSound != 0 )
            {
                events.emplace_back(SoundCue{ skin.acceptSound });
            }
            return;
        }
        if ( detail::get<Toggle>(widget) != nullptr )
        {
            const bool current = currentToggle(state, state.focus);
            const bool changed = !current;
            bool applied = true;
            const std::string_view name = detail::cvarName(widget);
            if ( !name.empty() )
            {
                applied = cvars.set(name, cvars::Value{ changed },
                    cvars::Source::User) == cvars::SetResult::Ok;
            }
            if ( applied )
            {
                state.local[state.focus] = changed;
                events.emplace_back(ValueChanged{ state.id, state.focus, changed });
                if ( skin.acceptSound != 0 )
                {
                    events.emplace_back(SoundCue{ skin.acceptSound });
                }
            }
            return;
        }
        if ( List* list = detail::get<List>(widget) )
        {
            if ( !list->items.empty() && list->action != kNoAction )
            {
                list->selected = std::min(list->selected, list->items.size() - 1);
                events.emplace_back(ActionPressed{ state.id, list->action,
                    list->items[list->selected].id });
                if ( skin.acceptSound != 0 )
                {
                    events.emplace_back(SoundCue{ skin.acceptSound });
                }
            }
            return;
        }
        if ( detail::get<BindingCapture>(widget) != nullptr )
        {
            state.capturing = true;
            state.captureWidget = state.focus;
            return;
        }
        if ( detail::get<TextEntry>(widget) != nullptr )
        {
            submitText(state, state.focus);
            return;
        }
    }

    void Ui::State::setBinding(detail::ScreenState& state, Binding binding)
    {
        const std::size_t widget = state.captureWidget;
        state.capturing = false;
        state.captureWidget = kNoWidget;
        events.emplace_back(BindingCaptured{ state.id, widget, binding });
        if ( skin.acceptSound != 0 )
        {
            events.emplace_back(SoundCue{ skin.acceptSound });
        }
    }

    void Ui::State::cancelCapture(detail::ScreenState& state)
    {
        state.capturing = false;
        state.captureWidget = kNoWidget;
    }

    void Ui::State::back(detail::ScreenState& state)
    {
        if ( state.capturing )
        {
            cancelCapture(state);
            return;
        }
        if ( state.focus != kNoWidget
            && detail::get<TextEntry>(state.screen.widgets[state.focus]) != nullptr
            && state.focus < state.local.size() && state.local[state.focus].has_value()
            && std::get<std::string>(*state.local[state.focus]) != state.editStartText )
        {
            setLocalText(state, state.focus, state.editStartText);
            state.editCursor = state.editStartText.size();
            return;
        }
        const ScreenId id = state.id;
        const ResultCode result = state.screen.backResult;
        screens.pop_back();
        events.emplace_back(ScreenClosed{ id, result });
        if ( skin.backSound != 0 )
        {
            events.emplace_back(SoundCue{ skin.backSound });
        }
    }

}
