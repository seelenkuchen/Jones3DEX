// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/ui/Ui.h>

#include <babylon/log/Log.h>

#include "UiState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::ui
{

Ui::Ui(Painter& painter, cvars::Registry& cvars)
    : m_state(std::make_unique<State>(painter, cvars, *this))
{
}

Ui::~Ui() = default;

bool Ui::setSkin(const Skin& skin)
{
    if ( !(skin.rowHeight > 0.0f) || skin.titleHeight < 0.0f
        || skin.rowSpacing < 0.0f || skin.panelPadding < 0.0f
        || skin.minPanelWidth < 0.0f || skin.canvasMargin < 0.0f
        || skin.fixedPanelWidth < 0.0f || skin.fixedPanelWidth > 1.0f
        || skin.topMargin < 0.0f
        || skin.borderWidth < 0.0f || skin.markerSpacing < 0.0f
        || skin.promptHeight < 0.0f )
    {
        babylon::log::warning("ui: rejected an invalid skin");
        return false;
    }
    m_state->skin = skin;
    for ( detail::ScreenState& screen : m_state->screens )
    {
        m_state->ensureScroll(screen);
    }
    return true;
}

const Skin& Ui::skin() const
{
    return m_state->skin;
}

bool Ui::setNavigation(const NavigationMap& map)
{
    if ( !(map.axisThreshold >= 0.0f && map.axisThreshold <= 1.0f) )
    {
        babylon::log::warning("ui: rejected a navigation map with axisThreshold "
            "outside 0..1");
        return false;
    }
    m_state->navigation = map;
    return true;
}

const NavigationMap& Ui::navigationMap() const
{
    return m_state->navigation;
}

bool Ui::setViewport(int pixelWidth, int pixelHeight)
{
    if ( pixelWidth <= 0 || pixelHeight <= 0 )
    {
        babylon::log::warning("ui: rejected a viewport of {}x{}", pixelWidth, pixelHeight);
        return false;
    }
    m_state->viewportWidth = pixelWidth;
    m_state->viewportHeight = pixelHeight;
    return true;
}

Size Ui::canvasSize() const
{
    return Size{ m_state->canvasWidth(), detail::kCanvasHeight };
}

void Ui::update(double seconds)
{
    if ( !(seconds > 0.0) || !std::isfinite(seconds) )
    {
        return;
    }
    m_state->advanceAxis(m_state->horizontalAxis, true, seconds);
    m_state->advanceAxis(m_state->verticalAxis, false, seconds);
}

ScreenId Ui::push(Screen screen)
{
    if ( !m_state->validateScreen(screen) )
    {
        return kInvalidScreen;
    }
    detail::ScreenState state;
    state.id = m_state->nextScreenId++;
    if ( m_state->nextScreenId == kInvalidScreen )
    {
        m_state->nextScreenId = 1;
    }
    state.screen = std::move(screen);
    if ( !state.screen.tabs.empty() )
    {
        state.screen.activeTab = std::min(state.screen.activeTab,
            state.screen.tabs.size() - 1);
    }
    else
    {
        state.screen.activeTab = 0;
    }
    m_state->initLocal(state);
    m_state->resetFocus(state);
    m_state->screens.push_back(std::move(state));
    m_state->syncTextInput();
    return m_state->screens.back().id;
}

bool Ui::setWidget(ScreenId screenId, std::size_t index, Widget widget)
{
    detail::ScreenState* state = m_state->findScreen(screenId);
    if ( state == nullptr || index >= state->screen.widgets.size() )
    {
        babylon::log::warning("ui: setWidget on an unknown screen or index");
        return false;
    }
    bool valid = true;
    m_state->validateOrLog(widget, valid);
    if ( !valid )
    {
        return false;
    }

    Widget& old = state->screen.widgets[index];
    const bool sameKind = old.index() == widget.index();
    const bool oldUnbound = detail::cvarName(old).empty();
    const bool newUnbound = detail::cvarName(widget).empty();
    std::optional<WidgetValue> local = index < state->local.size()
        ? state->local[index] : std::nullopt;
    std::size_t oldListSelection = 0;
    if ( const List* list = detail::get<List>(old) )
    {
        oldListSelection = list->selected;
    }

    old = std::move(widget);
    if ( sameKind && oldUnbound && newUnbound )
    {
        if ( List* list = detail::get<List>(old) )
        {
            if ( !list->items.empty() )
            {
                list->selected = std::min(oldListSelection, list->items.size() - 1);
            }
        }
        else if ( detail::valueKind(old) )
        {
            state->local[index] = local;
        }
    }
    else
    {
        if ( detail::valueKind(old) )
        {
            if ( const TextEntry* entry = detail::get<TextEntry>(old) )
            {
                std::string text = entry->defaultText;
                if ( !entry->cvar.empty() )
                {
                    const std::optional<std::string> value =
                        m_state->cvars.getString(entry->cvar);
                    if ( value.has_value() && !value->empty() )
                    {
                        text = *value;
                    }
                }
                state->local[index] = text;
            }
            else if ( detail::cvarName(old).empty() )
            {
                state->local[index] = detail::defaultValueOf(old);
            }
        }
        else
        {
            state->local[index] = std::nullopt;
        }
        if ( List* list = detail::get<List>(old) )
        {
            if ( !list->items.empty() )
            {
                list->selected = std::min(list->selected, list->items.size() - 1);
            }
            state->listOffsets[index] = 0;
        }
    }

    if ( state->focus == index && !m_state->isFocusable(*state, index) )
    {
        state->focus = m_state->nextFocusable(*state, index, 1);
    }
    if ( state->focus != kNoWidget && state->focus < state->screen.widgets.size()
        && detail::get<TextEntry>(state->screen.widgets[state->focus]) != nullptr )
    {
        state->editStartText = m_state->displayText(*state, state->focus);
        state->editCursor = state->editStartText.size();
    }
    m_state->ensureScroll(*state);
    m_state->ensureFocusVisible(*state);
    m_state->syncTextInput();
    return true;
}

ScreenId Ui::replace(Screen screen, ResultCode result)
{
    if ( !m_state->validateScreen(screen) )
    {
        return kInvalidScreen;
    }
    if ( !m_state->screens.empty() )
    {
        pop(result);
    }
    return push(std::move(screen));
}

bool Ui::pop(ResultCode result)
{
    detail::ScreenState* state = m_state->top();
    if ( state == nullptr )
    {
        return false;
    }
    const ScreenId id = state->id;
    m_state->screens.pop_back();
    m_state->events.emplace_back(ScreenClosed{ id, result });
    m_state->syncTextInput();
    return true;
}

bool Ui::isOpen(ScreenId screen) const
{
    return m_state->findScreen(screen) != nullptr;
}

ScreenId Ui::topScreen() const
{
    const detail::ScreenState* state = m_state->top();
    return state == nullptr ? kInvalidScreen : state->id;
}

std::size_t Ui::screenCount() const
{
    return m_state->screens.size();
}

void Ui::handleEvent(const platform::Event& event)
{
    // The callback may run a nested screen, whose event pump can replace the caller's event
    // storage. Keep a copy so the event stays valid for the callback and the state tracking
    // after it returns.
    const platform::Event savedEvent = event;

    if ( detail::ScreenState* state = m_state->top();
        state != nullptr && state->screen.event )
    {
        float pixelX = m_state->mouseX;
        float pixelY = m_state->mouseY;
        if ( const platform::MouseMoved* pMoved =
                std::get_if<platform::MouseMoved>(&savedEvent) )
        {
            pixelX = pMoved->x;
            pixelY = pMoved->y;
        }

        const Point pointer = m_state->canvasPoint(pixelX, pixelY);
        if ( state->screen.event(*this, savedEvent, pointer, m_state->viewportHeight > 0) )
        {
            m_state->trackConsumedEvent(savedEvent);
            m_state->syncTextInput();
            return;
        }
    }

    m_state->handleEvent(savedEvent);
    m_state->syncTextInput();
}

void Ui::draw()
{
    m_state->draw();
}

std::optional<UiEvent> Ui::pollEvent()
{
    if ( m_state->events.empty() )
    {
        return std::nullopt;
    }
    UiEvent event = std::move(m_state->events.front());
    m_state->events.pop_front();
    return event;
}

std::size_t Ui::focusedWidget() const
{
    const detail::ScreenState* state = m_state->top();
    return state == nullptr ? kNoWidget : state->focus;
}

bool Ui::focusedWidgetChangesValue() const
{
    const detail::ScreenState* state = m_state->top();
    if ( state == nullptr || state->focus >= state->screen.widgets.size() )
    {
        return false;
    }
    const Widget& widget = state->screen.widgets[state->focus];
    return detail::get<Choice>(widget) != nullptr || detail::get<Slider>(widget) != nullptr
        || detail::get<Toggle>(widget) != nullptr;
}

void Ui::setPrompts(std::vector<Prompt> prompts)
{
    detail::ScreenState* state = m_state->top();
    if ( state == nullptr )
    {
        return;
    }
    state->prompts = std::move(prompts);
    // The row area may shrink or grow when the bar appears, so keep the scroll and focus valid
    // for the new layout.
    m_state->ensureScroll(*state);
    m_state->ensureFocusVisible(*state);
}

const std::vector<Prompt>& Ui::prompts() const
{
    static const std::vector<Prompt> empty;
    const detail::ScreenState* state = m_state->top();
    return state == nullptr ? empty : state->prompts;
}

std::optional<WidgetValue> Ui::widgetValue(ScreenId screenId, std::size_t index) const
{
    const detail::ScreenState* state = m_state->findScreen(screenId);
    if ( state == nullptr || index >= state->screen.widgets.size() )
    {
        return std::nullopt;
    }
    const Widget& widget = state->screen.widgets[index];
    if ( detail::get<Toggle>(widget) != nullptr )
    {
        return m_state->currentToggle(*state, index);
    }
    if ( detail::get<Slider>(widget) != nullptr )
    {
        return m_state->currentSlider(*state, index);
    }
    if ( const Choice* choice = detail::get<Choice>(widget) )
    {
        return static_cast<std::int64_t>(m_state->currentChoiceIndex(*choice, *state, index));
    }
    if ( detail::get<TextEntry>(widget) != nullptr )
    {
        return m_state->displayText(*state, index);
    }
    return std::nullopt;
}

}
