#include "sithModernCamera.h"

#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithModernControls.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerControls.h>

#include <std/General/stdControl.h>
#include <std/General/stdMath.h>

#include <math.h>

static float sithModernCamera_worldYaw;
static float sithModernCamera_pitch;

static bool sithModernCamera_bInitialized;
static bool sithModernCamera_bSnapping;
static float sithModernCamera_snapStartYaw;
static float sithModernCamera_snapTargetYaw;
static float sithModernCamera_snapDeltaYaw;
static float sithModernCamera_snapStartPitch;
static float sithModernCamera_snapTime;

static float sithModernCamera_ReadStickAxis(size_t numJoysticks, size_t axisIndex)
{
    float value = 0.0f;
    for ( size_t i = 0; i < numJoysticks; ++i )
    {
        float axisValue = (float)stdControl_ReadAxisRaw(STDCONTROL_GET_JOYSTICK_AXIS(i, axisIndex)) / 32767.0f;
        axisValue = STDMATH_CLAMP(axisValue, -1.0f, 1.0f);
        if ( fabsf(axisValue) > fabsf(value) )
        {
            value = axisValue;
        }
    }

    return value;
}

static void sithModernCamera_EnableStickAxes(size_t numJoysticks)
{
    for ( size_t i = 0; i < numJoysticks; ++i )
    {
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_RX(i));
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_RY(i));
    }
}

static void sithModernCamera_ApplyDeadzone(float* pX, float* pY)
{
    float x = *pX;
    float y = *pY;
    float length = sqrtf(x * x + y * y);
    if ( length <= SITHMODERNCAMERA_DEADZONE )
    {
        *pX = 0.0f;
        *pY = 0.0f;
        return;
    }

    float scale = (length - SITHMODERNCAMERA_DEADZONE) / (1.0f - SITHMODERNCAMERA_DEADZONE);
    if ( scale > 1.0f )
    {
        scale = 1.0f;
    }

    *pX = x / length * scale;
    *pY = y / length * scale;
}

static void sithModernCamera_FinishSnap(void)
{
    sithModernCamera_worldYaw  = sithModernCamera_snapTargetYaw;
    sithModernCamera_pitch     = 0.0f;
    sithModernCamera_bSnapping = false;
    sithModernCamera_snapTime  = 0.0f;
}

void sithModernCamera_ResetGlobals(void)
{
    sithModernCamera_worldYaw       = 0.0f;
    sithModernCamera_pitch          = 0.0f;
    sithModernCamera_bInitialized   = false;
    sithModernCamera_bSnapping      = false;
    sithModernCamera_snapStartYaw   = 0.0f;
    sithModernCamera_snapTargetYaw  = 0.0f;
    sithModernCamera_snapDeltaYaw   = 0.0f;
    sithModernCamera_snapStartPitch = 0.0f;
    sithModernCamera_snapTime       = 0.0f;
}

float J3DAPI sithModernCamera_GetYaw(void)
{
    return sithModernCamera_worldYaw;
}

float J3DAPI sithModernCamera_GetPitch(void)
{
    return sithModernCamera_pitch;
}

void J3DAPI sithModernCamera_SetYawPitch(float yaw, float pitch)
{
    sithModernCamera_worldYaw     = stdMath_NormalizeAngle(yaw);
    sithModernCamera_pitch        = STDMATH_CLAMP(pitch, SITHMODERNCAMERA_PITCHMIN, SITHMODERNCAMERA_PITCHMAX);
    sithModernCamera_bInitialized = true;
    sithModernCamera_bSnapping    = false;
    sithModernCamera_snapTime     = 0.0f;
}

bool J3DAPI sithModernCamera_IsPlayerExternalCamera(const SithCamera* pCamera, const SithThing* pFocus)
{
    if ( !pCamera || !pFocus )
    {
        return false;
    }

    if ( !sithControl_IsModernControlStyle() )
    {
        return false;
    }

    if ( pCamera->type != SITHCAMERA_EXTERNAL || sithCamera_g_pCurCamera != pCamera )
    {
        return false;
    }

    if ( pFocus != sithPlayer_g_pLocalPlayerThing )
    {
        return false;
    }

    if ( sithPlayerControls_g_bCutsceneMode || sithCamera_g_bExtCameraLookMode )
    {
        return false;
    }

    return (sithCamera_g_stateFlags & SITHCAMERA_STATE_CUTSCENE) == 0;
}

bool J3DAPI sithModernCamera_IsActive(const SithCamera* pCamera, const SithThing* pFocus)
{
    if ( !sithModernCamera_IsPlayerExternalCamera(pCamera, pFocus) )
    {
        return false;
    }

    // Added: the free camera stays anchored in camera-relative and None states. Only the
    // original-camera set lets the original camera follow Indy.
    return sithModernControls_GetMode(pFocus) != SITHMODERNCONTROL_MODE_RELATIVE;
}

void J3DAPI sithModernCamera_Advance(float stickX, float stickY, float mouseX, float mouseY, float secDeltaTime)
{
    if ( secDeltaTime < 0.0f )
    {
        secDeltaTime = 0.0f;
    }

    if ( sithModernCamera_bSnapping )
    {
        sithModernCamera_snapTime += secDeltaTime;
        float progress = sithModernCamera_snapTime / SITHMODERNCAMERA_SNAPSECONDS;
        if ( progress >= 1.0f )
        {
            sithModernCamera_FinishSnap();
            return;
        }

        float smooth = progress * progress * (3.0f - 2.0f * progress);
        sithModernCamera_worldYaw = stdMath_NormalizeAngle(
            sithModernCamera_snapStartYaw + sithModernCamera_snapDeltaYaw * smooth);
        sithModernCamera_pitch = sithModernCamera_snapStartPitch * (1.0f - smooth);
        return;
    }

    sithModernCamera_ApplyDeadzone(&stickX, &stickY);

    sithModernCamera_worldYaw -= stickX * SITHMODERNCAMERA_YAWSPEED * secDeltaTime;
    sithModernCamera_pitch    -= stickY * SITHMODERNCAMERA_PITCHSPEED * secDeltaTime;

    sithModernCamera_worldYaw += mouseX * SITHMODERNCAMERA_MOUSESENSITIVITY;
    sithModernCamera_pitch    -= mouseY * SITHMODERNCAMERA_MOUSESENSITIVITY;

    sithModernCamera_pitch    = STDMATH_CLAMP(sithModernCamera_pitch, SITHMODERNCAMERA_PITCHMIN, SITHMODERNCAMERA_PITCHMAX);
    sithModernCamera_worldYaw = stdMath_NormalizeAngle(sithModernCamera_worldYaw);
}

void J3DAPI sithModernCamera_SnapTo(float targetYaw)
{
    sithModernCamera_snapStartYaw  = sithModernCamera_worldYaw;
    sithModernCamera_snapTargetYaw = stdMath_NormalizeAngle(targetYaw);
    sithModernCamera_snapDeltaYaw  = stdMath_NormalizeAngleAcute(sithModernCamera_snapTargetYaw - sithModernCamera_worldYaw);
    sithModernCamera_snapStartPitch = sithModernCamera_pitch;
    sithModernCamera_snapTime      = 0.0f;
    sithModernCamera_bSnapping     = true;

    if ( sithModernCamera_snapDeltaYaw == 0.0f && sithModernCamera_snapStartPitch == 0.0f )
    {
        sithModernCamera_FinishSnap();
    }
}

void J3DAPI sithModernCamera_TrackYaw(float targetYaw, float secDeltaTime)
{
    if ( secDeltaTime < 0.0f )
    {
        secDeltaTime = 0.0f;
    }

    float delta = stdMath_NormalizeAngleAcute(stdMath_NormalizeAngle(targetYaw) - sithModernCamera_worldYaw);
    float step  = SITHMODERNCAMERA_LOCKTRACKSPEED * secDeltaTime;

    if ( fabsf(delta) <= step )
    {
        sithModernCamera_worldYaw = stdMath_NormalizeAngle(targetYaw);
    }
    else
    {
        sithModernCamera_worldYaw = stdMath_NormalizeAngle(sithModernCamera_worldYaw + (delta > 0.0f ? step : -step));
    }

    sithModernCamera_bInitialized = true;
    sithModernCamera_bSnapping    = false;
    sithModernCamera_snapTime     = 0.0f;
}

void J3DAPI sithModernCamera_SyncYawFromOrient(const rdMatrix34* pOrient)
{
    if ( !pOrient )
    {
        return;
    }

    rdVector3 pyr;
    rdMatrix_ExtractAngles34(pOrient, &pyr);
    sithModernCamera_worldYaw     = stdMath_NormalizeAngle(pyr.yaw);
    sithModernCamera_bInitialized = true;
    sithModernCamera_bSnapping    = false;
    sithModernCamera_snapTime     = 0.0f;
}

bool J3DAPI sithModernCamera_Update(const SithCamera* pCamera, const SithThing* pFocus, float secDeltaTime)
{
    if ( !sithModernCamera_IsActive(pCamera, pFocus) )
    {
        return false;
    }

    size_t numJoysticks = stdControl_GetNumJoysticks();
    if ( !sithModernCamera_bInitialized )
    {
        // Added: the first free frame starts behind Indy; afterwards the orbit stays in the world.
        rdVector3 pyr;
        rdMatrix_ExtractAngles34(&pFocus->orient, &pyr);
        sithModernCamera_worldYaw     = stdMath_NormalizeAngle(pyr.yaw);
        sithModernCamera_pitch        = 0.0f;
        sithModernCamera_bInitialized = true;
    }

    sithModernCamera_EnableStickAxes(numJoysticks);

    // Added: a held target lock owns the camera yaw and eases it toward Indy -> target.
    float lockYaw;
    if ( sithModernControls_GetLockCameraYaw(&lockYaw) )
    {
        sithModernCamera_TrackYaw(lockYaw, secDeltaTime);
        return true;
    }

    float stickX = sithModernCamera_ReadStickAxis(numJoysticks, STDCONTROL_JOYSTICK_AXIS_RX);
    float stickY = sithModernCamera_ReadStickAxis(numJoysticks, STDCONTROL_JOYSTICK_AXIS_RY);
    float mouseX = sithControl_GetAxis(SITHCONTROL_MOUSETURN);
    float mouseY = sithControl_GetAxis(SITHCONTROL_PITCH);

    int pressed = 0;
    if ( sithControl_GetKey(SITHCONTROL_LOOK, &pressed) && pressed )
    {
        rdVector3 pyr;
        rdMatrix_ExtractAngles34(&pFocus->orient, &pyr);
        sithModernCamera_SnapTo(pyr.yaw);
    }

    sithModernCamera_Advance(stickX, stickY, mouseX, mouseY, secDeltaTime);
    return true;
}

void J3DAPI sithModernCamera_ComputeEyePosition(rdVector3* pPos, const rdVector3* pFocus, const rdVector3* pOffset, float yaw, float pitch)
{
    rdVector3 pyr = { pitch, yaw, 0.0f };
    rdMatrix34 orient;
    rdMatrix_BuildRotate34(&orient, &pyr);
    rdMatrix_PreTranslate34(&orient, pOffset);
    rdVector_Add3(pPos, &orient.dvec, pFocus);
}
