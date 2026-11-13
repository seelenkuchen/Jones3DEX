// Added: the Babylon column's one tabbed Options screen (ADR-0028); see JonesUiOptions.cpp.
// Gameplay, Controls, Display and Sound share one screen, switched with Q/E, LB/RB or a click on
// the tab strip. Each tab reuses the standalone screen's rows and apply logic.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIOPTIONS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIOPTIONS_H
#ifndef __cplusplus
#error "JonesUiOptions.h is C++ only: it names babylon::ui types."
#endif

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>
#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Ui.h>

#include <cstddef>

// The data the Options screen edits. All pointers must stay valid for the call.
struct JonesUiOptionsData
{
    bool bEnhancedProfile = true;
    const StdDisplayEnvironment* pDisplayEnv = nullptr;
    JonesDisplaySettings* pDisplaySettings = nullptr;
    JonesSoundSettings* pSoundSettings = nullptr;
    JonesControlsConfig* pControlsConfig = nullptr;
};

// Runs the tabbed Options screen through ui and returns 1 on OK, 2 on back. The standalone
// screens' entry points stay available. pPainter may be null, which runs without drawing.
int JonesUiOptions_RunOptionsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiOptionsData& data,
    const JonesUiModalBackground& background = JonesUiModalBackground{});

// Added: the blocking wrapper used by the main and pause menus. It loads the settings, registers
// the Cinzel tab and Jost row fonts, creates the Options skin, runs the screen over the menu
// background (darkened when one is given) and applies the sound and display changes. Returns the
// dialog result: 1 applied, 2 cancelled.
int JonesUiOptions_Show(const JonesUiModalBackground* pBackground);

#ifdef J3D_TEST
// Test support: builds one tab's screen with the given data, for the row and help checks.
babylon::ui::Screen JonesUiOptions_BuildTabScreenForTest(std::size_t tab,
    const JonesUiOptionsData& data);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIOPTIONS_H
