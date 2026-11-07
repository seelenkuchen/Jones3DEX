// Added: the Babylon windowed column's display-mode hotkey; see JonesWindowMode.h.
#include <Jones3D/Platform/Babylon/JonesWindowMode.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Main/JonesMain.h>

#include <babylon/input/InputState.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Window.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/stdConfig.h>

namespace
{
    // True while Enter was held at the previous check. The input state keeps a held key down
    // between reads, so without this one held Enter would toggle every frame; a repeat does not
    // change the state and is ignored the same way.
    bool bEnterHeld = false;
}

extern "C" bool JonesWindowMode_ToggleFullscreen(void)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // A headless run has no window: there is no mode to change and the config is untouched.
        return false;
    }

    babylon::platform::Window& rWindow = *pContext->pWindow;
    const bool bFullscreen = !rWindow.fullscreen();
    if ( !rWindow.setFullscreen(bFullscreen) )
    {
        return false;
    }

    // Windowed is the game's bWindowMode == 1; fullscreen is 0, like the Display screen and the
    // graphics.window default. Keeping the settings in step means a display restart from the
    // Display screen or a focus-driven refresh does not switch back to the old mode.
    const bool bWindowed = !bFullscreen;
    JonesDisplaySettings* pSettings = JonesMain_GetDisplaySettings();
    if ( pSettings )
    {
        pSettings->bWindowMode = bWindowed ? 1 : 0;
    }

    if ( stdConfig_HasStarted() )
    {
        static_cast<void>(stdConfig_SetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, bWindowed));
    }

    return true;
}

extern "C" bool JonesWindowMode_HandleHotkey(void)
{
    const babylon::input::InputState& rInput = stdBabylonEvents_InputState();
    const bool bEnter = rInput.keyDown(babylon::platform::Key::Enter)
        || rInput.keyDown(babylon::platform::Key::KeypadEnter);
    const bool bAlt = rInput.keyDown(babylon::platform::Key::LeftAlt)
        || rInput.keyDown(babylon::platform::Key::RightAlt);

    // The rising edge of Enter while Alt is held: Alt+Enter, once per press.
    const bool bPressed = bEnter && bAlt && !bEnterHeld;
    bEnterHeld = bEnter;

    if ( !bPressed )
    {
        return false;
    }

    return JonesWindowMode_ToggleFullscreen();
}
