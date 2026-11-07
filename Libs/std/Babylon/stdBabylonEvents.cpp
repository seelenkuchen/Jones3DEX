// Added: the Babylon column's one event pump, the owner of the shared babylon::input::InputState.
// See stdBabylonEvents.h. 0 is never a valid window id, so a context without a game window (the
// headless run) matches no window event and records no quit from one; a QuitRequested carries no
// window and is recorded either way.
// Adapter code: no Fixed/Added markers inside.
#include "stdBabylonEvents.h"

#include <babylon/platform/Event.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>

#include <cstddef>
#include <deque>
#include <optional>
#include <variant>
#include <vector>

#include "stdBabylon.h"
#include "stdBabylonTour.h"

namespace
{
    // The event kinds the scripted tour replaces while its input script is active. Window, focus,
    // resize and quit events stay the platform's.
    bool stdBabylonEvents_IsScriptedInput(const babylon::platform::Event& rEvent)
    {
        return std::holds_alternative<babylon::platform::KeyChanged>(rEvent)
            || std::holds_alternative<babylon::platform::MouseMoved>(rEvent)
            || std::holds_alternative<babylon::platform::MouseButtonChanged>(rEvent)
            || std::holds_alternative<babylon::platform::MouseWheelMoved>(rEvent)
            || std::holds_alternative<babylon::platform::TextInput>(rEvent)
            || std::holds_alternative<babylon::platform::GamepadConnected>(rEvent)
            || std::holds_alternative<babylon::platform::GamepadDisconnected>(rEvent)
            || std::holds_alternative<babylon::platform::GamepadButtonChanged>(rEvent)
            || std::holds_alternative<babylon::platform::GamepadAxisMoved>(rEvent);
    }

    // The state every pumped event goes into; owned here, read through stdBabylonEvents_InputState().
    babylon::input::InputState inputState;

    // The events the last pump took, for consumers that need the raw records (the modal ui loop).
    std::vector<babylon::platform::Event> lastEvents;

    // True while a recorded quit or game-window close waits to be taken by the window adapter.
    bool quitRequested = false;

    // The recorded game-window focus changes, oldest first; true = the window gained focus.
    std::deque<bool> focusChanges;
}

std::size_t stdBabylonEvents_Pump()
{
    StdBabylonContext& rContext = stdBabylon_RequireContext("stdBabylonEvents_Pump");

    const babylon::platform::WindowId windowId = rContext.pWindow ? rContext.pWindow->id() : 0;

    // Added: while the scripted tour's input script is active, the script's events replace the
    // platform's key, mouse, gamepad and text events; the window's own events still come through.
    const bool bScriptedInput = stdBabylonTour_InputActive();

    lastEvents.clear();
    std::size_t numEvents = 0;
    for ( ;; )
    {
        const std::optional<babylon::platform::Event> pEvent = rContext.pPlatform->pollEvent();
        if ( !pEvent )
        {
            break;
        }

        ++numEvents;

        if ( bScriptedInput && stdBabylonEvents_IsScriptedInput(*pEvent) )
        {
            continue;
        }

        lastEvents.push_back(*pEvent);

        // Events that carry no input (window, quit) are ignored by InputState, except the focus
        // loss, which releases every held key and button.
        inputState.apply(*pEvent);

        if ( std::holds_alternative<babylon::platform::QuitRequested>(*pEvent) )
        {
            quitRequested = true;
            continue;
        }

        if ( windowId == 0 )
        {
            continue; // no game window: its close and focus events are ignored
        }

        const babylon::platform::WindowCloseRequested* pClose =
            std::get_if<babylon::platform::WindowCloseRequested>(&*pEvent);
        if ( pClose && pClose->window == windowId )
        {
            quitRequested = true;
            continue;
        }

        const babylon::platform::WindowFocusChanged* pFocus =
            std::get_if<babylon::platform::WindowFocusChanged>(&*pEvent);
        if ( pFocus && pFocus->window == windowId )
        {
            focusChanges.push_back(pFocus->focused);
        }
    }

    if ( bScriptedInput )
    {
        // At most one delivery per present count, though the pump may run several times per frame.
        for ( const babylon::platform::Event& rEvent : stdBabylonTour_TakeInputEvents() )
        {
            lastEvents.push_back(rEvent);
            inputState.apply(rEvent);
            ++numEvents;
        }
    }

    // Added: --quit-after-script ends the run once the script's last frame has been presented.
    if ( stdBabylonTour_TakeQuitAfterScript() )
    {
        quitRequested = true;
    }

    return numEvents;
}

babylon::input::InputState& stdBabylonEvents_InputState()
{
    return inputState;
}

const std::vector<babylon::platform::Event>& stdBabylonEvents_LastEvents()
{
    return lastEvents;
}

bool stdBabylonEvents_PeekQuitRequest()
{
    return quitRequested;
}

bool stdBabylonEvents_TakeQuitRequest()
{
    const bool bQuit = quitRequested;
    quitRequested = false;
    return bQuit;
}

void stdBabylonEvents_RequestQuit()
{
    quitRequested = true;
}

std::optional<bool> stdBabylonEvents_TakeFocusChange()
{
    if ( focusChanges.empty() )
    {
        return std::nullopt;
    }

    const bool bGained = focusChanges.front();
    focusChanges.pop_front();
    return bGained;
}

#ifdef J3D_TEST
void stdBabylonEvents_ResetForTest()
{
    inputState = babylon::input::InputState();
    lastEvents.clear();
    quitRequested = false;
    focusChanges.clear();
}
#endif
