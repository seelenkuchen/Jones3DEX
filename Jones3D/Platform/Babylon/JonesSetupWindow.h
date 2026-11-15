// The setup wizard's window: its own windowed babylon::ui screen loop, built from the model. Run
// it from the composition root before the game window exists; see JonesSetupWindow.cpp.
#ifndef JONES3D_PLATFORM_BABYLON_JONESSETUPWINDOW_H
#define JONES3D_PLATFORM_BABYLON_JONESSETUPWINDOW_H
#ifndef __cplusplus
#error "JonesSetupWindow.h is C++ only: it names babylon::ui and babylon::platform types."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>

#include <cstddef>
#include <string>
#include <vector>

namespace babylon::ui
{
    class Ui;
}

namespace babylon::files
{
    class FileSystem;
}

namespace babylon::platform
{
    class Platform;
}

class JonesSetupWizard;

enum class JonesSetupResult
{
    Installed,
    Cancelled,
    Failed
};

// Runs the setup window until the player finishes or cancels. userDir is the user directory that
// main resolved; exeDir the executable's folder ("" if unknown). On Installed, outDataDir is the
// data folder that now holds a complete install (<chosen base>/GameData) and outUserDir the user
// directory the player chose (userDir, or exeDir for "next to the game"). Failed means no window
// or GPU device could be made and the native-dialog fallback was declined or failed too.
JonesSetupResult JonesSetupWindow_Run(babylon::platform::Platform& platform,
    babylon::files::FileSystem& fs, const std::string& userDir, const std::string& exeDir,
    std::string& outUserDir, std::string& outDataDir);

// The wizard's own look: the game skin's colours with a wide centered panel and no reserved help
// column, so the wrapped text gets about 85% of the canvas width under the title.
babylon::ui::Skin JonesSetupWindow_CreateSkin(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter);

// Builds the screen of the model's current step. The window uses it every time the step changes;
// the model tests lay the result out with a NullPainter. Text is wrapped and paths are elided
// against the real painter and the wizard panel's text width.
babylon::ui::Screen JonesSetupWindow_BuildScreen(const JonesSetupWizard& wizard,
    babylon::ui::Painter& painter, float canvasWidth, const babylon::ui::Skin& skin,
    std::size_t initialFocus = babylon::ui::kNoWidget);

// One scroll position of the licence text box: the wrapped lines, the first visible line and how
// many lines fit.
struct JonesSetupLicenceView
{
    const std::vector<std::string>* pLines = nullptr;
    std::size_t offset = 0;
    std::size_t visible = 12;
};

// How many licence lines fit the wizard panel at the fixed canvas height.
std::size_t JonesSetupWindow_LicenceVisibleLines(const babylon::ui::Skin& skin);

// Wraps the licence text against the wizard panel's text column, collapsing blank-line runs.
std::vector<std::string> JonesSetupWindow_WrapLicenceText(const std::string& text,
    babylon::ui::Painter& painter, const babylon::ui::Skin& skin, float canvasWidth);

// The licence screen for one scroll position.
babylon::ui::Screen JonesSetupWindow_BuildLicenceScreen(const JonesSetupWizard& wizard,
    const babylon::ui::Skin& skin, const JonesSetupLicenceView& view);

// Draws the progress bar into the Progress screen's bar row: the track, then the fill over it.
// The window calls this after Ui::draw(); the model test records both rectangles with a
// NullPainter.
void JonesSetupWindow_DrawProgressBar(babylon::ui::Painter& painter, const babylon::ui::Ui& ui,
    const babylon::ui::Skin& skin, double fraction);

#endif // JONES3D_PLATFORM_BABYLON_JONESSETUPWINDOW_H
