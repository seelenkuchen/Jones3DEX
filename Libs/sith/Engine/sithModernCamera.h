#ifndef SITH_SITHMODERNCAMERA_H
#define SITH_SITHMODERNCAMERA_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <std/types.h>

J3D_EXTERN_C_START

// Added: free camera for the Modern control style (ADR-0029). The orbit is anchored in the
// world, so turning Indy does not turn the camera.
#define SITHMODERNCAMERA_YAWSPEED         180.0f // degrees per second at full right stick
#define SITHMODERNCAMERA_PITCHSPEED       120.0f // degrees per second at full right stick
#define SITHMODERNCAMERA_PITCHMIN         (-35.0f)
#define SITHMODERNCAMERA_PITCHMAX         50.0f
#define SITHMODERNCAMERA_DEADZONE         0.2f  // radial deadzone, rescaled
#define SITHMODERNCAMERA_MOUSESENSITIVITY 5.0f // degrees per mouse axis unit
#define SITHMODERNCAMERA_SNAPSECONDS      0.25f
#define SITHMODERNCAMERA_LOCKTRACKSPEED   240.0f // degrees per second toward a locked target

void sithModernCamera_ResetGlobals(void);

float J3DAPI sithModernCamera_GetYaw(void);
float J3DAPI sithModernCamera_GetPitch(void);
void J3DAPI sithModernCamera_SetYawPitch(float yaw, float pitch);

// Added: true while the camera is the current player external camera and no cutscene or COG
// camera owns it. IsActive also requires a move state that is not part of the original-camera
// set, so the free camera anchors through camera-relative transitions and None states.
bool J3DAPI sithModernCamera_IsPlayerExternalCamera(const SithCamera* pCamera, const SithThing* pFocus);
bool J3DAPI sithModernCamera_IsActive(const SithCamera* pCamera, const SithThing* pFocus);

// Added: integrate one frame of orbit input. Stick values are -1..1 with Y positive down; mouse
// values are the existing SITHCONTROL_MOUSETURN/SITHCONTROL_PITCH axes. The snap, when running,
// takes priority and ignores the orbit input.
void J3DAPI sithModernCamera_Advance(float stickX, float stickY, float mouseX, float mouseY, float secDeltaTime);

// Added: smoothly turn the orbit behind the given facing over SITHMODERNCAMERA_SNAPSECONDS.
void J3DAPI sithModernCamera_SnapTo(float targetYaw);

// Added: ease the orbit toward a locked target at a steady rate.
void J3DAPI sithModernCamera_TrackYaw(float targetYaw, float secDeltaTime);

// Added: while the original camera runs (non-free state), keep the orbit yaw on the yaw the
// original camera would have, so returning to a free state does not snap.
void J3DAPI sithModernCamera_SyncYawFromOrient(const rdMatrix34* pOrient);

// Added: read the input and advance the orbit for one camera update. Returns true while the free
// camera is active; sithCamera_Update then uses GetYaw/GetPitch instead of the focus orientation.
bool J3DAPI sithModernCamera_Update(const SithCamera* pCamera, const SithThing* pFocus, float secDeltaTime);

// Added: the eye position the external offset produces for a focus, yaw and pitch. Used by the
// unit tests and kept separate from the collision and interpolation path.
void J3DAPI sithModernCamera_ComputeEyePosition(rdVector3* pPos, const rdVector3* pFocus, const rdVector3* pOffset, float yaw, float pitch);

J3D_EXTERN_C_END
#endif // SITH_SITHMODERNCAMERA_H
