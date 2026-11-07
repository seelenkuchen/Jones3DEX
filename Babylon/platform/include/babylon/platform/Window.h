// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/platform/Event.h>

#include <string>
#include <string_view>

namespace babylon::platform
{

    // The largest width or height a window may have, in pixels. Both the
    // creation of a window and Window::resize() refuse anything above it,
    // which is also beyond what any backend's window system can give.
    inline constexpr int kMaxWindowDimension = 16384;

    // How a window is created. The title is shown as given; an empty title
    // is allowed and means the window system's own default. size is the
    // client size in pixels, even on a high-density display whose window
    // coordinates are larger.
    struct WindowDesc
    {
        std::string title;
        Size size{ 640, 480 };
        bool resizable = false;
    };

    // One OS window, created by Platform::createWindow and never directly.
    // Destroy every window before the Platform that made it. Failures log
    // one babylon::log::warning line and change nothing; nothing throws.
    // Not thread-safe: call from the thread that created the window.
    class Window
    {
    public:
        virtual ~Window() = default;

        // The shared size check of every backend: 1 <= width and height <=
        // kMaxWindowDimension. On failure this logs one warning and returns
        // false with the size unchanged. Otherwise it asks the backend and
        // returns its answer; a backend that refuses logs its own warning.
        [[nodiscard]] bool resize(Size size);

        // Sets the title shown by the window system. This cannot fail in a
        // way the caller can act on: a backend that rejects the text logs
        // one warning and leaves the title as it was.
        virtual void setTitle(std::string_view title) = 0;

        // The title as the backend reports it now, which is what
        // setTitle() set last unless the backend refused it.
        virtual std::string title() const = 0;

        // The current client size in pixels, excluding decorations. On a
        // high-density display this is the drawable size, not the window
        // coordinate size.
        virtual Size size() const = 0;

        // Sets the display mode. Windowed is decorated and keeps the size
        // last set; fullscreen is desktop/borderless fullscreen on the
        // display that holds the window at that display's current
        // resolution, so size() then reports the display's pixel size.
        // Returns false and logs one warning if the backend refused,
        // leaving fullscreen() as it was. The Null backend always accepts
        // and records the state; it has no display, so its size() stays the
        // size it was given.
        [[nodiscard]] virtual bool setFullscreen(bool enabled) = 0;

        // The display mode the backend reports now: what setFullscreen()
        // set last unless it was refused.
        virtual bool fullscreen() const = 0;

        // The id this window was created with, stable for its lifetime and
        // never 0. Events about this window carry it.
        virtual WindowId id() const = 0;

        // Hides the cursor and reports only relative motion (mouse look):
        // the MouseMoved events of this window then carry their meaning in
        // dx and dy. Returns false and logs one warning if the backend
        // refused, leaving relativeMouse() as it was. The Null backend
        // always accepts and records the state.
        [[nodiscard]] virtual bool setRelativeMouse(bool enabled) = 0;

        // The relative mouse mode the backend reports now: what
        // setRelativeMouse() set last unless it was refused.
        virtual bool relativeMouse() const = 0;

        // Starts or stops delivering TextInput events for this window.
        // Text input is off until it is started, so a game starts it only
        // while a text field is being edited. Returns false and logs one
        // warning if the backend refused, leaving textInput() as it was.
        // The Null backend always accepts and records the state.
        [[nodiscard]] virtual bool setTextInput(bool enabled) = 0;

        // The text input mode the backend reports now: what setTextInput()
        // set last unless it was refused.
        virtual bool textInput() const = 0;

    private:
        // Called only with a size that passed the check in resize(). Returns
        // false and logs one warning if the backend refused it.
        virtual bool doResize(Size size) = 0;
    };

    // True when both dimensions are within 1..kMaxWindowDimension. Shared by
    // Window::resize() and by every Platform::createWindow(), so the same
    // size is accepted at creation and in a resize.
    bool isValidWindowSize(Size size);

}
