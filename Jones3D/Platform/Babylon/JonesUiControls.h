// Added: the Babylon column's Controls screen (ADR-0026), the original jonesConfig_ShowControlOptions
// dialog and its nested New, Edit, Assign Key and Reassign Key dialogs. The wrapper keeps the
// original return values: 1 applies the chosen scheme (writes modified custom schemes, makes the
// selection active and rebinds), 2 cancels and writes nothing.
//
// The scheme-name rule and the captured-binding mapping are the test surface: they keep the screen
// logic free of scripted platform events and let a test cover every control id the original and the
// EXTRAS section can name.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUICONTROLS_H
#define JONES3D_PLATFORM_BABYLON_JONESUICONTROLS_H
#ifndef __cplusplus
#error "JonesUiControls.h is C++ only: it names babylon::ui and std::string."
#endif

#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/types.h>

#include <babylon/ui/Binding.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <cstddef>
#include <string>
#include <vector>

// The original new-scheme name check, in the order it ran: empty, a character the Windows file
// system rejects, then a name already in the list.
enum class JonesUiControlsNameStatus
{
    Ok,
    Empty,
    Invalid,
    Duplicate,
};

JonesUiControlsNameStatus JonesUiControls_ValidateSchemeName(const char* pName,
    const JonesControlsConfig& config);

// Added: which stick the Modern control style owns an action with, or None when the action is
// free or the Classic style is active. The one check lives here so a later per-device Modern
// switch only changes this function.
enum class JonesUiStickOwnership
{
    None,
    Left,
    Right,
};

JonesUiStickOwnership JonesUiControls_GetModernStickOwnership(JonesControlActions action);

// Maps one babylon::ui binding to the game control id the scheme files use. Returns true and writes
// pControlId for a key, mouse button, gamepad button, POV direction or gamepad axis direction the
// engine and the EXTRAS section can express; returns false for an unmappable one (an unknown key,
// mouse button X2, an axis at rest, or a control with no name in the scheme format). The mapping is
// the same one the Babylon input adapter uses, so a captured control is what the game then binds.
bool JonesUiControls_MapBinding(const babylon::ui::Binding& binding, std::size_t& pControlId);

// The working state of a Controls screen. It owns the scheme list copy,
// the selected scheme, the settings and the files to delete on OK. Shared by
// the standalone Controls screen and the tabbed Options screen.
struct JonesUiControlsModel
{
    JonesControlsConfig working{};
    std::size_t selected = 0;
    bool bMouse = false;
    bool bRumble = false;
    bool bKeyboardModern = false; // Added: keyboard and mouse style, 0 Modern/1 Classic on screen
    bool bGamepadModern = false;  // Added: gamepad style, 0 Modern/1 Classic on screen
    std::vector<std::string> deleteFiles;
};

// The widget indices of one Controls layout. kNoWidget means the row is not
// present. The standalone screen shows the bind list and the Edit and Reset
// rows; the Options tab shows the Controller and Keyboard & Mouse entry rows
// instead, and keeps the Mouse camera row plus the two per-device style rows.
struct JonesUiControlsLayout
{
    std::size_t scheme = 0;
    std::size_t newScheme = 0;
    std::size_t deleteScheme = 0;
    std::size_t list = babylon::ui::kNoWidget;
    std::size_t mouse = babylon::ui::kNoWidget;
    std::size_t keyboardStyle = babylon::ui::kNoWidget;
    std::size_t gamepadStyle = babylon::ui::kNoWidget;
    std::size_t rumble = 0;
    std::size_t edit = babylon::ui::kNoWidget;
    std::size_t reset = babylon::ui::kNoWidget;
    std::size_t controller = babylon::ui::kNoWidget;
    std::size_t keyboard = babylon::ui::kNoWidget;
    std::size_t ok = babylon::ui::kNoWidget;
    std::size_t back = babylon::ui::kNoWidget;
};

// Copies config into a model and reads the current mouse and rumble
// settings. Returns false when the copy fails.
bool JonesUiControls_InitModel(JonesUiControlsModel& model, JonesControlsConfig& config);

// Releases the model's scheme list. Safe to call more than once.
void JonesUiControls_FreeModel(JonesUiControlsModel& model);

// Builds the Options Controls tab: scheme, New, Delete, the bind list,
// Mouse camera, Rumble, Edit and Reset, without OK or Cancel.
babylon::ui::Screen JonesUiControls_BuildOptionsTabScreen(const JonesUiControlsModel& model,
    JonesUiControlsLayout& layout);

// Handles the New, Delete, Edit and Reset actions, opening the nested
// screens as the original Controls screen did. Returns false for any other
// action.
bool JonesUiControls_HandleAction(babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    babylon::ui::ActionId action, babylon::ui::Painter* pPainter,
    JonesUiControlsModel& model, const JonesUiControlsLayout& layout);

// Reads the current Rumble and, when present, Mouse rows into model.
void JonesUiControls_ReadValues(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    const JonesUiControlsLayout& layout, JonesUiControlsModel& model);

// Applies the model on OK and adopts its list into config. Returns 1. On a
// failed apply it returns 2 and leaves config alone.
int JonesUiControls_Apply(JonesUiControlsModel& model, JonesControlsConfig& config);

// Added: helpers the button-layout screens share with the Controls editor. AskReassign opens the
// original reassign question and returns true on Yes. GetMissingActions returns the capitalised
// action names that still have no binding, one per line (empty when every required action is
// bound). ShowControlMessage opens the same message dialog the editor uses. InsertScheme inserts a
// custom scheme in name order and returns its new index in selected.
bool JonesUiControls_AskReassign(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesControlActions owner, JonesControlActions target, std::size_t controlId);
std::string JonesUiControls_GetMissingActions(const JonesControlsScheme& scheme);
void JonesUiControls_ShowControlMessage(const std::string& text);
bool JonesUiControls_InsertScheme(JonesControlsConfig& config,
    const JonesControlsScheme& scheme, std::size_t& selected);
std::size_t JonesUiControls_DefaultSchemeIndex(void);

// Runs the main Controls screen through ui. config is the loaded scheme list; the runner works on a
// copy. On OK it deletes the schemes marked for removal, writes every modified custom scheme, writes
// controls.mouse, makes the selected scheme active and rebinds; config then
// holds the applied list. On back it returns 2 and leaves config, the files and the bindings alone.
// pPainter may be null, which runs the screen without drawing.
int JonesUiControls_RunControlOptionsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesControlsConfig& config);

// Runs the Edit screen for one scheme. schemeIndex is the scheme's position in the list: at or past
// the built-in defaults the bindings can be changed and OK saves them, before them the screen is the
// original "View Scheme" read-only view. On OK it sets scheme.bModified and returns 1; on back it
// returns 2 and leaves scheme untouched. Also used by the drawing sample, which runs it directly.
int JonesUiControls_RunEditScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesControlsScheme& scheme, std::size_t schemeIndex);

#ifdef J3D_TEST
// Test support: appends every screen this module builds, for the action-id audit.
void JonesUiControls_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUICONTROLS_H
