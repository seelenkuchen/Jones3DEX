// Added: the Babylon column's pause menu (ADR-0026): Resume, Save, Load, Settings, Statistics and
// Exit. Each entry calls the shared JonesHud system-item functions, so it takes exactly the path
// the HUD menu takes. The build helpers and the runner are the test surface.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIPAUSEMENU_H
#define JONES3D_PLATFORM_BABYLON_JONESUIPAUSEMENU_H
#ifndef __cplusplus
#error "JonesUiPauseMenu.h is C++ only: it names babylon::ui and std::string."
#endif

#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Ui.h>

#include <std/types.h>

// The pause menu before it is pushed.
babylon::ui::Screen JonesUiPauseMenu_BuildScreen(void);

// The nested Settings screen: Gameplay, Controls, Display, Sound and Back.
babylon::ui::Screen JonesUiPauseMenu_BuildSettingsScreen(void);

// Runs the pause menu until Resume or Back closes it. Save, Load, Settings, Statistics and Exit
// call the shared JonesHud functions on the way, exactly as the HUD menu's system items do.
// background supplies the dimmed game frame and the lower-left shade; with the default empty
// value the modal loop's frozen frame and dim are used. pMenuSkin, when set, is applied to the
// pause menu only; the nested Settings screen keeps the caller's skin. The caller's skin is
// restored before this returns.
int JonesUiPauseMenu_RunPauseMenu(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiModalBackground& background = JonesUiModalBackground{},
    const babylon::ui::Skin* pMenuSkin = nullptr);

// Captures pFrame (or the display back buffer when null) as the savegame thumbnail, replacing any
// thumbnail an earlier opening left. Returns true when a thumbnail was produced. The opening use
// is pFrame = null.
bool JonesUiPauseMenu_CaptureThumbnail(tVBuffer* pFrame);

#endif // JONES3D_PLATFORM_BABYLON_JONESUIPAUSEMENU_H
