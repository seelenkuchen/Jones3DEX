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

TEST_CASE("pointer input is ignored before setViewport and hits after it")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Pointer";
    screen.widgets = { Button{ "One", 1, true }, Button{ "Two", 2, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);
    CHECK(ui.focusedWidget() == 0);

    ui.handleEvent(platform::Event(canvasMouse(222.0f, 268.0f)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    CHECK(ui.focusedWidget() == 0);

    REQUIRE(ui.setViewport(640, 480));
    ui.handleEvent(platform::Event(canvasMouse(222.0f, 268.0f)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    CHECK(ui.focusedWidget() == 1);

    // A second click on the now focused widget accepts it.
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    const std::vector<UiEvent> events = drain(ui);
    const ActionPressed* action = findEvent<ActionPressed>(events);
    REQUIRE(action != nullptr);
    CHECK(action->action == 2);
}

TEST_CASE("gamepad axis repeats after the fixed delay")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    Screen screen = twoButtons();
    REQUIRE(ui.push(screen) != kInvalidScreen);
    CHECK(ui.focusedWidget() == 0);

    ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::LeftY, -1.0f)));
    CHECK(ui.focusedWidget() == 2);

    // Before the initial delay nothing repeats.
    ui.update(0.2);
    CHECK(ui.focusedWidget() == 2);

    // One repeat after the initial delay moves focus again.
    ui.update(0.2);
    CHECK(ui.focusedWidget() == 1);

    ui.update(0.11);
    CHECK(ui.focusedWidget() == 0);
}

TEST_CASE("Q, E and the shoulder buttons report a tab change and wrap")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.tabs = { "One", "Two", "Three" };
    screen.widgets = { Button{ "Go", 1, true } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    tap(ui, platform::Key::E);
    std::vector<UiEvent> events = drain(ui);
    const TabChanged* changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->screen == id);
    CHECK(changed->tab == 1u);

    tap(ui, platform::Key::Q);
    events = drain(ui);
    changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->tab == 0u);

    // Previous from the first tab wraps to the last.
    tap(ui, platform::Key::Q);
    events = drain(ui);
    changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->tab == 2u);

    ui.handleEvent(platform::Event(padButtonEvent(
        platform::GamepadButton::RightShoulder, true)));
    events = drain(ui);
    changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->tab == 0u);

    ui.handleEvent(platform::Event(padButtonEvent(
        platform::GamepadButton::LeftShoulder, true)));
    events = drain(ui);
    changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->tab == 2u);

    // A screen without tabs ignores the tab inputs.
    Screen plain;
    plain.widgets = { Button{ "Go", 1, true } };
    REQUIRE(ui.push(plain) != kInvalidScreen);
    tap(ui, platform::Key::E);
    CHECK_FALSE(findEvent<TabChanged>(drain(ui)) != nullptr);
}

TEST_CASE("a click on a tab label reports the tab change")
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
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.tabs = { "One", "Two" };
    screen.widgets = { Button{ "Go", 1, true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);
    ui.draw();

    Rect second;
    bool found = false;
    for ( const RecordedCall& call : painter.recorded() )
    {
        if ( const RecordedDrawText* text = std::get_if<RecordedDrawText>(&call) )
        {
            if ( text->text == "Two" )
            {
                second = text->rect;
                found = true;
            }
        }
    }
    REQUIRE(found);

    ui.handleEvent(platform::Event(canvasMouse(
        second.x + second.width * 0.5f, second.y + second.height * 0.5f)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, true)));
    ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Left, false)));
    const std::vector<UiEvent> events = drain(ui);
    const TabChanged* changed = findEvent<TabChanged>(events);
    REQUIRE(changed != nullptr);
    CHECK(changed->tab == 1u);
}

TEST_CASE("a consumed key-up does not leave the key down for the next screen")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen custom;
    custom.title = "Custom";
    custom.event = [](Ui&, const platform::Event&, const Point&, bool) { return true; };
    REQUIRE(ui.push(custom) != kInvalidScreen);

    tap(ui, platform::Key::Enter);
    ui.pop();

    REQUIRE(ui.push(twoButtons(0)) != kInvalidScreen);
    tap(ui, platform::Key::Enter);
    const std::vector<UiEvent> events = drain(ui);
    const ActionPressed* pAction = findEvent<ActionPressed>(events);
    REQUIRE(pAction != nullptr);
    CHECK(pAction->action == 1);
}

