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

