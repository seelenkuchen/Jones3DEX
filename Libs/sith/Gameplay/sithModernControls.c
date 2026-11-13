#include "sithModernControls.h"

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithPlayer.h>

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
#define SITHMODERNCONTROL_TURN_THRESHOLD      8.0f
#define SITHMODERNCONTROL_FORWARD_THRESHOLD   50.0f
#define SITHMODERNCONTROL_TURN_FULL_ANGLE     60.0f
#define SITHMODERNCONTROL_TURN_MIN_AMOUNT     0.25f
#define SITHMODERNCONTROL_ZERO_EPSILON        0.0001f

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

static bool sithModernControls_IsJumpOrSlideStatus(SithThingMoveStatus moveStatus)
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
        case SITHPLAYERMOVE_SLIDING:
        case SITHPLAYERMOVE_SLIDEDOWNFORWARD:
        case SITHPLAYERMOVE_SLIDEDOWNBACK:
            return true;

        default:
            return false;
    }
}

static bool sithModernControls_IsCameraRelativeStatus(SithThingMoveStatus moveStatus)
{
    switch ( moveStatus )
    {
        case SITHPLAYERMOVE_STILL:
        case SITHPLAYERMOVE_WALKING:
        case SITHPLAYERMOVE_RUNNING:
        case SITHPLAYERMOVE_CRAWLIDLE:
        case SITHPLAYERMOVE_SWIMIDLE:
            return true;

        default:
            return false;
    }
}

static bool sithModernControls_IsRelativeStatus(SithThingMoveStatus moveStatus)
{
    switch ( moveStatus )
    {
        case SITHPLAYERMOVE_HANGING:

        case SITHPLAYERMOVE_PUSHING:
        case SITHPLAYERMOVE_PULLING:
        case SITHPLAYERMOVE_PUSHPULL_READY:

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

        case SITHPLAYERMOVE_WHIPSWINGING:
        case SITHPLAYERMOVE_WHIPCLIMBIDLE:
        case SITHPLAYERMOVE_WHIPCLIMB_START:
        case SITHPLAYERMOVE_WHIP_UNK2:

        case SITHPLAYERMOVE_JEWELFLYING:
        case SITHPLAYERMOVE_JEWELFLYING_UNKN1:
        case SITHPLAYERMOVE_JEWELFLYING_UNKN2:

        case SITHPLAYERMOVE_MINECAR_IDLE:
        case SITHPLAYERMOVE_JEEP_IDLE:
        case SITHPLAYERMOVE_RAFT_IDLE:
            return true;

        default:
            return false;
    }
}

static SithModernControlMode sithModernControls_GetMode(const SithThing* pPlayer)
{
    if ( pPlayer->thingInfo.actorInfo.bForceMovePlay )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    if ( sithModernControls_IsJumpOrSlideStatus(pPlayer->moveStatus) )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    if ( sithModernControls_IsCameraRelativeStatus(pPlayer->moveStatus) )
    {
        return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    }

    if ( sithModernControls_IsRelativeStatus(pPlayer->moveStatus) )
    {
        return SITHMODERNCONTROL_MODE_RELATIVE;
    }

    if ( sithModernControls_IsSwimming(pPlayer) )
    {
        return SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    }

    // Added: an unrecognized state with no floor under the player keeps the real keys.
    if ( pPlayer->attach.flags == 0 )
    {
        return SITHMODERNCONTROL_MODE_NONE;
    }

    return SITHMODERNCONTROL_MODE_RELATIVE;
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

    if ( magnitude < SITHMODERNCONTROL_ZERO_EPSILON )
    {
        return;
    }

    float sinCameraYaw, cosCameraYaw;
    stdMath_SinCos(pIn->cameraYaw, &sinCameraYaw, &cosCameraYaw);

    // Added: rotate the screen-space move vector into the world floor plane. Right is +X at yaw 0.
    float targetX = cosCameraYaw * pIn->x - sinCameraYaw * pIn->y;
    float targetY = sinCameraYaw * pIn->x + cosCameraYaw * pIn->y;

    float sinPlayerYaw, cosPlayerYaw;
    stdMath_SinCos(pIn->playerYaw, &sinPlayerYaw, &cosPlayerYaw);

    float playerX = -sinPlayerYaw;
    float playerY = cosPlayerYaw;

    float dot   = playerX * targetX + playerY * targetY;
    float cross = playerX * targetY - playerY * targetX;
    float angle = STDMATH_TODEGREESF(atan2f(cross, dot));

    if ( fabsf(angle) > SITHMODERNCONTROL_TURN_THRESHOLD )
    {
        float amount = STDMATH_CLAMP(fabsf(angle) / SITHMODERNCONTROL_TURN_FULL_ANGLE,
            SITHMODERNCONTROL_TURN_MIN_AMOUNT, 1.0f);

        if ( angle > 0.0f )
        {
            pOut->turnLeft.bPressed = true;
            pOut->turnLeft.amount   = amount;
        }
        else
        {
            pOut->turnRight.bPressed = true;
            pOut->turnRight.amount   = amount;
        }
    }

    if ( fabsf(angle) < SITHMODERNCONTROL_FORWARD_THRESHOLD )
    {
        pOut->forward.bPressed = true;
        pOut->forward.amount   = magnitude;
    }
}

void J3DAPI sithModernControls_Update(SithThing* pPlayer, float secDeltaTime)
{
    (void)secDeltaTime;

    sithControl_ClearKeyOverrides();

    if ( !sithControl_IsModernControlStyle()
        || !pPlayer
        || pPlayer != sithPlayer_g_pLocalPlayerThing
        || pPlayer->moveType != SITH_MT_PHYSICS )
    {
        return;
    }

    SithModernControlMode mode = sithModernControls_GetMode(pPlayer);
    if ( mode == SITHMODERNCONTROL_MODE_NONE )
    {
        return;
    }

    float playerYaw = sithModernControls_GetYaw(&pPlayer->orient);
    float cameraYaw = playerYaw;
    if ( sithCamera_g_pCurCamera )
    {
        cameraYaw = sithModernControls_GetYaw(&sithCamera_g_pCurCamera->orient);
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

    in.cameraYaw = cameraYaw;
    in.playerYaw = playerYaw;
    in.mode      = mode;
    in.bRunKey   = bRunKey;

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);

    sithModernControls_ApplyMovementKey(SITHCONTROL_FORWARD, &out.forward);
    sithModernControls_ApplyMovementKey(SITHCONTROL_BACK, &out.back);
    sithModernControls_ApplyMovementKey(SITHCONTROL_TURNLEFT, &out.turnLeft);
    sithModernControls_ApplyMovementKey(SITHCONTROL_TURNRIGHT, &out.turnRight);
    sithModernControls_ApplyMovementKey(SITHCONTROL_RUNFWD, &out.run);
}
