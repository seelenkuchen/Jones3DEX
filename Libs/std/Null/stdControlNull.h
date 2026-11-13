// Added: the device side of the Null-column stdControl, one implementation per platform column.
#ifndef STD_STDCONTROLNULL_H
#define STD_STDCONTROLNULL_H
#include <stdbool.h>
#include <stdint.h>

#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Reads the input devices into the key states, the way stdControlDX9.c's ReadKeyboard/ReadMouse do:
// called once per stdControl_ReadControls after the per-read state is cleared. readTime is the
// read's time stamp (stdControl_curReadTime); bReadMouse says whether mouse buttons are read
// (stdControl_EnableMouse). The Null column has no devices: it reports the scripted key state
// stdControl_QueueKeyEvent left behind, as one snapshot over the keyboard ids and, when the mouse
// is enabled, the four engine mouse buttons.
void stdControl_ReadDevices(uint32_t readTime, bool bReadMouse);

// Queues one key or mouse-button change from a script (the headless runner's --input). keyId is an
// engine key id (stdKeys.h, STDCONTROL_KID_MOUSE_*). Returns false when the column cannot deliver
// that key. Both columns queue rather than apply: the key takes effect at the next
// stdControl_ReadControls, where stdControl_ReadDevices reports it in the DX9 snapshot order, so
// its press count survives that read's clear. Applying it at once, as the Null column used to, lost
// the count to the clear.
bool stdControl_QueueKeyEvent(int keyId, int bPressed);

// Added: writes one axis's raw state, the way stdControlDX9.c's device reads wrote
// stdControl_aAxisStates directly. The Babylon column's device adapter calls it after
// stdControl_ReadControls cleared the states and before stdControl_ReadAxis reads them. Out-of-range
// axis ids are ignored; the value is whatever the backend measured (the caller clamps).
void stdControl_SetAxisState(size_t aid, int value);

// Added: reports the connected gamepad count observed by the platform adapter. The shared std
// layer raises stdControl_GetJoystickChangeCount() when it differs from the previous observation;
// the first observation after startup only establishes the baseline. The Null column never calls
// it, so its change count stays 0.
void stdControl_ObserveJoystickCount(size_t numJoysticks);

// Added: forwards one rumble request to the connected gamepads. The shared stdControl_Rumble()
// checks the enabled flag and calls this; the Null column has no device and does nothing, the
// Babylon column asks the platform for every connected pad. Intensities are 0..1; the platform
// clamps them and the duration.
void stdControl_RumbleDevices(float lowFrequency, float highFrequency, uint32_t durationMs);

#ifdef J3D_TEST
// Added: test seams of the Null column's rumble device side. They record the requests the adapter
// forwarded, so a test can prove which game trigger asked for rumble and with what values.
void stdControl_ResetRumbleForTest(void);
size_t stdControl_GetRumbleCountForTest(void);
void stdControl_GetRumbleForTest(size_t index, float* pLowFrequency, float* pHighFrequency,
    uint32_t* pDurationMs);
#endif

J3D_EXTERN_C_END
#endif // STD_STDCONTROLNULL_H
