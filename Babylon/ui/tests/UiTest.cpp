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
#include <variant>
#include <vector>

namespace
{
    using namespace babylon;
    using namespace babylon::ui;

    constexpr platform::WindowId kWindow = 1;

    platform::KeyChanged keyEvent(platform::Key key, bool down, bool repeat = false)
    {
        return platform::KeyChanged{ kWindow, key, down, repeat };
    }

    platform::MouseMoved mouseEvent(float x, float y)
    {
        return platform::MouseMoved{ kWindow, x, y, 0.0f, 0.0f };
    }

    platform::MouseButtonChanged mouseButtonEvent(platform::MouseButton button, bool down)
    {
        return platform::MouseButtonChanged{ kWindow, button, down };
    }

    platform::MouseWheelMoved wheelEvent(float dy)
    {
        return platform::MouseWheelMoved{ kWindow, 0.0f, dy };
    }

    platform::GamepadButtonChanged padButtonEvent(platform::GamepadButton button, bool down)
    {
        return platform::GamepadButtonChanged{ 1, button, down };
    }

    platform::GamepadAxisMoved padAxisEvent(platform::GamepadAxis axis, float value)
    {
        return platform::GamepadAxisMoved{ 1, axis, value };
    }

    void tap(Ui& ui, platform::Key key)
    {
        ui.handleEvent(platform::Event(keyEvent(key, true)));
        ui.handleEvent(platform::Event(keyEvent(key, false)));
    }

    void addCvar(cvars::Registry& registry, const std::string& name,
        cvars::Value value, cvars::Flag flags = cvars::Flag::None,
        std::optional<double> min = std::nullopt,
        std::optional<double> max = std::nullopt)
    {
        cvars::Desc desc;
        desc.name = name;
        desc.defaultValue = std::move(value);
        desc.flags = flags;
        desc.min = min;
        desc.max = max;
        REQUIRE(registry.add(desc));
    }

    std::vector<UiEvent> drain(Ui& ui)
    {
        std::vector<UiEvent> events;
        while ( std::optional<UiEvent> event = ui.pollEvent() )
        {
            events.push_back(std::move(*event));
        }
        return events;
    }

    template <class T>
    const T* findEvent(const std::vector<UiEvent>& events)
    {
        for ( const UiEvent& event : events )
        {
            if ( const T* found = std::get_if<T>(&event) )
            {
                return found;
            }
        }
        return nullptr;
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

    Screen twoButtons(std::size_t initialFocus = kNoWidget)
    {
        Screen screen;
        screen.title = "Actions";
        screen.widgets = {
            Button{ "First", 1, true },
            Button{ "Second", 2, true },
            Button{ "Third", 3, true },
        };
        screen.initialFocus = initialFocus;
        return screen;
    }

    // Canvas point to window pixel for a 640x480 window: the scale is 1.
    platform::MouseMoved canvasMouse(float x, float y)
    {
        return mouseEvent(x, y);
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

TEST_CASE("binding capture handles key, mouse, gamepad button and axis")
{
    SUBCASE("key")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Key", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(keyEvent(platform::Key::A, true)));
        const std::vector<UiEvent> events = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.kind == BindingKind::Key);
        CHECK(captured->binding.key == platform::Key::A);
    }

    SUBCASE("mouse button")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Mouse", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(mouseButtonEvent(platform::MouseButton::Right, true)));
        const std::vector<UiEvent> events = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.kind == BindingKind::MouseButton);
        CHECK(captured->binding.mouseButton == platform::MouseButton::Right);
    }

    SUBCASE("gamepad button")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Pad", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(padButtonEvent(platform::GamepadButton::West, true)));
        const std::vector<UiEvent> events = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.kind == BindingKind::GamepadButton);
        CHECK(captured->binding.button == platform::GamepadButton::West);
    }

    SUBCASE("axis and back cancel")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Axis", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::LeftX, 0.9f)));
        std::vector<UiEvent> events = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.kind == BindingKind::GamepadAxis);
        CHECK(captured->binding.axis == platform::GamepadAxis::LeftX);
        CHECK(captured->binding.axisDirection == 1.0f);

        // Back cancels a capture without reporting or closing the screen.
        tap(ui, platform::Key::Enter);
        tap(ui, platform::Key::Escape);
        events = drain(ui);
        CHECK(findEvent<BindingCaptured>(events) == nullptr);
        CHECK(findEvent<ScreenClosed>(events) == nullptr);
        CHECK(ui.topScreen() != kInvalidScreen);
    }
}

TEST_CASE("keys held when capture starts are ignored")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);
    Screen screen;
    screen.title = "Bind";
    screen.widgets = { BindingCapture{ "Key", true } };
    REQUIRE(ui.push(screen) != kInvalidScreen);

    // A is already down when capture begins.
    ui.handleEvent(platform::Event(keyEvent(platform::Key::A, true)));
    ui.handleEvent(platform::Event(keyEvent(platform::Key::Enter, true)));
    ui.handleEvent(platform::Event(keyEvent(platform::Key::A, true)));
    std::vector<UiEvent> events = drain(ui);
    CHECK(findEvent<BindingCaptured>(events) == nullptr);

    ui.handleEvent(platform::Event(keyEvent(platform::Key::A, false)));
    ui.handleEvent(platform::Event(keyEvent(platform::Key::B, true)));
    events = drain(ui);
    const BindingCaptured* captured = findEvent<BindingCaptured>(events);
    REQUIRE(captured != nullptr);
    CHECK(captured->binding.key == platform::Key::B);
}

TEST_CASE("text entry accepts the default, edits and restores on back")
{
    NullPainter painter;
    cvars::Registry registry;
    addCvar(registry, "test.name", std::string("Alice"));
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Name";
    screen.widgets = { TextEntry{ "Name", "test.name", "Default" } };
    screen.backResult = 7;
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    tap(ui, platform::Key::Enter);
    std::vector<UiEvent> events = drain(ui);
    const TextSubmitted* submitted = findEvent<TextSubmitted>(events);
    REQUIRE(submitted != nullptr);
    CHECK(submitted->text == "Alice");
    CHECK(registry.getString("test.name").value() == "Alice");
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Alice");

    // Edit the text: append "b", accept, and read it back.
    tap(ui, platform::Key::B);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Aliceb");
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    submitted = findEvent<TextSubmitted>(events);
    REQUIRE(submitted != nullptr);
    CHECK(submitted->text == "Aliceb");
    CHECK(registry.getString("test.name").value() == "Aliceb");

    // A new edit is restored by back; the screen stays open.
    tap(ui, platform::Key::C);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Alicebc");
    tap(ui, platform::Key::Escape);
    events = drain(ui);
    CHECK(findEvent<ScreenClosed>(events) == nullptr);
    CHECK(ui.topScreen() == id);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Aliceb");

    // With nothing left to restore, back closes with the screen's result.
    tap(ui, platform::Key::Escape);
    events = drain(ui);
    const ScreenClosed* closed = findEvent<ScreenClosed>(events);
    REQUIRE(closed != nullptr);
    CHECK(closed->screen == id);
    CHECK(closed->result == 7);
}

TEST_CASE("unbound text entry accepts its default without typing")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Name";
    screen.widgets = { TextEntry{ "Name", "", "Bob" } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    tap(ui, platform::Key::Enter);
    const std::vector<UiEvent> events = drain(ui);
    const TextSubmitted* submitted = findEvent<TextSubmitted>(events);
    REQUIRE(submitted != nullptr);
    CHECK(submitted->text == "Bob");
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Bob");
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
