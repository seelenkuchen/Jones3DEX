// Added: the interaction prompt's native-resolution present overlay. The shared HUD code caches
// the prompt kind in JonesHudPrompt.c; this module draws the bound button icon and the verb at the
// bottom centre of the window, above the game frame and below any menu overlay.
#ifndef JONES3D_PLATFORM_BABYLON_JONESHUD_PROMPT_OVERLAY_H
#define JONES3D_PLATFORM_BABYLON_JONESHUD_PROMPT_OVERLAY_H
#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Installs the overlay when the render device is open. Safe to call every frame: a display restart
// that clears the render state installs it again. Does nothing before the first display.
void JonesHudPromptOverlay_EnsureInstalled(void);

// Clears the overlay and releases its painter and assets. Safe when nothing is installed and
// before the display closes.
void JonesHudPromptOverlay_Shutdown(void);

J3D_EXTERN_C_END
#endif // JONES3D_PLATFORM_BABYLON_JONESHUD_PROMPT_OVERLAY_H
