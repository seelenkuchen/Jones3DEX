#include "sithModernControls.h"

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithModernCamera.h>
#include <sith/Gameplay/sithPlayer.h>
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
#define SITHMODERNCONTROL_TURN_SPEED          720.0f // degrees per second toward the target facing
#define SITHMODERNCONTROL_TURN_EASE_ANGLE     10.0f  // scale the step down below this angle
#define SITHMODERNCONTROL_LOCK_SIDEWAYS       0.5f
#define SITHMODERNCONTROL_LOCK_LOST_SECONDS   0.5f

static SithThing* sithModernControls_pLockedTarget;
static bool sithModernControls_bLockHeld;
static bool sithModernControls_bLockActive;
static bool sithModernControls_bLockWasHeld;
static float sithModernControls_secLockLost;

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

// Added: rotate Indy's facing toward a target heading the way the original turn does, about the
// world up axis. Short angles ease down so the facing settles without overshoot or jitter.
static void sithModernControls_TurnToYaw(SithThing* pPlayer, float targetYaw, float secDeltaTime)
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
    if ( step > angle )
    {
        step = angle;
    }

    // Added: ease the last few degrees so a changing target does not make the facing snap.
    if ( angle < SITHMODERNCONTROL_TURN_EASE_ANGLE )
    {
        step *= angle / SITHMODERNCONTROL_TURN_EASE_ANGLE;
    }

    rdVector3 turn = { 0.0f, delta > 0.0f ? step : -step, 0.0f };
    rdMatrix_PreRotate34(&pPlayer->orient, &turn);
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
        return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    }

    // Added: everything else on a floor is camera-relative. An unrecognized airborne state
    // keeps the real keys; the free camera stays anchored for it too.
    if ( pPlayer->attach.flags == 0 )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
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

// Added: a Modern quick-step counts only the press edge. The original sidestep force move starts
// once per press, but a held STPLEFT/STPRIGHT starts another step as soon as the previous one
// finishes. Suppressing the held key leaves the original move code and its alignment untouched.
static void sithModernControls_UpdateQuickStep(bool bLocked)
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

void J3DAPI sithModernControls_Update(SithThing* pPlayer, float secDeltaTime)
{
    sithControl_ClearKeyOverrides();

    if ( !sithControl_IsModernControlStyle()
        || !pPlayer
        || pPlayer != sithPlayer_g_pLocalPlayerThing
        || pPlayer->moveType != SITH_MT_PHYSICS )
    {
        sithModernControls_ResetLock();
        return;
    }

    SithModernControlMode mode = sithModernControls_GetMode(pPlayer);

    float playerYaw = sithModernControls_GetYaw(&pPlayer->orient);
    float cameraYaw = playerYaw;

    // Added: the free camera's orbit yaw is authoritative while it is active; the camera
    // orientation lags behind by the interpolation. Fall back to the orientation only when the
    // original camera owns the view.
    if ( sithModernCamera_IsActive(sithCamera_g_pCurCamera, pPlayer) )
    {
        cameraYaw = sithModernCamera_GetYaw();
    }
    else if ( sithCamera_g_pCurCamera )
    {
        cameraYaw = sithModernControls_GetYaw(&sithCamera_g_pCurCamera->orient);
    }

    // Added: the target lock is read in every move state; only camera-relative states apply it.
    sithModernControls_UpdateLock(pPlayer, mode, cameraYaw, secDeltaTime);

    sithModernControls_UpdateQuickStep(sithModernControls_IsLockActive());

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
    in.playerYaw      = playerYaw;
    in.mode           = mode;
    in.bRunKey        = bRunKey;
    in.bLocked        = sithModernControls_IsLockActive();
    in.bHasLockTarget = sithModernControls_pLockedTarget != NULL;
    if ( in.bHasLockTarget )
    {
        in.lockTargetYaw = sithModernControls_GetDirectionYaw(&pPlayer->pos, &sithModernControls_pLockedTarget->pos);
    }

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);

    sithModernControls_ApplyMovementKey(SITHCONTROL_FORWARD, &out.forward);
    sithModernControls_ApplyMovementKey(SITHCONTROL_BACK, &out.back);
    sithModernControls_ApplyMovementKey(SITHCONTROL_TURNLEFT, &out.turnLeft);
    sithModernControls_ApplyMovementKey(SITHCONTROL_TURNRIGHT, &out.turnRight);
    sithModernControls_ApplyMovementKey(SITHCONTROL_RUNFWD, &out.run);

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

    // Added: the facing turns directly toward the stick or lock heading before the original
    // movement code runs, so a direction change starts a run at once.
    if ( out.bTurnTowardTarget )
    {
        sithModernControls_TurnToYaw(pPlayer, out.targetYaw, secDeltaTime);
    }
}
