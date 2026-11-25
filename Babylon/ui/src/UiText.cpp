// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Text entry for Ui::State.

#include "UiState.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace babylon::ui
{

namespace
{
    // The largest edit buffer a TextEntry may hold, in bytes. A code point
    // is only ever inserted whole, so the buffer can stay below it.
    constexpr std::size_t kMaxTextBytes = 1024;
}

    std::string Ui::State::displayText(const detail::ScreenState& state, std::size_t index) const
    {
        const Widget& widget = state.screen.widgets[index];
        const TextEntry* entry = detail::get<TextEntry>(widget);
        if ( entry == nullptr )
        {
            return {};
        }
        const bool editing = state.focus == index;
        if ( !editing && !entry->cvar.empty() )
        {
            const std::optional<std::string> value = cvars.getString(entry->cvar);
            return value.has_value() && !value->empty() ? *value : entry->defaultText;
        }
        if ( index < state.local.size() && state.local[index].has_value() )
        {
            return std::get<std::string>(*state.local[index]);
        }
        return entry->defaultText;
    }

    void Ui::State::setLocalText(detail::ScreenState& state, std::size_t index, std::string text)
    {
        if ( index < state.local.size() )
        {
            state.local[index] = std::move(text);
        }
    }

    // Inserts a whole UTF-8 string at the edit cursor, keeping only the
    // complete code points that fit in kMaxTextBytes. A sequence is never
    // cut in half, so the buffer stays valid UTF-8 when the input was.
    void Ui::State::insertText(detail::ScreenState& state, std::string_view input)
    {
        if ( state.focus >= state.screen.widgets.size()
            || detail::get<TextEntry>(state.screen.widgets[state.focus]) == nullptr
            || state.focus >= state.local.size() || !state.local[state.focus].has_value() )
        {
            return;
        }

        std::string& text = std::get<std::string>(*state.local[state.focus]);
        const std::size_t room = text.size() < kMaxTextBytes
            ? kMaxTextBytes - text.size() : 0;
        std::string added;
        for ( std::size_t pos = 0; pos < input.size(); )
        {
            const std::size_t length =
                detail::utf8SequenceLength(static_cast<unsigned char>(input[pos]));
            if ( length > input.size() - pos || added.size() + length > room )
            {
                break;
            }
            added.append(input.substr(pos, length));
            pos += length;
        }
        if ( added.empty() )
        {
            return;
        }
        if ( state.editCursor > text.size() )
        {
            state.editCursor = text.size();
        }
        text.insert(state.editCursor, added);
        state.editCursor += added.size();
    }

    // True when the top screen is editing a TextEntry, so the game should
    // have the platform's text input started.
    bool Ui::State::textInputWanted() const
    {
        const detail::ScreenState* state = top();
        return state != nullptr && !state->capturing
            && state->focus < state->screen.widgets.size()
            && detail::get<TextEntry>(state->screen.widgets[state->focus]) != nullptr;
    }

    // Reports a change of textInputWanted() once, after focus, the stack or
    // a widget changed. The ui owns no window, so the game turns this into
    // the platform call.
    void Ui::State::syncTextInput()
    {
        const bool wanted = textInputWanted();
        if ( wanted != textInputOn )
        {
            textInputOn = wanted;
            events.emplace_back(TextInputWanted{ wanted });
        }
    }

    void Ui::State::submitText(detail::ScreenState& state, std::size_t index)
    {
        TextEntry* entry = detail::get<TextEntry>(state.screen.widgets[index]);
        if ( entry == nullptr )
        {
            return;
        }
        const std::string text = index < state.local.size()
            && state.local[index].has_value()
            ? std::get<std::string>(*state.local[index]) : entry->defaultText;

        if ( !entry->cvar.empty() )
        {
            const cvars::SetResult result = cvars.set(entry->cvar,
                cvars::Value{ text }, cvars::Source::User);
            if ( result != cvars::SetResult::Ok
                && result != cvars::SetResult::Unchanged )
            {
                return;
            }
        }
        setLocalText(state, index, text);
        state.editStartText = text;
        events.emplace_back(TextSubmitted{ state.id, index, text });
        if ( skin.acceptSound != 0 )
        {
            events.emplace_back(SoundCue{ skin.acceptSound });
        }
    }

    // Inserts the platform's committed text into an active TextEntry. Text
    // for a screen that is not editing one is ignored, as are text events
    // while a binding capture owns the input.
    void Ui::State::handleTextInput(const platform::TextInput& input)
    {
        detail::ScreenState* state = top();
        if ( state == nullptr || state->capturing )
        {
            return;
        }
        insertText(*state, input.text);
    }

}
