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

    platform::MouseWheelMoved wheelEvent(float dy)
    {
        return platform::MouseWheelMoved{ kWindow, 0.0f, dy };
    }

    std::size_t countSounds(const std::vector<UiEvent>& events, std::uint32_t cue)
    {
        std::size_t count = 0;
        for ( const UiEvent& event : events )
        {
            if ( const SoundCue* sound = std::get_if<SoundCue>(&event) )
            {
                count += sound->cue == cue ? 1 : 0;
            }
        }
        return count;
    }

}

TEST_CASE("focus skips labels and disabled rows and honors initialFocus")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Focus";
    screen.widgets = {
        Label{ "A label", "" },
        Button{ "Disabled", 1, false },
        Button{ "Enabled", 2, true },
        Button{ "Also enabled", 3, true },
        Label{ "Another label", "" },
    };

    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);
    CHECK(ui.focusedWidget() == 2);

    tap(ui, platform::Key::Down);
    CHECK(ui.focusedWidget() == 3);
    tap(ui, platform::Key::Down);
    CHECK(ui.focusedWidget() == 2);
    tap(ui, platform::Key::Up);
    CHECK(ui.focusedWidget() == 3);

    const ScreenId second = ui.push(twoButtons(1));
    REQUIRE(second != kInvalidScreen);
    CHECK(ui.focusedWidget() == 1);

    ui.pop();
    const ScreenId third = ui.push(twoButtons(0));
    REQUIRE(third != kInvalidScreen);
    CHECK(ui.focusedWidget() == 0);

    Screen fallback;
    fallback.title = "Fallback";
    fallback.widgets = { Label{ "Label", "" }, Button{ "Disabled", 1, false },
        Button{ "First enabled", 2, true } };
    fallback.initialFocus = 1;
    REQUIRE(ui.push(fallback) != kInvalidScreen);
    CHECK(ui.focusedWidget() == 2);
}

TEST_CASE("bound and unbound value widgets write cvars and report ValueChanged")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.toggle", false);
    addCvar(registry, "test.slider", std::int64_t{ 5 }, cvars::Flag::None, 0.0, 10.0);
    addCvar(registry, "test.float", 1.0, cvars::Flag::None, 0.0, 2.0);
    addCvar(registry, "test.choice", std::int64_t{ 0 }, cvars::Flag::None, 0.0, 2.0);
    addCvar(registry, "test.string_choice", std::string("B"));
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Values";
    screen.widgets = {
        Toggle{ "Toggle", "test.toggle", true },
        Slider{ "Slider", "test.slider", 0.0, 10.0, 1.0, true },
        Slider{ "Float", "test.float", 0.0, 2.0, 0.5, true },
        Choice{ "Choice", "test.choice", { "A", "B", "C" }, true },
        Choice{ "String", "test.string_choice", { "A", "B", "C" }, true },
        Toggle{ "Local", "", true },
        Slider{ "Local slider", "", 0.0, 5.0, 1.0, true },
        Choice{ "Local choice", "", { "x", "y", "z" }, true },
    };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    tap(ui, platform::Key::Enter);
    std::vector<UiEvent> events = drain(ui);
    const ValueChanged* changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->screen == id);
    CHECK(changed->widget == 0);
    CHECK(std::get<bool>(changed->value));
    CHECK(registry.getBool("test.toggle").value());

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 1);
    CHECK(std::get<double>(changed->value) == doctest::Approx(6.0));
    CHECK(registry.getInt("test.slider").value() == 6);

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 2);
    CHECK(std::get<double>(changed->value) == doctest::Approx(1.5));
    CHECK(registry.getFloat("test.float").value() == doctest::Approx(1.5));

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 3);
    CHECK(std::get<std::int64_t>(changed->value) == 1);
    CHECK(registry.getInt("test.choice").value() == 1);

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 4);
    CHECK(std::get<std::int64_t>(changed->value) == 2);
    CHECK(registry.getString("test.string_choice").value() == "C");

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 5);
    CHECK(std::get<bool>(changed->value));
    CHECK(std::get<bool>(ui.widgetValue(id, 5).value()));

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 6);
    CHECK(std::get<double>(changed->value) == doctest::Approx(1.0));
    CHECK(std::get<double>(ui.widgetValue(id, 6).value()) == doctest::Approx(1.0));

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 7);
    CHECK(std::get<std::int64_t>(changed->value) == 1);
    CHECK(std::get<std::int64_t>(ui.widgetValue(id, 7).value()) == 1);
}

TEST_CASE("unbound value widgets start from their declared values")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Initial values";
    screen.widgets = {
        Toggle{ "Toggle", "", true, true },
        Slider{ "Slider", "", 0.0, 10.0, 1.0, true, 4.0 },
        Slider{ "Clamped", "", 0.0, 10.0, 1.0, true, 20.0 },
        Choice{ "Choice", "", { "A", "B", "C" }, true, 2 },
        Choice{ "Clamped choice", "", { "A", "B" }, true, 7 },
    };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    CHECK(std::get<bool>(ui.widgetValue(id, 0).value()));
    CHECK(std::get<double>(ui.widgetValue(id, 1).value()) == doctest::Approx(4.0));
    CHECK(std::get<double>(ui.widgetValue(id, 2).value()) == doctest::Approx(10.0));
    CHECK(std::get<std::int64_t>(ui.widgetValue(id, 3).value()) == 2);
    CHECK(std::get<std::int64_t>(ui.widgetValue(id, 4).value()) == 1);

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    CHECK(std::get<double>(ui.widgetValue(id, 1).value()) == doctest::Approx(5.0));

    drain(ui);
}

TEST_CASE("a ReadOnly cvar is skipped by focus and drawn disabled")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.readonly", true, cvars::Flag::ReadOnly);
    Ui ui(painter, registry);
    Skin skin;
    skin.textDisabled = Color{ 1.0f, 0.0f, 0.0f, 1.0f };
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.title = "ReadOnly";
    screen.widgets = {
        Toggle{ "ReadOnly", "test.readonly", true },
        Button{ "Action", 1, true },
    };
    REQUIRE(ui.push(screen) != kInvalidScreen);
    CHECK(ui.focusedWidget() == 1);

    ui.draw();
    bool foundDisabledText = false;
    for ( const RecordedCall& call : painter.recorded() )
    {
        if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
        {
            if ( text->text == "ReadOnly" && text->color == skin.textDisabled )
            {
                foundDisabledText = true;
            }
        }
    }
    CHECK(foundDisabledText);
    CHECK(registry.getBool("test.readonly").value());
}

TEST_CASE("slider range is the intersection with the cvar range")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.range", std::int64_t{ 5 }, cvars::Flag::None, 2.0, 8.0);
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Range";
    screen.widgets = { Slider{ "Range", "test.range", 0.0, 10.0, 1.0, true } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    for ( int i = 0; i < 20; ++i )
    {
        tap(ui, platform::Key::Left);
    }
    CHECK(registry.getInt("test.range").value() == 2);
    CHECK(std::get<double>(ui.widgetValue(id, 0).value()) == doctest::Approx(2.0));

    for ( int i = 0; i < 20; ++i )
    {
        tap(ui, platform::Key::Right);
    }
    CHECK(registry.getInt("test.range").value() == 8);
    CHECK(std::get<double>(ui.widgetValue(id, 0).value()) == doctest::Approx(8.0));
}

TEST_CASE("int slider steps are whole and report the stored value")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.auto", std::int64_t{ 0 }, cvars::Flag::None, 0.0, 1000.0);
    addCvar(registry, "test.fine", std::int64_t{ 0 }, cvars::Flag::None, 0.0, 10.0);
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Steps";
    screen.widgets = {
        Slider{ "Auto", "test.auto", 0.0, 1000.0, 0.0, true },
        Slider{ "Fine", "test.fine", 0.0, 10.0, 0.4, true },
        Slider{ "Local", "", 0.0, 10.0, 0.4, true },
    };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    // The automatic step is one hundredth of the range: 10 over 0..1000.
    tap(ui, platform::Key::Right);
    std::vector<UiEvent> events = drain(ui);
    const ValueChanged* changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 0);
    CHECK(std::get<double>(changed->value) == doctest::Approx(10.0));
    CHECK(registry.getInt("test.auto").value() == 10);
    CHECK(std::get<double>(ui.widgetValue(id, 0).value()) == doctest::Approx(10.0));

    // A fractional step still advances an int cvar by one each press.
    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 1);
    CHECK(std::get<double>(changed->value) == doctest::Approx(1.0));
    CHECK(registry.getInt("test.fine").value() == 1);

    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(std::get<double>(changed->value) == doctest::Approx(2.0));
    CHECK(registry.getInt("test.fine").value() == 2);

    // An unbound slider stores and reports the same value.
    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Right);
    events = drain(ui);
    changed = findEvent<ValueChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->widget == 2);
    CHECK(std::get<double>(changed->value) == doctest::Approx(0.4));
    CHECK(std::get<double>(ui.widgetValue(id, 2).value()) == doctest::Approx(0.4));
}

TEST_CASE("list selection reports SelectionChanged and the wheel does not select")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.title = "List";
    List list;
    list.text = "";
    for ( std::int64_t i = 0; i < 8; ++i )
    {
        list.items.push_back(ListItem{ "Row " + std::to_string(i), 0, i });
    }
    list.action = 9;
    screen.widgets = { list };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    tap(ui, platform::Key::Down);
    std::vector<UiEvent> events = drain(ui);
    const SelectionChanged* selection = findEvent<SelectionChanged>(events);
    REQUIRE(selection != nullptr);
    CHECK(selection->widget == 0);
    CHECK(selection->row == 1);
    CHECK(selection->id == 1);

    // A wheel over the list scrolls its window but keeps the selection.
    ui.handleEvent(platform::Event(canvasMouse(250.0f, 230.0f)));
    ui.handleEvent(platform::Event(wheelEvent(-1.0f)));
    events = drain(ui);
    CHECK(findEvent<SelectionChanged>(events) == nullptr);
    CHECK(ui.widgetValue(id, 0) == std::nullopt);

    // A click on a row selects it and reports the change.
    ui.handleEvent(platform::Event(canvasMouse(250.0f, 230.0f)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    events = drain(ui);
    selection = findEvent<SelectionChanged>(events);
    REQUIRE(selection != nullptr);
    CHECK(selection->row == 2);

    tap(ui, platform::Key::Enter);
    events = drain(ui);
    const ActionPressed* action = findEvent<ActionPressed>(events);
    REQUIRE(action != nullptr);
    CHECK(action->action == 9);
    CHECK(action->item == 2);
}

TEST_CASE("up and down leave a list past its first and last row")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.title = "Saves";
    List list;
    for ( std::int64_t i = 0; i < 2; ++i )
    {
        list.items.push_back(ListItem{ "Row " + std::to_string(i), 0, i });
    }
    list.action = 9;
    screen.widgets = { Button{ "Above", 1, true }, list, Button{ "Below", 2, true } };
    screen.initialFocus = 1;
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // Down moves through the rows, then on to the widget below the list.
    tap(ui, platform::Key::Down);
    std::vector<UiEvent> events = drain(ui);
    const SelectionChanged* selection = findEvent<SelectionChanged>(events);
    REQUIRE(selection != nullptr);
    CHECK(selection->row == 1);

    tap(ui, platform::Key::Down);
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    const ActionPressed* action = findEvent<ActionPressed>(events);
    REQUIRE(action != nullptr);
    CHECK(action->action == 2);

    // Up re-enters the list on its kept row, moves up through it and leaves it above.
    tap(ui, platform::Key::Up);
    tap(ui, platform::Key::Up);
    tap(ui, platform::Key::Up);
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    action = findEvent<ActionPressed>(events);
    REQUIRE(action != nullptr);
    CHECK(action->action == 1);

    // The pad's d-pad does the same as the arrow keys.
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, true)));
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, false)));
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, true)));
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, false)));
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, true)));
    ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::DpadDown, false)));
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    action = findEvent<ActionPressed>(events);
    REQUIRE(action != nullptr);
    CHECK(action->action == 2);
}

TEST_CASE("back and replace order ScreenClosed events")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen first;
    first.title = "First";
    first.widgets = { Button{ "Close", 1, true } };
    first.backResult = 10;
    Screen second;
    second.title = "Second";
    second.widgets = { Button{ "Close", 2, true } };
    second.backResult = 20;

    const ScreenId a = ui.push(first);
    const ScreenId b = ui.push(second);
    REQUIRE(a != kInvalidScreen);
    REQUIRE(b != kInvalidScreen);

    tap(ui, platform::Key::Escape);
    std::vector<UiEvent> events = drain(ui);
    const ScreenClosed* closed = findEvent<ScreenClosed>(events);
    REQUIRE(closed != nullptr);
    CHECK(closed->screen == b);
    CHECK(closed->result == 20);
    CHECK(ui.topScreen() == a);

    const ScreenId c = ui.replace(first, 30);
    REQUIRE(c != kInvalidScreen);
    events = drain(ui);
    closed = findEvent<ScreenClosed>(events);
    REQUIRE(closed != nullptr);
    CHECK(closed->screen == a);
    CHECK(closed->result == 30);
    CHECK(ui.topScreen() == c);

    CHECK(ui.pop(40));
    events = drain(ui);
    closed = findEvent<ScreenClosed>(events);
    REQUIRE(closed != nullptr);
    CHECK(closed->screen == c);
    CHECK(closed->result == 40);
    CHECK_FALSE(ui.pop());
}

TEST_CASE("setWidget keeps focus and moves it off a non-focusable replacement")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Update";
    screen.widgets = { Button{ "First", 1, true }, Label{ "Middle", "" },
        Button{ "Last", 3, true } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);
    CHECK(ui.focusedWidget() == 0);

    REQUIRE(ui.setWidget(id, 0, Button{ "Changed", 4, true }));
    CHECK(ui.focusedWidget() == 0);
    CHECK(ui.widgetValue(id, 0) == std::nullopt);

    REQUIRE(ui.setWidget(id, 0, Label{ "Now a label", "" }));
    CHECK(ui.focusedWidget() == 2);

    REQUIRE(ui.setWidget(id, 1, Label{ "Stats", "42" }));
    CHECK(ui.focusedWidget() == 2);
}

TEST_CASE("sound cues report focus, accept and back")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    Skin skin;
    skin.focusSound = 11;
    skin.acceptSound = 12;
    skin.backSound = 13;
    REQUIRE(ui.setSkin(skin));

    Screen screen = twoButtons();
    screen.backResult = 1;
    REQUIRE(ui.push(screen) != kInvalidScreen);
    drain(ui);

    tap(ui, platform::Key::Down);
    std::vector<UiEvent> events = drain(ui);
    CHECK(countSounds(events, 11) == 1);

    tap(ui, platform::Key::Enter);
    events = drain(ui);
    CHECK(countSounds(events, 12) == 1);
    REQUIRE(findEvent<ActionPressed>(events) != nullptr);

    tap(ui, platform::Key::Escape);
    events = drain(ui);
    CHECK(countSounds(events, 13) == 1);
    REQUIRE(findEvent<ScreenClosed>(events) != nullptr);
}

