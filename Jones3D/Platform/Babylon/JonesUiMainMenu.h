// Added: the windowed Babylon build's main menu (ADR-0026, ADR-0028). The menu replaces the
// launcher: Continue, New Game, Load Game, Options and Exit Game. The runner is the test surface:
// the game-level actions arrive as hooks, so the screen logic runs in a unit test with a scripted
// platform and no game assets. jonesConfig_ShowMainMenu supplies the real hooks, loads the
// background and plays the menu theme.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIMAINMENU_H
#define JONES3D_PLATFORM_BABYLON_JONESUIMAINMENU_H
#ifndef __cplusplus
#error "JonesUiMainMenu.h is C++ only: it names babylon::ui types and std::function."
#endif

#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <rdroid/types.h>

#include <functional>
#include <vector>

// The value JonesUiMainMenu_Run answers when one of its actions closes the menu.
enum class JonesUiMainMenuAction
{
    Quit,
    NewGame,
    LoadGame,
};

// What the menu calls for its actions. Empty functions are safe and do nothing, which keeps the
// drawing test free of game state. loadGame returns true when it wrote a save path into pOutPath
// and the game should start loading; a false answer keeps the menu open.
struct JonesUiMainMenuHooks
{
    std::function<void()> newGame;
    std::function<bool(char* pOutPath)> loadGame;
    // Continue returns the newest save's path, the same way loadGame returns a chosen one. It is
    // only called while the CONTINUE row is shown.
    std::function<bool(char* pOutPath)> continueGame;
    // Called after the modal loop applied a window resize, for screens that rebake fonts at the
    // new pixel scale.
    std::function<void(int width, int height)> resized;
};

// Runs the main menu through JonesUiModal_Run until Quit, New Game, Continue or Load closes it.
// The CONTINUE row exists only when bCanLoad is true (saves present) and Load is disabled
// otherwise. Options opens the tabbed Options screen over the same background. background is
// drawn behind the menu (see JonesUiModalBackground); the caller keeps it alive. On a successful
// Continue or Load, pOutLoadPath receives the save path, bounded by
// JONESCONFIG_GAMESAVE_FILEPATHSIZE; a cancelled Load writes nothing. pMenuSkin, when non-null, is
// applied for the main menu; the Options screen manages and restores its own skin. The ui must
// draw through pPainter, like every other screen.
JonesUiMainMenuAction JonesUiMainMenu_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiModalBackground& background, bool bCanLoad,
    const JonesUiMainMenuHooks& hooks, char* pOutLoadPath,
    const babylon::ui::Skin* pMenuSkin = nullptr);

// Puts a wallpaper into the finished load screen's state: the load screen advances its cels at
// each of its three completion stages before it closes. The menu uses this for its background.
void JonesUiMainMenu_SetFinishedWallpaper(rdWallpaper* pWallpaper);

#ifdef J3D_TEST
// Test support: the main menu screen as it is built for bCanLoad, and every screen the module
// builds for the action-id audit.
babylon::ui::Screen JonesUiMainMenu_BuildScreenForTest(bool bCanLoad);
void JonesUiMainMenu_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIMAINMENU_H
