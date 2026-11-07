// Added: Alt+Enter and the graphics.window key for the Babylon windowed run. The original game
// had no such hotkey (its Win32 window procedure handled only quick save/load, the screenshot and
// the intro skip keys); this is a presentation addition on top of the existing windowed and
// fullscreen styles. The toggled mode is also stored in JonesDisplaySettings so a later Display
// screen or display restart uses it, and written to graphics.window so the next run keeps it.
#ifndef JONES3D_PLATFORM_BABYLON_JONESWINDOWMODE_H
#define JONES3D_PLATFORM_BABYLON_JONESWINDOWMODE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Switches the game window between windowed and desktop/borderless fullscreen, updates
// JonesDisplaySettings::bWindowMode and writes graphics.window. Returns false and changes nothing
// when there is no game window (a headless run) or the backend refused the mode.
bool JonesWindowMode_ToggleFullscreen(void);

// Checks the pump's input state for Alt+Enter, in either Alt and either Enter, and toggles once per
// Enter press. Returns true when it toggled. The windowed run and the modal screen loop call this
// after their event pump; it does nothing without a game window.
bool JonesWindowMode_HandleHotkey(void);

#ifdef __cplusplus
}
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESWINDOWMODE_H
