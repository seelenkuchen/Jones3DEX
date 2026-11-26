// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shared private helpers and Ui::State for the ui module.

#pragma once

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/Navigation.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace babylon::ui
{

namespace detail
{
    inline constexpr float kCanvasHeight = 480.0f;
    inline constexpr float kColumnGap = 8.0f;
    inline constexpr std::size_t kListVisibleRows = 5;

    template <class T>
    inline const T* get(const Widget& widget)
    {
        return std::get_if<T>(&widget);
    }

    template <class T>
    inline T* get(Widget& widget)
    {
        return std::get_if<T>(&widget);
    }

    inline bool focusableKind(const Widget& widget)
    {
        return std::visit([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            return std::is_same_v<T, Button> || std::is_same_v<T, Toggle>
                || std::is_same_v<T, Slider> || std::is_same_v<T, Choice>
                || std::is_same_v<T, List> || std::is_same_v<T, BindingCapture>
                || std::is_same_v<T, TextEntry>;
        }, widget);
    }

    inline bool valueKind(const Widget& widget)
    {
        return get<Toggle>(widget) != nullptr || get<Slider>(widget) != nullptr
            || get<Choice>(widget) != nullptr || get<TextEntry>(widget) != nullptr;
    }

    inline std::string_view cvarName(const Widget& widget)
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

    inline WidgetValue defaultValueOf(const Widget& widget)
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

    // The number of bytes of the UTF-8 sequence that starts with lead, or 1
    // for a byte that is not a valid lead, so an invalid buffer is walked
    // one byte at a time instead of looping.
    inline std::size_t utf8SequenceLength(unsigned char lead)
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
    inline std::size_t previousCodePoint(const std::string& text, std::size_t pos)
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
    inline std::size_t nextCodePoint(const std::string& text, std::size_t pos)
    {
        if ( pos >= text.size() )
        {
            return text.size();
        }
        const std::size_t length =
            utf8SequenceLength(static_cast<unsigned char>(text[pos]));
        return std::min(pos + length, text.size());
    }

    struct LayoutRow
    {
        std::size_t widget = kNoWidget;
        Rect rect;
        float height = 0.0f;
        float textInset = 0.0f;
        float markerWidth = 0.0f;
    };

    struct LayoutTab
    {
        std::size_t index = 0;
        Rect rect;
        Rect underline;
    };

    struct Layout
    {
        Rect panel;
        Rect inner;
        Rect title;
        Rect rowsClip;
        Rect helpColumn;
        std::vector<LayoutRow> rows;
        std::vector<LayoutTab> tabs;
        float contentHeight = 0.0f;
        float maxScroll = 0.0f;
        float markerWidth = 0.0f;
        float textInset = 0.0f;
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
        std::vector<Prompt> prompts;
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
    State(Painter& painterRef, cvars::Registry& cvarsRef, Ui& ownerRef)
        : painter(painterRef), cvars(cvarsRef), owner(ownerRef)
    {
    }

    Painter& painter;
    cvars::Registry& cvars;
    Ui& owner;
    Skin skin;
    NavigationMap navigation;
    int viewportWidth = 0;
    int viewportHeight = 0;
    std::vector<detail::ScreenState> screens;
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

    detail::AxisState horizontalAxis;
    detail::AxisState verticalAxis;

    // The last capture direction of every gamepad axis, so an axis already past the capture
    // threshold when capture starts is ignored until it returns to rest.
    std::array<int, static_cast<std::size_t>(platform::GamepadAxis::RightTrigger) + 1>
        gamepadAxisDirections = {};

    float stateClipTop = -std::numeric_limits<float>::infinity();
    float stateClipBottom = std::numeric_limits<float>::infinity();

    // The layout and hit helpers below only read the top screen and the
    // painter's measurements; they never change UI state.
    detail::ScreenState* top();

    const detail::ScreenState* top() const;

    detail::ScreenState* findScreen(ScreenId id);

    const detail::ScreenState* findScreen(ScreenId id) const;

    float canvasWidth() const;

    // True when the prompt bar is actually drawn this frame: it needs entries and a positive
    // prompt height. Only then does the row area make room for it.
    bool promptsVisible(const detail::ScreenState& state) const;

    // The lowest canvas y the panel and rows may reach: the canvas margin, minus the prompt bar
    // when one is drawn.
    float lowerLimit(const detail::ScreenState& state) const;

    Point canvasPoint(float pixelX, float pixelY) const;

    float textWidth(std::string_view text) const;

    float titleWidth(std::string_view text) const;

    FontId helpFont() const;

    // True when the skin reserves a help column and the screen draws into it.
    // A shorter help vector or empty entries simply say nothing for those
    // rows. A Top panel always reserves the column, with or without text, so
    // its rows and the values in them keep the same width on every screen.
    bool hasHelp(const detail::ScreenState& state) const;

    float tabHeight() const;

    // Splits text into lines that fit width, at spaces, in the given font.
    // A single word wider than the column is kept whole and is clipped by
    // the painter when drawn.
    std::vector<std::string> wrapText(FontId font, std::string_view text, float width) const;

    void selectTab(detail::ScreenState& state, std::size_t index);

    void switchTab(detail::ScreenState& state, int direction);

    // Rows draw their text twice when a shadow is set: the shadow offsets down
    // and right, then the real text sits on top.
    void drawRowText(FontId font, std::string_view text, const Rect& rect,
        TextAlign align, const Color& color);

    // Draws the prompt bar left to right along the bottom left: icon, label,
    // then the next entry. The icons are promptHeight square and the labels
    // sit in a line box of that height, so every entry shares one baseline.
    void drawPrompts(const std::vector<Prompt>& prompts);

    std::string sliderText(const Slider& slider, double value) const;

    float widgetWidth(const Screen& screen, std::size_t index) const;

    float widgetHeight(const Widget& widget) const;

    detail::Layout computeLayout(const detail::ScreenState& state) const;

    float effectiveScroll(const detail::ScreenState& state, const detail::Layout& layout) const;

    void ensureScroll(detail::ScreenState& state);

    Rect listHeadingRect(const detail::ScreenState& state, const detail::LayoutRow& row) const;

    Rect listItemRect(const detail::ScreenState& state, const detail::LayoutRow& row,
        std::size_t item) const;

    std::size_t visibleListRows() const;

    void ensureListVisible(detail::ScreenState& state, std::size_t widgetIndex);

    // True when the whole row lies inside the visible row area at this scroll. Drawing and
    // pointer input use the same rule, so a partly hidden row is neither drawn nor clickable.
    bool rowVisible(const detail::Layout& layout, const detail::LayoutRow& row, float scroll) const;

    void ensureFocusVisible(detail::ScreenState& state);

    bool isReadOnly(std::string_view name) const;

    bool isWidgetDisabled(const detail::ScreenState& state, std::size_t index) const;

    bool isFocusable(const detail::ScreenState& state, std::size_t index) const;

    std::size_t firstFocusable(const detail::ScreenState& state) const;

    std::size_t nextFocusable(const detail::ScreenState& state, std::size_t from, int direction) const;

    std::string displayText(const detail::ScreenState& state, std::size_t index) const;

    void setLocalText(detail::ScreenState& state, std::size_t index, std::string text);

    // Inserts a whole UTF-8 string at the edit cursor, keeping only the
    // complete code points that fit in kMaxTextBytes. A sequence is never
    // cut in half, so the buffer stays valid UTF-8 when the input was.
    void insertText(detail::ScreenState& state, std::string_view input);

    // True when the top screen is editing a TextEntry, so the game should
    // have the platform's text input started.
    bool textInputWanted() const;

    // Reports a change of textInputWanted() once, after focus, the stack or
    // a widget changed. The ui owns no window, so the game turns this into
    // the platform call.
    void syncTextInput();

    bool currentToggle(const detail::ScreenState& state, std::size_t index) const;

    bool sliderIsInt(const Slider& slider) const;

    double currentSlider(const detail::ScreenState& state, std::size_t index) const;

    std::size_t currentChoiceIndex(const Choice& choice, const detail::ScreenState& state,
        std::size_t index) const;

    void setFocus(detail::ScreenState& state, std::size_t index);

    void resetFocus(detail::ScreenState& state);

    void moveFocus(detail::ScreenState& state, int direction);

    // Moves a focused list's selection one row; false when there is no row in that direction.
    bool moveSelection(detail::ScreenState& state, int direction);

    // Up and down move a focused list's selection; past its first or last row they leave the list
    // like any other widget, so a pad without Tab reaches the widgets above and below it.
    void navigateVertical(detail::ScreenState& state, int direction);

    void adjustSlider(detail::ScreenState& state, std::size_t index, int direction);

    void adjustChoice(detail::ScreenState& state, std::size_t index, int direction);

    void adjustHorizontal(detail::ScreenState& state, int direction);

    void submitText(detail::ScreenState& state, std::size_t index);

    void acceptFocused(detail::ScreenState& state);

    void setBinding(detail::ScreenState& state, Binding binding);

    void cancelCapture(detail::ScreenState& state);

    void back(detail::ScreenState& state);

    void handleKey(const platform::KeyChanged& key);

    // Inserts the platform's committed text into an active TextEntry. Text
    // for a screen that is not editing one is ignored, as are text events
    // while a binding capture owns the input.
    void handleTextInput(const platform::TextInput& input);

    detail::Hit hitTest(const detail::ScreenState& state, Point point) const;

    void handleMouseButton(const platform::MouseButtonChanged& button);

    void handleMouseWheel(const platform::MouseWheelMoved& wheel);

    void handleGamepadButton(const platform::GamepadButtonChanged& button);

    int axisDirection(platform::GamepadAxis axis, float value) const;

    // The capture rule for every axis: sticks report either half, the triggers rest at 0 and only
    // report their positive half. 0 is at rest or below the threshold.
    static int captureAxisDirection(platform::GamepadAxis axis, float value);

    void fireAxis(bool horizontal, int direction);

    void handleAxis(const platform::GamepadAxisMoved& axis);

    void advanceAxis(detail::AxisState& axis, bool horizontal, double seconds);

    // Keeps the raw input state current when a screen's event callback consumed an event
    // before the default handlers could see it. Without this a consumed key-up would leave the
    // key down for the next screen, which then ignores its first press as a repeat.
    void trackConsumedEvent(const platform::Event& event);

    void handleEvent(const platform::Event& event);

    void validateOrLog(const Widget& widget, bool& valid);

    bool validateScreen(const Screen& screen);

    void initLocal(detail::ScreenState& state);

    void drawRow(const detail::ScreenState& state, const detail::LayoutRow& row, float scroll);

    // Small up/down markers at the right edge of the row area when rows are hidden above or
    // below the scroll window.
    void drawScrollMarkers(const detail::Layout& layout, float scroll);

    // The right-hand help column: only the focused row's text, wrapped at
    // spaces, in the help font. The column is clipped to its own rectangle
    // so a long word cannot run into the rows.
    void drawHelp(const detail::ScreenState& state, const detail::Layout& layout);

    void drawList(const detail::ScreenState& state, const detail::LayoutRow& row, float scroll,
        const List& list, std::size_t index, bool disabled, bool focused);

    void draw();
};

}
