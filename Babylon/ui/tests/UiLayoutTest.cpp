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

TEST_CASE("push validation logs one warning per failure")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.bool", false);
    addCvar(registry, "test.text", std::string("x"));
    addCvar(registry, "test.range", std::int64_t{ 0 }, cvars::Flag::None, 0.0, 1.0);
    Ui ui(painter, registry);
    log::CaptureSink sink;

    Screen missing;
    missing.widgets = { Toggle{ "Missing", "test.missing", true } };
    CHECK(ui.push(missing) == kInvalidScreen);
    REQUIRE(sink.records().size() == 1);
    CHECK(sink.records()[0].level == log::Level::Warning);
    sink.clear();

    Screen wrong;
    wrong.widgets = { TextEntry{ "Wrong", "test.bool", "" } };
    CHECK(ui.push(wrong) == kInvalidScreen);
    REQUIRE(sink.records().size() == 1);
    sink.clear();

    Screen badRange;
    badRange.widgets = { Slider{ "Bad", "", 5.0, 1.0, 1.0, true } };
    CHECK(ui.push(badRange) == kInvalidScreen);
    REQUIRE(sink.records().size() == 1);
    sink.clear();

    Screen emptyChoice;
    emptyChoice.widgets = { Choice{ "Empty", "", {}, true } };
    CHECK(ui.push(emptyChoice) == kInvalidScreen);
    REQUIRE(sink.records().size() == 1);
    sink.clear();

    Screen narrowChoice;
    narrowChoice.widgets = { Choice{ "Narrow", "test.range", { "A", "B", "C" }, true } };
    CHECK(ui.push(narrowChoice) == kInvalidScreen);
    REQUIRE(sink.records().size() == 1);
    sink.clear();

    CHECK(ui.screenCount() == 0);
}

TEST_CASE("layout rectangles and the clip stack are exact")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);
    Skin skin;
    skin.rowHeight = 20.0f;
    skin.titleHeight = 30.0f;
    skin.rowSpacing = 4.0f;
    skin.panelPadding = 10.0f;
    skin.minPanelWidth = 100.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));
    CHECK(ui.canvasSize() == Size{ 640.0f, 480.0f });

    Screen screen;
    screen.title = "T";
    screen.widgets = { Label{ "a", "b" }, Button{ "Go", 1, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    ui.draw();
    const std::vector<RecordedCall>& calls = painter.recorded();

    const RecordedFillRect* panel = nullptr;
    const RecordedFillRect* focus = nullptr;
    const RecordedDrawText* title = nullptr;
    std::size_t pushes = 0;
    std::size_t pops = 0;
    for ( const RecordedCall& call : calls )
    {
        if ( const RecordedFillRect* fill = std::get_if<RecordedFillRect>(&call) )
        {
            if ( fill->rect == Rect{ 270.0f, 191.0f, 100.0f, 98.0f } )
            {
                panel = fill;
            }
            if ( fill->rect == Rect{ 280.0f, 259.0f, 80.0f, 20.0f } )
            {
                focus = fill;
            }
        }
        else if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
        {
            if ( text->text == "T" )
            {
                title = text;
            }
        }
        else if ( std::get_if<RecordedPushClip>(&call) != nullptr )
        {
            ++pushes;
        }
        else if ( std::get_if<RecordedPopClip>(&call) != nullptr )
        {
            ++pops;
        }
    }

    REQUIRE(panel != nullptr);
    CHECK(panel->color == skin.panel);
    REQUIRE(focus != nullptr);
    CHECK(focus->color == skin.highlight);
    REQUIRE(title != nullptr);
    CHECK(title->rect == Rect{ 280.0f, 201.0f, 80.0f, 30.0f });
    CHECK(pushes == 2);
    CHECK(pops == 2);
}

TEST_CASE("panel placement is exact for the center and bottom-left layouts")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);

    Skin skin;
    skin.rowHeight = 20.0f;
    skin.titleHeight = 0.0f;
    skin.rowSpacing = 0.0f;
    skin.panelPadding = 10.0f;
    skin.minPanelWidth = 160.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    skin.panel = Color{ 0.1f, 0.2f, 0.3f, 1.0f };
    REQUIRE(ui.setSkin(skin));

    Screen screen;
    screen.widgets = { Button{ "Go", 1, true } };
    REQUIRE(ui.setViewport(640, 480));
    REQUIRE(ui.push(screen) != kInvalidScreen);

    const auto panelRect = [&painter, &ui, &skin]() -> Rect
    {
        ui.draw();
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedFillRect* fill = std::get_if<RecordedFillRect>(&call) )
            {
                if ( fill->color == skin.panel )
                {
                    return fill->rect;
                }
            }
        }
        return Rect{};
    };

    // 640 x 480: the panel is 160 x 40; centered or in the lower left at margin * 4.
    CHECK(panelRect() == Rect{ 240.0f, 220.0f, 160.0f, 40.0f });

    Skin bottomLeft = skin;
    bottomLeft.placement = PanelPlacement::BottomLeft;
    REQUIRE(ui.setSkin(bottomLeft));
    painter.clearRecorded();
    CHECK(panelRect() == Rect{ 32.0f, 408.0f, 160.0f, 40.0f });

    // 1280 x 720: the canvas is 853.33 wide, so the centered panel moves but the lower-left one
    // keeps its distance from the left and bottom edges.
    REQUIRE(ui.setViewport(1280, 720));
    painter.clearRecorded();
    const Rect wideBottomLeft = panelRect();
    const float expectedX = 32.0f;
    CHECK(wideBottomLeft.x == expectedX);
    CHECK(wideBottomLeft.y == 408.0f);
    CHECK(wideBottomLeft.width == 160.0f);
    CHECK(wideBottomLeft.height == 40.0f);

    Skin centered = skin;
    REQUIRE(ui.setSkin(centered));
    painter.clearRecorded();
    const Rect wideCentered = panelRect();
    CHECK(wideCentered.y == 220.0f);
    CHECK(wideCentered.width == 160.0f);
    CHECK(wideCentered.height == 40.0f);
    CHECK(wideCentered.x > 300.0f);
}

TEST_CASE("a top panel keeps the title and row column fixed across screens and help")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);

    Skin skin;
    skin.placement = PanelPlacement::Top;
    skin.fixedPanelWidth = 0.5f;
    skin.topMargin = 12.0f;
    skin.titleHeight = 24.0f;
    skin.rowHeight = 20.0f;
    skin.rowSpacing = 4.0f;
    skin.panelPadding = 10.0f;
    skin.minPanelWidth = 80.0f;
    skin.canvasMargin = 8.0f;
    skin.borderWidth = 0.0f;
    skin.helpColumnWidth = 0.35f;
    skin.panel = Color{ 0.1f, 0.2f, 0.3f, 1.0f };
    skin.panelBorder = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlightUnfocused = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    struct Placement
    {
        Rect panel;
        Rect title;
        Rect label;
        Rect value;
    };

    auto draw = [&painter, &ui, &skin](const Screen& screen,
        const char* pTitle, const char* pLabel, const char* pValue)
    {
        REQUIRE(ui.push(screen) != kInvalidScreen);
        painter.clearRecorded();
        ui.draw();

        Placement placement{};
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedFillRect* fill = std::get_if<RecordedFillRect>(&call) )
            {
                if ( fill->color == skin.panel )
                {
                    placement.panel = fill->rect;
                }
            }
            else if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == pTitle )
                {
                    placement.title = text->rect;
                }
                else if ( text->text == pLabel )
                {
                    placement.label = text->rect;
                }
                else if ( text->text == pValue )
                {
                    placement.value = text->rect;
                }
            }
        }

        REQUIRE(ui.pop() > 0);
        return placement;
    };

    Screen shortScreen;
    shortScreen.title = "One";
    shortScreen.widgets = { Label{ "Alpha", "11" } };

    Screen longScreen;
    longScreen.title = "Two";
    longScreen.widgets = {
        Label{ "A much longer label", "222" },
        Button{ "Next", 1, true },
    };

    Screen helpScreen;
    helpScreen.title = "Help";
    helpScreen.widgets = { Label{ "Alpha", "11" } };
    helpScreen.help = { "Some help text." };

    const Placement shortPlacement = draw(shortScreen, "One", "Alpha", "11");
    const Placement longPlacement = draw(longScreen, "Two", "A much longer label", "222");
    const Placement helpPlacement = draw(helpScreen, "Help", "Alpha", "11");

    // The fixed width is the same fraction of the canvas whatever the content, and the panel is
    // centered and anchored at topMargin.
    for ( const Placement* pPlacement :
        { &shortPlacement, &longPlacement, &helpPlacement } )
    {
        CHECK(pPlacement->panel.width == 320.0f);
        CHECK(pPlacement->panel.x == 160.0f);
        CHECK(pPlacement->panel.y == 12.0f);
        CHECK(pPlacement->title.y == 22.0f);
        CHECK(pPlacement->label.x == 170.0f);
        CHECK(pPlacement->value.x + pPlacement->value.width == 361.0f);
    }
    CHECK(shortPlacement.label.x == helpPlacement.label.x);
    CHECK(shortPlacement.value.x + shortPlacement.value.width
        == helpPlacement.value.x + helpPlacement.value.width);
    CHECK(longPlacement.label.x == shortPlacement.label.x);
    CHECK(longPlacement.value.x + longPlacement.value.width
        == shortPlacement.value.x + shortPlacement.value.width);
}

TEST_CASE("a negative prompt height is rejected with the rest of the skin")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Skin skin;
    skin.promptHeight = -1.0f;
    CHECK_FALSE(ui.setSkin(skin));
    CHECK(ui.skin().promptHeight == 20.0f);
}

TEST_CASE("a long screen scrolls whole rows above the prompt bar and marks what is hidden")
{
    NullPainter painter(NullPainterDesc{ 8.0f, 16.0f, true });
    cvars::Registry registry;
    Ui ui(painter, registry);

    Skin skin;
    skin.titleHeight = 0.0f;
    skin.rowHeight = 30.0f;
    skin.rowSpacing = 0.0f;
    skin.panelPadding = 0.0f;
    skin.minPanelWidth = 100.0f;
    skin.canvasMargin = 10.0f;
    skin.borderWidth = 0.0f;
    skin.promptHeight = 30.0f;
    skin.panel = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlight = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    for ( int row = 0; row < 20; ++row )
    {
        screen.widgets.emplace_back(Button{ "Row " + std::to_string(row), 1, true });
    }
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // The prompt bar sits at the canvas bottom minus the margin and its height.
    constexpr float kPromptTop = 480.0f - 10.0f - 30.0f;
    ui.setPrompts({ Prompt{ 0, "Select" } });

    auto rowCalls = [&painter]() -> std::vector<const RecordedDrawText*>
    {
        std::vector<const RecordedDrawText*> rows;
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text.rfind("Row ", 0) == 0 )
                {
                    rows.push_back(text);
                }
            }
        }
        return rows;
    };

    auto hasText = [&painter](const std::string& value)
    {
        for ( const RecordedCall& call : painter.recorded() )
        {
            if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
            {
                if ( text->text == value )
                {
                    return true;
                }
            }
        }
        return false;
    };

    ui.draw();
    std::vector<const RecordedDrawText*> rows = rowCalls();
    REQUIRE(rows.size() == 14u);
    CHECK(rows.front()->text == "Row 0");
    CHECK(rows.back()->text == "Row 13");
    for ( const RecordedDrawText* row : rows )
    {
        CHECK(row->rect.y + row->rect.height <= kPromptTop + 0.01f);
    }
    CHECK_FALSE(hasText("\xE2\x96\xB2"));
    CHECK(hasText("\xE2\x96\xBC"));

    // Moving focus to the last row scrolls by whole rows: rows 6..19 fill the window, the last
    // row is fully visible and there is nothing left below it.
    for ( int step = 0; step < 19; ++step )
    {
        tap(ui, platform::Key::Down);
    }
    CHECK(ui.focusedWidget() == 19u);
    painter.clearRecorded();
    ui.draw();
    rows = rowCalls();
    REQUIRE(rows.size() == 14u);
    CHECK(rows.front()->text == "Row 6");
    CHECK(rows.back()->text == "Row 19");
    for ( const RecordedDrawText* row : rows )
    {
        CHECK(row->rect.y >= skin.canvasMargin - 0.01f);
        CHECK(row->rect.y + row->rect.height <= kPromptTop + 0.01f);
    }
    CHECK(hasText("\xE2\x96\xB2"));
    CHECK_FALSE(hasText("\xE2\x96\xBC"));

    // Pointer hit-testing uses the scrolled position: clicking Row 10 focuses it.
    Rect rowTen;
    bool found = false;
    for ( const RecordedDrawText* row : rows )
    {
        if ( row->text == "Row 10" )
        {
            rowTen = row->rect;
            found = true;
        }
    }
    REQUIRE(found);
    ui.handleEvent(platform::Event(canvasMouse(rowTen.x + rowTen.width * 0.5f,
        rowTen.y + rowTen.height * 0.5f)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    CHECK(ui.focusedWidget() == 10u);
}

