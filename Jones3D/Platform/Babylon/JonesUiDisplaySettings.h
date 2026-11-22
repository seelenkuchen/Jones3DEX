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
#include <optional>
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

// Added: the "Brightness..." row opens the calibration screen. The standalone Display screen
// handles it itself; a caller that builds this screen as a tab (Options) must handle the same
// action and run JonesUiBrightness_Run while its screen is open.
inline constexpr babylon::ui::ActionId kJonesDisplayActionBrightness = 4;

// Collects the video modes of one display device, deduplicated by width and height in the order
// the environment lists them. Returns false when the environment, the device or its mode list is
// missing, and leaves modes empty.
bool JonesUiDisplay_CollectVideoModes(const StdDisplayEnvironment* pEnv, int displayDeviceNum,
    std::vector<JonesUiDisplayVideoMode>& modes);

// The values a Display screen edits and the widget indices its OK handler
// reads. Shared by the standalone Display screen and the tabbed Options
// screen.
struct JonesUiDisplayValues
{
    bool bBitDepth = false;
    std::size_t bitDepth = 0;
    bool bResolution = false;
    std::size_t resolution = 0;
    bool bWindowed = false;
    std::size_t filter = 0;
    bool bFog = false;
    double fogDensity = 1.0;
    std::size_t performance = 0;
    bool bHiPoly = false;
    bool bLitFaces = false;
    double gameplayFOV = 90.0; // Added: Field of view, horizontal degrees at 4:3.
    // Added: index into the render scale choices (50..200%, 100% at index 3).
    std::size_t renderScale = 3;
    // Added: index into the anti-aliasing choices (Off, SMAA; SMAA at index 1 in Enhanced).
    std::size_t antiAliasing = 1;
    // Added: index into the bloom choices (Off, Low, Medium, High; Low at index 1 in Enhanced).
    std::size_t bloom = 1;
    // Added: index into the light-shaft choices (Off, Low, Medium, High; Low at index 1 in
    // Enhanced).
    std::size_t lightShafts = 1;
    // Added: index into the ambient-occlusion choices (Off, Low, Medium, High; Low at index 1 in
    // Enhanced).
    std::size_t ssao = 1;
    // Added: lens flare, a toggle (on in Enhanced, off in Retail/QOL-off).
    bool bLensFlare = true;
    // Added: vignette and film grain, toggles (off in every build).
    bool bVignette = false;
    bool bFilmGrain = false;
    // Added: water ripples, a toggle (on in Enhanced, off in Retail/QOL-off).
    bool bWaterRipples = true;
    // Added: depth of field, a toggle for cutscene cameras and one for gameplay cameras.
    bool bDofCutscenes = true;
    bool bDofGameplay = true;
    // Added: index into the sharpening choices (Off, Low, Medium, High; Low at index 1 in
    // Enhanced).
    std::size_t sharpening = 1;
    // Added: index into the anisotropic choices (1, 2, 4, 8, 16; 8 at index 3).
    std::size_t anisotropic = 3;
    // Added: index into the tone-mapping choices (Clamp, Neutral, Filmic = operator 0, 1, 2;
    // Neutral at index 1 in Enhanced).
    std::size_t toneMapping = 1;
    // Added: exposure multiplier applied before the tone-map curve, 0.5..2.0.
    double exposure = 1.0;
    // Added: index into the color-grading choices (Neutral, Warm, Cool, Adventure, Vivid, Muted =
    // preset 0..5; Neutral at index 0 in every build).
    std::size_t colorGrading = 0;
    bool bCrispPixels = false;
};

struct JonesUiDisplayLayout
{
    std::optional<std::size_t> bitDepth;
    std::optional<std::size_t> resolution;
    std::size_t windowed = 0;
    std::size_t filter = 0;
    std::size_t fog = 0;
    std::size_t fogDensity = 0;
    std::size_t performance = 0;
    std::size_t hiPoly = 0;
    std::size_t litFaces = 0;
    std::size_t gameplayFOV = 0; // Added
    std::size_t renderScale = 0;
    std::size_t antiAliasing = 0; // Added
    std::size_t bloom = 0; // Added
    std::size_t lightShafts = 0; // Added: M9.16 light-shaft level
    std::size_t ssao = 0; // Added: M9.17b ambient-occlusion level
    std::size_t lensFlare = 0; // Added: M9.18 lens flare toggle
    std::size_t vignette = 0; // Added: M9.18 vignette toggle
    std::size_t filmGrain = 0; // Added: M9.18 film grain toggle
    std::size_t waterRipples = 0; // Added: M9.17 water ripples toggle
    std::size_t dofCutscenes = 0; // Added: M9.14 depth of field in cutscenes
    std::size_t dofGameplay = 0; // Added: M9.14 depth of field in gameplay
    std::size_t sharpening = 0; // Added
    std::size_t anisotropic = 0; // Added
    std::size_t toneMapping = 0; // Added
    std::size_t exposure = 0; // Added
    std::size_t colorGrading = 0; // Added
    std::size_t crispPixels = 0; // Added
    std::size_t brightness = 0; // Added: opens the brightness calibration screen
    std::size_t ok = 0;
    std::size_t back = 0;
};

// Everything the screen needs from the game and the display environment,
// captured when it opens.
struct JonesUiDisplayModel
{
    const StdDisplayEnvironment* pDisplayEnv = nullptr;
    JonesDisplaySettings* pSettings = nullptr;
    std::vector<JonesUiDisplayVideoMode> modes;
    std::vector<int> bitDepths;
    bool bHasCurMode = false;
    std::size_t curModeNum = 0;
    int curBpp = 0;
    std::size_t curBitDepth = 0;
    std::size_t curResolution = 0;
    int perfLevel = 4;
    bool bHiPoly = false;
    bool bLitFaces = false;
    float gameplayFOV = 90.0f; // Added: sithCamera_GetGameplayFOV()
    float renderScale = 1.0f;
    int antiAliasing = 1;       // Added: engine.renderer.antiAliasing, 0 off / 1 SMAA
    int bloom = 1;              // Added: engine.renderer.bloom, 0 off / 1 low / 2 medium / 3 high
    int lightShafts = 1;        // Added: engine.renderer.lightShafts, 0 off / 1 low / 2 medium / 3 high
    int ssao = 1;               // Added: engine.renderer.ssao, 0 off / 1 low / 2 medium / 3 high
    bool bLensFlare = true;     // Added: engine.renderer.lensFlare
    bool bVignette = false;     // Added: engine.renderer.vignette
    bool bFilmGrain = false;    // Added: engine.renderer.filmGrain
    bool bWaterRipples = true;  // Added: engine.renderer.water.ripples
    bool bDofCutscenes = true;  // Added: engine.renderer.dof.cutscenes
    bool bDofGameplay = true;   // Added: engine.renderer.dof.gameplay
    float sharpening = 0.3f;    // Added: engine.renderer.sharpening, 0..1
    int anisotropic = 8;        // Added: graphics.anisotropicFilter, normalized
    int toneMapping = 1;        // Added: engine.renderer.toneMapping, 0 clamp / 1 Neutral / 2 ACES
    float exposure = 1.0f;      // Added: engine.renderer.exposure, 0.5..2.0
    int colorGrading = 0;       // Added: engine.renderer.colorGrading, 0 Neutral .. 5 Muted
    bool bCrispPixels = false;  // Added: engine.renderer.texturePixelated
};

// Reads the environment and settings into a model and the initial values.
JonesUiDisplayModel JonesUiDisplay_LoadModel(const StdDisplayEnvironment* pDisplayEnv,
    JonesDisplaySettings* pSettings, JonesUiDisplayValues& values);

// Builds the rows from values. bShowPerformance is false for the Options
// Display tab, which drops the Performance row while the apply path still
// writes the stored level unchanged.
babylon::ui::Screen JonesUiDisplay_BuildScreen(const JonesUiDisplayModel& model,
    const JonesUiDisplayValues& values, JonesUiDisplayLayout& layout, bool bShowPerformance);

// Reads the current widget values while screen is open.
void JonesUiDisplay_ReadValues(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    const JonesUiDisplayLayout& layout, JonesUiDisplayValues& values);

// Applies values exactly as the original OK handlers did. Returns 1.
int JonesUiDisplay_Apply(const JonesUiDisplayModel& model, const JonesUiDisplayValues& values);

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
