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

    platform::TextInput textEvent(std::string text)
    {
        return platform::TextInput{ kWindow, std::move(text) };
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

TEST_CASE("binding capture accepts every gamepad axis")
{
    auto captureAxis = [](platform::GamepadAxis axis, float value)
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Axis", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(padAxisEvent(axis, value)));
        return drain(ui);
    };

    SUBCASE("right stick X reports both directions")
    {
        const std::vector<UiEvent> right = captureAxis(platform::GamepadAxis::RightX, 0.9f);
        const BindingCaptured* captured = findEvent<BindingCaptured>(right);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.kind == BindingKind::GamepadAxis);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightX);
        CHECK(captured->binding.axisDirection == 1.0f);

        const std::vector<UiEvent> left = captureAxis(platform::GamepadAxis::RightX, -0.9f);
        captured = findEvent<BindingCaptured>(left);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightX);
        CHECK(captured->binding.axisDirection == -1.0f);
    }

    SUBCASE("right stick Y reports both directions")
    {
        const std::vector<UiEvent> up = captureAxis(platform::GamepadAxis::RightY, 0.9f);
        const BindingCaptured* captured = findEvent<BindingCaptured>(up);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightY);
        CHECK(captured->binding.axisDirection == 1.0f);

        const std::vector<UiEvent> down = captureAxis(platform::GamepadAxis::RightY, -0.9f);
        captured = findEvent<BindingCaptured>(down);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightY);
        CHECK(captured->binding.axisDirection == -1.0f);
    }

    SUBCASE("the triggers report their positive direction")
    {
        std::vector<UiEvent> events = captureAxis(platform::GamepadAxis::LeftTrigger, 0.9f);
        const BindingCaptured* captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::LeftTrigger);
        CHECK(captured->binding.axisDirection == 1.0f);

        events = captureAxis(platform::GamepadAxis::RightTrigger, 0.9f);
        captured = findEvent<BindingCaptured>(events);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightTrigger);
        CHECK(captured->binding.axisDirection == 1.0f);
    }

    SUBCASE("a trigger below the threshold without crossing does not report")
    {
        std::vector<UiEvent> events = captureAxis(platform::GamepadAxis::LeftTrigger, -0.9f);
        CHECK(findEvent<BindingCaptured>(events) == nullptr);
        events = captureAxis(platform::GamepadAxis::RightTrigger, 0.49f);
        CHECK(findEvent<BindingCaptured>(events) == nullptr);
    }

    SUBCASE("an axis resting near the threshold does not report until it crosses")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Axis", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightX, 0.49f)));
        const auto resting = drain(ui);
        CHECK(findEvent<BindingCaptured>(resting) == nullptr);

        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightY, -0.49f)));
        const auto stillResting = drain(ui);
        CHECK(findEvent<BindingCaptured>(stillResting) == nullptr);

        // The axis captures only once it crosses the threshold.
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightY, -0.9f)));
        const auto crossed = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(crossed);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightY);
        CHECK(captured->binding.axisDirection == -1.0f);
    }

    SUBCASE("an axis already held when capture starts is ignored until it returns")
    {
        NullPainter painter;
        cvars::Registry registry;
        Ui ui(painter, registry);
        Screen screen;
        screen.title = "Bind";
        screen.widgets = { BindingCapture{ "Axis", true } };
        REQUIRE(ui.push(screen) != kInvalidScreen);

        // The right stick is already past the threshold when capture starts.
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightX, 0.9f)));
        tap(ui, platform::Key::Enter);
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightX, 0.9f)));
        const auto held = drain(ui);
        CHECK(findEvent<BindingCaptured>(held) == nullptr);

        // It returns to rest and then crosses the other way.
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightX, 0.0f)));
        const auto returned = drain(ui);
        CHECK(findEvent<BindingCaptured>(returned) == nullptr);
        ui.handleEvent(platform::Event(padAxisEvent(platform::GamepadAxis::RightX, -0.9f)));
        const auto crossed = drain(ui);
        const BindingCaptured* captured = findEvent<BindingCaptured>(crossed);
        REQUIRE(captured != nullptr);
        CHECK(captured->binding.axis == platform::GamepadAxis::RightX);
        CHECK(captured->binding.axisDirection == -1.0f);
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
    ui.handleEvent(platform::Event(textEvent("b")));
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Aliceb");
    tap(ui, platform::Key::Enter);
    events = drain(ui);
    submitted = findEvent<TextSubmitted>(events);
    REQUIRE(submitted != nullptr);
    CHECK(submitted->text == "Aliceb");
    CHECK(registry.getString("test.name").value() == "Aliceb");

    // A new edit is restored by back; the screen stays open.
    ui.handleEvent(platform::Event(textEvent("c")));
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

TEST_CASE("text entry inserts multibyte text and backspace deletes one code point")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Name";
    screen.widgets = { TextEntry{ "Name", "", "" } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    ui.handleEvent(platform::Event(textEvent("Grüße")));
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Grüße");

    // Key events no longer spell characters; only TextInput inserts text.
    tap(ui, platform::Key::A);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Grüße");

    // Backspace deletes a whole code point, not one byte of a sequence.
    tap(ui, platform::Key::Backspace);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Grüß");
    tap(ui, platform::Key::Backspace);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Grü");
    tap(ui, platform::Key::Backspace);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "Gr");

    // Left moves over a whole code point; Delete removes the next one.
    tap(ui, platform::Key::Left);
    tap(ui, platform::Key::Delete);
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == "G");
}

TEST_CASE("the 1024-byte limit never splits a UTF-8 sequence")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Name";
    screen.widgets = { TextEntry{ "Name", "", "" } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);

    // 1022 bytes plus a two-byte code point exactly fills the buffer.
    ui.handleEvent(platform::Event(textEvent(std::string(1022, 'a') + "ü")));
    const std::string filled = std::get<std::string>(ui.widgetValue(id, 0).value());
    REQUIRE(filled.size() == 1024);
    CHECK(filled.substr(1022) == "ü");

    // One more code point does not fit, and neither half of it is inserted.
    ui.handleEvent(platform::Event(textEvent("x")));
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()).size() == 1024);
    ui.handleEvent(platform::Event(textEvent("ü")));
    CHECK(std::get<std::string>(ui.widgetValue(id, 0).value()) == filled);
}

TEST_CASE("text input is wanted only while a text entry is edited")
{
    NullPainter painter;
    cvars::Registry registry;
    Ui ui(painter, registry);

    Screen screen;
    screen.title = "Name";
    screen.widgets = { TextEntry{ "Name", "", "" }, Button{ "Done", 1, true } };
    const ScreenId id = ui.push(screen);
    REQUIRE(id != kInvalidScreen);
    CHECK(ui.focusedWidget() == 0);

    std::vector<UiEvent> events = drain(ui);
    const TextInputWanted* wanted = findEvent<TextInputWanted>(events);
    REQUIRE(wanted != nullptr);
    CHECK(wanted->on);

    // Moving to the button stops it; moving back starts it again.
    tap(ui, platform::Key::Down);
    events = drain(ui);
    wanted = findEvent<TextInputWanted>(events);
    REQUIRE(wanted != nullptr);
    CHECK_FALSE(wanted->on);

    tap(ui, platform::Key::Up);
    events = drain(ui);
    wanted = findEvent<TextInputWanted>(events);
    REQUIRE(wanted != nullptr);
    CHECK(wanted->on);

    // Closing the screen ends the editing too.
    ui.pop();
    events = drain(ui);
    wanted = findEvent<TextInputWanted>(events);
    REQUIRE(wanted != nullptr);
    CHECK_FALSE(wanted->on);
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
