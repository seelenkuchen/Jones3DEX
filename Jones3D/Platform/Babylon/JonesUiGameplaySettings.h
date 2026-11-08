// Added: the Babylon column's Gameplay settings screen (ADR-0026), the original Gameplay Options
// dialog (jonesConfig.c, dialog 112). The wrapper keeps the original return values: 1 applies the
// changed settings, 2 cancels and writes nothing.
//
// The screen logic is its own function, so a test can drive it with a Null painter and no window.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIGAMEPLAYSETTINGS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIGAMEPLAYSETTINGS_H
#ifndef __cplusplus
#error "JonesUiGameplaySettings.h is C++ only: it names babylon::ui."
#endif

#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <vector>

// Runs the Gameplay screen through ui. bEnhancedProfile is the run's game.profile: true for
// Enhanced, false for Retail. On OK it applies the original options, writes their Jones.cfg keys,
// writes the enhancement keys and returns 1. The profile itself is fixed by the build, so its row
// is shown disabled and changes nothing. On back it returns 2 and writes nothing. pPainter may be
// null, which runs the screen without drawing.
int JonesUiGameplay_RunGameplayScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    bool bEnhancedProfile);

#ifdef J3D_TEST
// Test support: appends every screen this module builds, for the action-id audit.
void JonesUiGameplay_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIGAMEPLAYSETTINGS_H
