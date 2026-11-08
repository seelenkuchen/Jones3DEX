// Added: the Babylon column's Display settings screen (ADR-0026), the merged original Display and
// Advanced Display dialogs (jonesConfig.c, dialogs 114 and 148). The wrapper keeps the original
// return values: 1 applies the changed settings, 2 cancels and writes nothing.
//
// The option list and the mode helper are the test surface: they keep the screen logic free of a
// running game and let a test build the resolution list from a synthetic display environment.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIDISPLAYSETTINGS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIDISPLAYSETTINGS_H
#ifndef __cplusplus
#error "JonesUiDisplaySettings.h is C++ only: it names babylon::ui and std::string."
#endif

#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <std/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One resolution the screen offers. text is the label the resolution choice shows. The parallel
// arrays name every bit depth the size offers in the display environment and the environment's
// video-mode index that implements it; a size is listed once however many bit depths or refresh
// rates it has.
struct JonesUiDisplayVideoMode
{
    std::string text;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t refreshRate = 0;
    std::vector<int> aBitDepths;
    std::vector<std::size_t> aModeNums;
};

// Collects the video modes of one display device, deduplicated by width and height in the order
// the environment lists them. Returns false when the environment, the device or its mode list is
// missing, and leaves modes empty.
bool JonesUiDisplay_CollectVideoModes(const StdDisplayEnvironment* pEnv, int displayDeviceNum,
    std::vector<JonesUiDisplayVideoMode>& modes);

// Runs the Display screen through ui. On OK it writes the display settings the original dialogs
// wrote (video mode, size, window mode, fog, mipmap filter, buffering) and their Jones.cfg keys,
// applies fog and the high-poly setting, and returns 1. On back it returns 2 and writes nothing.
// pPainter may be null, which runs the screen without drawing. pSettings must not be null.
int JonesUiDisplay_RunDisplayScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pSettings);

#ifdef J3D_TEST
// Test support: appends every screen this module builds, for the action-id audit.
void JonesUiDisplay_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIDISPLAYSETTINGS_H
