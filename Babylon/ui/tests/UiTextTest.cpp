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

    platform::TextInput textEvent(std::string text)
    {
        return platform::TextInput{ kWindow, std::move(text) };
    }

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

