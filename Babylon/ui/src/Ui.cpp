// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/ui/Ui.h>

#include <babylon/log/Log.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace babylon::ui
{

namespace
{
    constexpr float kCanvasHeight = 480.0f;
    constexpr float kColumnGap = 8.0f;

    // The magnitude a gamepad axis must reach to be reported by a binding capture. It is a fixed
    // rule, independent of NavigationMap::axisThreshold: capture accepts every gamepad axis, not
    // only the two the navigation map uses.
    constexpr float kCaptureAxisThreshold = 0.5f;
    constexpr std::size_t kListVisibleRows = 5;
    constexpr double kAxisInitialDelay = 0.35;
    constexpr double kAxisRepeatRate = 0.10;

    template <class T>
    const T* get(const Widget& widget)
    {
        return std::get_if<T>(&widget);
    }

    template <class T>
    T* get(Widget& widget)
    {
        return std::get_if<T>(&widget);
    }

    bool focusableKind(const Widget& widget)
    {
        return std::visit([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            return std::is_same_v<T, Button> || std::is_same_v<T, Toggle>
                || std::is_same_v<T, Slider> || std::is_same_v<T, Choice>
                || std::is_same_v<T, List> || std::is_same_v<T, BindingCapture>
                || std::is_same_v<T, TextEntry>;
        }, widget);
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

    bool valueKind(const Widget& widget)
    {
        return get<Toggle>(widget) != nullptr || get<Slider>(widget) != nullptr
            || get<Choice>(widget) != nullptr || get<TextEntry>(widget) != nullptr;
    }

    std::string_view cvarName(const Widget& widget)
    {
        return std::visit([](const auto& value) -> std::string_view {
            using T = std::decay_t<decltype(value)>;
            if constexpr ( std::is_same_v<T, Toggle> || std::is_same_v<T, Slider>
                || std::is_same_v<T, Choice> || std::is_same_v<T, TextEntry> )
            {
                return value.cvar;
            }
            else
            {
                return {};
            }
        }, widget);
    }

    WidgetValue defaultValueOf(const Widget& widget)
    {
        if ( const Toggle* toggle = get<Toggle>(widget) )
        {
            return toggle->value;
        }
        if ( const Slider* slider = get<Slider>(widget) )
        {
            return std::max(slider->min, std::min(slider->value, slider->max));
        }
        if ( const Choice* choice = get<Choice>(widget) )
        {
            if ( choice->options.empty() )
            {
                return std::int64_t{ 0 };
            }

            const std::size_t selected = std::min(choice->selected,
                choice->options.size() - 1);
            return static_cast<std::int64_t>(selected);
        }
        if ( const TextEntry* entry = get<TextEntry>(widget) )
        {
            return entry->defaultText;
        }
        return false;
    }

    // The largest edit buffer a TextEntry may hold, in bytes. A code point
    // is only ever inserted whole, so the buffer can stay below it.
    constexpr std::size_t kMaxTextBytes = 1024;

    // The number of bytes of the UTF-8 sequence that starts with lead, or 1
    // for a byte that is not a valid lead, so an invalid buffer is walked
    // one byte at a time instead of looping.
    std::size_t utf8SequenceLength(unsigned char lead)
    {
        if ( lead < 0x80 )
        {
            return 1;
        }
        if ( (lead & 0xE0) == 0xC0 )
        {
            return 2;
        }
        if ( (lead & 0xF0) == 0xE0 )
        {
            return 3;
        }
        if ( (lead & 0xF8) == 0xF0 )
        {
            return 4;
        }
        return 1;
    }

    // The start of the code point before pos, walking back over its
    // continuation bytes. 0 when pos is 0.
    std::size_t previousCodePoint(const std::string& text, std::size_t pos)
    {
        if ( pos == 0 )
        {
            return 0;
        }
        std::size_t start = pos - 1;
        while ( start > 0
            && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80 )
        {
            --start;
        }
        return start;
    }

    // The end of the code point that starts at pos, clamped to the string.
    std::size_t nextCodePoint(const std::string& text, std::size_t pos)
    {
        if ( pos >= text.size() )
        {
            return text.size();
        }
        const std::size_t length =
            utf8SequenceLength(static_cast<unsigned char>(text[pos]));
        return std::min(pos + length, text.size());
    }

    float clampFloat(float value, float low, float high)
    {
        return std::max(low, std::min(value, high));
    }

    double clampDouble(double value, double low, double high)
    {
        return std::max(low, std::min(value, high));
    }

    struct LayoutRow
    {
        std::size_t widget = kNoWidget;
        Rect rect;
        float height = 0.0f;
    };

    struct Layout
    {
        Rect panel;
        Rect inner;
        Rect title;
        Rect rowsClip;
        std::vector<LayoutRow> rows;
        float contentHeight = 0.0f;
        float maxScroll = 0.0f;
    };

    struct Hit
    {
        std::size_t widget = kNoWidget;
        std::size_t item = kNoWidget;
    };

    struct ScreenState
    {
        ScreenId id = kInvalidScreen;
        Screen screen;
        std::size_t focus = kNoWidget;
        std::vector<std::optional<WidgetValue>> local;
        std::vector<std::size_t> listOffsets;
        bool capturing = false;
        std::size_t captureWidget = kNoWidget;
        std::size_t editCursor = 0;
        std::string editStartText;
        float scroll = 0.0f;
    };

    struct AxisState
    {
        int direction = 0;
        double held = 0.0;
        bool repeating = false;
    };
}

struct Ui::State
{
    State(Painter& painterRef, cvars::Registry& cvarsRef)
        : painter(painterRef), cvars(cvarsRef)
    {
    }

    Painter& painter;
    cvars::Registry& cvars;
    Skin skin;
    NavigationMap navigation;
    int viewportWidth = 0;
    int viewportHeight = 0;
    std::vector<ScreenState> screens;
    ScreenId nextScreenId = 1;
    std::deque<UiEvent> events;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    std::array<bool, static_cast<std::size_t>(platform::MouseButton::X2) + 1>
        mouseButtons = {};
    std::array<bool, static_cast<std::size_t>(platform::Key::Count)> keysDown = {};
    std::array<bool, 16> gamepadButtons = {};
    bool shiftDown = false;
    bool textInputOn = false;

    AxisState horizontalAxis;
    AxisState verticalAxis;

    // The last capture direction of every gamepad axis, so an axis already past the capture
    // threshold when capture starts is ignored until it returns to rest.
    std::array<int, static_cast<std::size_t>(platform::GamepadAxis::RightTrigger) + 1>
        gamepadAxisDirections = {};

    // The layout and hit helpers below only read the top screen and the
    // painter's measurements; they never change UI state.
    ScreenState* top()
    {
        return screens.empty() ? nullptr : &screens.back();
    }

    const ScreenState* top() const
    {
        return screens.empty() ? nullptr : &screens.back();
    }

    ScreenState* findScreen(ScreenId id)
    {
        for ( ScreenState& screen : screens )
        {
            if ( screen.id == id )
            {
                return &screen;
            }
        }
        return nullptr;
    }

    const ScreenState* findScreen(ScreenId id) const
    {
        for ( const ScreenState& screen : screens )
        {
            if ( screen.id == id )
            {
                return &screen;
            }
        }
        return nullptr;
    }

    float canvasWidth() const
    {
        if ( viewportHeight <= 0 )
        {
            return 0.0f;
        }
        return kCanvasHeight * static_cast<float>(viewportWidth)
            / static_cast<float>(viewportHeight);
    }

    Point canvasPoint(float pixelX, float pixelY) const
    {
        if ( viewportHeight <= 0 )
        {
            return {};
        }
        const float scale = kCanvasHeight / static_cast<float>(viewportHeight);
        return Point{ pixelX * scale, pixelY * scale };
    }

    float textWidth(std::string_view text) const
    {
        return painter.measureText(skin.textFont, text).width;
    }

    float titleWidth(std::string_view text) const
    {
        return painter.measureText(skin.titleFont, text).width;
    }

    std::string sliderText(const Slider& slider, double value) const
    {
        if ( !slider.cvar.empty() && cvars.getInt(slider.cvar).has_value() )
        {
            return std::format("{}", static_cast<long long>(std::llround(value)));
        }
        return std::format("{:.2f}", value);
    }

    float widgetWidth(const Screen& screen, std::size_t index) const
    {
        const Widget& widget = screen.widgets[index];
        if ( const Label* label = get<Label>(widget) )
        {
            const float value = label->value.empty() ? 0.0f : textWidth(label->value);
            return textWidth(label->text) + (label->value.empty() ? 0.0f : kColumnGap + value);
        }
        if ( const Image* image = get<Image>(widget) )
        {
            return image->size.width;
        }
        if ( const Button* button = get<Button>(widget) )
        {
            return textWidth(button->text);
        }
        if ( const Toggle* toggle = get<Toggle>(widget) )
        {
            return textWidth(toggle->text) + kColumnGap
                + std::max(textWidth("On"), textWidth("Off"));
        }
        if ( const Slider* slider = get<Slider>(widget) )
        {
            return textWidth(slider->text) + kColumnGap
                + std::max(textWidth(sliderText(*slider, slider->min)),
                    textWidth(sliderText(*slider, slider->max)));
        }
        if ( const Choice* choice = get<Choice>(widget) )
        {
            float options = 0.0f;
            for ( const std::string& option : choice->options )
            {
                options = std::max(options, textWidth(option));
            }
            return textWidth(choice->text) + kColumnGap + options;
        }
        if ( const List* list = get<List>(widget) )
        {
            float width = textWidth(list->text);
            const bool images = std::any_of(list->items.begin(), list->items.end(),
                [](const ListItem& item) { return item.image != 0; });
            for ( const ListItem& item : list->items )
            {
                width = std::max(width,
                    textWidth(item.text) + (images ? skin.rowHeight + kColumnGap : 0.0f));
            }
            return width;
        }
        if ( const BindingCapture* capture = get<BindingCapture>(widget) )
        {
            return textWidth(capture->text) + kColumnGap
                + std::max(textWidth("Press input"), textWidth("Press a key"));
        }
        if ( const TextEntry* entry = get<TextEntry>(widget) )
        {
            return textWidth(entry->text) + kColumnGap + textWidth(entry->defaultText);
        }
        return 0.0f;
    }

    float widgetHeight(const Widget& widget) const
    {
        if ( const Image* image = get<Image>(widget) )
        {
            return image->size.width > 0.0f && image->size.height > 0.0f
                ? image->size.height : 0.0f;
        }
        if ( const List* list = get<List>(widget) )
        {
            const std::size_t visible = std::min(list->items.size(), kListVisibleRows);
            return (list->text.empty() ? 0.0f : skin.rowHeight)
                + static_cast<float>(visible) * skin.rowHeight;
        }
        return skin.rowHeight;
    }

    Layout computeLayout(const ScreenState& state) const
    {
        Layout layout;
        const float canvas = canvasWidth();
        if ( canvas <= 0.0f )
        {
            return layout;
        }

        const std::size_t count = state.screen.widgets.size();
        std::vector<float> heights(count, 0.0f);
        float contentHeight = skin.titleHeight;
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

        const float availableWidth = std::max(0.0f, canvas - 2.0f * skin.canvasMargin);
        const float availableHeight = std::max(0.0f, kCanvasHeight - 2.0f * skin.canvasMargin);
        float panelWidth = std::max(skin.minPanelWidth, widest + 2.0f * skin.panelPadding);
        panelWidth = std::min(panelWidth, availableWidth);
        float panelHeight = contentHeight + 2.0f * skin.panelPadding;
        panelHeight = std::min(panelHeight, availableHeight);

        layout.panel = Rect{ (canvas - panelWidth) * 0.5f,
            (kCanvasHeight - panelHeight) * 0.5f, panelWidth, panelHeight };
        layout.inner = Rect{ layout.panel.x + skin.panelPadding,
            layout.panel.y + skin.panelPadding,
            std::max(0.0f, layout.panel.width - 2.0f * skin.panelPadding),
            std::max(0.0f, layout.panel.height - 2.0f * skin.panelPadding) };
        layout.title = Rect{ layout.inner.x, layout.inner.y, layout.inner.width,
            std::min(skin.titleHeight, layout.inner.height) };
        layout.rowsClip = Rect{ layout.inner.x, layout.title.y + layout.title.height,
            layout.inner.width,
            std::max(0.0f, layout.inner.y + layout.inner.height
                - (layout.title.y + layout.title.height)) };

        float y = layout.inner.y + skin.titleHeight;
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( heights[i] <= 0.0f )
            {
                continue;
            }
            y += skin.rowSpacing;
            layout.rows.push_back(LayoutRow{ i,
                Rect{ layout.inner.x, y, layout.inner.width, heights[i] }, heights[i] });
            y += heights[i];
        }
        layout.contentHeight = contentHeight;
        layout.maxScroll = std::max(0.0f,
            contentHeight + 2.0f * skin.panelPadding - panelHeight);
        return layout;
    }

    float effectiveScroll(const ScreenState& state, const Layout& layout) const
    {
        return clampFloat(state.scroll, 0.0f, layout.maxScroll);
    }

    void ensureScroll(ScreenState& state)
    {
        const Layout layout = computeLayout(state);
        state.scroll = effectiveScroll(state, layout);
    }

    Rect listHeadingRect(const ScreenState& state, const LayoutRow& row) const
    {
        const List* list = get<List>(state.screen.widgets[row.widget]);
        if ( list == nullptr || list->text.empty() )
        {
            return Rect{};
        }
        return Rect{ row.rect.x, row.rect.y, row.rect.width, skin.rowHeight };
    }

    Rect listItemRect(const ScreenState& state, const LayoutRow& row,
        std::size_t item) const
    {
        const List* list = get<List>(state.screen.widgets[row.widget]);
        if ( list == nullptr || item >= list->items.size() )
        {
            return Rect{};
        }
        const float heading = list->text.empty() ? 0.0f : skin.rowHeight;
        const std::size_t offset = std::min(state.listOffsets[row.widget],
            list->items.size());
        if ( item < offset || item >= offset + kListVisibleRows )
        {
            return Rect{};
        }
        return Rect{ row.rect.x, row.rect.y + heading
            + static_cast<float>(item - offset) * skin.rowHeight,
            row.rect.width, skin.rowHeight };
    }

    std::size_t visibleListRows() const
    {
        return kListVisibleRows;
    }

    void ensureListVisible(ScreenState& state, std::size_t widgetIndex)
    {
        List* list = get<List>(state.screen.widgets[widgetIndex]);
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
        if ( list->selected >= offset + kListVisibleRows )
        {
            offset = list->selected - kListVisibleRows + 1;
        }
    }

    void ensureFocusVisible(ScreenState& state)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        const Layout layout = computeLayout(state);
        const float scroll = effectiveScroll(state, layout);
        for ( const LayoutRow& row : layout.rows )
        {
            if ( row.widget != state.focus )
            {
                continue;
            }
            const float top = row.rect.y - scroll;
            const float bottom = top + row.height;
            const float clipTop = layout.rowsClip.y;
            const float clipBottom = layout.rowsClip.y + layout.rowsClip.height;
            if ( top < clipTop )
            {
                state.scroll = std::max(0.0f, state.scroll - (clipTop - top));
            }
            else if ( bottom > clipBottom )
            {
                state.scroll = std::min(layout.maxScroll,
                    state.scroll + (bottom - clipBottom));
            }
            return;
        }
    }

    bool isReadOnly(std::string_view name) const
    {
        const std::optional<cvars::Flag> flags = cvars.flags(name);
        return flags.has_value() && cvars::hasFlag(*flags, cvars::Flag::ReadOnly);
    }

    bool isWidgetDisabled(const ScreenState& state, std::size_t index) const
    {
        const Widget& widget = state.screen.widgets[index];
        if ( !enabledFlag(widget) )
        {
            return true;
        }
        const std::string_view name = cvarName(widget);
        return !name.empty() && isReadOnly(name);
    }

    bool isFocusable(const ScreenState& state, std::size_t index) const
    {
        return index < state.screen.widgets.size() && focusableKind(state.screen.widgets[index])
            && !isWidgetDisabled(state, index);
    }

    std::size_t firstFocusable(const ScreenState& state) const
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

    std::size_t nextFocusable(const ScreenState& state, std::size_t from, int direction) const
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

    std::string displayText(const ScreenState& state, std::size_t index) const
    {
        const Widget& widget = state.screen.widgets[index];
        const TextEntry* entry = get<TextEntry>(widget);
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

    void setLocalText(ScreenState& state, std::size_t index, std::string text)
    {
        if ( index < state.local.size() )
        {
            state.local[index] = std::move(text);
        }
    }

    // Inserts a whole UTF-8 string at the edit cursor, keeping only the
    // complete code points that fit in kMaxTextBytes. A sequence is never
    // cut in half, so the buffer stays valid UTF-8 when the input was.
    void insertText(ScreenState& state, std::string_view input)
    {
        if ( state.focus >= state.screen.widgets.size()
            || get<TextEntry>(state.screen.widgets[state.focus]) == nullptr
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
                utf8SequenceLength(static_cast<unsigned char>(input[pos]));
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
    bool textInputWanted() const
    {
        const ScreenState* state = top();
        return state != nullptr && !state->capturing
            && state->focus < state->screen.widgets.size()
            && get<TextEntry>(state->screen.widgets[state->focus]) != nullptr;
    }

    // Reports a change of textInputWanted() once, after focus, the stack or
    // a widget changed. The ui owns no window, so the game turns this into
    // the platform call.
    void syncTextInput()
    {
        const bool wanted = textInputWanted();
        if ( wanted != textInputOn )
        {
            textInputOn = wanted;
            events.emplace_back(TextInputWanted{ wanted });
        }
    }

    bool currentToggle(const ScreenState& state, std::size_t index) const
    {
        const Toggle* toggle = get<Toggle>(state.screen.widgets[index]);
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

    bool sliderIsInt(const Slider& slider) const
    {
        return !slider.cvar.empty() && cvars.getInt(slider.cvar).has_value();
    }

    double currentSlider(const ScreenState& state, std::size_t index) const
    {
        const Slider* slider = get<Slider>(state.screen.widgets[index]);
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

    std::size_t currentChoiceIndex(const Choice& choice, const ScreenState& state,
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

    void setFocus(ScreenState& state, std::size_t index)
    {
        if ( state.focus == index )
        {
            return;
        }
        const bool enteringTextEntry = index < state.screen.widgets.size()
            && get<TextEntry>(state.screen.widgets[index]) != nullptr;
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

    void resetFocus(ScreenState& state)
    {
        state.focus = state.screen.initialFocus;
        if ( state.focus >= state.screen.widgets.size()
            || !isFocusable(state, state.focus) )
        {
            state.focus = firstFocusable(state);
        }
        if ( state.focus != kNoWidget
            && get<TextEntry>(state.screen.widgets[state.focus]) != nullptr )
        {
            const std::string text = displayText(state, state.focus);
            setLocalText(state, state.focus, text);
            state.editStartText = text;
            state.editCursor = text.size();
        }
        ensureFocusVisible(state);
    }

    void moveFocus(ScreenState& state, int direction)
    {
        const std::size_t next = nextFocusable(state, state.focus, direction);
        setFocus(state, next);
    }

    void moveSelection(ScreenState& state, int direction)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        List* list = get<List>(state.screen.widgets[state.focus]);
        if ( list == nullptr || list->items.empty() )
        {
            return;
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
        }
    }

    void navigateVertical(ScreenState& state, int direction)
    {
        if ( state.focus != kNoWidget
            && get<List>(state.screen.widgets[state.focus]) != nullptr )
        {
            moveSelection(state, direction);
        }
        else
        {
            moveFocus(state, direction);
        }
    }

    void adjustSlider(ScreenState& state, std::size_t index, int direction)
    {
        Slider* slider = get<Slider>(state.screen.widgets[index]);
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

    void adjustChoice(ScreenState& state, std::size_t index, int direction)
    {
        Choice* choice = get<Choice>(state.screen.widgets[index]);
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

    void adjustHorizontal(ScreenState& state, int direction)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        Widget& widget = state.screen.widgets[state.focus];
        if ( get<Slider>(widget) != nullptr )
        {
            adjustSlider(state, state.focus, direction);
        }
        else if ( get<Choice>(widget) != nullptr )
        {
            adjustChoice(state, state.focus, direction);
        }
        else if ( get<TextEntry>(widget) != nullptr )
        {
            const std::string& text = std::get<std::string>(*state.local[state.focus]);
            state.editCursor = direction < 0
                ? previousCodePoint(text, state.editCursor)
                : nextCodePoint(text, state.editCursor);
        }
    }

    void submitText(ScreenState& state, std::size_t index)
    {
        TextEntry* entry = get<TextEntry>(state.screen.widgets[index]);
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

    void acceptFocused(ScreenState& state)
    {
        if ( state.focus == kNoWidget )
        {
            return;
        }
        Widget& widget = state.screen.widgets[state.focus];

        if ( Button* button = get<Button>(widget) )
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
        if ( get<Toggle>(widget) != nullptr )
        {
            const bool current = currentToggle(state, state.focus);
            const bool changed = !current;
            bool applied = true;
            const std::string_view name = cvarName(widget);
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
        if ( List* list = get<List>(widget) )
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
        if ( get<BindingCapture>(widget) != nullptr )
        {
            state.capturing = true;
            state.captureWidget = state.focus;
            return;
        }
        if ( get<TextEntry>(widget) != nullptr )
        {
            submitText(state, state.focus);
            return;
        }
    }

    void setBinding(ScreenState& state, Binding binding)
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

    void cancelCapture(ScreenState& state)
    {
        state.capturing = false;
        state.captureWidget = kNoWidget;
    }

    void back(ScreenState& state)
    {
        if ( state.capturing )
        {
            cancelCapture(state);
            return;
        }
        if ( state.focus != kNoWidget
            && get<TextEntry>(state.screen.widgets[state.focus]) != nullptr
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

    void handleKey(const platform::KeyChanged& key)
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

        ScreenState* state = top();
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

        TextEntry* entry = state->focus < state->screen.widgets.size()
            ? get<TextEntry>(state->screen.widgets[state->focus]) : nullptr;
        if ( entry != nullptr )
        {
            if ( key.key == platform::Key::Backspace )
            {
                std::string& text = std::get<std::string>(*state->local[state->focus]);
                if ( state->editCursor > 0 && state->editCursor <= text.size() )
                {
                    const std::size_t start =
                        previousCodePoint(text, state->editCursor);
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
                        nextCodePoint(text, state->editCursor);
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
                state->editCursor = previousCodePoint(text, state->editCursor);
                return;
            }
            if ( key.key == platform::Key::Right )
            {
                const std::string& text =
                    std::get<std::string>(*state->local[state->focus]);
                state->editCursor = nextCodePoint(text, state->editCursor);
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

    // Inserts the platform's committed text into an active TextEntry. Text
    // for a screen that is not editing one is ignored, as are text events
    // while a binding capture owns the input.
    void handleTextInput(const platform::TextInput& input)
    {
        ScreenState* state = top();
        if ( state == nullptr || state->capturing )
        {
            return;
        }
        insertText(*state, input.text);
    }

    Hit hitTest(const ScreenState& state, Point point) const
    {
        const Layout layout = computeLayout(state);
        const float scroll = effectiveScroll(state, layout);
        for ( auto it = layout.rows.rbegin(); it != layout.rows.rend(); ++it )
        {
            const LayoutRow& row = *it;
            const Rect rect{ row.rect.x, row.rect.y - scroll, row.rect.width, row.rect.height };
            if ( const List* list = get<List>(state.screen.widgets[row.widget]) )
            {
                for ( std::size_t i = 0; i < list->items.size(); ++i )
                {
                    Rect itemRect = listItemRect(state, row, i);
                    itemRect.y -= scroll;
                    if ( itemRect.contains(point) )
                    {
                        return Hit{ row.widget, i };
                    }
                }
                const Rect heading = listHeadingRect(state, row);
                if ( heading.width > 0.0f && Rect{ heading.x, heading.y - scroll,
                    heading.width, heading.height }.contains(point) )
                {
                    return Hit{ row.widget, kNoWidget };
                }
                continue;
            }
            if ( rect.contains(point) )
            {
                return Hit{ row.widget, kNoWidget };
            }
        }
        return {};
    }

    void handleMouseButton(const platform::MouseButtonChanged& button)
    {
        const std::size_t index = static_cast<std::size_t>(button.button);
        if ( index >= mouseButtons.size() )
        {
            return;
        }
        const bool wasDown = mouseButtons[index];
        mouseButtons[index] = button.down;

        ScreenState* state = top();
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
        const Hit hit = hitTest(*state, point);
        if ( hit.widget == kNoWidget )
        {
            return;
        }
        if ( List* list = get<List>(state->screen.widgets[hit.widget]) )
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

    void handleMouseWheel(const platform::MouseWheelMoved& wheel)
    {
        ScreenState* state = top();
        if ( state == nullptr || viewportHeight <= 0 )
        {
            return;
        }
        const Hit hit = hitTest(*state, canvasPoint(mouseX, mouseY));
        if ( hit.widget == kNoWidget )
        {
            return;
        }
        List* list = get<List>(state->screen.widgets[hit.widget]);
        if ( list == nullptr || list->items.size() <= kListVisibleRows )
        {
            return;
        }
        const std::size_t maxOffset = list->items.size() - kListVisibleRows;
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

    void handleGamepadButton(const platform::GamepadButtonChanged& button)
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
        ScreenState* state = top();
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

    int axisDirection(platform::GamepadAxis axis, float value) const
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
    static int captureAxisDirection(platform::GamepadAxis axis, float value)
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

    void fireAxis(bool horizontal, int direction)
    {
        ScreenState* state = top();
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

    void handleAxis(const platform::GamepadAxisMoved& axis)
    {
        const std::size_t axisIndex = static_cast<std::size_t>(axis.axis);
        if ( axisIndex >= gamepadAxisDirections.size() )
        {
            return;
        }

        ScreenState* screen = top();
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
        AxisState& state = axis.axis == navigation.horizontalAxis
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

    void advanceAxis(AxisState& axis, bool horizontal, double seconds)
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

    void handleEvent(const platform::Event& event)
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

    void validateOrLog(const Widget& widget, bool& valid)
    {
        valid = true;
        const std::string_view name = cvarName(widget);

        if ( const Slider* slider = get<Slider>(widget) )
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
        if ( const Choice* choice = get<Choice>(widget) )
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

        if ( get<Toggle>(widget) != nullptr )
        {
            if ( !cvars.getBool(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not a bool",
                    name);
                valid = false;
            }
            return;
        }
        if ( get<Slider>(widget) != nullptr )
        {
            if ( !cvars.getInt(name).has_value() && !cvars.getFloat(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not numeric",
                    name);
                valid = false;
            }
            return;
        }
        if ( const Choice* choice = get<Choice>(widget) )
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
        if ( get<TextEntry>(widget) != nullptr )
        {
            if ( !cvars.getString(name).has_value() )
            {
                babylon::log::warning("ui: screen rejected: cvar '{}' is not a string",
                    name);
                valid = false;
            }
        }
    }

    bool validateScreen(const Screen& screen)
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

    void initLocal(ScreenState& state)
    {
        const std::size_t count = state.screen.widgets.size();
        state.local.assign(count, std::nullopt);
        state.listOffsets.assign(count, 0);
        for ( std::size_t i = 0; i < count; ++i )
        {
            const Widget& widget = state.screen.widgets[i];
            if ( !valueKind(widget) )
            {
                continue;
            }
            const std::string_view name = cvarName(widget);
            if ( const TextEntry* entry = get<TextEntry>(widget) )
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
                state.local[i] = defaultValueOf(widget);
            }
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( List* list = get<List>(state.screen.widgets[i]) )
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

    void drawRow(const ScreenState& state, const LayoutRow& row, float scroll)
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

        if ( const Image* image = get<Image>(widget) )
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

        if ( const List* list = get<List>(widget) )
        {
            drawList(state, row, scroll, *list, index, disabled, focused);
            return;
        }

        if ( focused && focusableKind(widget) )
        {
            painter.fillRect(rect, skin.highlight);
        }
        const Color color = disabled ? skin.textDisabled
            : (focused ? skin.textFocused : skin.text);

        if ( const Label* label = get<Label>(widget) )
        {
            painter.drawText(skin.textFont, label->text, rect, TextAlign::Left, color);
            if ( !label->value.empty() )
            {
                painter.drawText(skin.textFont, label->value, rect, TextAlign::Right, color);
            }
            return;
        }
        if ( const Button* button = get<Button>(widget) )
        {
            painter.drawText(skin.textFont, button->text, rect, TextAlign::Left, color);
            return;
        }
        if ( const Toggle* toggle = get<Toggle>(widget) )
        {
            painter.drawText(skin.textFont, toggle->text, rect, TextAlign::Left, color);
            painter.drawText(skin.textFont, currentToggle(state, index) ? "On" : "Off",
                rect, TextAlign::Right, color);
            return;
        }
        if ( const Slider* slider = get<Slider>(widget) )
        {
            painter.drawText(skin.textFont, slider->text, rect, TextAlign::Left, color);
            painter.drawText(skin.textFont, sliderText(*slider, currentSlider(state, index)),
                rect, TextAlign::Right, color);
            return;
        }
        if ( const Choice* choice = get<Choice>(widget) )
        {
            painter.drawText(skin.textFont, choice->text, rect, TextAlign::Left, color);
            const std::size_t selected = currentChoiceIndex(*choice, state, index);
            if ( selected < choice->options.size() )
            {
                painter.drawText(skin.textFont, choice->options[selected], rect,
                    TextAlign::Right, color);
            }
            return;
        }
        if ( const BindingCapture* capture = get<BindingCapture>(widget) )
        {
            painter.drawText(skin.textFont, capture->text, rect, TextAlign::Left, color);
            if ( state.capturing && state.captureWidget == index )
            {
                painter.drawText(skin.textFont, "Press input", rect, TextAlign::Right,
                    color);
            }
            return;
        }
        if ( const TextEntry* entry = get<TextEntry>(widget) )
        {
            painter.drawText(skin.textFont, entry->text, rect, TextAlign::Left, color);
            painter.drawText(skin.textFont, displayText(state, index), rect,
                TextAlign::Right, color);
        }
    }

    float stateClipTop = -std::numeric_limits<float>::infinity();
    float stateClipBottom = std::numeric_limits<float>::infinity();

    void drawList(const ScreenState& state, const LayoutRow& row, float scroll,
        const List& list, std::size_t index, bool disabled, bool focused)
    {
        const Rect rect{ row.rect.x, row.rect.y - scroll, row.rect.width, row.rect.height };
        const Color color = disabled ? skin.textDisabled
            : (focused ? skin.textFocused : skin.text);
        float y = rect.y;
        if ( !list.text.empty() )
        {
            painter.drawText(skin.textFont, list.text,
                Rect{ rect.x, y, rect.width, skin.rowHeight }, TextAlign::Left, color);
            y += skin.rowHeight;
        }
        const std::size_t offset = std::min(state.listOffsets[index], list.items.size());
        const bool images = std::any_of(list.items.begin(), list.items.end(),
            [](const ListItem& item) { return item.image != 0; });
        const std::size_t visible = std::min(kListVisibleRows,
            list.items.size() - std::min(offset, list.items.size()));
        for ( std::size_t i = 0; i < visible; ++i )
        {
            const std::size_t itemIndex = offset + i;
            const ListItem& item = list.items[itemIndex];
            const Rect itemRect{ rect.x, y + static_cast<float>(i) * skin.rowHeight,
                rect.width, skin.rowHeight };
            if ( itemIndex == list.selected )
            {
                painter.fillRect(itemRect, focused ? skin.highlight
                    : skin.highlightUnfocused);
            }
            float textX = itemRect.x;
            float textWidth = itemRect.width;
            if ( images )
            {
                if ( item.image != 0 )
                {
                    painter.drawImage(item.image,
                        Rect{ itemRect.x, itemRect.y, skin.rowHeight, skin.rowHeight });
                }
                textX += skin.rowHeight + kColumnGap;
                textWidth -= skin.rowHeight + kColumnGap;
            }
            painter.drawText(skin.textFont, item.text,
                Rect{ textX, itemRect.y, std::max(0.0f, textWidth), itemRect.height },
                TextAlign::Left,
                itemIndex == list.selected ? skin.textFocused : color);
        }
    }

    void draw()
    {
        const ScreenState* state = top();
        if ( state == nullptr )
        {
            return;
        }
        const float canvas = canvasWidth();
        if ( canvas <= 0.0f )
        {
            return;
        }
        const Layout layout = computeLayout(*state);
        if ( layout.panel.width <= 0.0f || layout.panel.height <= 0.0f )
        {
            return;
        }
        const float scroll = effectiveScroll(*state, layout);

        painter.pushClip(Rect{ 0.0f, 0.0f, canvas, kCanvasHeight });
        painter.fillRect(layout.panel, skin.panel);
        if ( skin.panelImage != 0 )
        {
            painter.drawImage(skin.panelImage, layout.panel);
        }
        if ( skin.borderWidth > 0.0f )
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
        painter.pushClip(layout.rowsClip);
        stateClipTop = layout.rowsClip.y;
        stateClipBottom = layout.rowsClip.y + layout.rowsClip.height;
        for ( const LayoutRow& row : layout.rows )
        {
            drawRow(*state, row, scroll);
        }
        stateClipTop = -std::numeric_limits<float>::infinity();
        stateClipBottom = std::numeric_limits<float>::infinity();
        painter.popClip();
        painter.popClip();
    }
};

Ui::Ui(Painter& painter, cvars::Registry& cvars)
    : m_state(std::make_unique<State>(painter, cvars))
{
}

Ui::~Ui() = default;

bool Ui::setSkin(const Skin& skin)
{
    if ( !(skin.rowHeight > 0.0f) || skin.titleHeight < 0.0f
        || skin.rowSpacing < 0.0f || skin.panelPadding < 0.0f
        || skin.minPanelWidth < 0.0f || skin.canvasMargin < 0.0f
        || skin.borderWidth < 0.0f )
    {
        babylon::log::warning("ui: rejected an invalid skin");
        return false;
    }
    m_state->skin = skin;
    for ( ScreenState& screen : m_state->screens )
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
    return Size{ m_state->canvasWidth(), kCanvasHeight };
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
    ScreenState state;
    state.id = m_state->nextScreenId++;
    if ( m_state->nextScreenId == kInvalidScreen )
    {
        m_state->nextScreenId = 1;
    }
    state.screen = std::move(screen);
    m_state->initLocal(state);
    m_state->resetFocus(state);
    m_state->screens.push_back(std::move(state));
    m_state->syncTextInput();
    return m_state->screens.back().id;
}

bool Ui::setWidget(ScreenId screenId, std::size_t index, Widget widget)
{
    ScreenState* state = m_state->findScreen(screenId);
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
    const bool oldUnbound = cvarName(old).empty();
    const bool newUnbound = cvarName(widget).empty();
    std::optional<WidgetValue> local = index < state->local.size()
        ? state->local[index] : std::nullopt;
    std::size_t oldListSelection = 0;
    if ( const List* list = get<List>(old) )
    {
        oldListSelection = list->selected;
    }

    old = std::move(widget);
    if ( sameKind && oldUnbound && newUnbound )
    {
        if ( List* list = get<List>(old) )
        {
            if ( !list->items.empty() )
            {
                list->selected = std::min(oldListSelection, list->items.size() - 1);
            }
        }
        else if ( valueKind(old) )
        {
            state->local[index] = local;
        }
    }
    else
    {
        if ( valueKind(old) )
        {
            if ( const TextEntry* entry = get<TextEntry>(old) )
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
            else if ( cvarName(old).empty() )
            {
                state->local[index] = defaultValueOf(old);
            }
        }
        else
        {
            state->local[index] = std::nullopt;
        }
        if ( List* list = get<List>(old) )
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
        && get<TextEntry>(state->screen.widgets[state->focus]) != nullptr )
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
    ScreenState* state = m_state->top();
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
    const ScreenState* state = m_state->top();
    return state == nullptr ? kInvalidScreen : state->id;
}

std::size_t Ui::screenCount() const
{
    return m_state->screens.size();
}

void Ui::handleEvent(const platform::Event& event)
{
    m_state->handleEvent(event);
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
    const ScreenState* state = m_state->top();
    return state == nullptr ? kNoWidget : state->focus;
}

std::optional<WidgetValue> Ui::widgetValue(ScreenId screenId, std::size_t index) const
{
    const ScreenState* state = m_state->findScreen(screenId);
    if ( state == nullptr || index >= state->screen.widgets.size() )
    {
        return std::nullopt;
    }
    const Widget& widget = state->screen.widgets[index];
    if ( get<Toggle>(widget) != nullptr )
    {
        return m_state->currentToggle(*state, index);
    }
    if ( get<Slider>(widget) != nullptr )
    {
        return m_state->currentSlider(*state, index);
    }
    if ( const Choice* choice = get<Choice>(widget) )
    {
        return static_cast<std::int64_t>(m_state->currentChoiceIndex(*choice, *state, index));
    }
    if ( get<TextEntry>(widget) != nullptr )
    {
        return m_state->displayText(*state, index);
    }
    return std::nullopt;
}

}
