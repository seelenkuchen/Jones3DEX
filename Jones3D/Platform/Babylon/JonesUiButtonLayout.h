// Added: the "Button Layout" binding screens (ADR-0036). One screen is shared by the Controller
// and Keyboard & Mouse variants; the device switch changes the rows, the icon set and the
// capture rules, not the layout. The model keeps focus, scrolling and capture outside the
// drawing, so a unit test runs it without a GPU.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIBUTTONLAYOUT_H
#define JONES3D_PLATFORM_BABYLON_JONESUIBUTTONLAYOUT_H
#ifndef __cplusplus
#error "JonesUiButtonLayout.h is C++ only: it names babylon::ui types."
#endif

#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Ui.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

enum class JonesUiButtonLayoutDevice
{
    Controller,
    KeyboardMouse,
};

// One row of the action list: the action, its display name, the first binding of this screen's
// device (or kNoControlId) and the em-dash text the row draws when it has none.
inline constexpr std::size_t kJonesUiButtonLayoutNoControl = (std::size_t)-1;

struct JonesUiButtonLayoutRow
{
    JonesControlActions action = JONESCONTROL_ACTION_WALKFORWARD;
    std::string name;
    std::size_t controlId = kJonesUiButtonLayoutNoControl;
    std::string binding;
    JonesUiStickOwnership stickOwnership = JonesUiStickOwnership::None;
};

// The images the screen draws. Missing files leave their id 0, which draws no icon and no
// diagram. diagram is the Xbox pad drawing; icons maps a control id to its prompt icon.
struct JonesUiButtonLayoutAssets
{
    babylon::ui::ImageId diagram = 0;
    babylon::ui::ImageId mouse = 0;
    std::unordered_map<std::size_t, babylon::ui::ImageId> icons;
};

// The working state of one binding screen. pConfig and schemeIndex name the scheme the screen
// edits; RefreshRows rebuilds rows from it. focus and scroll are in row indices.
struct JonesUiButtonLayoutModel
{
    JonesControlsConfig* pConfig = nullptr;
    std::size_t schemeIndex = 0;
    JonesUiButtonLayoutDevice device = JonesUiButtonLayoutDevice::Controller;
    std::vector<JonesUiButtonLayoutRow> rows;
    std::size_t focus = 0;
    std::size_t scroll = 0;
    bool capturing = false;
    bool bChanged = false;
};

// The number of list rows that fit between the header and the prompt bar.
inline constexpr std::size_t kJonesUiButtonLayoutVisibleRows = 8;

// Added: the Data/ui-relative Xelu prompt icon for a control, such as the interaction prompt
// needs. pControllerSet is "xbox", "ps5", "switch" or "steamdeck"; null or empty means Xbox. An
// empty result means the set has no icon for that control.
std::string JonesUiButtonLayout_GetPromptIconPath(std::size_t controlId,
    JonesUiButtonLayoutDevice device, const char* pControllerSet);

// True for a control id the given screen's device can bind: keyboard keys and mouse buttons for
// Keyboard & Mouse, gamepad buttons, POV and axes for Controller.
bool JonesUiButtonLayout_IsDeviceControl(std::size_t controlId,
    JonesUiButtonLayoutDevice device);

// Reads the scheme's first binding of the device for one action. Returns false when the action
// has none.
bool JonesUiButtonLayout_GetDeviceBinding(const JonesControlsScheme& scheme,
    JonesControlActions action, JonesUiButtonLayoutDevice device, std::size_t& controlId);

// Builds one row per action, in action order, from the scheme.
std::vector<JonesUiButtonLayoutRow> JonesUiButtonLayout_BuildRows(
    const JonesControlsScheme& scheme, JonesUiButtonLayoutDevice device);

// Rebuilds model.rows from the selected scheme and clamps focus and scroll.
void JonesUiButtonLayout_RefreshRows(JonesUiButtonLayoutModel& model);

// Moves focus by delta rows, clamped, and scrolls to keep it visible.
void JonesUiButtonLayout_MoveFocus(JonesUiButtonLayoutModel& model, long delta);

// Scrolls the list by delta rows, clamped to the rows that exist.
void JonesUiButtonLayout_Scroll(JonesUiButtonLayoutModel& model, long delta);

// The callout text for one pad input: the capitalised names of every action bound to controlId,
// joined by " | ", in action order. Empty when no action uses it.
std::string JonesUiButtonLayout_CalloutText(const JonesControlsScheme& scheme,
    std::size_t controlId);

// Replaces the action's first binding of this device, or adds one when it has none. The other
// device's bindings stay untouched.
bool JonesUiButtonLayout_SetDeviceBinding(JonesControlsScheme& scheme,
    JonesControlActions action, JonesUiButtonLayoutDevice device, std::size_t controlId);

// Restores this device's bindings of every action from one default scheme. The other device's
// bindings stay untouched.
void JonesUiButtonLayout_ResetDevice(JonesControlsScheme& scheme,
    JonesUiButtonLayoutDevice device, std::size_t defaultIndex);

// Draws the whole screen through a painter. The layout test calls it directly; the runner
// installs it as the screen's draw callback.
void JonesUiButtonLayout_Draw(const JonesUiButtonLayoutModel& model,
    babylon::ui::Painter& painter, const babylon::ui::Size& canvas,
    const babylon::ui::Skin& skin, const JonesUiButtonLayoutAssets& assets);

// Runs the screen for model's selected scheme. On Back it validates the required bindings,
// turns a built-in default into a custom copy, writes the scheme and rebinds. Returns 1 on
// Back. A missing required binding keeps the screen open with the editor's message.
int JonesUiButtonLayout_RunScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiButtonLayoutModel& model);

#ifdef J3D_TEST
// Test support: the number of events the screen has seen since the last reset. The scripted
// platform delivers one event per frame, so a test uses it to prove a nested screen stayed open
// rather than closing on the same action that opened it.
unsigned JonesUiButtonLayout_GetLayoutFramesForTest(void);
unsigned JonesUiButtonLayout_GetLayoutRunsForTest(void);
JonesUiButtonLayoutDevice JonesUiButtonLayout_GetLastDeviceForTest(void);
void JonesUiButtonLayout_ResetLayoutFramesForTest(void);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUIBUTTONLAYOUT_H
