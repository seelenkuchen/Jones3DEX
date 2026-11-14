#include "sithModernControls.h"

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithModernCamera.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerActions.h>
#include <sith/Gameplay/sithPlayerControls.h>

#include <rdroid/Math/rdMatrix.h>

#include <std/General/stdControl.h>
#include <std/General/stdMath.h>

#include <math.h>

// Added: the Modern control style's move translation (ADR-0029). The original movement code is
// left alone: this layer only sets the virtual keys it reads for the frame.

#define SITHMODERNCONTROL_STICK_RANGE         32767.0f
#define SITHMODERNCONTROL_STICK_DEADZONE      0.2f
#define SITHMODERNCONTROL_KEY_WALK_MAGNITUDE  0.5f
#define SITHMODERNCONTROL_KEY_RUN_MAGNITUDE   1.0f
#define SITHMODERNCONTROL_RUN_THRESHOLD       0.75f
#define SITHMODERNCONTROL_ZERO_EPSILON        0.0001f
#define SITHMODERNCONTROL_TURN_SPEED          1080.0f // degrees per second toward the target facing
#define SITHMODERNCONTROL_TURN_EASE_ANGLE     10.0f   // scale the step down below this angle
#define SITHMODERNCONTROL_TURN_SNAP_ANGLE     100.0f  // snapped in one update instead of rotating
#define SITHMODERNCONTROL_LOCK_SIDEWAYS       0.5f
#define SITHMODERNCONTROL_LOCK_LOST_SECONDS   0.5f
#define SITHMODERNCONTROL_QUICKSTEP_THRESHOLD 0.5f // one direction must pass this to step
#define SITHMODERNCONTROL_QUICKSTEP_REARM     0.3f // the stick must fall below this to step again
#define SITHMODERNCONTROL_ACCEL_TIME          0.15f  // seconds from rest to the original target speed
#define SITHMODERNCONTROL_GROUND_THRUST_SCALE 0.30000001f // the player physics' ground thrust scale
#define SITHMODERNCONTROL_SWIM_YAW_SPEED      360.0f // degrees per second toward the camera direction
#define SITHMODERNCONTROL_SWIM_PITCH_SPEED    180.0f // degrees per second toward the camera direction
#define SITHMODERNCONTROL_SWIM_EASE_ANGLE     10.0f  // ease the last degrees of a swim turn
#define SITHMODERNCONTROL_SWIM_SNAP_ANGLE     1.0f   // finish the last degree instead of a blind spot

static SithThing* sithModernControls_pLockedTarget;
static bool sithModernControls_bLockHeld;
static bool sithModernControls_bLockActive;
static bool sithModernControls_bLockWasHeld;
static float sithModernControls_secLockLost;
static bool sithModernControls_bQuickStepLatched; // Added: this press already took a step

// Added: a floor-plane yaw in the engine's convention: 0 faces +Y and positive turns left.
static float sithModernControls_GetYaw(const rdMatrix34* pOrient)
{
    float length = sqrtf(pOrient->lvec.x * pOrient->lvec.x + pOrient->lvec.y * pOrient->lvec.y);
    if ( length < 0.001f )
    {
        return 0.0f;
    }

    return STDMATH_TODEGREESF(atan2f(-pOrient->lvec.x, pOrient->lvec.y));
}

// Added: the yaw from one floor position to another in the engine's convention.
static float sithModernControls_GetDirectionYaw(const rdVector3* pFrom, const rdVector3* pTo)
{
    return STDMATH_TODEGREESF(atan2f(-(pTo->x - pFrom->x), pTo->y - pFrom->y));
}

// Added: turn the camera-relative move vector into the nearest of Indy's four directions. While
// locked the stick is already relative to Indy's facing toward the target, so only the screen
// axes rotate into that facing; otherwise the camera yaw rotates it into the world first.
static SithModernStep sithModernControls_ResolveQuickStep(const SithModernInput* pIn)
{
    float absX = fabsf(pIn->x);
    float absY = fabsf(pIn->y);
    if ( absX < SITHMODERNCONTROL_QUICKSTEP_THRESHOLD
        && absY < SITHMODERNCONTROL_QUICKSTEP_THRESHOLD )
    {
        return SITHMODERNSTEP_NONE;
    }

    float relativeYaw;
    if ( pIn->bLocked )
    {
        relativeYaw = -STDMATH_TODEGREESF(atan2f(pIn->x, pIn->y));
    }
    else
    {
        float worldYaw = pIn->cameraYaw - STDMATH_TODEGREESF(atan2f(pIn->x, pIn->y));
        relativeYaw    = stdMath_NormalizeAngleAcute(worldYaw - pIn->playerYaw);
    }

    if ( relativeYaw >= -45.0f && relativeYaw < 45.0f )
    {
        return SITHMODERNSTEP_FORWARD;
    }

    if ( relativeYaw >= 45.0f && relativeYaw < 135.0f )
    {
        return SITHMODERNSTEP_LEFT;
    }

    if ( relativeYaw >= -135.0f && relativeYaw < -45.0f )
    {
        return SITHMODERNSTEP_RIGHT;
    }

    return SITHMODERNSTEP_BACK;
}

// Added: rotate Indy's facing toward a target heading the way the original turn does, about the
// world up axis. A large change or a move from standing snaps the facing in one update; short
// angles ease down so the facing settles without overshoot or jitter.
static void sithModernControls_TurnToYaw(SithThing* pPlayer, float targetYaw, float secDeltaTime, bool bSnap)
{
    if ( !pPlayer || secDeltaTime <= 0.0f )
    {
        return;
    }

    float currentYaw = sithModernControls_GetYaw(&pPlayer->orient);
    float delta      = stdMath_NormalizeAngleAcute(targetYaw - currentYaw);
    float angle      = fabsf(delta);
    if ( angle <= SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    float step = SITHMODERNCONTROL_TURN_SPEED * secDeltaTime;
    if ( bSnap || step > angle )
    {
        step = angle;
    }

    // Added: ease the last few degrees so a changing target does not make the facing snap.
    if ( !bSnap && angle < SITHMODERNCONTROL_TURN_EASE_ANGLE )
    {
        step *= angle / SITHMODERNCONTROL_TURN_EASE_ANGLE;
    }

    rdVector3 turn = { 0.0f, delta > 0.0f ? step : -step, 0.0f };
    rdMatrix_PreRotate34(&pPlayer->orient, &turn);
    rdMatrix_Normalize34(&pPlayer->orient);
}

// Added: underwater swim steering. The target is a 3D direction, so the call rotates yaw about
// the world up and pitch about the local right axis the way the original swim code does. The
// pitch limit is the original swim code's, and short angles ease so the facing settles.
static void sithModernControls_TurnToDirection(SithThing* pPlayer, float targetYaw, float targetPitch, float secDeltaTime)
{
    if ( !pPlayer || secDeltaTime <= 0.0f )
    {
        return;
    }

    rdVector3 pyr;
    rdMatrix_ExtractAngles34(&pPlayer->orient, &pyr);

    float maxPitch = sithPlayerControls_GetSwimMaxPitchAngle();
    targetPitch    = STDMATH_CLAMP(targetPitch, -maxPitch, maxPitch);

    float yawDelta = stdMath_NormalizeAngleAcute(targetYaw - pyr.yaw);
    float yawAngle = fabsf(yawDelta);
    if ( yawAngle > SITHMODERNCONTROL_ZERO_EPSILON )
    {
        float step = SITHMODERNCONTROL_SWIM_YAW_SPEED * secDeltaTime;
        if ( step > yawAngle )
        {
            step = yawAngle;
        }

        if ( yawAngle < SITHMODERNCONTROL_SWIM_EASE_ANGLE )
        {
            step *= yawAngle / SITHMODERNCONTROL_SWIM_EASE_ANGLE;
        }

        // Added: the sine table cannot represent a step this close to 360 degrees, so the last
        // degree finishes instead of leaving a sub-degree residual.
        if ( yawAngle < SITHMODERNCONTROL_SWIM_SNAP_ANGLE )
        {
            step = yawAngle;
        }

        rdVector3 turn = { 0.0f, yawDelta > 0.0f ? step : -step, 0.0f };
        rdMatrix_PostRotate34(&pPlayer->orient, &turn);
    }

    float pitchDelta = targetPitch - pyr.pitch;
    float pitchAngle = fabsf(pitchDelta);
    if ( pitchAngle > SITHMODERNCONTROL_ZERO_EPSILON )
    {
        float step = SITHMODERNCONTROL_SWIM_PITCH_SPEED * secDeltaTime;
        if ( step > pitchAngle )
        {
            step = pitchAngle;
        }

        if ( pitchAngle < SITHMODERNCONTROL_SWIM_EASE_ANGLE )
        {
            step *= pitchAngle / SITHMODERNCONTROL_SWIM_EASE_ANGLE;
        }

        if ( pitchAngle < SITHMODERNCONTROL_SWIM_SNAP_ANGLE )
        {
            step = pitchAngle;
        }

        rdVector3 turn = { pitchDelta > 0.0f ? step : -step, 0.0f, 0.0f };
        rdMatrix_PreRotate34(&pPlayer->orient, &turn);
    }

    rdMatrix_Normalize34(&pPlayer->orient);
}

void J3DAPI sithModernControls_ResetLock(void)
{
    sithModernControls_pLockedTarget = NULL;
    sithModernControls_bLockHeld     = false;
    sithModernControls_bLockActive   = false;
    sithModernControls_bLockWasHeld  = false;
    sithModernControls_secLockLost   = 0.0f;
}

SithThing* J3DAPI sithModernControls_GetLockedTarget(void)
{
    return sithModernControls_pLockedTarget;
}

bool J3DAPI sithModernControls_IsLockActive(void)
{
    return sithModernControls_bLockHeld && sithModernControls_bLockActive;
}

bool J3DAPI sithModernControls_GetLockCameraYaw(float* pYaw)
{
    if ( !pYaw || !sithModernControls_IsLockActive() || !sithModernControls_pLockedTarget )
    {
        return false;
    }

    SithThing* pPlayer = sithPlayer_g_pLocalPlayerThing;
    if ( !pPlayer )
    {
        return false;
    }

    *pYaw = sithModernControls_GetDirectionYaw(&pPlayer->pos, &sithModernControls_pLockedTarget->pos);
    return true;
}

// Added: acquire on the press edge, then keep or drop the target. Other move states keep the
// lock but do not apply it; returning to a camera-relative state resumes the lock.
static void sithModernControls_UpdateLock(SithThing* pPlayer, SithModernControlMode mode, float cameraYaw, float secDeltaTime)
{
    if ( secDeltaTime < 0.0f )
    {
        secDeltaTime = 0.0f;
    }

    int pressed = 0;
    bool bHeld  = sithControl_GetKey(SITHCONTROL_TARGETLOCK, &pressed) != 0;

    if ( !bHeld )
    {
        sithModernControls_ResetLock();
        return;
    }

    if ( !sithModernControls_bLockWasHeld )
    {
        sithModernControls_pLockedTarget = sithPlayerControls_FindLockTarget(pPlayer, cameraYaw);
        sithModernControls_secLockLost   = 0.0f;

        // Added: with no target, center the free camera forward for this press.
        if ( mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE && !sithModernControls_pLockedTarget )
        {
            sithModernCamera_SnapTo(sithModernControls_GetYaw(&pPlayer->orient));
        }
    }

    sithModernControls_bLockHeld    = true;
    sithModernControls_bLockWasHeld = true;

    if ( sithModernControls_pLockedTarget )
    {
        if ( !sithPlayerControls_IsLockTargetAlive(sithModernControls_pLockedTarget) )
        {
            sithModernControls_pLockedTarget = NULL;
            sithModernControls_secLockLost   = 0.0f;
        }
        else if ( !sithPlayerControls_IsLockTargetVisible(pPlayer, sithModernControls_pLockedTarget) )
        {
            sithModernControls_secLockLost += secDeltaTime;
            if ( sithModernControls_secLockLost > SITHMODERNCONTROL_LOCK_LOST_SECONDS )
            {
                sithModernControls_pLockedTarget = NULL;
                sithModernControls_secLockLost   = 0.0f;
            }
        }
        else
        {
            sithModernControls_secLockLost = 0.0f;
        }
    }

    sithModernControls_bLockActive = (mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE);
}

static bool sithModernControls_IsSwimming(const SithThing* pPlayer)
{
    if ( pPlayer->moveStatus == SITHPLAYERMOVE_SWIMIDLE )
    {
        return true;
    }

    if ( (pPlayer->moveInfo.physics.flags & SITH_PF_ONWATERSURFACE) != 0 )
    {
        return true;
    }

    return pPlayer->pInSector
        && (pPlayer->pInSector->flags & (SITH_SECTOR_UNDERWATER | SITH_SECTOR_AETHERIUM)) != 0;
}

// Added: the Modern camera-relative states that move along the floor. Swimming and airborne
// states keep their momentum untouched.
static bool sithModernControls_IsGroundStatus(SithThingMoveStatus moveStatus)
{
    return moveStatus == SITHPLAYERMOVE_WALKING
        || moveStatus == SITHPLAYERMOVE_RUNNING
        || moveStatus == SITHPLAYERMOVE_CRAWLIDLE;
}

static float sithModernControls_GetHorizontalSpeed(const rdVector3* pVelocity)
{
    return sqrtf(pVelocity->x * pVelocity->x + pVelocity->y * pVelocity->y);
}

// Added: rotate the horizontal velocity by the same yaw the facing turned this frame, so the
// old momentum follows the new facing instead of carrying across it. The vertical velocity stays.
static void sithModernControls_RotateVelocityToFacing(SithThing* pPlayer, float yawDelta)
{
    if ( fabsf(yawDelta) <= SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    float sinYaw, cosYaw;
    stdMath_SinCos(yawDelta, &sinYaw, &cosYaw);

    rdVector3* pVelocity = &pPlayer->moveInfo.physics.velocity;
    float x = pVelocity->x;
    float y = pVelocity->y;
    pVelocity->x = cosYaw * x - sinYaw * y;
    pVelocity->y = sinYaw * x + cosYaw * y;
}

static bool sithModernControls_IsNoneStatus(SithThingMoveStatus moveStatus)
{
    switch ( moveStatus )
    {
        case SITHPLAYERMOVE_JUMPFWD:
        case SITHPLAYERMOVE_JUMPUP:
        case SITHPLAYERMOVE_JUMP_READY:
        case SITHPLAYERMOVE_JUMPROLLFWD:
        case SITHPLAYERMOVE_JUMPROLLBACK:
        case SITHPLAYERMOVE_JUMPLEFT:
        case SITHPLAYERMOVE_JUMPRIGHT:
        case SITHPLAYERMOVE_JUMPBACK:
        case SITHPLAYERMOVE_LEAPFWD:
        case SITHPLAYERMOVE_FALLING:
        case SITHPLAYERMOVE_LAND:
        case SITHPLAYERMOVE_KNOCKEDOUT:
            return true;

        default:
            return false;
    }
}

static bool sithModernControls_IsOriginalCameraStatus(SithThingMoveStatus moveStatus)
{
    switch ( moveStatus )
    {
        case SITHPLAYERMOVE_HANGING:

        case SITHPLAYERMOVE_CLIMBIDLE:
        case SITHPLAYERMOVE_MOUNTING_WALL:
        case SITHPLAYERMOVE_CLIMBING_UP:
        case SITHPLAYERMOVE_CLIMBING_DOWN:
        case SITHPLAYERMOVE_CLIMBING_LEFT:
        case SITHPLAYERMOVE_CLIMBING_RIGHT:
        case SITHPLAYERMOVE_PULLINGUP:
        case SITHPLAYERMOVE_PULLINGUP_1M:
        case SITHPLAYERMOVE_CLIMB_DOWN_TO_MOUNT:
        case SITHPLAYERMOVE_CLIMB_TO_HANG:

        case SITHPLAYERMOVE_PUSHING:
        case SITHPLAYERMOVE_PULLING:
        case SITHPLAYERMOVE_PUSHPULL_READY:

        case SITHPLAYERMOVE_WHIPSWINGING:
        case SITHPLAYERMOVE_WHIPCLIMBIDLE:
        case SITHPLAYERMOVE_WHIPCLIMB_START:
        case SITHPLAYERMOVE_WHIP_UNK2:

        case SITHPLAYERMOVE_JEWELFLYING:
        case SITHPLAYERMOVE_JEWELFLYING_UNKN1:
        case SITHPLAYERMOVE_JEWELFLYING_UNKN2:

        case SITHPLAYERMOVE_SLIDING:
        case SITHPLAYERMOVE_SLIDEDOWNFORWARD:
        case SITHPLAYERMOVE_SLIDEDOWNBACK:

        case SITHPLAYERMOVE_JEEP_IMPACT:
        case SITHPLAYERMOVE_MINECAR_IDLE:
        case SITHPLAYERMOVE_JEEP_IDLE:
        case SITHPLAYERMOVE_RAFT_IDLE:
        case SITHPLAYERMOVE_RAFT_PADDLE_FORWARD_LEFT:
        case SITHPLAYERMOVE_RAFT_PADDLE_FORWARD_RIGHT:
        case SITHPLAYERMOVE_RAFT_TURN_LEFT:
        case SITHPLAYERMOVE_RAFT_TURN_RIGHT:
        case SITHPLAYERMOVE_RAFT_PADDLE_BACK_LEFT:
        case SITHPLAYERMOVE_RAFT_PADDLE_BACK_RIGHT:
        case SITHPLAYERMOVE_RAFT_STARTPADDLE_LEFT:
        case SITHPLAYERMOVE_RAFT_ENDPADDLE_LEFT:
        case SITHPLAYERMOVE_RAFT_ENDPADDLE_RIGHT:
        case SITHPLAYERMOVE_RAFT_STARTPADDLE_RIGHT:
        case SITHPLAYERMOVE_RAFT_PADDLERIGHT_STARTPADDLE_LEFT:
        case SITHPLAYERMOVE_RAFT_PADDLELEFT_STARTPADDLE_RIGHT:
        case SITHPLAYERMOVE_RAFT_DOCKING:
        case SITHPLAYERMOVE_MINECAR_BOARDING:
        case SITHPLAYERMOVE_MINECAR_UNBOARDING_LEFT:
        case SITHPLAYERMOVE_MINECAR_UNBOARDING_RIGHT:
        case SITHPLAYERMOVE_MINECAR_DUCKING:
        case SITHPLAYERMOVE_MINECAR_DUCKED:
        case SITHPLAYERMOVE_MINECAR_GETTING_UP:
        case SITHPLAYERMOVE_JEEP_BOARDING:
        case SITHPLAYERMOVE_RAFT_BOARDING:
        case SITHPLAYERMOVE_RAFT_UNBOARDING_LEFT:
        case SITHPLAYERMOVE_RAFT_UNBOARDING_RIGHT:
        case SITHPLAYERMOVE_RAFT_UNBOARD_START:
        case SITHPLAYERMOVE_JEEP_UNBOARDING:
            return true;

        default:
            return false;
    }
}

SithModernControlMode J3DAPI sithModernControls_GetMode(const SithThing* pPlayer)
{
    if ( !pPlayer )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    if ( pPlayer->thingInfo.actorInfo.bForceMovePlay
        || sithModernControls_IsNoneStatus(pPlayer->moveStatus) )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    // Added: the original tank-style states, plus any status while a vehicle is boarded, keep
    // the original camera and stick-relative movement.
    if ( sithModernControls_IsOriginalCameraStatus(pPlayer->moveStatus)
        || sithPlayerControls_GetVehicleBoardedThing() != NULL )
    {
        return SITHMODERNCONTROL_MODE_RELATIVE;
    }

    if ( sithModernControls_IsSwimming(pPlayer) )
    {
        // Added: on the surface the original thrust and ledge grab stay camera-relative. Below
        // it the stick steers in 3D and the original swim thrust comes from a JUMP override.
        if ( (pPlayer->moveInfo.physics.flags & SITH_PF_ONWATERSURFACE) != 0 )
        {
            return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
        }

        return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM;
    }

    // Added: everything else on a floor is camera-relative. An unrecognized airborne state
    // keeps the real keys; the free camera stays anchored for it too.
    if ( pPlayer->attach.flags == 0 )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
}

// Added: the transition skip applies only when the Modern style actually owns the state.
bool J3DAPI sithModernControls_IsCameraRelative(const SithThing* pPlayer)
{
    if ( !sithControl_IsModernControlStyle() )
    {
        return false;
    }

    SithModernControlMode mode = sithModernControls_GetMode(pPlayer);
    return mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE
        || mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM;
}

static void sithModernControls_ApplyMovementKey(SithControlFunction functionId, const SithModernKey* pKey)
{
    sithControl_SuppressKey(functionId);
    sithControl_SetKeyOverride(functionId, pKey->bPressed ? 1 : 0, pKey->amount);
}

// Added: reads the left stick of joystick 0 and returns it as a camera-independent move vector.
static void sithModernControls_ReadStick(float* pX, float* pY, float* pMagnitude)
{
    float rawX = (float)stdControl_ReadAxisRaw(STDCONTROL_GET_JOYSTICK_AXIS_X(0));
    float rawY = (float)stdControl_ReadAxisRaw(STDCONTROL_GET_JOYSTICK_AXIS_Y(0));

    float x = STDMATH_CLAMP(rawX / SITHMODERNCONTROL_STICK_RANGE, -1.0f, 1.0f);
    float y = STDMATH_CLAMP(rawY / SITHMODERNCONTROL_STICK_RANGE, -1.0f, 1.0f);

    float magnitude = sqrtf(x * x + y * y);
    *pX          = 0.0f;
    *pY          = 0.0f;
    *pMagnitude  = 0.0f;

    if ( magnitude <= SITHMODERNCONTROL_STICK_DEADZONE )
    {
        return;
    }

    float scale = (magnitude - SITHMODERNCONTROL_STICK_DEADZONE) / (1.0f - SITHMODERNCONTROL_STICK_DEADZONE);
    scale = STDMATH_CLAMP(scale, 0.0f, 1.0f);

    // Added: stdControl reports Y down positive, so forward is -Y.
    *pX         = x / magnitude * scale;
    *pY         = -y / magnitude * scale;
    *pMagnitude = scale;
}

void J3DAPI sithModernControls_Compute(const SithModernInput* pIn, SithModernOutput* pOut)
{
    if ( !pIn || !pOut )
    {
        return;
    }

    *pOut = (SithModernOutput){ 0 };

    if ( pIn->mode == SITHMODERNCONTROL_MODE_NONE )
    {
        return;
    }

    float magnitude = sqrtf(pIn->x * pIn->x + pIn->y * pIn->y);

    if ( pIn->mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM )
    {
        // Added: a neutral stick underwater leaves every real key alone, so Classic's JUMP swim
        // and pitch/turn keys still work.
        if ( magnitude < SITHMODERNCONTROL_ZERO_EPSILON )
        {
            return;
        }

        float sinCameraYaw, cosCameraYaw;
        stdMath_SinCos(pIn->cameraYaw, &sinCameraYaw, &cosCameraYaw);

        float sinCameraPitch, cosCameraPitch;
        stdMath_SinCos(pIn->cameraPitch, &sinCameraPitch, &cosCameraPitch);

        // Added: the camera's forward includes its pitch, so stick up dives or rises with the
        // view; its right stays horizontal. Positive pitch is up in both.
        float forwardX = -sinCameraYaw * cosCameraPitch;
        float forwardY = cosCameraYaw * cosCameraPitch;
        float forwardZ = sinCameraPitch;
        float rightX   = cosCameraYaw;
        float rightY   = sinCameraYaw;

        float targetX = rightX * pIn->x + forwardX * pIn->y;
        float targetY = rightY * pIn->x + forwardY * pIn->y;
        float targetZ = forwardZ * pIn->y;

        float horizontal = sqrtf(targetX * targetX + targetY * targetY);
        float targetPitch = horizontal > SITHMODERNCONTROL_ZERO_EPSILON
            ? STDMATH_TODEGREESF(atan2f(targetZ, horizontal))
            : (targetZ >= 0.0f ? 90.0f : -90.0f);

        float maxPitch = sithPlayerControls_GetSwimMaxPitchAngle();
        pOut->targetPitch = STDMATH_CLAMP(targetPitch, -maxPitch, maxPitch);

        pOut->swimForward.bPressed = true;
        pOut->swimForward.amount   = 1.0f;
        pOut->bTurnTowardTarget    = true;
        pOut->targetYaw            = STDMATH_TODEGREESF(atan2f(-targetX, targetY));
        return;
    }

    bool bRun       = pIn->bRunKey || magnitude >= SITHMODERNCONTROL_RUN_THRESHOLD;
    pOut->run.bPressed = bRun;
    pOut->run.amount   = bRun ? 1.0f : 0.0f;

    if ( pIn->mode == SITHMODERNCONTROL_MODE_RELATIVE )
    {
        if ( pIn->y > 0.0f )
        {
            pOut->forward.bPressed = true;
            pOut->forward.amount   = pIn->y;
        }
        else if ( pIn->y < 0.0f )
        {
            pOut->back.bPressed = true;
            pOut->back.amount   = -pIn->y;
        }

        if ( pIn->x < 0.0f )
        {
            pOut->turnLeft.bPressed = true;
            pOut->turnLeft.amount   = -pIn->x;
        }
        else if ( pIn->x > 0.0f )
        {
            pOut->turnRight.bPressed = true;
            pOut->turnRight.amount   = pIn->x;
        }

        return;
    }

    // Added: the quick-step modifier takes the frame. One direction crosses the threshold and
    // starts one step; every other movement key stays off so a held modifier never runs.
    if ( pIn->bQuickStepHeld )
    {
        if ( pIn->bQuickStepAllowed )
        {
            pOut->quickStep = sithModernControls_ResolveQuickStep(pIn);
        }

        if ( pIn->bLocked && pIn->bHasLockTarget )
        {
            pOut->bTurnTowardTarget = true;
            pOut->targetYaw         = pIn->lockTargetYaw;
        }

        return;
    }

    if ( pIn->bLocked )
    {
        // Added: Indy faces the locked target directly, so the original turn keys stay off.
        if ( pIn->bHasLockTarget )
        {
            pOut->bTurnTowardTarget = true;
            pOut->targetYaw         = pIn->lockTargetYaw;
        }

        // Added: a sideways stick holds the original sidestep key; the original code repeats the
        // step with its own animation and distance while the key stays down.
        if ( fabsf(pIn->x) > SITHMODERNCONTROL_LOCK_SIDEWAYS
            && fabsf(pIn->x) > fabsf(pIn->y) )
        {
            if ( pIn->x > 0.0f )
            {
                pOut->strafeRight.bPressed = true;
                pOut->strafeRight.amount   = fabsf(pIn->x);
            }
            else
            {
                pOut->strafeLeft.bPressed = true;
                pOut->strafeLeft.amount   = fabsf(pIn->x);
            }

            return;
        }

        // Added: a backward stick while locked plays the original back hop, chained while held,
        // instead of the slow backpedal walk.
        if ( pIn->y < -SITHMODERNCONTROL_LOCK_SIDEWAYS
            && -pIn->y >= fabsf(pIn->x) )
        {
            pOut->backStep.bPressed = true;
            pOut->backStep.amount   = -pIn->y;
            return;
        }

        if ( pIn->y > SITHMODERNCONTROL_ZERO_EPSILON )
        {
            pOut->forward.bPressed = true;
            pOut->forward.amount   = pIn->y;
        }
        else if ( pIn->y < -SITHMODERNCONTROL_ZERO_EPSILON )
        {
            pOut->back.bPressed = true;
            pOut->back.amount   = -pIn->y;
        }

        return;
    }

    if ( magnitude < SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    float sinCameraYaw, cosCameraYaw;
    stdMath_SinCos(pIn->cameraYaw, &sinCameraYaw, &cosCameraYaw);

    // Added: rotate the screen-space move vector into the world floor plane. Right is +X at yaw 0.
    float targetX = cosCameraYaw * pIn->x - sinCameraYaw * pIn->y;
    float targetY = sinCameraYaw * pIn->x + cosCameraYaw * pIn->y;

    // Added: the movement layer turns the facing directly toward this heading; it never presses
    // the original turn keys in camera-relative mode.
    pOut->bTurnTowardTarget = true;
    pOut->targetYaw         = STDMATH_TODEGREESF(atan2f(-targetX, targetY));
    pOut->forward.bPressed  = true;
    pOut->forward.amount    = magnitude;
}

// Added: a physical step key bound by an older Modern scheme counts only the press edge. The
// original sidestep force move starts once per press, but a held STPLEFT/STPRIGHT starts another
// step as soon as the previous one finishes. Suppressing the held key leaves the original move
// code and its alignment untouched.
static void sithModernControls_SuppressHeldStepKeys(bool bLocked)
{
    // Added: the lock uses the held sidestep, so quick-step's press-edge rule stays off for it.
    if ( bLocked )
    {
        return;
    }

    int pressed = 0;
    if ( sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed) && !pressed )
    {
        sithControl_SuppressKey(SITHCONTROL_STPLEFT);
    }

    pressed = 0;
    if ( sithControl_GetKey(SITHCONTROL_STPRIGHT, &pressed) && !pressed )
    {
        sithControl_SuppressKey(SITHCONTROL_STPRIGHT);
    }
}

// Added: run one original step. Left and right are the sidestep force moves; back is the original
// back hop, and forward is the short forced walk that matches the sidestep distance.
static void sithModernControls_TriggerStep(SithThing* pPlayer, SithModernStep step)
{
    switch ( step )
    {
        case SITHMODERNSTEP_FORWARD:
            sithPlayerActions_QuickStepForward(pPlayer);
            break;

        case SITHMODERNSTEP_BACK:
            sithPlayerActions_QuickStepBack(pPlayer);
            break;

        case SITHMODERNSTEP_LEFT:
            sithPlayerActions_StrafeLeft(pPlayer);
            break;

        case SITHMODERNSTEP_RIGHT:
            sithPlayerActions_StrafeRight(pPlayer);
            break;

        default:
            break;
    }
}

void J3DAPI sithModernControls_Update(SithThing* pPlayer, float secDeltaTime)
{
    sithControl_ClearKeyOverrides();

    if ( !sithControl_IsModernControlStyle()
        || !pPlayer
        || pPlayer != sithPlayer_g_pLocalPlayerThing
        || pPlayer->moveType != SITH_MT_PHYSICS )
    {
        sithModernControls_bQuickStepLatched = false;
        sithModernControls_ResetLock();
        return;
    }

    SithModernControlMode mode = sithModernControls_GetMode(pPlayer);

    int quickStepPressed = 0;
    bool bQuickStepHeld  = sithControl_GetKey(SITHCONTROL_QUICKSTEP, &quickStepPressed) != 0;
    if ( !bQuickStepHeld )
    {
        sithModernControls_bQuickStepLatched = false;
    }

    float playerYaw   = sithModernControls_GetYaw(&pPlayer->orient);
    float cameraYaw   = playerYaw;
    float cameraPitch = 0.0f;

    // Added: the free camera's orbit yaw and pitch are authoritative while it is active; the
    // camera orientation lags behind by the interpolation. Fall back to the orientation only
    // when the original camera owns the view.
    if ( sithModernCamera_IsActive(sithCamera_g_pCurCamera, pPlayer) )
    {
        cameraYaw   = sithModernCamera_GetYaw();
        cameraPitch = sithModernCamera_GetPitch();
    }
    else if ( sithCamera_g_pCurCamera )
    {
        rdVector3 cameraPYR;
        rdMatrix_ExtractAngles34(&sithCamera_g_pCurCamera->orient, &cameraPYR);
        cameraYaw   = sithModernControls_GetYaw(&sithCamera_g_pCurCamera->orient);
        cameraPitch = cameraPYR.pitch;
    }

    // Added: the target lock is read in every move state; only camera-relative states apply it.
    sithModernControls_UpdateLock(pPlayer, mode, cameraYaw, secDeltaTime);

    sithModernControls_SuppressHeldStepKeys(sithModernControls_IsLockActive());

    if ( mode == SITHMODERNCONTROL_MODE_NONE )
    {
        return;
    }

    float stickX, stickY, stickMagnitude;
    sithModernControls_ReadStick(&stickX, &stickY, &stickMagnitude);

    int bForward   = sithControl_GetKey(SITHCONTROL_FORWARD, NULL) != 0;
    int bBack      = sithControl_GetKey(SITHCONTROL_BACK, NULL) != 0;
    int bTurnLeft  = sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL) != 0;
    int bTurnRight = sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL) != 0;

    bool bRunOption   = (sithControl_g_controlOptions & 2) != 0;
    bool bRunKey      = sithControl_GetKey(SITHCONTROL_RUNFWD, NULL) != 0;
    bool bAct1        = sithControl_GetKey(SITHCONTROL_ACT1, NULL) != 0;
    bool bKeyboardRun = bRunOption || bRunKey || bAct1;
    if ( bRunOption && bAct1 )
    {
        bKeyboardRun = false;
    }

    float keyX = (float)(bTurnRight - bTurnLeft);
    float keyY = (float)(bForward - bBack);
    float keyMagnitude = sqrtf(keyX * keyX + keyY * keyY);

    float keyXOut = 0.0f;
    float keyYOut = 0.0f;
    float keyOutMagnitude = 0.0f;
    if ( keyMagnitude > 0.0f )
    {
        keyOutMagnitude = bKeyboardRun
            ? SITHMODERNCONTROL_KEY_RUN_MAGNITUDE
            : SITHMODERNCONTROL_KEY_WALK_MAGNITUDE;
        keyXOut = keyX / keyMagnitude * keyOutMagnitude;
        keyYOut = keyY / keyMagnitude * keyOutMagnitude;
    }

    SithModernInput in = { 0 };
    if ( stickMagnitude >= keyOutMagnitude )
    {
        in.x = stickX;
        in.y = stickY;
    }
    else
    {
        in.x = keyXOut;
        in.y = keyYOut;
    }

    in.cameraYaw      = cameraYaw;
    in.cameraPitch    = cameraPitch;
    in.playerYaw      = playerYaw;
    in.mode           = mode;
    in.bRunKey        = bRunKey;
    in.bLocked        = sithModernControls_IsLockActive();
    in.bHasLockTarget = sithModernControls_pLockedTarget != NULL;
    if ( in.bHasLockTarget )
    {
        in.lockTargetYaw = sithModernControls_GetDirectionYaw(&pPlayer->pos, &sithModernControls_pLockedTarget->pos);
    }

    // Added: quick-step is a grounded Modern action. A held modifier with no direction takes the
    // frame and suppresses normal movement; a direction past the threshold takes one step, and the
    // latch only clears once the input falls below the re-arm magnitude or the modifier is released.
    float inMagnitude = sqrtf(in.x * in.x + in.y * in.y);
    in.bQuickStepHeld = bQuickStepHeld
        && mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE
        && !sithModernControls_IsSwimming(pPlayer);
    if ( in.bQuickStepHeld && inMagnitude < SITHMODERNCONTROL_QUICKSTEP_REARM )
    {
        sithModernControls_bQuickStepLatched = false;
    }

    in.bQuickStepAllowed = in.bQuickStepHeld && !sithModernControls_bQuickStepLatched;

    // Added: a neutral underwater input sets no overrides, so the real keys stay Classic.
    if ( mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM
        && sqrtf(in.x * in.x + in.y * in.y) < SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);

    // Added: one quick-step per press. Left and right drive the original sidestep keys for this
    // frame; forward and back call the original step actions directly because neither has a key.
    if ( out.quickStep != SITHMODERNSTEP_NONE )
    {
        sithModernControls_bQuickStepLatched = true;

        // Added: also hold the original sidestep key on the press frame. The direct action has
        // already started the force move, so the original still-state step guard skips it, while
        // the key keeps the edge visible to the original code and to tests.
        if ( out.quickStep == SITHMODERNSTEP_LEFT )
        {
            SithModernKey stepKey = { true, 1.0f };
            sithModernControls_ApplyMovementKey(SITHCONTROL_STPLEFT, &stepKey);
        }
        else if ( out.quickStep == SITHMODERNSTEP_RIGHT )
        {
            SithModernKey stepKey = { true, 1.0f };
            sithModernControls_ApplyMovementKey(SITHCONTROL_STPRIGHT, &stepKey);
        }

        sithModernControls_TriggerStep(pPlayer, out.quickStep);
    }

    // Added: locked back hops as long as the stick stays back; the action itself starts the next
    // hop only once the previous one has landed, so holding it chains.
    if ( out.backStep.bPressed )
    {
        sithModernControls_TriggerStep(pPlayer, SITHMODERNSTEP_BACK);
    }

    bool bStepActive = in.bQuickStepHeld || out.backStep.bPressed;

    if ( bStepActive )
    {
        // Added: a held quick-step, or a locked back step, never runs or walks.
        sithControl_SuppressKey(SITHCONTROL_FORWARD);
        sithControl_SuppressKey(SITHCONTROL_BACK);
        sithControl_SuppressKey(SITHCONTROL_TURNLEFT);
        sithControl_SuppressKey(SITHCONTROL_TURNRIGHT);
        sithControl_SuppressKey(SITHCONTROL_RUNFWD);
        sithControl_SuppressKey(SITHCONTROL_ACT1);
        sithControl_SuppressKey(SITHCONTROL_STPLEFT);
        sithControl_SuppressKey(SITHCONTROL_STPRIGHT);
    }
    else
    {
        sithModernControls_ApplyMovementKey(SITHCONTROL_FORWARD, &out.forward);
        sithModernControls_ApplyMovementKey(SITHCONTROL_BACK, &out.back);
        sithModernControls_ApplyMovementKey(SITHCONTROL_TURNLEFT, &out.turnLeft);
        sithModernControls_ApplyMovementKey(SITHCONTROL_TURNRIGHT, &out.turnRight);

        // Added: underwater the real RUN binding still starts the original swim boost, so the swim
        // mode does not turn the stick's magnitude into a virtual run key.
        if ( mode != SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM )
        {
            sithModernControls_ApplyMovementKey(SITHCONTROL_RUNFWD, &out.run);
        }

        // Added: only a lock's sideways stick holds a sidestep key, leaving the physical quick-step
        // key alone otherwise.
        if ( out.strafeLeft.bPressed )
        {
            sithModernControls_ApplyMovementKey(SITHCONTROL_STPLEFT, &out.strafeLeft);
        }
        else if ( out.strafeRight.bPressed )
        {
            sithModernControls_ApplyMovementKey(SITHCONTROL_STPRIGHT, &out.strafeRight);
        }
    }

    // Added: underwater the original JUMP path is the swim thrust (and keeps its boost logic);
    // on the surface JUMP stays the real key and still grabs ledges.
    if ( out.swimForward.bPressed )
    {
        sithModernControls_ApplyMovementKey(SITHCONTROL_JUMP, &out.swimForward);
    }

    // Added: the facing turns directly toward the stick or lock heading before the original
    // movement code runs, so a direction change starts a run at once. From standing or for a
    // large change the facing snaps; smaller changes rotate at the Modern turn speed.
    if ( out.bTurnTowardTarget )
    {
        // Added: swimming steers both yaw and pitch smoothly and never snaps; the water momentum
        // stays untouched.
        if ( mode == SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM )
        {
            sithModernControls_TurnToDirection(pPlayer, out.targetYaw, out.targetPitch, secDeltaTime);
        }
        else
        {
            float delta = fabsf(stdMath_NormalizeAngleAcute(out.targetYaw - sithModernControls_GetYaw(&pPlayer->orient)));
            bool bSnap  = pPlayer->moveStatus == SITHPLAYERMOVE_STILL
                || delta > SITHMODERNCONTROL_TURN_SNAP_ANGLE;
            sithModernControls_TurnToYaw(pPlayer, out.targetYaw, secDeltaTime, bSnap);

            // Added: the horizontal momentum turns with the facing, so a direction change bends
            // the path at once instead of letting the old velocity carry across it.
            if ( sithModernControls_IsGroundStatus(pPlayer->moveStatus) )
            {
                float yawDelta = stdMath_NormalizeAngleAcute(
                    sithModernControls_GetYaw(&pPlayer->orient) - playerYaw);
                sithModernControls_RotateVelocityToFacing(pPlayer, yawDelta);
            }
        }
    }
}

// Added: shorten the start of a Modern ground move to the original target speed without raising
// it. The original movement code has already set this frame's thrust; the steady speed of that
// thrust under the player physics' drag is the target, and the extra velocity is capped there.
void J3DAPI sithModernControls_ApplyGroundAcceleration(SithThing* pPlayer, float secDeltaTime)
{
    if ( !sithControl_IsModernControlStyle()
        || !pPlayer
        || pPlayer != sithPlayer_g_pLocalPlayerThing
        || pPlayer->moveType != SITH_MT_PHYSICS
        || !sithModernControls_IsGroundStatus(pPlayer->moveStatus)
        || secDeltaTime <= 0.0f )
    {
        return;
    }

    SithPhysicsInfo* pPhysics = &pPlayer->moveInfo.physics;
    if ( pPhysics->thrust.y == 0.0f )
    {
        return;
    }

    // Added: the ground move runs the surface-attached physics, which uses the surface drag and
    // the full attachment scale; a detached thing uses the air drag and the player ground scale.
    float drag;
    float thrustScale;
    if ( (pPlayer->attach.flags & (SITH_ATTACH_SURFACE | SITH_ATTACH_THINGFACE)) != 0 )
    {
        drag        = pPhysics->surfDrag;
        thrustScale = (pPhysics->flags & SITH_PF_CROUCHING) != 0 ? 0.80000001f : 1.0f;
    }
    else
    {
        drag        = pPhysics->airDrag;
        thrustScale = SITHMODERNCONTROL_GROUND_THRUST_SCALE;
    }

    if ( drag <= SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    // Added: the local forward thrust reaches the world floor plane through the horizontal part
    // of the facing.
    float facingX = pPlayer->orient.lvec.x;
    float facingY = pPlayer->orient.lvec.y;
    float facingLength = sqrtf(facingX * facingX + facingY * facingY);
    if ( facingLength <= SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    float targetSpeed = thrustScale * fabsf(pPhysics->thrust.y) * facingLength / drag;

    // Added: add velocity along the facing at a constant rate, capped at the target. The
    // original thrust then holds the target without ever passing it.
    float speed = sithModernControls_GetHorizontalSpeed(&pPhysics->velocity);
    if ( speed >= targetSpeed )
    {
        return;
    }

    float acceleratedSpeed = speed + targetSpeed * secDeltaTime / SITHMODERNCONTROL_ACCEL_TIME;
    if ( acceleratedSpeed > targetSpeed )
    {
        acceleratedSpeed = targetSpeed;
    }

    float direction = pPhysics->thrust.y > 0.0f ? 1.0f : -1.0f;
    float extraSpeed = acceleratedSpeed - speed;

    pPhysics->velocity.x += facingX / facingLength * direction * extraSpeed;
    pPhysics->velocity.y += facingY / facingLength * direction * extraSpeed;
}
