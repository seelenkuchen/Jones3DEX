#ifndef SITH_SITHCONTROL_H
#define SITH_SITHCONTROL_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <sith/Main/sithMain.h>
#include <std/types.h>

J3D_EXTERN_C_START

#define SITHCONTROL_MAXFUNCTIONS   58u
#define STDCONTROL_IDLECAM_TIMEOUT 90000 // 90 sec, time in milliseconds before switching to idle camera due to control inactivity

#define SITHCONTROL_CFG_ENGINE_INPUT_CONTROLSTYLE "engine.input.controlStyle" // Added: modern or classic control style, QOL builds only

extern int sithControl_g_controlOptions;
// extern int sithControl_g_controlOptions;

int J3DAPI sithControl_Startup(int bKeyboardForeground);
void J3DAPI sithControl_Shutdown(void);

void sithControl_DefaultInit(void); // Added

int J3DAPI sithControl_Open(void);
int J3DAPI sithControl_Close(void);
bool sithControl_IsOpen(void); // Added

void J3DAPI sithControl_RegisterAxisFunction(SithControlFunction functionId, SithControlFunctionFlag flag);
void J3DAPI sithControl_RegisterKeyFunction(SithControlFunction functionId);
void J3DAPI sithControl_BindControl(SithControlFunction functionId, size_t controlId, SithControlBindFlag flags);
SithControlBinding* J3DAPI sithControl_BindAxis(SithControlFunction functionId, size_t controlId, SithControlBindFlag flags);

void J3DAPI sithControl_UnbindFunction(SithControlFunction fctnID);
void J3DAPI sithControl_UnbindFunctionIndex(size_t funcId, size_t bindIndex);
void J3DAPI sithControl_UnbindFunctionControl(SithControlFunction fctnID, size_t controlId); // Added
void J3DAPI sithControl_UnbindControl(SithControlBindFlag flags, size_t controlId);

void J3DAPI sithControl_RegisterControlCallback(SithControlCallback pfCallback);
void J3DAPI sithControl_Update(float secDeltaTime, uint32_t msecDeltaTime);

float J3DAPI sithControl_GetKeyAsAxisNormalized(size_t axisId); // Added;
float J3DAPI sithControl_GetKeyAsAxis(SithControlFunction axisId);
float J3DAPI sithControl_GetAxis(SithControlFunction axisId);
int J3DAPI sithControl_GetKey(SithControlFunction keyId, int* pState);

// Added: per-frame virtual key overrides for the Modern control style. SetKeyOverride makes the
// function read as bPressed with amount (0..1) on its axes, ignoring its real bindings;
// SuppressKey makes a function without an override read as released and 0. ClearKeyOverrides
// drops both and keeps the previous frame's pressed state for the next press edge. Axis-only
// functions are rejected.
void J3DAPI sithControl_SetKeyOverride(SithControlFunction keyId, int bPressed, float amount); // Added
void J3DAPI sithControl_SuppressKey(SithControlFunction keyId); // Added
void J3DAPI sithControl_ClearKeyOverrides(void); // Added

// Added: the control style. QOL builds start Modern and non-QOL builds always stay Classic.
bool J3DAPI sithControl_IsModernControlStyle(void); // Added
void J3DAPI sithControl_SetModernControlStyle(bool bModern); // Added

void sithControl_ReadControls(void);
void sithControl_FinishRead(void);
void sithControl_RebindKeyboard(void); // Added

void sithControl_UnbindJoystickAxes(void);
void sithControl_RebindJoystick(void); // Added

void sithControl_UnbindMouseAxes(void);
void sithControl_RebindMouse(void); // Added

void sithControl_Reset(void); // Clears all bindings

void sithControl_RegisterControlFunctions(void);
void sithControl_RegisterKeyboardBindings(void);
void sithControl_RegisterJoystickBindings(void);
void sithControl_RegisterMouseBindings(void); // Added

void sithControl_EnableDevControls(bool bEnable); // Added

void sithControl_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // SITH_SITHCONTROL_H
