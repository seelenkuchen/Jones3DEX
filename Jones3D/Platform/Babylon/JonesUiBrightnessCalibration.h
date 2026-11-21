// Added: the Babylon column's brightness calibration screen (M9.11). It shows a dark test
// pattern of three symbols on black, drawn with the final tone-map pass's own formula, so it
// matches what the 3D view presents. The slider writes engine.renderer.brightness (0.6..1.8,
// 0.05 steps, 1.0 identity); OK and Back both keep the value, as Options does, and write the key.
//
// The first-start wrapper shows it before the main menu when Jones.cfg has no brightness key.
// The Display screen's "Brightness..." row opens the same screen while the game is running.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIBRIGHTNESSCALIBRATION_H
#define JONES3D_PLATFORM_BABYLON_JONESUIBRIGHTNESSCALIBRATION_H
#ifndef __cplusplus
#error "JonesUiBrightnessCalibration.h is C++ only: it names babylon::ui types."
#endif

#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>

#include <cstdint>
#include <vector>

// The calibration value and its step. The same bounds the display settings read and the frame
// owner clamps engine.renderer.brightness to.
inline constexpr float kJonesUiBrightnessMin     = 0.6f;
inline constexpr float kJonesUiBrightnessMax     = 1.8f;
inline constexpr float kJonesUiBrightnessStep    = 0.05f;
inline constexpr float kJonesUiBrightnessDefault = 1.0f;

// The displayed color of one scene value after the tone-map pass: exposure, operator (0 clamp,
// 1 Khronos PBR Neutral, 2 ACES fitted), then pow(saturate(x), 1 / brightness). It mirrors
// Shaders/tonemap.frag.hlsl exactly enough for the calibration swatches. Alpha is 1.
babylon::ui::Color JonesUiBrightness_MapSceneColor(float sceneValue, float exposure,
    std::uint32_t oper, float brightness);

// True when engine.renderer.brightness is missing from a started Jones.cfg, so the first start
// must show the calibration screen. False without a started config.
bool JonesUiBrightness_NeedsCalibration(void);

// Runs the calibration screen through ui and returns 1 after OK, 2 after back. The value is
// written to Jones.cfg either way. pPainter may be null; it then runs without the test pattern.
// Called from Options while the shared context is held, so the screen reuses the Options skin and
// background.
int JonesUiBrightness_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter);

// The blocking first-start wrapper. It needs the windowed column's display and window; it returns
// 2 without showing anything in the headless or Null columns.
int JonesUiBrightness_Show(void);

#ifdef J3D_TEST
// Test support: appends the screen with one value, for the action-id audit.
void JonesUiBrightness_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIBRIGHTNESSCALIBRATION_H
