// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen stack helpers and widget validation for Ui::State.

#include "UiState.h"

#include <babylon/log/Log.h>

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

    // The layout and hit helpers below only read the top screen and the
    // painter's measurements; they never change UI state.
    detail::ScreenState* Ui::State::top()
    {
        return screens.empty() ? nullptr : &screens.back();
    }

    const detail::ScreenState* Ui::State::top() const
    {
        return screens.empty() ? nullptr : &screens.back();
    }

    detail::ScreenState* Ui::State::findScreen(ScreenId id)
    {
        for ( detail::ScreenState& screen : screens )
        {
            if ( screen.id == id )
            {
                return &screen;
            }
        }
        return nullptr;
    }

    const detail::ScreenState* Ui::State::findScreen(ScreenId id) const
    {
        for ( const detail::ScreenState& screen : screens )
        {
            if ( screen.id == id )
            {
                return &screen;
            }
        }
        return nullptr;
    }

    void Ui::State::validateOrLog(const Widget& widget, bool& valid)
    {
        valid = true;
        const std::string_view name = detail::cvarName(widget);

        if ( const Slider* slider = detail::get<Slider>(widget) )
        {
            if ( !std::isfinite(slider->min) || !std::isfinite(slider->max)
                || !std::isfinite(slider->step) || slider->min > slider->max
                || slider->step < 0.0 )
            {
                babylon::log::warning("ui: screen rejected: invalid slider range");
                valid = false;
                return;
            }
        }
        if ( const Choice* choice = detail::get<Choice>(widget) )
        {
            if ( choice->options.empty()
                || std::any_of(choice->options.begin(), choice->options.end(),
                    [](const std::string& option) { return option.empty(); }) )
            {
                babylon::log::warning("ui: screen rejected: a choice needs "
                    "at least one non-empty option");
                valid = false;
                return;
            }
        }
        if ( name.empty() )
        {
            return;
        }

        if ( detail::get<Toggle>(widget) != nullptr )
        {
            if ( !cvars.getBool(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not a bool",
                    name);
                valid = false;
            }
            return;
        }
        if ( detail::get<Slider>(widget) != nullptr )
        {
            if ( !cvars.getInt(name).has_value() && !cvars.getFloat(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not numeric",
                    name);
                valid = false;
            }
            return;
        }
        if ( const Choice* choice = detail::get<Choice>(widget) )
        {
            if ( const std::optional<std::int64_t> value = cvars.getInt(name) )
            {
                (void)value;
                if ( const cvars::Desc* desc = cvars.desc(name) )
                {
                    if ( desc->min.has_value() && *desc->min > 0.0 )
                    {
                        babylon::log::warning("ui: screen rejected: cvar '{}' range "
                            "excludes option 0", name);
                        valid = false;
                    }
                    else if ( desc->max.has_value()
                        && *desc->max < static_cast<double>(choice->options.size() - 1) )
                    {
                        babylon::log::warning("ui: screen rejected: cvar '{}' range "
                            "excludes the last option", name);
                        valid = false;
                    }
                }
            }
            else if ( !cvars.getString(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is neither int "
                    "nor string", name);
                valid = false;
            }
            return;
        }
        if ( detail::get<TextEntry>(widget) != nullptr )
        {
            if ( !cvars.getString(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not a string",
                    name);
                valid = false;
            }
        }
    }

    bool Ui::State::validateScreen(const Screen& screen)
    {
        for ( const Widget& widget : screen.widgets )
        {
            bool valid = true;
            validateOrLog(widget, valid);
            if ( !valid )
            {
                return false;
            }
        }
        return true;
    }

    void Ui::State::initLocal(detail::ScreenState& state)
    {
        const std::size_t count = state.screen.widgets.size();
        state.local.assign(count, std::nullopt);
        state.listOffsets.assign(count, 0);
        for ( std::size_t i = 0; i < count; ++i )
        {
            const Widget& widget = state.screen.widgets[i];
            if ( !detail::valueKind(widget) )
            {
                continue;
            }
            const std::string_view name = detail::cvarName(widget);
            if ( const TextEntry* entry = detail::get<TextEntry>(widget) )
            {
                std::string text = entry->defaultText;
                if ( !entry->cvar.empty() )
                {
                    const std::optional<std::string> value = cvars.getString(name);
                    if ( value.has_value() && !value->empty() )
                    {
                        text = *value;
                    }
                }
                state.local[i] = text;
            }
            else if ( !name.empty() )
            {
                continue;
            }
            else
            {
                state.local[i] = detail::defaultValueOf(widget);
            }
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( List* list = detail::get<List>(state.screen.widgets[i]) )
            {
                if ( !list->items.empty() )
                {
                    list->selected = std::min(list->selected, list->items.size() - 1);
                }
                else
                {
                    list->selected = 0;
                }
                ensureListVisible(state, i);
            }
        }
    }

}
