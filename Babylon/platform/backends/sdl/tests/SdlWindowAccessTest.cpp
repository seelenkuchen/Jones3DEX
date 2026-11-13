// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>

#include <babylon/log/CaptureSink.h>

#include <SDL3/SDL_video.h>

#include <doctest.h>

#include <memory>

TEST_CASE("sdlWindowOf gives the SDL window of SDL windows and nullptr for others")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<babylon::platform::Platform> platform =
        babylon::platform::createSdlPlatform();
    if ( platform == nullptr )
    {
        FAIL("createSdlPlatform() returned nullptr: run this test with "
            "the offscreen or dummy video driver");
    }
    const std::unique_ptr<babylon::platform::Window> window =
        platform->createWindow(babylon::platform::WindowDesc{ "accessor",
            babylon::platform::Size{ 64, 48 } });
    REQUIRE(window != nullptr);

    // The pointer names the very window: SDL knows it by the same id.
    SDL_Window* const sdlWindow = babylon::platform::sdlWindowOf(*window);
    REQUIRE(sdlWindow != nullptr);
    CHECK(SDL_GetWindowID(sdlWindow) == window->id());
    CHECK(babylon::platform::sdlWindowOf(*window) == sdlWindow);

    // A window of another backend has no SDL window behind it.
    babylon::platform::NullPlatform nullPlatform;
    const std::unique_ptr<babylon::platform::Window> nullWindow =
        nullPlatform.createWindow(babylon::platform::WindowDesc{ "null",
            babylon::platform::Size{ 64, 48 } });
    REQUIRE(nullWindow != nullptr);
    CHECK(babylon::platform::sdlWindowOf(*nullWindow) == nullptr);
    CHECK(sink.records().empty());
}

TEST_CASE("the SDL platform refuses rumble for a gamepad it has not opened")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<babylon::platform::Platform> platform =
        babylon::platform::createSdlPlatform();
    if ( platform == nullptr )
    {
        FAIL("createSdlPlatform() returned nullptr: run this test with "
            "the offscreen or dummy video driver");
    }

    // No gamepad is connected in the headless test, so any id is unknown.
    CHECK_FALSE(platform->rumbleGamepad(4242, 0.5f, 0.5f, 100));
    CHECK(sink.records().empty());
}
