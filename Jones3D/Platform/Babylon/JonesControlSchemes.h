#ifndef JONES3D_PLATFORM_BABYLON_JONESCONTROLSCHEMES_H
#define JONES3D_PLATFORM_BABYLON_JONESCONTROLSCHEMES_H
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/types.h>
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <std/types.h>

J3D_EXTERN_C_START

// Added: the Babylon column's control scheme files (.kfg), ported from the removed Win32
// jonesConfig.c without any dialog code. The file format, default schemes, key-sets directory and
// binding calls are the original ones; see Docs/arc42/05-building-blocks/Control-And-Input.md and
// ADR-0026. This file and the Null column's jonesConfig startup are mutually exclusive: the Null
// column keeps its stub, the Babylon column links this module.

// The original joystick axis pseudo control ids.
#define JONESCONFIG_JOYAXISKID_UP       0x8000u
#define JONESCONFIG_JOYAXISKID_RIGHT    0x8001u
#define JONESCONFIG_JOYAXISKID_DOWN     0x8002u
#define JONESCONFIG_JOYAXISKID_LEFT     0x8003u
#define JONESCONFIG_JOYAXISKID_TWISTL   0x8004u
#define JONESCONFIG_JOYAXISKID_TWISTR   0x8005u
#define JONESCONFIG_JOYAXISKID_AIR      0x8006u
#define JONESCONFIG_JOYAXISKID_GROUND   0x8007u
#define JONESCONFIG_NUM_JOYAXISKIDS     8u

// Added: the two gamepad axes the original scheme format cannot express (right stick X/Y). They
// are stored in the trailing EXTRAS section, which the original reader skips after its 37 action
// lines. See ADR-0026.
#define JONESCONTROLSCHEMES_JOYAXIS_RX_NEG  0x8008u
#define JONESCONTROLSCHEMES_JOYAXIS_RX_POS  0x8009u
#define JONESCONTROLSCHEMES_JOYAXIS_RY_NEG  0x800Au
#define JONESCONTROLSCHEMES_JOYAXIS_RY_POS  0x800Bu

#define JONESCONFIG_CONTROLSTRING_MAXLEN 256u

// The in-memory list of schemes, the original sJonesControlsConfig.
typedef struct sJonesControlsConfig
{
    int maxNameLen;
    size_t numDeleteSchemes;
    char (*paDeleteSchemes)[128];
    size_t numSchemes;
    int selectedShemeIdx; // can be -1
    JonesControlsScheme* aSchemes;
} JonesControlsConfig;

// Startup/shutdown of the Babylon column's jonesConfig side. The windowed build loads the active
// scheme and binds it; the headless run does nothing and keeps the engine's default bindings.
int jonesConfig_Startup(void);
void jonesConfig_Shutdown(void);

// Added: applies the active scheme again after sithControl has registered its functions. The
// original bound the scheme from the HUD startup, before sithControl_Startup, so its bind calls
// were dropped. JonesControl_Startup calls this once after it has registered the functions, only
// in the windowed Babylon build.
void J3DAPI JonesControlSchemes_BindActive(void);

// The loaded active scheme, or NULL. Owned by the module; do not free.
const JonesControlsScheme* J3DAPI JonesControlSchemes_GetActiveScheme(void);

// The directory the original built: <installPath>\<SITHSTRING_KEYSETS> when installPath is set,
// else ..\<SITHSTRING_KEYSETS> relative to the working directory. Empty before startup.
const char* J3DAPI JonesControlSchemes_GetDirectory(void);

// True when a binding uses the trailing EXTRAS section (mouse buttons, right stick and the Z
// trigger axes) instead of the original action lines.
int J3DAPI JonesControlSchemes_IsExtraControl(size_t controlId);

// Number of built-in default schemes (the original three).
size_t J3DAPI JonesControlSchemes_GetNumDefaultSchemes(void);

// Name of a built-in default scheme, resolved through the string table with the JONES_STR_ key as
// fallback.
const char* J3DAPI JonesControlSchemes_GetDefaultSchemeName(size_t index);

// Fills pScheme with default scheme index; returns 1 on success.
int J3DAPI JonesControlSchemes_SetDefaultScheme(JonesControlsScheme* pScheme, size_t index);

// Loads one .kfg file. Returns 1 on success, 0 on a malformed file or I/O error; the reason is
// logged. pConfig is reset on failure. The path's file stem becomes aName.
int J3DAPI JonesControlSchemes_LoadScheme(const char* pFilePath, JonesControlsScheme* pConfig);

// Writes pScheme to <directory>/<aName>.kfg. Returns 1 on success.
int J3DAPI JonesControlSchemes_SaveScheme(const JonesControlsScheme* pScheme);

// Deletes <directory>/<name>.kfg. Returns 1 when the file is gone (including when absent).
int J3DAPI JonesControlSchemes_DeleteScheme(const char* pName);

// Creates pOut as a copy of default scheme 0 named pName. Returns 1 on success.
int J3DAPI JonesControlSchemes_CreateScheme(const char* pName, JonesControlsScheme* pOut);

// Writes controls.configFile and binds the scheme. Returns 1 on success.
int J3DAPI JonesControlSchemes_SetActiveScheme(const JonesControlsScheme* pScheme);

// Loads the three defaults, then every .kfg in the key-sets directory. On success pConfig owns
// memory that jonesConfig_FreeControlConfigEntry releases.
int J3DAPI JonesControlSchemes_LoadAllSchemes(JonesControlsConfig* pConfig);

// One action's binding access, for the Controls screen (M7.17b).
size_t J3DAPI JonesControlSchemes_GetNumBindings(const JonesControlsScheme* pScheme, JonesControlActions action);
int J3DAPI JonesControlSchemes_GetBinding(const JonesControlsScheme* pScheme, JonesControlActions action, size_t index, size_t* pControlId);
int J3DAPI JonesControlSchemes_AddBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t controlId);
int J3DAPI JonesControlSchemes_RemoveBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t index);
int J3DAPI JonesControlSchemes_SetBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t index, size_t controlId);
void J3DAPI JonesControlSchemes_ResetToDefault(JonesControlsScheme* pScheme, size_t defaultIndex);
void J3DAPI JonesControlSchemes_ResetAction(JonesControlsScheme* pScheme, JonesControlActions action, size_t defaultIndex);
const char* J3DAPI JonesControlSchemes_GetActionName(JonesControlActions action);
const char* J3DAPI JonesControlSchemes_GetCapActionName(JonesControlActions action);

// Formats a control id (keyboard, mouse button, joystick button/POV/axis) the way the scheme files
// and the Controls screen show it. pDest must hold JONESCONFIG_CONTROLSTRING_MAXLEN bytes.
void J3DAPI jonesConfig_ControlToString(size_t controlId, char* pDest);

// Parses a control name back to a control id. Returns 1 on success.
int J3DAPI JonesControlSchemes_ParseControlName(const char* pName, size_t* pControlId);

// Maps an engine axis control id (an aid with POSITIVE/NEGATIVE flags) to the scheme pseudo id
// used in EXTRA blocks, or returns (size_t)-1 when the axis has no scheme representation.
size_t J3DAPI JonesControlSchemes_MapAxisControl(size_t engineAxisControlId);

// The recovered functions. They keep the original names so the port can be compared with the
// decompilation. jonesConfig_EnableMouseControl is declared in jonesConfig.h.
int J3DAPI jonesConfig_InitKeySetsPath(void);
int J3DAPI jonesConfig_InitKeyActions(void);
JonesControlsScheme* J3DAPI jonesConfig_LoadActiveControlScheme(void);
int J3DAPI jonesConfig_LoadControlScheme(const char* pFilePath, JonesControlsScheme* pConfig);
int J3DAPI jonesConfig_SetDefaultControlScheme(JonesControlsScheme* pScheme, size_t num);
void J3DAPI jonesConfig_BindControls(JonesControlsScheme* pConfig);
void J3DAPI jonesConfig_BindJoystickControl(SithControlFunction functionId, size_t controlId);
void J3DAPI jonesConfig_FreeControlScheme(JonesControlsScheme* pScheme);
void J3DAPI jonesConfig_FreeControlConfigEntry(JonesControlsConfig* pConfig);
int J3DAPI jonesConfig_CopyControlConfig(JonesControlsConfig* pSrc, JonesControlsConfig* pDst);
JonesControlsScheme* J3DAPI jonesConfig_CloneControlSchemesList(JonesControlsScheme* aSchemes, size_t start, size_t numSchemes);
int J3DAPI jonesConfig_ControlOptions_LoadAllControlSchemes(JonesControlsConfig* pConfig);
int J3DAPI jonesConfig_ControlOptions_SetAllDefaultControlSchemes(JonesControlsConfig* pConfig);
int J3DAPI jonesConfig_ControlOptions_WriteScheme(JonesControlsScheme* pScheme);
JonesControlsScheme* J3DAPI jonesConfig_GetDefaultControlScheme(JonesControlsConfig* pConfig);

J3D_EXTERN_C_END
#endif // JONES3D_PLATFORM_BABYLON_JONESCONTROLSCHEMES_H
