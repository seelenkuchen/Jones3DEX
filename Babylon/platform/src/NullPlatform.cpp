// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/platform/NullPlatform.h>

#include <babylon/log/Log.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::platform
{
namespace
{

    // One Null window: the title and size it was made with, kept in memory.
    class NullWindow final : public Window
    {
    public:
        NullWindow(std::string title, Size size, WindowId id) :
            m_title(std::move(title)), m_size(size), m_id(id)
        {
        }

        void setTitle(std::string_view title) override
        {
            m_title.assign(title);
        }

        std::string title() const override
        {
            return m_title;
        }

        Size size() const override
        {
            return m_size;
        }

        // There is no display behind a Null window, so nothing can refuse:
        // the mode is only the state the interface promises to report back.
        bool setFullscreen(bool enabled) override
        {
            m_fullscreen = enabled;
            return true;
        }

        bool fullscreen() const override
        {
            return m_fullscreen;
        }

        WindowId id() const override
        {
            return m_id;
        }

        // A Null window has no cursor to hide, so nothing can refuse: the
        // mode is only the state the interface promises to report back.
        bool setRelativeMouse(bool enabled) override
        {
            m_relativeMouse = enabled;
            return true;
        }

        bool relativeMouse() const override
        {
            return m_relativeMouse;
        }

        // There is no keyboard or input method behind a Null window, so
        // nothing can refuse: the mode is only the state the interface
        // promises to report back. Text arrives only when a test pushes a
        // TextInput event for this window.
        bool setTextInput(bool enabled) override
        {
            m_textInput = enabled;
            return true;
        }

        bool textInput() const override
        {
            return m_textInput;
        }

    private:
        // Called only with a size that passed the shared check in resize(). Returns
        // false and logs one warning if the backend refused it.
        bool doResize(Size size) override
        {
            m_size = size;
            return true;
        }

        std::string m_title;
        Size m_size;
        WindowId m_id;
        bool m_fullscreen = false;
        bool m_relativeMouse = false;
        bool m_textInput = false;
    };

}

    std::unique_ptr<Window> NullPlatform::createWindow(const WindowDesc& desc)
    {
        if ( !isValidWindowSize(desc.size) )
        {
            // The same check and the same one warning as Window::resize(),
            // so a size is accepted at creation exactly when it is later.
            log::warning("platform: rejected window size {}x{}: both dimensions "
                "must be within 1 and {}", desc.size.width, desc.size.height,
                kMaxWindowDimension);
            return nullptr;
        }
        return std::make_unique<NullWindow>(desc.title, desc.size, m_nextId++);
    }

    bool NullPlatform::showMessageBox(std::string_view title, std::string_view text)
    {
        log::warning("{}: {}", title, text);
        return false;
    }

    std::optional<std::size_t> NullPlatform::askChoice(std::string_view title,
        std::string_view text, std::span<const std::string_view> buttons)
    {
        ChoiceRequest request{ std::string(title), std::string(text), {} };
        request.buttons.reserve(buttons.size());
        for ( const std::string_view button : buttons )
        {
            request.buttons.emplace_back(button);
        }
        m_lastChoice = std::move(request);

        // The same limit as the interface, checked before the scripted answer
        // so a refused list is never reported as shown.
        if ( buttons.empty() || buttons.size() > kMaxChoiceButtons )
        {
            log::warning("{}: {}", title, text);
            return std::nullopt;
        }
        if ( !m_choiceAnswer.has_value() )
        {
            log::warning("{}: {}", title, text);
            return std::nullopt;
        }
        return m_choiceAnswer;
    }

    void NullPlatform::setChoiceAnswer(std::optional<std::size_t> answer)
    {
        m_choiceAnswer = answer;
    }

    std::optional<NullPlatform::ChoiceRequest> NullPlatform::lastChoice() const
    {
        return m_lastChoice;
    }

    FolderChoice NullPlatform::chooseFolder(std::string_view title,
        std::string_view defaultLocation)
    {
        m_lastFolder = FolderRequest{ std::string(title), std::string(defaultLocation) };
        if ( !m_folderAnswer.has_value() )
        {
            log::warning("{}: {}", title, defaultLocation);
            return {};
        }
        return *m_folderAnswer;
    }

    void NullPlatform::setFolderAnswer(FolderChoice answer)
    {
        m_folderAnswer = std::move(answer);
    }

    std::optional<NullPlatform::FolderRequest> NullPlatform::lastFolder() const
    {
        return m_lastFolder;
    }

    bool NullPlatform::rumbleGamepad(GamepadId gamepad, float lowFrequency,
        float highFrequency, std::uint32_t durationMs)
    {
        m_lastRumble = RumbleCall{ gamepad, clampRumbleIntensity(lowFrequency),
            clampRumbleIntensity(highFrequency),
            std::min(durationMs, kMaxRumbleDurationMs) };
        return false;   // no device to vibrate
    }

    std::optional<NullPlatform::RumbleCall> NullPlatform::lastRumble() const
    {
        return m_lastRumble;
    }

    std::optional<Event> NullPlatform::pollEvent()
    {
        if ( m_events.empty() )
        {
            return std::nullopt;   // nothing was ever pushed: never blocks
        }
        const Event event = m_events.front();
        m_events.pop_front();
        return event;
    }

    void NullPlatform::pushEvent(const Event& event)
    {
        m_events.push_back(event);
    }

    void NullPlatform::setGamepadType(GamepadType type)
    {
        m_gamepadType = type;
    }

    GamepadType NullPlatform::gamepadType() const
    {
        return m_gamepadType;
    }

    void NullPlatform::connectGamepad(GamepadId gamepad)
    {
        m_events.push_back(GamepadConnected{ gamepad, m_gamepadType });
    }

}
