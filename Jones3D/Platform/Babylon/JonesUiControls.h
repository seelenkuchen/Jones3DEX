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
    std::vector<std::string> deleteFiles;
};

// The widget indices of one Controls layout. kNoWidget means the row is not
// present. The standalone screen has a Mouse row; the Options tab has a bind
// list instead.
struct JonesUiControlsLayout
{
    std::size_t scheme = 0;
    std::size_t newScheme = 0;
    std::size_t deleteScheme = 0;
    std::size_t list = babylon::ui::kNoWidget;
    std::size_t mouse = babylon::ui::kNoWidget;
    std::size_t rumble = 0;
    std::size_t edit = 0;
    std::size_t reset = 0;
    std::size_t ok = babylon::ui::kNoWidget;
    std::size_t back = babylon::ui::kNoWidget;
};

// Copies config into a model and reads the current mouse and rumble
// settings. Returns false when the copy fails.
bool JonesUiControls_InitModel(JonesUiControlsModel& model, JonesControlsConfig& config);

// Releases the model's scheme list. Safe to call more than once.
void JonesUiControls_FreeModel(JonesUiControlsModel& model);

// Builds the Options Controls tab: scheme, New, Delete, the bind list,
// Rumble, Edit and Reset, without OK or Cancel.
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
