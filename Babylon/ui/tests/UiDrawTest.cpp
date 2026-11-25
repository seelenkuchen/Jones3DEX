// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

#include "UiTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace babylon::ui;
    using namespace babylon::ui::test;
}

TEST_CASE("the focus marker is drawn only on the focused row at a fixed x")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    Skin skin;
    skin.rowHeight = 20.0f;
    skin.titleHeight = 0.0f;
    skin.rowSpacing = 0.0f;
    skin.panelPadding = 0.0f;
    skin.minPanelWidth = 0.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.focusMarker = "*";
    skin.markerSpacing = 4.0f;
    REQUIRE(ui.setSkin(skin));

    Screen screen;
    screen.widgets = { Button{ "A", 1, true }, Button{ "B", 2, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // One glyph is 8 canvas units, so every row's text starts 12 units in.
    const float markerX = 310.0f; // panel is 20 wide, centered at (640 - 20) / 2
    const float textX   = 322.0f;

    const auto checkDraw = [&painter, &ui, markerX, textX](const char* firstRow)
    {
        ui.draw();
        int markers = 0;
        float markerDrawnX = 0.0f;
        float firstTextX = 0.0f;
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == "*" )
                {
                    ++markers;
                    markerDrawnX = text->rect.x;
                }
                if ( text->text == firstRow )
                {
                    firstTextX = text->rect.x;
                }
            }
        }
        CHECK(markers == 1);
        CHECK(markerDrawnX == markerX);
        CHECK(firstTextX == textX);
    };

    checkDraw("A");

    ui.handleEvent(platform::Event(keyEvent(platform::Key::Down, true)));
    ui.handleEvent(platform::Event(keyEvent(platform::Key::Down, false)));
    painter.clearRecorded();
    checkDraw("B");
}

TEST_CASE("a focused list marks its selected row without a highlight fill")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    // The modern menu look: no highlight fill, a marker and a brighter text colour.
    Skin skin;
    skin.rowHeight = 20.0f;
    skin.titleHeight = 0.0f;
    skin.rowSpacing = 0.0f;
    skin.panelPadding = 0.0f;
    skin.minPanelWidth = 0.0f;
    skin.borderWidth = 0.0f;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.text = Color{ 0.5f, 0.5f, 0.5f, 1.0f };
    skin.textFocused = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.focusMarker = "*";
    skin.markerSpacing = 4.0f;
    REQUIRE(ui.setSkin(skin));

    Screen screen;
    List list;
    for ( std::int64_t i = 0; i < 3; ++i )
    {
        list.items.push_back(ListItem{ "Row" + std::to_string(i), 0, i });
    }
    list.action = 9;
    screen.widgets = { list };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // The marker sits on the selected row, which alone has the focused colour (owner, 2026-11-24:
    // with a pad the selected save could not be seen).
    const auto checkSelected = [&painter, &ui](const std::string& selectedRow)
    {
        painter.clearRecorded();
        ui.draw();
        float markerY = -1.0f;
        float selectedY = -2.0f;
        int bright = 0;
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == "*" )
                {
                    markerY = text->rect.y;
                }
                else if ( text->text.rfind("Row", 0) == 0 )
                {
                    if ( text->text == selectedRow )
                    {
                        selectedY = text->rect.y;
                        CHECK(text->color.r == doctest::Approx(1.0f));
                    }
                    else
                    {
                        CHECK(text->color.r == doctest::Approx(0.5f));
                    }
                    bright += text->color.r > 0.9f ? 1 : 0;
                }
            }
        }
        CHECK(markerY == selectedY);
        CHECK(bright == 1);
    };

    checkSelected("Row0");
    tap(ui, platform::Key::Down);
    checkSelected("Row1");
    tap(ui, platform::Key::Down);
    checkSelected("Row2");
}

TEST_CASE("a transparent panel, border and highlight draw no fills")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    Skin skin;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.panelBorder = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.borderWidth = 0.0f;
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlightUnfocused = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE(ui.setSkin(skin));

    Screen screen;
    screen.widgets = { Button{ "A", 1, true }, Button{ "B", 2, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);
    ui.draw();

    std::size_t fills = 0;
    for ( const RecordedCall& call : painter.recorded() )
    {
        fills += std::get_if<RecordedFillRect>(&call) != nullptr ? 1u : 0u;
    }
    CHECK(fills == 0u);
}

TEST_CASE("row text draws a shadow only when textShadow has alpha")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    Skin skin;
    skin.borderWidth = 0.0f;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE(ui.setSkin(skin));

    Screen screen;
    screen.widgets = { Button{ "A", 1, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    const auto texts = [&painter](std::string_view value)
    {
        std::vector<const RecordedDrawText*> found;
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == value )
                {
                    found.push_back(text);
                }
            }
        }
        return found;
    };

    ui.draw();
    CHECK(texts("A").size() == 1u);

    painter.clearRecorded();
    skin.textShadow = Color{ 0.0f, 0.0f, 0.0f, 0.6f };
    skin.textShadowOffset = 2.0f;
    REQUIRE(ui.setSkin(skin));
    ui.draw();

    const std::vector<const RecordedDrawText*> shadowed = texts("A");
    REQUIRE(shadowed.size() == 2u);
    CHECK(shadowed[0]->color == skin.textShadow);
    CHECK(shadowed[0]->rect.x == shadowed[1]->rect.x + 2.0f);
    CHECK(shadowed[0]->rect.y == shadowed[1]->rect.y + 2.0f);
}

TEST_CASE("a tab strip draws labels and underlines the active tab")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    Skin skin;
    skin.titleHeight = 0.0f;
    skin.rowHeight = 20.0f;
    skin.panelPadding = 10.0f;
    skin.minPanelWidth = 100.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    skin.titleFont = 5;
    skin.textFont = 5;
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.tabs = { "One", "Two" };
    screen.widgets = { Button{ "Go", 1, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    auto tabText = [&painter](const std::string& value) -> const RecordedDrawText*
    {
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == value )
                {
                    return text;
                }
            }
        }
        return nullptr;
    };

    ui.draw();
    const RecordedDrawText* one = tabText("One");
    const RecordedDrawText* two = tabText("Two");
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK(one->color == skin.textFocused);
    CHECK(two->color == skin.text);
    CHECK(two->rect.x > one->rect.x + one->rect.width);

    const RecordedFillRect* underline = nullptr;
    for ( const RecordedCall& call : painter.recorded() )
    {
        if ( const RecordedFillRect* fill = std::get_if<RecordedFillRect>(&call) )
        {
            if ( fill->color == skin.textFocused && fill->rect.height == 2.0f )
            {
                underline = fill;
            }
        }
    }
    REQUIRE(underline != nullptr);
    CHECK(underline->rect.x == one->rect.x);
    CHECK(underline->rect.width == one->rect.width);
    CHECK(underline->rect.y > one->rect.y);

    // Next-tab moves the active underline to the second label.
    tap(ui, platform::Key::E);
    painter.clearRecorded();
    ui.draw();
    one = tabText("One");
    two = tabText("Two");
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK(one->color == skin.text);
    CHECK(two->color == skin.textFocused);
    underline = nullptr;
    for ( const RecordedCall& call : painter.recorded() )
    {
        if ( const RecordedFillRect* fill = std::get_if<RecordedFillRect>(&call) )
        {
            if ( fill->color == skin.textFocused && fill->rect.height == 2.0f )
            {
                underline = fill;
            }
        }
    }
    REQUIRE(underline != nullptr);
    CHECK(underline->rect.x == two->rect.x);
}

TEST_CASE("help text is drawn only for the focused row and wraps in its column")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    Skin skin;
    skin.titleHeight = 0.0f;
    skin.rowHeight = 20.0f;
    skin.panelPadding = 10.0f;
    skin.minPanelWidth = 100.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    skin.helpColumnWidth = 0.35f;
    skin.textFont = 5;
    skin.helpFont = 9;
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.widgets = { Button{ "A", 1, true }, Button{ "B", 2, true } };
    screen.help = { "First help.", "Second help." };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    auto helpTexts = [&painter]() -> std::vector<const RecordedDrawText*>
    {
        std::vector<const RecordedDrawText*> found;
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->font == 9 )
                {
                    found.push_back(text);
                }
            }
        }
        return found;
    };

    auto joined = [](const std::vector<const RecordedDrawText*>& lines)
    {
        std::string text;
        for ( const RecordedDrawText* line : lines )
        {
            if ( !text.empty() )
            {
                text += ' ';
            }
            text += line->text;
        }
        return text;
    };

    ui.draw();
    std::vector<const RecordedDrawText*> texts = helpTexts();
    REQUIRE(!texts.empty());
    CHECK(joined(texts) == "First help.");

    tap(ui, platform::Key::Down);
    painter.clearRecorded();
    ui.draw();
    texts = helpTexts();
    REQUIRE(!texts.empty());
    CHECK(joined(texts) == "Second help.");

    // A long help string is split at spaces, and every line stays inside
    // one column: same x and width, all within the canvas.
    Screen wide;
    wide.widgets = { Button{ "A", 1, true }, Button{ "B", 2, true },
        Button{ "C", 3, true }, Button{ "D", 4, true }, Button{ "E", 5, true } };
    wide.help = { "one two three four five six seven eight nine ten eleven twelve" };
    REQUIRE(ui.pop() > 0);
    REQUIRE(ui.push(wide) != kInvalidScreen);
    painter.clearRecorded();
    ui.draw();
    texts = helpTexts();
    REQUIRE(texts.size() > 1u);
    const float columnX = texts.front()->rect.x;
    const float columnWidth = texts.front()->rect.width;
    float previousY = -1.0f;
    for ( const RecordedDrawText* text : texts )
    {
        CHECK(text->rect.x == columnX);
        CHECK(text->rect.width == columnWidth);
        CHECK(text->rect.x >= 0.0f);
        CHECK(text->rect.x + text->rect.width <= 640.0f);
        CHECK(text->rect.y > previousY);
        previousY = text->rect.y;
    }
}

TEST_CASE("choice rows draw arrows around the value and highlight them on focus")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    Skin skin;
    skin.textFont = 5;
    skin.titleHeight = 0.0f;
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.widgets = { Choice{ "Quality", std::string(), { "Low", "High" }, true, 0 } };
    REQUIRE(ui.push(screen) != kInvalidScreen);
    ui.draw();

    const RecordedDrawText* leftArrow = nullptr;
    const RecordedDrawText* rightArrow = nullptr;
    const RecordedDrawText* value = nullptr;
    for ( const RecordedCall& call : painter.recorded() )
    {
        if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
        {
            if ( text->text == "\xE2\x97\x80" )
            {
                leftArrow = text;
            }
            else if ( text->text == "\xE2\x96\xB6" )
            {
                rightArrow = text;
            }
            else if ( text->text == "Low" )
            {
                value = text;
            }
        }
    }
    REQUIRE(leftArrow != nullptr);
    REQUIRE(rightArrow != nullptr);
    REQUIRE(value != nullptr);
    CHECK(leftArrow->color == skin.textFocused);
    CHECK(rightArrow->color == skin.textFocused);
    CHECK(leftArrow->rect.x < value->rect.x);
    CHECK(rightArrow->rect.x > value->rect.x);
}

TEST_CASE("the prompt bar draws its entries left to right at the bottom left")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);

    Skin skin;
    skin.borderWidth = 0.0f;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.promptHeight = 20.0f;
    skin.canvasMargin = 8.0f;
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.widgets = { Button{ "Go", 1, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // A label-only prompt, then one with an icon, then a second icon-only
    // entry such as the right arrow of Change.
    ui.setPrompts({
        Prompt{ 0, "Select" },
        Prompt{ 9, "Back" },
        Prompt{ 10, "" },
    });
    ui.draw();

    const RecordedDrawText* select = nullptr;
    const RecordedDrawText* back = nullptr;
    const RecordedDrawImage* backIcon = nullptr;
    const RecordedDrawImage* rightIcon = nullptr;
    int selectIndex = -1;
    int backIconIndex = -1;
    int backIndex = -1;
    int rightIndex = -1;
    const std::vector<RecordedCall>& calls = painter.recorded();
    for ( std::size_t i = 0; i < calls.size(); ++i )
    {
        if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&calls[i]) )
        {
            if ( text->text == "Select" )
            {
                select = text;
                selectIndex = static_cast<int>(i);
            }
            else if ( text->text == "Back" )
            {
                back = text;
                backIndex = static_cast<int>(i);
            }
        }
        else if ( const RecordedDrawImage* image =
            std::get_if<RecordedDrawImage>(&calls[i]) )
        {
            if ( image->image == 9 )
            {
                backIcon = image;
                backIconIndex = static_cast<int>(i);
            }
            else if ( image->image == 10 )
            {
                rightIcon = image;
                rightIndex = static_cast<int>(i);
            }
        }
    }

    // y is the canvas bottom minus the margin and one prompt height.
    REQUIRE(select != nullptr);
    CHECK(select->rect == Rect{ 8.0f, 452.0f, 48.0f, 20.0f });
    CHECK(select->color == skin.text);
    REQUIRE(backIcon != nullptr);
    CHECK(backIcon->rect == Rect{ 80.0f, 452.0f, 20.0f, 20.0f });
    REQUIRE(back != nullptr);
    CHECK(back->rect == Rect{ 106.0f, 452.0f, 32.0f, 20.0f });
    CHECK(back->color == skin.text);
    REQUIRE(rightIcon != nullptr);
    CHECK(rightIcon->rect == Rect{ 162.0f, 452.0f, 20.0f, 20.0f });

    // The entries are drawn in list order.
    CHECK(selectIndex < backIconIndex);
    CHECK(backIconIndex < backIndex);
    CHECK(backIndex < rightIndex);

    // An empty list draws no prompt at all.
    ui.setPrompts({});
    painter.clearRecorded();
    ui.draw();
    for ( const RecordedCall& call : painter.recorded() )
    {
        const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call);
        CHECK((text == nullptr || text->text != "Select"));
        const RecordedDrawImage* image = std::get_if<RecordedDrawImage>(&call);
        CHECK((image == nullptr || (image->image != 9 && image->image != 10)));
    }
}

TEST_CASE("only a Choice, Slider or Toggle asks for the Change prompt")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.widgets = {
        Button{ "Go", 1, true },
        Slider{ "Volume", "r_test_volume", 0.0, 1.0, 0.1 },
        Toggle{ "Sound", "r_test_sound" },
    };
    addCvar(registry, "r_test_volume", cvars::Value{ 0.5 },
        cvars::Flag::None, 0.0, 1.0);
    addCvar(registry, "r_test_sound", cvars::Value{ true });
    REQUIRE(ui.push(screen) != kInvalidScreen);

    CHECK_FALSE(ui.focusedWidgetChangesValue());       // the button
    tap(ui, platform::Key::Down);
    CHECK(ui.focusedWidgetChangesValue());             // the slider
    tap(ui, platform::Key::Down);
    CHECK(ui.focusedWidgetChangesValue());             // the toggle
}

TEST_CASE("a screen draw callback replaces widget drawing and its event tap runs first")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    int draws = 0;
    std::vector<platform::Event> seen;

    Screen screen;
    screen.title = "Custom";
    screen.widgets = { Button{ "Ignored", 7, true } };
    screen.draw = [&](Ui&, Painter& target, const Size& canvas)
    {
        ++draws;
        CHECK(canvas.height == 480.0f);
        CHECK(canvas.width == 640.0f);
        target.fillRect(Rect{ 1.0f, 2.0f, 3.0f, 4.0f }, Color{ 1.0f, 0.0f, 0.0f, 1.0f });
    };
    screen.event = [&](Ui&, const platform::Event& event, const Point& pointer,
        bool pointerValid) -> bool
    {
        seen.push_back(event);
        CHECK(pointerValid);
        if ( std::holds_alternative<platform::MouseMoved>(event) )
        {
            CHECK(pointer.x == 10.0f);
            CHECK(pointer.y == 20.0f);
        }
        return true;
    };

    REQUIRE(ui.push(screen) != kInvalidScreen);

    ui.handleEvent(platform::Event(mouseEvent(10.0f, 20.0f)));
    REQUIRE(seen.size() == 1u);
    CHECK(std::holds_alternative<platform::MouseMoved>(seen.front()));

    painter.clearRecorded();
    ui.draw();
    CHECK(draws == 1);
    const std::vector<RecordedCall>& calls = painter.recorded();
    REQUIRE(calls.size() == 3u);
    CHECK(std::holds_alternative<RecordedPushClip>(calls[0]));
    REQUIRE(std::holds_alternative<RecordedFillRect>(calls[1]));
    CHECK(std::get<RecordedFillRect>(calls[1]).rect == Rect{ 1.0f, 2.0f, 3.0f, 4.0f });
    CHECK(std::holds_alternative<RecordedPopClip>(calls[2]));

    // The tap consumed the event, so the button did not take focus or accept.
    CHECK(ui.pollEvent().has_value() == false);
    ui.handleEvent(platform::Event(keyEvent(platform::Key::Enter, true)));
    ui.handleEvent(platform::Event(keyEvent(platform::Key::Enter, false)));
    CHECK(ui.isOpen(ui.topScreen()));
}

