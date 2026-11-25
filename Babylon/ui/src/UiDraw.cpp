// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Drawing of Ui::State screens, rows, tabs and prompts.

#include "UiState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace babylon::ui
{

namespace
{
    constexpr float kPromptIconGap = 6.0f;
    constexpr float kPromptSpacing = 24.0f;
}

    FontId Ui::State::helpFont() const
    {
        return skin.helpFont != 0 ? skin.helpFont : skin.textFont;
    }

    // Rows draw their text twice when a shadow is set: the shadow offsets down
    // and right, then the real text sits on top.
    void Ui::State::drawRowText(FontId font, std::string_view text, const Rect& rect,
        TextAlign align, const Color& color)
    {
        if ( skin.textShadow.a > 0.0f )
        {
            Rect shadow = rect;
            shadow.x += skin.textShadowOffset;
            shadow.y += skin.textShadowOffset;
            painter.drawText(font, text, shadow, align, skin.textShadow);
        }
        painter.drawText(font, text, rect, align, color);
    }

    // Draws the prompt bar left to right along the bottom left: icon, label,
    // then the next entry. The icons are promptHeight square and the labels
    // sit in a line box of that height, so every entry shares one baseline.
    void Ui::State::drawPrompts(const std::vector<Prompt>& prompts)
    {
        if ( prompts.empty() || !(skin.promptHeight > 0.0f) )
        {
            return;
        }
        const float canvas = canvasWidth();
        if ( canvas <= 0.0f )
        {
            return;
        }

        const FontId font = skin.promptFont != 0 ? skin.promptFont : skin.textFont;
        const float rowTop = detail::kCanvasHeight - skin.canvasMargin - skin.promptHeight;
        float x = skin.canvasMargin;
        for ( const Prompt& prompt : prompts )
        {
            if ( prompt.icon != 0 )
            {
                painter.drawImage(prompt.icon,
                    Rect{ x, rowTop, skin.promptHeight, skin.promptHeight });
                x += skin.promptHeight + kPromptIconGap;
            }
            if ( !prompt.label.empty() )
            {
                const float width = painter.measureText(font, prompt.label).width;
                drawRowText(font, prompt.label,
                    Rect{ x, rowTop, width, skin.promptHeight },
                    TextAlign::Left, skin.text);
                x += width;
            }
            x += kPromptSpacing;
        }
    }

    std::string Ui::State::sliderText(const Slider& slider, double value) const
    {
        if ( !slider.cvar.empty() && cvars.getInt(slider.cvar).has_value() )
        {
            return std::format("{}", static_cast<long long>(std::llround(value)));
        }
        return std::format("{:.2f}", value);
    }

    void Ui::State::drawRow(const detail::ScreenState& state, const detail::LayoutRow& row, float scroll)
    {
        const std::size_t index = row.widget;
        const Widget& widget = state.screen.widgets[index];
        const Rect rect{ row.rect.x, row.rect.y - scroll, row.rect.width, row.rect.height };
        if ( rect.y + rect.height <= stateClipTop || rect.y >= stateClipBottom )
        {
            return;
        }
        const bool disabled = isWidgetDisabled(state, index);
        const bool focused = state.focus == index;

        // The marker sits in the reserved column and the row's own text starts
        // after it, so the text does not move when focus moves.
        Rect textRect = rect;
        textRect.x += row.textInset;
        textRect.width = std::max(0.0f, textRect.width - row.textInset);

        if ( const Image* image = detail::get<Image>(widget) )
        {
            if ( image->image != 0 && image->size.width > 0.0f && image->size.height > 0.0f )
            {
                float x = rect.x;
                if ( image->align == TextAlign::Center )
                {
                    x = rect.x + (rect.width - image->size.width) * 0.5f;
                }
                else if ( image->align == TextAlign::Right )
                {
                    x = rect.x + rect.width - image->size.width;
                }
                painter.drawImage(image->image,
                    Rect{ x, rect.y, image->size.width, image->size.height });
            }
            return;
        }

        if ( const List* list = detail::get<List>(widget) )
        {
            drawList(state, row, scroll, *list, index, disabled, focused);
            return;
        }

        if ( focused && detail::focusableKind(widget) && !skin.focusMarker.empty() )
        {
            drawRowText(skin.textFont, skin.focusMarker,
                Rect{ rect.x, rect.y, row.markerWidth, rect.height },
                TextAlign::Left, skin.textFocused);
        }

        if ( focused && detail::focusableKind(widget) && skin.highlight.a > 0.0f )
        {
            painter.fillRect(rect, skin.highlight);
        }
        const Color color = disabled ? skin.textDisabled
            : (focused ? skin.textFocused : skin.text);

        if ( const Label* label = detail::get<Label>(widget) )
        {
            drawRowText(skin.textFont, label->text, textRect, skin.rowAlign, color);
            if ( !label->value.empty() )
            {
                drawRowText(skin.textFont, label->value, textRect, TextAlign::Right, color);
            }
            return;
        }
        if ( const Button* button = detail::get<Button>(widget) )
        {
            drawRowText(skin.textFont, button->text, textRect, skin.rowAlign, color);
            return;
        }
        if ( const Toggle* toggle = detail::get<Toggle>(widget) )
        {
            drawRowText(skin.textFont, toggle->text, textRect, skin.rowAlign, color);
            drawRowText(skin.textFont, currentToggle(state, index) ? "On" : "Off",
                textRect, TextAlign::Right, color);
            return;
        }
        if ( const Slider* slider = detail::get<Slider>(widget) )
        {
            drawRowText(skin.textFont, slider->text, textRect, skin.rowAlign, color);
            drawRowText(skin.textFont, sliderText(*slider, currentSlider(state, index)),
                textRect, TextAlign::Right, color);
            return;
        }
        if ( const Choice* choice = detail::get<Choice>(widget) )
        {
            drawRowText(skin.textFont, choice->text, textRect, skin.rowAlign, color);
            const std::size_t selected = currentChoiceIndex(*choice, state, index);
            if ( selected < choice->options.size() )
            {
                // The value sits on the right with a left and a right arrow
                // around it. The arrows are in the focused color while the
                // row has focus, so the row still reads as adjustable.
                constexpr std::string_view kLeftArrow  = "\xE2\x97\x80"; // U+25C0
                constexpr std::string_view kRightArrow = "\xE2\x96\xB6"; // U+25B6
                const std::string& option = choice->options[selected];
                const float valueWidth = textWidth(option);
                const float arrowWidth = std::max(textWidth(kLeftArrow), textWidth(kRightArrow));
                const float groupWidth = arrowWidth + detail::kColumnGap + valueWidth + detail::kColumnGap
                    + arrowWidth;
                const float startX = textRect.x + std::max(0.0f, textRect.width - groupWidth);
                const float arrowY = rect.y;
                const Color arrowColor = focused ? skin.textFocused : color;
                drawRowText(skin.textFont, kLeftArrow,
                    Rect{ startX, arrowY, arrowWidth, rect.height },
                    TextAlign::Left, arrowColor);
                drawRowText(skin.textFont, option,
                    Rect{ startX + arrowWidth + detail::kColumnGap, arrowY, valueWidth, rect.height },
                    TextAlign::Left, color);
                drawRowText(skin.textFont, kRightArrow,
                    Rect{ startX + arrowWidth + detail::kColumnGap + valueWidth + detail::kColumnGap,
                        arrowY, arrowWidth, rect.height },
                    TextAlign::Left, arrowColor);
            }
            return;
        }
        if ( const BindingCapture* capture = detail::get<BindingCapture>(widget) )
        {
            drawRowText(skin.textFont, capture->text, textRect, skin.rowAlign, color);
            if ( state.capturing && state.captureWidget == index )
            {
                drawRowText(skin.textFont, "Press input", textRect, TextAlign::Right,
                    color);
            }
            return;
        }
        if ( const TextEntry* entry = detail::get<TextEntry>(widget) )
        {
            drawRowText(skin.textFont, entry->text, textRect, skin.rowAlign, color);
            drawRowText(skin.textFont, displayText(state, index), textRect,
                TextAlign::Right, color);
        }
    }

    // Small up/down markers at the right edge of the row area when rows are hidden above or
    // below the scroll window.
    void Ui::State::drawScrollMarkers(const detail::Layout& layout, float scroll)
    {
        if ( layout.rows.empty() || layout.rowsClip.height <= 0.0f )
        {
            return;
        }

        const float clipTop = layout.rowsClip.y;
        const float clipBottom = layout.rowsClip.y + layout.rowsClip.height;
        const bool hiddenAbove = scroll > 0.01f;
        const bool hiddenBelow = layout.rows.back().rect.y + layout.rows.back().height
            - scroll > clipBottom + 0.01f;
        if ( !hiddenAbove && !hiddenBelow )
        {
            return;
        }

        constexpr std::string_view kUpMarker   = "\xE2\x96\xB2"; // U+25B2
        constexpr std::string_view kDownMarker = "\xE2\x96\xBC"; // U+25BC
        const FontId font = skin.textFont;
        float height = painter.measureText(font, "").height;
        if ( !(height > 0.0f) )
        {
            height = skin.rowHeight;
        }
        height = std::min(height, layout.rowsClip.height);

        const float width = std::max(painter.measureText(font, kUpMarker).width,
            painter.measureText(font, kDownMarker).width);
        const float x = layout.rowsClip.x + layout.rowsClip.width - width;
        if ( hiddenAbove )
        {
            drawRowText(font, kUpMarker,
                Rect{ x, clipTop, width, height }, TextAlign::Left, skin.text);
        }
        if ( hiddenBelow )
        {
            drawRowText(font, kDownMarker,
                Rect{ x, clipBottom - height, width, height }, TextAlign::Left, skin.text);
        }
    }

    // The right-hand help column: only the focused row's text, wrapped at
    // spaces, in the help font. The column is clipped to its own rectangle
    // so a long word cannot run into the rows.
    void Ui::State::drawHelp(const detail::ScreenState& state, const detail::Layout& layout)
    {
        if ( layout.helpColumn.width <= 0.0f || layout.helpColumn.height <= 0.0f
            || state.focus == kNoWidget || state.focus >= state.screen.help.size() )
        {
            return;
        }
        const std::string& text = state.screen.help[state.focus];
        if ( text.empty() )
        {
            return;
        }

        const FontId font = helpFont();
        const std::vector<std::string> lines = wrapText(font, text,
            layout.helpColumn.width);
        if ( lines.empty() )
        {
            return;
        }

        float lineHeight = painter.measureText(font, "").height;
        if ( !(lineHeight > 0.0f) )
        {
            lineHeight = skin.rowHeight;
        }

        painter.pushClip(layout.helpColumn);
        float y = layout.helpColumn.y;
        for ( const std::string& line : lines )
        {
            if ( y + lineHeight > layout.helpColumn.y + layout.helpColumn.height )
            {
                break;
            }
            drawRowText(font, line,
                Rect{ layout.helpColumn.x, y, layout.helpColumn.width, lineHeight },
                TextAlign::Left, skin.text);
            y += lineHeight;
        }
        painter.popClip();
    }

    void Ui::State::drawList(const detail::ScreenState& state, const detail::LayoutRow& row, float scroll,
        const List& list, std::size_t index, bool disabled, bool focused)
    {
        const Rect rect{ row.rect.x, row.rect.y - scroll, row.rect.width, row.rect.height };
        Rect textRect = rect;
        textRect.x += row.textInset;
        textRect.width = std::max(0.0f, textRect.width - row.textInset);
        const Color color = disabled ? skin.textDisabled
            : (focused ? skin.textFocused : skin.text);
        float y = rect.y;
        if ( !list.text.empty() )
        {
            drawRowText(skin.textFont, list.text,
                Rect{ textRect.x, y, textRect.width, skin.rowHeight },
                skin.rowAlign, color);
            y += skin.rowHeight;
        }
        const std::size_t offset = std::min(state.listOffsets[index], list.items.size());
        const bool images = std::any_of(list.items.begin(), list.items.end(),
            [](const ListItem& item) { return item.image != 0; });
        const std::size_t visible = std::min(detail::kListVisibleRows,
            list.items.size() - std::min(offset, list.items.size()));

        // A focused list marks its selected row with the focus marker; the other rows keep the
        // normal text colour, so a skin without a highlight fill still shows the selection.
        if ( focused && !skin.focusMarker.empty() )
        {
            float markerY = rect.y;
            if ( list.selected >= offset && list.selected < offset + visible )
            {
                markerY = y + static_cast<float>(list.selected - offset) * skin.rowHeight;
            }
            drawRowText(skin.textFont, skin.focusMarker,
                Rect{ rect.x, markerY, row.markerWidth, skin.rowHeight },
                TextAlign::Left, skin.textFocused);
        }

        const Color& itemColor = disabled ? skin.textDisabled : skin.text;
        for ( std::size_t i = 0; i < visible; ++i )
        {
            const std::size_t itemIndex = offset + i;
            const ListItem& item = list.items[itemIndex];
            const Rect itemRect{ rect.x, y + static_cast<float>(i) * skin.rowHeight,
                rect.width, skin.rowHeight };
            const Color& selection = focused ? skin.highlight : skin.highlightUnfocused;
            if ( itemIndex == list.selected && selection.a > 0.0f )
            {
                painter.fillRect(itemRect, selection);
            }
            float textX = textRect.x;
            float textWidth = textRect.width;
            if ( images )
            {
                if ( item.image != 0 )
                {
                    painter.drawImage(item.image,
                        Rect{ itemRect.x, itemRect.y, skin.rowHeight, skin.rowHeight });
                }
                textX += skin.rowHeight + detail::kColumnGap;
                textWidth -= skin.rowHeight + detail::kColumnGap;
            }
            drawRowText(skin.textFont, item.text,
                Rect{ textX, itemRect.y, std::max(0.0f, textWidth), itemRect.height },
                TextAlign::Left,
                itemIndex == list.selected && !disabled ? skin.textFocused : itemColor);
        }
    }

    void Ui::State::draw()
    {
        const detail::ScreenState* state = top();
        if ( state == nullptr )
        {
            return;
        }
        const float canvas = canvasWidth();
        if ( canvas <= 0.0f )
        {
            return;
        }
        if ( state->screen.draw )
        {
            painter.pushClip(Rect{ 0.0f, 0.0f, canvas, detail::kCanvasHeight });
            state->screen.draw(owner, painter, Size{ canvas, detail::kCanvasHeight });
            drawPrompts(state->prompts);
            painter.popClip();
            return;
        }
        const detail::Layout layout = computeLayout(*state);
        if ( layout.panel.width <= 0.0f || layout.panel.height <= 0.0f )
        {
            return;
        }
        const float scroll = effectiveScroll(*state, layout);

        painter.pushClip(Rect{ 0.0f, 0.0f, canvas, detail::kCanvasHeight });
        if ( skin.panel.a > 0.0f )
        {
            painter.fillRect(layout.panel, skin.panel);
        }
        if ( skin.panelImage != 0 )
        {
            painter.drawImage(skin.panelImage, layout.panel);
        }
        if ( skin.borderWidth > 0.0f && skin.panelBorder.a > 0.0f )
        {
            const float width = std::min(skin.borderWidth,
                std::min(layout.panel.width, layout.panel.height) * 0.5f);
            painter.fillRect(Rect{ layout.panel.x, layout.panel.y,
                layout.panel.width, width }, skin.panelBorder);
            painter.fillRect(Rect{ layout.panel.x,
                layout.panel.y + layout.panel.height - width,
                layout.panel.width, width }, skin.panelBorder);
            painter.fillRect(Rect{ layout.panel.x, layout.panel.y,
                width, layout.panel.height }, skin.panelBorder);
            painter.fillRect(Rect{ layout.panel.x + layout.panel.width - width,
                layout.panel.y, width, layout.panel.height }, skin.panelBorder);
        }
        if ( !state->screen.title.empty() )
        {
            painter.drawText(skin.titleFont, state->screen.title,
                layout.title, TextAlign::Center, skin.title);
        }
        for ( const detail::LayoutTab& tab : layout.tabs )
        {
            const bool active = tab.index == state->screen.activeTab;
            painter.drawText(skin.titleFont, state->screen.tabs[tab.index],
                tab.rect, TextAlign::Left, active ? skin.textFocused : skin.text);
            if ( active )
            {
                painter.fillRect(tab.underline, skin.textFocused);
            }
        }
        painter.pushClip(layout.rowsClip);
        stateClipTop = layout.rowsClip.y;
        stateClipBottom = layout.rowsClip.y + layout.rowsClip.height;
        for ( const detail::LayoutRow& row : layout.rows )
        {
            if ( rowVisible(layout, row, scroll) )
            {
                drawRow(*state, row, scroll);
            }
        }
        drawScrollMarkers(layout, scroll);
        stateClipTop = -std::numeric_limits<float>::infinity();
        stateClipBottom = std::numeric_limits<float>::infinity();
        painter.popClip();
        drawHelp(*state, layout);
        drawPrompts(state->prompts);
        painter.popClip();
    }

}
