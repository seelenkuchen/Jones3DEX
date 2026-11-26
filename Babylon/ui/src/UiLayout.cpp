// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Layout, text measuring and scrolling for Ui::State.

#include "UiState.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::ui
{

namespace
{
    constexpr float kTabGap = 18.0f;
    constexpr float kTabUnderlineHeight = 2.0f;

    float clampFloat(float value, float low, float high)
    {
        return std::max(low, std::min(value, high));
    }
}

    float Ui::State::canvasWidth() const
    {
        if ( viewportHeight <= 0 )
        {
            return 0.0f;
        }
        return detail::kCanvasHeight * static_cast<float>(viewportWidth)
            / static_cast<float>(viewportHeight);
    }

    // True when the prompt bar is actually drawn this frame: it needs entries and a positive
    // prompt height. Only then does the row area make room for it.
    bool Ui::State::promptsVisible(const detail::ScreenState& state) const
    {
        return !state.prompts.empty() && skin.promptHeight > 0.0f;
    }

    // The lowest canvas y the panel and rows may reach: the canvas margin, minus the prompt bar
    // when one is drawn.
    float Ui::State::lowerLimit(const detail::ScreenState& state) const
    {
        return detail::kCanvasHeight - skin.canvasMargin
            - (promptsVisible(state) ? skin.promptHeight : 0.0f);
    }

    Point Ui::State::canvasPoint(float pixelX, float pixelY) const
    {
        if ( viewportHeight <= 0 )
        {
            return {};
        }
        const float scale = detail::kCanvasHeight / static_cast<float>(viewportHeight);
        return Point{ pixelX * scale, pixelY * scale };
    }

    float Ui::State::textWidth(std::string_view text) const
    {
        return painter.measureText(skin.textFont, text).width;
    }

    float Ui::State::titleWidth(std::string_view text) const
    {
        return painter.measureText(skin.titleFont, text).width;
    }

    // True when the skin reserves a help column and the screen draws into it.
    // A shorter help vector or empty entries simply say nothing for those
    // rows. A Top panel always reserves the column, with or without text, so
    // its rows and the values in them keep the same width on every screen.
    bool Ui::State::hasHelp(const detail::ScreenState& state) const
    {
        if ( !(skin.helpColumnWidth > 0.0f) )
        {
            return false;
        }
        if ( skin.placement == PanelPlacement::Top )
        {
            return true;
        }
        for ( const std::string& text : state.screen.help )
        {
            if ( !text.empty() )
            {
                return true;
            }
        }
        return false;
    }

    float Ui::State::tabHeight() const
    {
        const float lineHeight = painter.measureText(skin.titleFont, "").height;
        return std::max(skin.rowHeight, lineHeight);
    }

    // Splits text into lines that fit width, at spaces, in the given font.
    // A single word wider than the column is kept whole and is clipped by
    // the painter when drawn.
    std::vector<std::string> Ui::State::wrapText(FontId font, std::string_view text, float width) const
    {
        std::vector<std::string> lines;
        if ( text.empty() )
        {
            return lines;
        }

        std::string line;
        std::size_t pos = 0;
        while ( pos < text.size() )
        {
            while ( pos < text.size() && text[pos] == ' ' )
            {
                ++pos;
            }
            if ( pos >= text.size() )
            {
                break;
            }

            const std::size_t start = pos;
            while ( pos < text.size() && text[pos] != ' ' )
            {
                ++pos;
            }
            const std::string_view word = text.substr(start, pos - start);
            std::string candidate = line.empty()
                ? std::string(word) : line + " " + std::string(word);
            if ( line.empty()
                || painter.measureText(font, candidate).width <= width )
            {
                line = std::move(candidate);
            }
            else
            {
                lines.push_back(std::move(line));
                line.assign(word);
            }
        }

        if ( !line.empty() )
        {
            lines.push_back(std::move(line));
        }
        return lines;
    }

    void Ui::State::selectTab(detail::ScreenState& state, std::size_t index)
    {
        const std::size_t count = state.screen.tabs.size();
        if ( count == 0 )
        {
            return;
        }
        const std::size_t tab = std::min(index, count - 1);
        if ( tab == state.screen.activeTab )
        {
            return;
        }
        state.screen.activeTab = tab;
        events.emplace_back(TabChanged{ state.id, tab });
    }

    void Ui::State::switchTab(detail::ScreenState& state, int direction)
    {
        const std::size_t count = state.screen.tabs.size();
        if ( count == 0 )
        {
            return;
        }
        const std::size_t active = std::min(state.screen.activeTab, count - 1);
        const long delta = direction > 0 ? 1L : -1L;
        const long next = ((long)active + delta + (long)count) % (long)count;
        selectTab(state, (std::size_t)next);
    }

    float Ui::State::widgetWidth(const Screen& screen, std::size_t index) const
    {
        const Widget& widget = screen.widgets[index];
        if ( const Label* label = detail::get<Label>(widget) )
        {
            const float value = label->value.empty() ? 0.0f : textWidth(label->value);
            return textWidth(label->text) + (label->value.empty() ? 0.0f : detail::kColumnGap + value);
        }
        if ( const Image* image = detail::get<Image>(widget) )
        {
            return image->size.width;
        }
        if ( const Button* button = detail::get<Button>(widget) )
        {
            return textWidth(button->text);
        }
        if ( const Toggle* toggle = detail::get<Toggle>(widget) )
        {
            return textWidth(toggle->text) + detail::kColumnGap
                + std::max(textWidth("On"), textWidth("Off"));
        }
        if ( const Slider* slider = detail::get<Slider>(widget) )
        {
            return textWidth(slider->text) + detail::kColumnGap
                + std::max(textWidth(sliderText(*slider, slider->min)),
                    textWidth(sliderText(*slider, slider->max)));
        }
        if ( const Choice* choice = detail::get<Choice>(widget) )
        {
            float options = 0.0f;
            for ( const std::string& option : choice->options )
            {
                options = std::max(options, textWidth(option));
            }
            return textWidth(choice->text) + detail::kColumnGap + options;
        }
        if ( const List* list = detail::get<List>(widget) )
        {
            float width = textWidth(list->text);
            const bool images = std::any_of(list->items.begin(), list->items.end(),
                [](const ListItem& item) { return item.image != 0; });
            for ( const ListItem& item : list->items )
            {
                width = std::max(width,
                    textWidth(item.text) + (images ? skin.rowHeight + detail::kColumnGap : 0.0f));
            }
            return width;
        }
        if ( const BindingCapture* capture = detail::get<BindingCapture>(widget) )
        {
            return textWidth(capture->text) + detail::kColumnGap
                + std::max(textWidth("Press input"), textWidth("Press a key"));
        }
        if ( const TextEntry* entry = detail::get<TextEntry>(widget) )
        {
            return textWidth(entry->text) + detail::kColumnGap + textWidth(entry->defaultText);
        }
        return 0.0f;
    }

    float Ui::State::widgetHeight(const Widget& widget) const
    {
        if ( const Image* image = detail::get<Image>(widget) )
        {
            return image->size.width > 0.0f && image->size.height > 0.0f
                ? image->size.height : 0.0f;
        }
        if ( const List* list = detail::get<List>(widget) )
        {
            const std::size_t visible = std::min(list->items.size(), detail::kListVisibleRows);
            return (list->text.empty() ? 0.0f : skin.rowHeight)
                + static_cast<float>(visible) * skin.rowHeight;
        }
        return skin.rowHeight;
    }

    detail::Layout Ui::State::computeLayout(const detail::ScreenState& state) const
    {
        detail::Layout layout;
        const float canvas = canvasWidth();
        if ( canvas <= 0.0f )
        {
            return layout;
        }

        const bool bTabs = !state.screen.tabs.empty();
        const bool bHelp = hasHelp(state);
        const float tabsHeight = bTabs ? tabHeight() : 0.0f;

        const std::size_t count = state.screen.widgets.size();
        std::vector<float> heights(count, 0.0f);
        float contentHeight = skin.titleHeight + tabsHeight;
        float widest = titleWidth(state.screen.title);
        for ( std::size_t i = 0; i < count; ++i )
        {
            heights[i] = widgetHeight(state.screen.widgets[i]);
            widest = std::max(widest, widgetWidth(state.screen, i));
            if ( heights[i] > 0.0f )
            {
                contentHeight += skin.rowSpacing + heights[i];
            }
        }

        // The tab strip is part of the panel width: every label must fit.
        if ( bTabs )
        {
            float tabsWidth = 0.0f;
            for ( std::size_t i = 0; i < state.screen.tabs.size(); ++i )
            {
                tabsWidth += titleWidth(state.screen.tabs[i]);
                if ( i + 1 < state.screen.tabs.size() )
                {
                    tabsWidth += kTabGap;
                }
            }
            widest = std::max(widest, tabsWidth);
        }

        // The focus marker occupies a fixed column on the left of every row,
        // so text keeps its place while focus moves.
        if ( !skin.focusMarker.empty() )
        {
            layout.markerWidth = painter.measureText(skin.textFont, skin.focusMarker).width;
            layout.textInset   = layout.markerWidth + skin.markerSpacing;
        }

        // With a help column the rows get only the rest of the panel, so the
        // panel grows until they still fit at their natural width. The ratio
        // is clamped so the rows can never vanish.
        float rowsNeeded = widest + layout.textInset;
        float helpRatio = 0.0f;
        if ( bHelp )
        {
            helpRatio = std::min(skin.helpColumnWidth, 0.9f);
            rowsNeeded /= 1.0f - helpRatio;
        }

        float panelWidth = std::max(skin.minPanelWidth,
            rowsNeeded + 2.0f * skin.panelPadding);
        float panelHeight = contentHeight + 2.0f * skin.panelPadding;
        const float bottomLimit = lowerLimit(state);
        if ( skin.placement == PanelPlacement::Top )
        {
            // A fixed width keeps the title, the rows and their values at the
            // same place on every screen of a set, whatever the content. The
            // panel starts at topMargin and only grows downward; the row clip
            // then ends at the prompt bar.
            const float availableWidth = std::max(0.0f,
                canvas - 2.0f * skin.canvasMargin);
            if ( skin.fixedPanelWidth > 0.0f )
            {
                panelWidth = std::min(canvas * skin.fixedPanelWidth, availableWidth);
            }
            else
            {
                panelWidth = std::min(panelWidth, availableWidth);
            }
            const float top = std::min(skin.topMargin, bottomLimit);
            const float availableHeight = std::max(0.0f, bottomLimit - top);
            panelHeight = std::min(panelHeight, availableHeight);
            layout.panel = Rect{ (canvas - panelWidth) * 0.5f, top,
                panelWidth, panelHeight };
        }
        else if ( skin.placement == PanelPlacement::BottomLeft )
        {
            const float left   = skin.canvasMargin * 4.0f;
            const float bottom = skin.canvasMargin * 4.0f;
            const float availableWidth = std::max(0.0f, canvas - left - skin.canvasMargin);
            const float panelBottom = promptsVisible(state)
                ? std::min(detail::kCanvasHeight - bottom, bottomLimit)
                : detail::kCanvasHeight - bottom;
            const float availableHeight = promptsVisible(state)
                ? std::max(0.0f, panelBottom - skin.canvasMargin)
                : std::max(0.0f, detail::kCanvasHeight - bottom);
            panelWidth = std::min(panelWidth, availableWidth);
            panelHeight = std::min(panelHeight, availableHeight);
            layout.panel = Rect{ left, panelBottom - panelHeight,
                panelWidth, panelHeight };
        }
        else
        {
            const float availableWidth = std::max(0.0f, canvas - 2.0f * skin.canvasMargin);
            const float availableHeight = std::max(0.0f,
                bottomLimit - skin.canvasMargin);
            panelWidth = std::min(panelWidth, availableWidth);
            panelHeight = std::min(panelHeight, availableHeight);
            layout.panel = Rect{ (canvas - panelWidth) * 0.5f,
                skin.canvasMargin + (availableHeight - panelHeight) * 0.5f,
                panelWidth, panelHeight };
        }
        layout.inner = Rect{ layout.panel.x + skin.panelPadding,
            layout.panel.y + skin.panelPadding,
            std::max(0.0f, layout.panel.width - 2.0f * skin.panelPadding),
            std::max(0.0f, layout.panel.height - 2.0f * skin.panelPadding) };
        layout.title = Rect{ layout.inner.x, layout.inner.y, layout.inner.width,
            std::min(skin.titleHeight, layout.inner.height) };

        // The tab strip sits directly below the title, its labels in a row and
        // an underline under the active one.
        float rowsY = layout.inner.y + skin.titleHeight + tabsHeight;
        if ( bTabs )
        {
            float x = layout.inner.x;
            const float y = layout.inner.y + skin.titleHeight;
            for ( std::size_t i = 0; i < state.screen.tabs.size(); ++i )
            {
                const float width = titleWidth(state.screen.tabs[i]);
                detail::LayoutTab tab;
                tab.index = i;
                tab.rect  = Rect{ x, y, width, tabsHeight };
                tab.underline = Rect{ x, y + tabsHeight - kTabUnderlineHeight,
                    width, kTabUnderlineHeight };
                layout.tabs.push_back(tab);
                x += width + kTabGap;
            }
        }

        // A Top panel's rows run from below the title to the prompt bar; the panel's bottom
        // padding is not a clip edge. The other placements clip at the panel's inner bottom.
        const float rowsBottom = skin.placement == PanelPlacement::Top
            ? bottomLimit
            : std::min(layout.inner.y + layout.inner.height, bottomLimit);
        const float rowsHeight = std::max(0.0f, rowsBottom - rowsY);
        float rowsWidth = layout.inner.width;
        if ( bHelp )
        {
            const float helpWidth = std::max(0.0f,
                layout.inner.width * helpRatio - detail::kColumnGap * 0.5f);
            const float width = std::min(helpWidth, layout.inner.width);
            layout.helpColumn = Rect{ layout.inner.x + layout.inner.width - width,
                rowsY, width, rowsHeight };
            rowsWidth = std::max(0.0f, layout.inner.width - width - detail::kColumnGap);
        }
        layout.rowsClip = Rect{ layout.inner.x, rowsY, rowsWidth, rowsHeight };

        float y = rowsY;
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( heights[i] <= 0.0f )
            {
                continue;
            }
            y += skin.rowSpacing;
            layout.rows.push_back(detail::LayoutRow{ i,
                Rect{ layout.inner.x, y, rowsWidth, heights[i] }, heights[i],
                layout.textInset, layout.markerWidth });
            y += heights[i];
        }
        layout.contentHeight = contentHeight;
        // A Top panel starts at topMargin, so its last row reaches the prompt bar one bottom
        // padding earlier than a panel whose clip ends at its inner bottom.
        const float bottomPadding = skin.placement == PanelPlacement::Top
            ? skin.panelPadding : 2.0f * skin.panelPadding;
        layout.maxScroll = std::max(0.0f,
            contentHeight + bottomPadding - panelHeight);
        return layout;
    }

    float Ui::State::effectiveScroll(const detail::ScreenState& state, const detail::Layout& layout) const
    {
        return clampFloat(state.scroll, 0.0f, layout.maxScroll);
    }

    void Ui::State::ensureScroll(detail::ScreenState& state)
    {
        const detail::Layout layout = computeLayout(state);
        state.scroll = effectiveScroll(state, layout);
    }

    Rect Ui::State::listHeadingRect(const detail::ScreenState& state, const detail::LayoutRow& row) const
    {
        const List* list = detail::get<List>(state.screen.widgets[row.widget]);
        if ( list == nullptr || list->text.empty() )
        {
            return Rect{};
        }
        return Rect{ row.rect.x, row.rect.y, row.rect.width, skin.rowHeight };
    }

    Rect Ui::State::listItemRect(const detail::ScreenState& state, const detail::LayoutRow& row,
        std::size_t item) const
    {
        const List* list = detail::get<List>(state.screen.widgets[row.widget]);
        if ( list == nullptr || item >= list->items.size() )
        {
            return Rect{};
        }
        const float heading = list->text.empty() ? 0.0f : skin.rowHeight;
        const std::size_t offset = std::min(state.listOffsets[row.widget],
            list->items.size());
        if ( item < offset || item >= offset + detail::kListVisibleRows )
        {
            return Rect{};
        }
        return Rect{ row.rect.x, row.rect.y + heading
            + static_cast<float>(item - offset) * skin.rowHeight,
            row.rect.width, skin.rowHeight };
    }

    std::size_t Ui::State::visibleListRows() const
    {
        return detail::kListVisibleRows;
    }

    void Ui::State::ensureListVisible(detail::ScreenState& state, std::size_t widgetIndex)
    {
        List* list = detail::get<List>(state.screen.widgets[widgetIndex]);
        if ( list == nullptr )
        {
            return;
        }
        if ( list->items.empty() )
        {
            state.listOffsets[widgetIndex] = 0;
            return;
        }
        list->selected = std::min(list->selected, list->items.size() - 1);
        std::size_t& offset = state.listOffsets[widgetIndex];
        if ( list->selected < offset )
        {
            offset = list->selected;
        }
        if ( list->selected >= offset + detail::kListVisibleRows )
        {
            offset = list->selected - detail::kListVisibleRows + 1;
        }
    }

    // True when the whole row lies inside the visible row area at this scroll. Drawing and
    // pointer input use the same rule, so a partly hidden row is neither drawn nor clickable.
    bool Ui::State::rowVisible(const detail::Layout& layout, const detail::LayoutRow& row, float scroll) const
    {
        const float top = row.rect.y - scroll;
        return top >= layout.rowsClip.y - 0.01f
            && top + row.height <= layout.rowsClip.y + layout.rowsClip.height + 0.01f;
    }

    void Ui::State::ensureFocusVisible(detail::ScreenState& state)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        const detail::Layout layout = computeLayout(state);
        if ( layout.rows.empty() || layout.rowsClip.height <= 0.0f )
        {
            return;
        }
        if ( layout.maxScroll <= 0.0f )
        {
            state.scroll = 0.0f;
            return;
        }

        std::size_t focusIndex = kNoWidget;
        for ( std::size_t index = 0; index < layout.rows.size(); ++index )
        {
            if ( layout.rows[index].widget == state.focus )
            {
                focusIndex = index;
                break;
            }
        }
        if ( focusIndex == kNoWidget )
        {
            return;
        }

        // Keep the scroll on a row boundary: the first visible row's top sits at the clip top.
        const float scroll = effectiveScroll(state, layout);
        const float clipTop = layout.rowsClip.y;
        std::size_t first = 0;
        for ( std::size_t index = 0; index < layout.rows.size(); ++index )
        {
            if ( layout.rows[index].rect.y - scroll >= clipTop - 0.01f )
            {
                first = index;
                break;
            }
        }

        if ( focusIndex < first )
        {
            first = focusIndex;
        }
        else
        {
            const float focusBottom = layout.rows[focusIndex].rect.y
                + layout.rows[focusIndex].height;
            while ( first < focusIndex
                && focusBottom - layout.rows[first].rect.y
                    > layout.rowsClip.height + 0.01f )
            {
                ++first;
            }
        }

        state.scroll = clampFloat(layout.rows[first].rect.y - clipTop, 0.0f,
            layout.maxScroll);
    }

}
