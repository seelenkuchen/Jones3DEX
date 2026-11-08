// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/platform/SdlPlatform.h>

#include "SdlEventTranslation.h"

#include <babylon/log/Log.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace babylon::platform
{
namespace
{

    // SDL's video subsystem belongs to the whole process, so this backend
    // runs one platform at a time: the factory sets the flag, the destructor
    // clears it, and a second platform while one lives is refused.
    bool videoInUse = false;

    // SDL sizes a window in window coordinates (points) but Window works in
    // pixels. On a high-density display one point is several pixels, so a
    // requested pixel size is converted with the window's pixel density and
    // the result checked. Returns true when size() reports the size asked
    // for; a refusal logs one warning.
    bool setWindowPixelSize(SDL_Window* window, Size size)
    {
        int curWidth = 0;
        int curHeight = 0;
        if ( !SDL_GetWindowSizeInPixels(window, &curWidth, &curHeight) )
        {
            log::warning("platform: reading the window size failed: {}", SDL_GetError());
            return false;
        }

        if ( curWidth == size.width && curHeight == size.height )
        {
            return true;
        }

        const float density = SDL_GetWindowPixelDensity(window);
        if ( !(density > 0.0f) )
        {
            log::warning("platform: the window reported an invalid pixel density {}", density);
            return false;
        }

        // The density is pixels per point; the point size is rounded and then
        // checked against the pixels the window system actually applied.
        const int pointWidth  = static_cast<int>(std::lround(size.width / density));
        const int pointHeight = static_cast<int>(std::lround(size.height / density));
        if ( pointWidth < 1 || pointHeight < 1 )
        {
            log::warning("platform: {}x{} pixels are less than one window point at density {}",
                size.width, size.height, density);
            return false;
        }

        if ( !SDL_SetWindowSize(window, pointWidth, pointHeight) )
        {
            log::warning("platform: resizing the window to {}x{} pixels ({}x{} points) failed: {}",
                size.width, size.height, pointWidth, pointHeight, SDL_GetError());
            return false;
        }
        if ( !SDL_SyncWindow(window) )
        {
            log::warning("platform: waiting for the resize to {}x{} pixels failed: {}",
                size.width, size.height, SDL_GetError());
            return false;
        }

        if ( !SDL_GetWindowSizeInPixels(window, &curWidth, &curHeight)
            || curWidth != size.width || curHeight != size.height )
        {
            log::warning("platform: the window system made the window {}x{} pixels, not {}x{}; "
                "pixel density {}", curWidth, curHeight, size.width, size.height, density);
            return false;
        }
        return true;
    }

    // One window SDL created. SDL_Window* is owned here and destroyed with
    // this object, which must happen before the SdlPlatform quits the video
    // subsystem.
    class SdlWindow final : public Window
    {
    public:
        SdlWindow(SDL_Window* window, WindowId id) : m_window(window), m_id(id)
        {
        }

        ~SdlWindow() override
        {
            SDL_DestroyWindow(m_window);
        }

        void setTitle(std::string_view title) override
        {
            // SDL wants a NUL-terminated string; the view is copied rather
            // than read past its end.
            const std::string text(title);
            if ( !SDL_SetWindowTitle(m_window, text.c_str()) )
            {
                log::warning("platform: setting the window title failed: {}", SDL_GetError());
            }
        }

        std::string title() const override
        {
            const char* const text = SDL_GetWindowTitle(m_window);
            if ( text == nullptr )
            {
                log::warning("platform: reading the window title failed: {}", SDL_GetError());
                return {};
            }
            return text;
        }

        Size size() const override
        {
            // SDL counts a destroyed window as no window at all, so the only
            // way this fails is a handle the video subsystem already let go;
            // there is then no size left to report.
            int width = 0;
            int height = 0;
            if ( !SDL_GetWindowSizeInPixels(m_window, &width, &height) )
            {
                log::warning("platform: reading the window size failed: {}", SDL_GetError());
            }
            return Size{width, height};
        }

        // With no fullscreen mode set, SDL_SetWindowFullscreen's bool overload
        // is desktop/borderless fullscreen at the display's current
        // resolution: exactly the fullscreen this interface promises. It may
        // fail on a driver with no display, which is one warning and the mode
        // unchanged.
        bool setFullscreen(bool enabled) override
        {
            if ( !SDL_SetWindowFullscreen(m_window, enabled) )
            {
                log::warning("platform: setting fullscreen {} on the window failed: {}",
                    enabled, SDL_GetError());
                return false;
            }
            return true;
        }

        bool fullscreen() const override
        {
            return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
        }

        WindowId id() const override
        {
            return m_id;
        }

        SDL_Window* sdlWindow() const
        {
            return m_window;
        }

        // SDL hides the cursor and reports only relative motion in this
        // mode; a window system that cannot do it makes the call fail, which
        // is one warning and the mode unchanged.
        bool setRelativeMouse(bool enabled) override
        {
            if ( !SDL_SetWindowRelativeMouseMode(m_window, enabled) )
            {
                log::warning("platform: setting relative mouse mode {} on the window failed: {}",
                    enabled, SDL_GetError());
                return false;
            }
            return true;
        }

        bool relativeMouse() const override
        {
            return SDL_GetWindowRelativeMouseMode(m_window);
        }

        // SDL delivers SDL_EVENT_TEXT_INPUT only while text input is
        // started, and an on-screen keyboard or IME may then take the key
        // presses; a refused start or stop is one warning and the mode
        // unchanged.
        bool setTextInput(bool enabled) override
        {
            const bool started = enabled ? SDL_StartTextInput(m_window)
                                         : SDL_StopTextInput(m_window);
            if ( !started )
            {
                log::warning("platform: setting text input {} on the window failed: {}",
                    enabled, SDL_GetError());
                return false;
            }
            return true;
        }

        bool textInput() const override
        {
            return SDL_TextInputActive(m_window);
        }

    private:
        // The shared check passed a pixel size; setWindowPixelSize converts
        // it to SDL's window coordinates and waits until the window reports
        // exactly that many pixels, so size() is right when this returns.
        bool doResize(Size size) override
        {
            return setWindowPixelSize(m_window, size);
        }

        SDL_Window* m_window;
        WindowId m_id;
    };

    class SdlPlatform final : public Platform
    {
    public:
        ~SdlPlatform() override
        {
            // Windows and gamepads come first: Platform.h says every window
            // is destroyed before its platform, SDL frees window resources
            // as part of quitting the video subsystem, and a gamepad handle
            // must be closed before the subsystem that made it goes away.
            closeAllGamepads();
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            videoInUse = false;
        }

        std::unique_ptr<Window> createWindow(const WindowDesc& desc) override
        {
            if ( !isValidWindowSize(desc.size) )
            {
                // Checked here rather than left to SDL, so the same sizes are
                // refused in every backend with the same one warning as
                // resize().
                log::warning("platform: rejected window size {}x{}: both dimensions "
                    "must be within 1 and {}", desc.size.width, desc.size.height,
                    kMaxWindowDimension);
                return nullptr;
            }
            // The high-density flag asks for a drawable at pixel resolution;
            // the size below is then corrected from SDL's points to pixels.
            const SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY
                | (desc.resizable ? SDL_WINDOW_RESIZABLE : 0);
            SDL_Window* const window =
                SDL_CreateWindow(desc.title.c_str(), desc.size.width, desc.size.height, flags);
            if ( window == nullptr )
            {
                log::warning("platform: creating a {}x{} window failed: {}",
                    desc.size.width, desc.size.height, SDL_GetError());
                return nullptr;
            }
            // SDL's window ids are nonzero for every window it knows, and
            // 0 would collide with the "no window" id of the interface.
            const WindowId id = static_cast<WindowId>(SDL_GetWindowID(window));
            if ( id == 0 )
            {
                log::warning("platform: asking the new window for its id failed: {}",
                    SDL_GetError());
                SDL_DestroyWindow(window);
                return nullptr;
            }
            // The window's size is a pixel size on the interface; SDL took
            // the same number as points, which differs on a high-density
            // display. Correct it and refuse the window if it cannot match.
            if ( !setWindowPixelSize(window, desc.size) )
            {
                SDL_DestroyWindow(window);
                return nullptr;
            }
            return std::make_unique<SdlWindow>(window, id);
        }

        std::optional<Event> pollEvent() override
        {
            SDL_Event sdlEvent;
            for ( ; ; )
            {
                if ( !SDL_PollEvent(&sdlEvent) )
                {
                    return std::nullopt;   // nothing of ours left in the queue
                }
                // SDL only sends button and axis events for a gamepad that
                // is open, so open and close them as they come and go, and
                // keep the handles until the platform dies.
                if ( sdlEvent.type == SDL_EVENT_GAMEPAD_ADDED )
                {
                    openGamepad(sdlEvent.gdevice.which);
                }
                else if ( sdlEvent.type == SDL_EVENT_GAMEPAD_REMOVED )
                {
                    closeGamepad(sdlEvent.gdevice.which);
                }
                if ( std::optional<Event> event = sdl::translateEvent(sdlEvent); event.has_value() )
                {
                    return event;
                }
                // Everything translateEvent() does not deliver (display
                // changes, sensors, audio devices, unmapped buttons) is
                // dropped here and the search continues.
            }
        }

        bool showMessageBox(std::string_view title, std::string_view text) override
        {
            // SDL wants NUL-terminated strings; the views are copied rather
            // than read past their ends. No parent window, as the interface
            // says, so the box is not tied to one game window.
            const std::string aTitle(title);
            const std::string aText(text);
            if ( !SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, aTitle.c_str(),
                    aText.c_str(), nullptr) )
            {
                log::warning("platform: could not show the message box '{}': {}",
                    aTitle, SDL_GetError());
                return false;
            }
            return true;
        }

    private:
        // SDL ids count up from 1, so the cast to GamepadId never collides
        // with "no gamepad" and maps back without loss.
        void openGamepad(SDL_JoystickID which)
        {
            const GamepadId gamepad = static_cast<GamepadId>(which);
            SDL_Gamepad* const handle = SDL_OpenGamepad(which);
            if ( handle == nullptr )
            {
                // The connect event is still delivered; only button and
                // axis events are missing for this gamepad.
                log::warning("platform: opening gamepad {} failed: {}", gamepad, SDL_GetError());
                return;
            }
            m_gamepads[gamepad] = handle;
        }

        void closeGamepad(SDL_JoystickID which)
        {
            const auto found = m_gamepads.find(static_cast<GamepadId>(which));
            if ( found == m_gamepads.end() )
            {
                return;   // the open failed, or a removal without an add
            }
            SDL_CloseGamepad(found->second);
            m_gamepads.erase(found);
        }

        void closeAllGamepads()
        {
            for ( const auto& entry : m_gamepads )
            {
                SDL_CloseGamepad(entry.second);
            }
            m_gamepads.clear();
        }

        std::unordered_map<GamepadId, SDL_Gamepad*> m_gamepads;
    };

}

    SDL_Window* sdlWindowOf(Window& window)
    {
        SdlWindow* const sdlWindow = dynamic_cast<SdlWindow*>(&window);
        return sdlWindow != nullptr ? sdlWindow->sdlWindow() : nullptr;
    }

    std::unique_ptr<Platform> createSdlPlatform()
    {
        if ( videoInUse )
        {
            log::warning("platform: an SDL platform already exists; there is only "
                "one video subsystem per process");
            return nullptr;
        }
        if ( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
        {
            log::warning("platform: initializing the SDL video subsystem failed: {}",
                SDL_GetError());
            return nullptr;
        }
        // Gamepads need their own subsystem; SDL only sends the hotplug,
        // button and axis events this queue carries while it is up.
        if ( !SDL_InitSubSystem(SDL_INIT_GAMEPAD) )
        {
            log::warning("platform: initializing the SDL gamepad subsystem failed: {}",
                SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return nullptr;
        }
        videoInUse = true;
        return std::make_unique<SdlPlatform>();
    }

}
