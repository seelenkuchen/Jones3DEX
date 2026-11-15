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

// The values a Gameplay screen edits, and the widget indices its OK handler reads. They are shared
// with the tabbed Options screen so both use the same rows and apply logic.
struct JonesUiGameplayValues
{
    bool bShowText = false;
    bool bShowHints = false;
    bool bRotateMap = false;
    std::size_t difficulty = 0;
    bool bDefaultRun = false;
    bool bReticle = false;
    bool bBlood = false;
    bool bFireFlash = false;
    bool bExternalMaterials = false;
    bool bExternalKeyframes = false;
};

struct JonesUiGameplayLayout
{
    std::size_t showText = 0;
    std::size_t showHints = 0;
    std::size_t rotateMap = 0;
    std::size_t difficulty = 0;
    std::size_t defaultRun = 0;
    std::size_t reticle = 0;
    std::size_t blood = 0;
    std::size_t profile = 0;
    std::size_t fireFlash = 0;
    std::size_t externalMaterials = 0;
    std::size_t externalKeyframes = 0;
    std::size_t ok = 0;
    std::size_t back = 0;
};

// Fills values from the running game and the config.
JonesUiGameplayValues JonesUiGameplay_LoadValues(bool bEnhancedProfile);

// Builds the rows, including the OK and Cancel buttons, from values.
babylon::ui::Screen JonesUiGameplay_BuildScreen(bool bEnhancedProfile,
    const JonesUiGameplayValues& values, JonesUiGameplayLayout& layout);

// Reads the current widget values while screen is open into values. A widget
// that is absent or of another kind keeps its previous value.
void JonesUiGameplay_ReadValues(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    const JonesUiGameplayLayout& layout, JonesUiGameplayValues& values);

// Applies values exactly as the original OK handler did. Returns kJonesDialogOk (1).
int JonesUiGameplay_Apply(const JonesUiGameplayValues& values, bool bEnhancedProfile);

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
