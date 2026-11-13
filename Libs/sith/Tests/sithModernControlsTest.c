// Added: the Modern control style's camera-relative move translation (ADR-0029). The Compute
// cases are pure; the Update cases drive the virtual key layer with a stubbed camera and player.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithModernCamera.h>
#include <sith/Gameplay/sithModernControls.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/Main/sithMain.h>

#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/General/stdMath.h>
#include <std/General/stdUtil.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(sithModernControls);

static tHostServices sithModernControlsTest_hostServices;
static size_t sithModernControlsTest_assertCount;

static SithThing sithModernControlsTest_player;
static SithCamera sithModernControlsTest_camera;
static SithSector sithModernControlsTest_sector;

static int J3DAPI sithModernControlsTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithModernControlsTest_Assert(const char* pCondition, const char* pFile, int line)
{
    (void)pCondition;
    (void)pFile;
    (void)line;
    ++sithModernControlsTest_assertCount;
}

static void* J3DAPI sithModernControlsTest_Malloc(size_t size)
{
    return malloc(size ? size : 1u);
}

static void J3DAPI sithModernControlsTest_Free(void* pMemory)
{
    free(pMemory);
}

static void sithModernControlsTest_ReadKey(int keyId, int keyDown)
{
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(keyId, keyDown));
    sithControl_ReadControls();
}

static void sithModernControlsTest_ResetWorld(void)
{
    memset(&sithModernControlsTest_sector, 0, sizeof(sithModernControlsTest_sector));

    memset(&sithModernControlsTest_player, 0, sizeof(sithModernControlsTest_player));
    sithModernControlsTest_player.type       = SITH_THING_PLAYER;
    sithModernControlsTest_player.moveType   = SITH_MT_PHYSICS;
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    sithModernControlsTest_player.orient.lvec  = (rdVector3){ { 0.0f }, { 1.0f }, { 0.0f } };
    sithModernControlsTest_player.orient.rvec  = (rdVector3){ { 1.0f }, { 0.0f }, { 0.0f } };
    sithModernControlsTest_player.orient.uvec  = (rdVector3){ { 0.0f }, { 0.0f }, { 1.0f } };

    memset(&sithModernControlsTest_camera, 0, sizeof(sithModernControlsTest_camera));
    sithModernControlsTest_camera.orient.lvec = (rdVector3){ { 0.0f }, { 1.0f }, { 0.0f } };

    sithPlayer_g_pLocalPlayerThing = &sithModernControlsTest_player;
    sithCamera_g_pCurCamera        = &sithModernControlsTest_camera;
    sithPlayerControls_SetVehicleBoardedThing(NULL);
}

static SithModernOutput sithModernControlsTest_Compute(float x, float y, float cameraYaw,
    float playerYaw, SithModernControlMode mode, bool bRunKey)
{
    SithModernInput in = { 0 };
    in.x          = x;
    in.y          = y;
    in.cameraYaw  = cameraYaw;
    in.playerYaw  = playerYaw;
    in.mode       = mode;
    in.bRunKey    = bRunKey;

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);
    return out;
}

static SithModernOutput sithModernControlsTest_ComputeLock(float x, float y, float cameraYaw,
    float playerYaw, bool bHasTarget, float targetYaw)
{
    SithModernInput in = { 0 };
    in.x             = x;
    in.y             = y;
    in.cameraYaw     = cameraYaw;
    in.playerYaw     = playerYaw;
    in.mode          = SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    in.bLocked       = true;
    in.bHasLockTarget = bHasTarget;
    in.lockTargetYaw = targetYaw;

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);
    return out;
}

static float sithModernControlsTest_PlayerYaw(void)
{
    const rdVector3* pLvec = &sithModernControlsTest_player.orient.lvec;
    return STDMATH_TODEGREESF(atan2f(-pLvec->x, pLvec->y));
}

static SithModernControlMode sithModernControlsTest_ModeForStatus(SithThingMoveStatus moveStatus)
{
    sithModernControlsTest_player.moveStatus  = moveStatus;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    return sithModernControls_GetMode(&sithModernControlsTest_player);
}

TEST_SETUP(sithModernControls)
{
    memset(&sithModernControlsTest_hostServices, 0, sizeof(sithModernControlsTest_hostServices));
    sithModernControlsTest_hostServices.pAssert       = sithModernControlsTest_Assert;
    sithModernControlsTest_hostServices.pStatusPrint  = sithModernControlsTest_Print;
    sithModernControlsTest_hostServices.pMessagePrint = sithModernControlsTest_Print;
    sithModernControlsTest_hostServices.pWarningPrint = sithModernControlsTest_Print;
    sithModernControlsTest_hostServices.pErrorPrint   = sithModernControlsTest_Print;
    sithModernControlsTest_hostServices.pDebugPrint   = sithModernControlsTest_Print;
    sithModernControlsTest_hostServices.pMalloc       = sithModernControlsTest_Malloc;
    sithModernControlsTest_hostServices.pFree         = sithModernControlsTest_Free;
    sith_g_pHS  = &sithModernControlsTest_hostServices;
    std_g_pHS   = &sithModernControlsTest_hostServices;
    sithModernControlsTest_assertCount = 0;

    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, sithControl_Startup(0));

    sithControl_RegisterAxisFunction(SITHCONTROL_FORWARD, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_BACK, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TURNLEFT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TURNRIGHT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_RUNFWD, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_ACT1, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TARGETLOCK, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_STPLEFT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_STPRIGHT, SITHCONTROLFUNCTION_KEY);

    TEST_ASSERT_EQUAL_INT(1, sithControl_Open());

    sithControl_g_controlOptions = 0x04;

    sithControl_BindControl(SITHCONTROL_FORWARD, STDKEY_UP, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_BACK, STDKEY_DOWN, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNLEFT, STDKEY_LEFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNRIGHT, STDKEY_RIGHT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_RUNFWD, STDKEY_LSHIFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_ACT1, STDKEY_SPACE, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TARGETLOCK, STDKEY_F, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_STPLEFT, STDKEY_Q, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_STPRIGHT, STDKEY_E, (SithControlBindFlag)0);

    sithModernControlsTest_ResetWorld();
}

TEST_TEAR_DOWN(sithModernControls)
{
    sithControl_ClearKeyOverrides();

    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();
    sithModernControls_ResetLock();
    sithModernCamera_ResetGlobals();
    sithControl_SetModernControlStyle(J3D_QOL_VALUE(true, false));

    sithPlayer_g_pLocalPlayerThing = NULL;
    sithCamera_g_pCurCamera        = NULL;
    sithPlayerControls_SetVehicleBoardedThing(NULL);

    sith_g_pHS = NULL;
    std_g_pHS  = NULL;
}

TEST(sithModernControls, ComputeCameraBehindPressingUpMovesForward)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, 1.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.forward.amount);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.run.bPressed);
}

TEST(sithModernControls, ComputePressingBackFacesBackAndMovesForward)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, -1.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.forward.amount);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, fabsf(out.targetYaw));
}

TEST(sithModernControls, ComputePressingRightFacesRightAndMovesForward)
{
    SithModernOutput out = sithModernControlsTest_Compute(1.0f, 0.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -90.0f, out.targetYaw);

    // Facing +X, the same stick input now points along Indy's facing.
    out = sithModernControlsTest_Compute(1.0f, 0.0f, 0.0f, -90.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -90.0f, out.targetYaw);
}

TEST(sithModernControls, ComputeCameraYawRotatesWorldDirection)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, 1.0f, 90.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    // A camera yawed 90 degrees makes stick up point to world -X, which is Indy's left.
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, out.targetYaw);

    out = sithModernControlsTest_Compute(0.0f, 1.0f, 90.0f, 90.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, out.targetYaw);
}

TEST(sithModernControls, ComputeUsesHeadingForAnyStickAngle)
{
    // A target 30 degrees to the left still moves forward and faces that heading.
    SithModernOutput out = sithModernControlsTest_Compute(-0.5f, 0.8660254f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, out.targetYaw);
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);

    // A target 10 degrees to the right keeps the same rule.
    out = sithModernControlsTest_Compute(0.1736482f, 0.9848078f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, out.targetYaw);
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeWalkAndRunFromMagnitude)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, 0.5f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, out.forward.amount);
    TEST_ASSERT_FALSE(out.run.bPressed);

    out = sithModernControlsTest_Compute(0.0f, 1.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.run.bPressed);
}

TEST(sithModernControls, ComputeRelativeModeMapsAxesLikeClassic)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.5f, 0.8f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, out.forward.amount);
    TEST_ASSERT_TRUE(out.turnRight.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, out.turnRight.amount);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);

    out = sithModernControlsTest_Compute(-0.5f, -0.8f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_RELATIVE, false);

    TEST_ASSERT_TRUE(out.back.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, out.back.amount);
    TEST_ASSERT_TRUE(out.turnLeft.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, out.turnLeft.amount);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeNoneSetsNothing)
{
    SithModernOutput out = sithModernControlsTest_Compute(1.0f, 1.0f, 45.0f, 30.0f,
        SITHMODERNCONTROL_MODE_NONE, true);

    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.run.bPressed);
}

#ifdef J3D_QOL_IMPROVEMENTS
TEST(sithModernControls, UpdateAppliesOverridesWhenModern)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_ReadKey(STDKEY_UP, 1);

    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_FLOAT(0.5f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_RUNFWD, NULL));

    // A real BACK press becomes a forward move toward the camera and never a backpedal.
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_DOWN, 0);
}

TEST(sithModernControls, UpdateBackRunsAtOnceAndTurnsDirectly)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);

    sithModernControls_Update(&sithModernControlsTest_player, 0.1f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));

    // 0.1 s at 720 deg/s is 72 degrees of the 180-degree turn.
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 72.0f, sithModernControlsTest_PlayerYaw());

    // Repeated updates settle on the target heading without overshooting it.
    for ( int i = 0; i < 600; ++i )
    {
        sithModernControls_Update(&sithModernControlsTest_player, 0.016f);
        float delta = stdMath_NormalizeAngleAcute(180.0f - sithModernControlsTest_PlayerYaw());
        TEST_ASSERT_TRUE(delta >= -0.1f);
        if ( fabsf(delta) <= 0.1f )
        {
            break;
        }
    }

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 180.0f, fabsf(sithModernControlsTest_PlayerYaw()));

    sithModernControlsTest_ReadKey(STDKEY_DOWN, 0);
}

TEST(sithModernControls, UpdateLeavesRealKeysWhenClassic)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(false);
    sithModernControlsTest_ReadKey(STDKEY_UP, 1);

    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);

    // Without a Modern override the key binding contributes no axis value.
    TEST_ASSERT_EQUAL_FLOAT(0.0f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));

    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithModernControls, UpdateQuickStepCountsOnlyThePressEdge)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_ReadKey(STDKEY_Q, 1);

    // The press edge reaches the original sidestep code once.
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);

    // A held key is suppressed on the next frame, so the step does not repeat.
    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    // Releasing and pressing again is a new edge.
    sithModernControlsTest_ReadKey(STDKEY_Q, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    sithModernControlsTest_ReadKey(STDKEY_Q, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    sithModernControlsTest_ReadKey(STDKEY_Q, 0);

    // The right step follows the same rule.
    sithModernControlsTest_ReadKey(STDKEY_E, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));
    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));
    sithModernControlsTest_ReadKey(STDKEY_E, 0);
}

TEST(sithModernControls, UpdateLeavesQuickStepHoldInClassic)
{
    sithControl_SetModernControlStyle(false);
    sithModernControlsTest_ReadKey(STDKEY_Q, 1);

    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    // Classic keeps the original hold behavior.
    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_Q, 0);
}

TEST(sithModernControls, UpdateAppliesStickDeadzone)
{
    size_t axisX = STDCONTROL_GET_JOYSTICK_AXIS_X(0);
    size_t axisY = STDCONTROL_GET_JOYSTICK_AXIS_Y(0);

    sithControl_SetModernControlStyle(true);

    stdControl_RegisterAxis(axisX, -32768, 32767, 0.0f);
    stdControl_RegisterAxis(axisY, -32768, 32767, 0.0f);
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisX));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisY));

    // Y is down positive, so -4000 is a small push forward, inside the 0.2 radial dead zone.
    stdControl_SetAxisState(axisX, 0);
    stdControl_SetAxisState(axisY, -4000);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));

    // -10000 is just outside the dead zone and walks forward.
    stdControl_SetAxisState(axisY, -10000);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_RUNFWD, NULL));

    stdControl_SetAxisState(axisY, 0);
}

TEST(sithModernControls, UpdateLockedSidewaysHoldsSidestepKey)
{
    size_t axisX = STDCONTROL_GET_JOYSTICK_AXIS_X(0);
    size_t axisY = STDCONTROL_GET_JOYSTICK_AXIS_Y(0);

    sithControl_SetModernControlStyle(true);

    stdControl_RegisterAxis(axisX, -32768, 32767, 0.0f);
    stdControl_RegisterAxis(axisY, -32768, 32767, 0.0f);
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisX));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisY));
    stdControl_SetAxisState(axisX, 0);
    stdControl_SetAxisState(axisY, 0);

    // A held lock with no target still owns the camera-relative movement.
    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());

    // Stick right holds the original sidestep key for as long as it stays right.
    stdControl_SetAxisState(axisX, 32767);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));

    // The Modern layer adds no lateral thrust of its own.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernControlsTest_player.moveInfo.physics.thrust.x);

    // The other side follows the same rule.
    stdControl_SetAxisState(axisX, -32767);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));

    stdControl_SetAxisState(axisX, 0);
    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
}

TEST(sithModernControls, UpdateQuickStepRuleIsOffWhileLocked)
{
    sithControl_SetModernControlStyle(true);

    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());

    // While locked the physical sidestep key keeps its original hold behavior.
    sithModernControlsTest_ReadKey(STDKEY_Q, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    // Releasing the lock puts the quick-step press-edge rule back.
    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_FALSE(sithModernControls_IsLockActive());
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_Q, 0);
}
#endif

TEST(sithModernControls, ClassifierKeepsOriginalCameraStatesRelative)
{
    static const SithThingMoveStatus aRelative[] =
    {
        SITHPLAYERMOVE_HANGING,

        SITHPLAYERMOVE_CLIMBIDLE,
        SITHPLAYERMOVE_MOUNTING_WALL,
        SITHPLAYERMOVE_CLIMBING_UP,
        SITHPLAYERMOVE_CLIMBING_DOWN,
        SITHPLAYERMOVE_CLIMBING_LEFT,
        SITHPLAYERMOVE_CLIMBING_RIGHT,
        SITHPLAYERMOVE_PULLINGUP,
        SITHPLAYERMOVE_PULLINGUP_1M,
        SITHPLAYERMOVE_CLIMB_DOWN_TO_MOUNT,
        SITHPLAYERMOVE_CLIMB_TO_HANG,

        SITHPLAYERMOVE_PUSHING,
        SITHPLAYERMOVE_PULLING,
        SITHPLAYERMOVE_PUSHPULL_READY,

        SITHPLAYERMOVE_WHIPSWINGING,
        SITHPLAYERMOVE_WHIPCLIMBIDLE,
        SITHPLAYERMOVE_WHIPCLIMB_START,
        SITHPLAYERMOVE_WHIP_UNK2,

        SITHPLAYERMOVE_JEWELFLYING,
        SITHPLAYERMOVE_JEWELFLYING_UNKN1,
        SITHPLAYERMOVE_JEWELFLYING_UNKN2,

        SITHPLAYERMOVE_JEEP_IMPACT,
        SITHPLAYERMOVE_MINECAR_IDLE,
        SITHPLAYERMOVE_MINECAR_BOARDING,
        SITHPLAYERMOVE_MINECAR_UNBOARDING_LEFT,
        SITHPLAYERMOVE_MINECAR_UNBOARDING_RIGHT,
        SITHPLAYERMOVE_MINECAR_DUCKING,
        SITHPLAYERMOVE_MINECAR_DUCKED,
        SITHPLAYERMOVE_MINECAR_GETTING_UP,
        SITHPLAYERMOVE_JEEP_IDLE,
        SITHPLAYERMOVE_JEEP_BOARDING,
        SITHPLAYERMOVE_JEEP_UNBOARDING,
        SITHPLAYERMOVE_RAFT_IDLE,
        SITHPLAYERMOVE_RAFT_PADDLE_FORWARD_LEFT,
        SITHPLAYERMOVE_RAFT_PADDLE_FORWARD_RIGHT,
        SITHPLAYERMOVE_RAFT_TURN_LEFT,
        SITHPLAYERMOVE_RAFT_TURN_RIGHT,
        SITHPLAYERMOVE_RAFT_PADDLE_BACK_LEFT,
        SITHPLAYERMOVE_RAFT_PADDLE_BACK_RIGHT,
        SITHPLAYERMOVE_RAFT_STARTPADDLE_LEFT,
        SITHPLAYERMOVE_RAFT_ENDPADDLE_LEFT,
        SITHPLAYERMOVE_RAFT_ENDPADDLE_RIGHT,
        SITHPLAYERMOVE_RAFT_STARTPADDLE_RIGHT,
        SITHPLAYERMOVE_RAFT_PADDLERIGHT_STARTPADDLE_LEFT,
        SITHPLAYERMOVE_RAFT_PADDLELEFT_STARTPADDLE_RIGHT,
        SITHPLAYERMOVE_RAFT_DOCKING,
        SITHPLAYERMOVE_RAFT_BOARDING,
        SITHPLAYERMOVE_RAFT_UNBOARDING_LEFT,
        SITHPLAYERMOVE_RAFT_UNBOARDING_RIGHT,
        SITHPLAYERMOVE_RAFT_UNBOARD_START,

        SITHPLAYERMOVE_SLIDING,
        SITHPLAYERMOVE_SLIDEDOWNFORWARD,
        SITHPLAYERMOVE_SLIDEDOWNBACK,
    };

    for ( size_t i = 0; i < STD_ARRAYLEN(aRelative); ++i )
    {
        TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_RELATIVE,
            sithModernControlsTest_ModeForStatus(aRelative[i]));
    }

    // A boarded vehicle keeps the original camera and stick-relative movement in every status.
    sithModernControlsTest_player.moveStatus  = SITHPLAYERMOVE_STILL;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    sithPlayerControls_SetVehicleBoardedThing(&sithModernControlsTest_player);
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_RELATIVE,
        sithModernControls_GetMode(&sithModernControlsTest_player));
    sithPlayerControls_SetVehicleBoardedThing(NULL);
}

TEST(sithModernControls, ClassifierLeavesJumpAndForcedStatesToRealKeys)
{
    static const SithThingMoveStatus aNone[] =
    {
        SITHPLAYERMOVE_JUMPFWD,
        SITHPLAYERMOVE_JUMPUP,
        SITHPLAYERMOVE_JUMP_READY,
        SITHPLAYERMOVE_JUMPROLLFWD,
        SITHPLAYERMOVE_JUMPROLLBACK,
        SITHPLAYERMOVE_JUMPLEFT,
        SITHPLAYERMOVE_JUMPRIGHT,
        SITHPLAYERMOVE_JUMPBACK,
        SITHPLAYERMOVE_LEAPFWD,
        SITHPLAYERMOVE_FALLING,
        SITHPLAYERMOVE_LAND,
        SITHPLAYERMOVE_KNOCKEDOUT,
    };

    for ( size_t i = 0; i < STD_ARRAYLEN(aNone); ++i )
    {
        TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_NONE,
            sithModernControlsTest_ModeForStatus(aNone[i]));
    }

    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    sithModernControlsTest_player.thingInfo.actorInfo.bForceMovePlay = 1;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_NONE,
        sithModernControls_GetMode(&sithModernControlsTest_player));
    sithModernControlsTest_player.thingInfo.actorInfo.bForceMovePlay = 0;
}

TEST(sithModernControls, ClassifierMakesTransitionsAndUnknownsCameraRelative)
{
    static const SithThingMoveStatus aCameraRelative[] =
    {
        SITHPLAYERMOVE_STILL,
        SITHPLAYERMOVE_WALKING,
        SITHPLAYERMOVE_RUNNING,
        SITHPLAYERMOVE_CRAWLIDLE,
        SITHPLAYERMOVE_SWIMIDLE,

        SITHPLAYERMOVE_UNKNOWN_4,
        SITHPLAYERMOVE_UNKNOWN_5,
        SITHPLAYERMOVE_UNKNOWN_21,
        SITHPLAYERMOVE_UNKNOWN_46,
        SITHPLAYERMOVE_UNKNOWN_60,
        SITHPLAYERMOVE_UNKNOWN_63,
        SITHPLAYERMOVE_UNKNOWN_78,
        SITHPLAYERMOVE_UNKNOWN_80,
        SITHPLAYERMOVE_UNKNOWN_81,
        SITHPLAYERMOVE_UNKNOWN_82,
        SITHPLAYERMOVE_UNKNOWN_83,
        SITHPLAYERMOVE_UNKNOWN_84,
        SITHPLAYERMOVE_UNKNOWN_86,

        SITHPLAYERMOVE_ROLLING_LEFT,
        SITHPLAYERMOVE_ROLLING_RIGHT,
        SITHPLAYERMOVE_STRAFING_LEFT,
        SITHPLAYERMOVE_STRAFING_RIGHT,
        SITHPLAYERMOVE_ACTIVATING,
        SITHPLAYERMOVE_STAND_TO_CRAWL,
        SITHPLAYERMOVE_CRAWL_TO_STAND,

        SITHPLAYERMOVE_TURNING_LEFT,
        SITHPLAYERMOVE_TURNING_RIGHT,
        SITHPLAYERMOVE_WALK2STAND,
        SITHPLAYERMOVE_STAND2WALK,
        SITHPLAYERMOVE_STAND2RUN,

        SITHPLAYERMOVE_TURNING_LEFT_45_DEGREES,
        SITHPLAYERMOVE_TURNING_RIGHT_45_DEGREES,
        SITHPLAYERMOVE_TURNING_LEFT_90_DEGREES,
        SITHPLAYERMOVE_TURNING_RIGHT_90_DEGREES,
        SITHPLAYERMOVE_TURNING_LEFT_135_DEGREES,
        SITHPLAYERMOVE_TURNING_RIGHT_135_DEGREES,
        SITHPLAYERMOVE_TURNING_180_DEGREES,
        SITHPLAYERMOVE_RUNOVER,
    };

    for ( size_t i = 0; i < STD_ARRAYLEN(aCameraRelative); ++i )
    {
        TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE,
            sithModernControlsTest_ModeForStatus(aCameraRelative[i]));
    }
}

TEST(sithModernControls, ClassifierUsesWaterAndFloorFallback)
{
    sithModernControlsTest_player.moveStatus  = SITHPLAYERMOVE_UNKNOWN_46;
    sithModernControlsTest_player.attach.flags = 0;
    sithModernControlsTest_player.pInSector    = &sithModernControlsTest_sector;

    sithModernControlsTest_sector.flags = SITH_SECTOR_UNDERWATER;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE,
        sithModernControls_GetMode(&sithModernControlsTest_player));

    sithModernControlsTest_sector.flags = SITH_SECTOR_AETHERIUM;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE,
        sithModernControls_GetMode(&sithModernControlsTest_player));

    // Airborne and not swimming keeps the real keys.
    sithModernControlsTest_sector.flags = 0;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_NONE,
        sithModernControls_GetMode(&sithModernControlsTest_player));
}

TEST(sithModernControls, SelectLockTargetPrefersTheCameraDirection)
{
    SithThing selector;
    memset(&selector, 0, sizeof(selector));
    selector.type = SITH_THING_ACTOR;
    // Indy faces away from the camera's forward direction (+Y).
    selector.orient.lvec.y = -1.0f;
    selector.orient.uvec.z = 1.0f;
    selector.orient.rvec.x = 1.0f;

    // 60 degrees off the camera, 1 m away.
    SithThing offCamera60;
    memset(&offCamera60, 0, sizeof(offCamera60));
    offCamera60.type = SITH_THING_ACTOR;
    offCamera60.pos.x = -0.8660254f;
    offCamera60.pos.y = 0.5f;

    // 10 degrees off the camera and 2 m away, behind Indy's facing.
    SithThing camera10;
    memset(&camera10, 0, sizeof(camera10));
    camera10.type = SITH_THING_ACTOR;
    camera10.pos.x = 0.3472964f;
    camera10.pos.y = 1.9696155f;

    // 20 degrees off the camera but much closer: the smaller angle still wins.
    SithThing camera20Near;
    memset(&camera20Near, 0, sizeof(camera20Near));
    camera20Near.type = SITH_THING_ACTOR;
    camera20Near.pos.x = 0.1710101f;
    camera20Near.pos.y = 0.4698463f;

    SithThing dead;
    memset(&dead, 0, sizeof(dead));
    dead.type  = SITH_THING_ACTOR;
    dead.pos.y = 0.4f;
    dead.flags = SITH_TF_DYING;

    SithThing notAnActor;
    memset(&notAnActor, 0, sizeof(notAnActor));
    notAnActor.type  = SITH_THING_FREE;
    notAnActor.pos.y = 0.2f;

    SithThing* aCandidates[] = { &offCamera60, &camera20Near, &dead, &notAnActor, &camera10 };
    TEST_ASSERT_EQUAL_PTR(&camera10, sithPlayerControls_SelectLockTarget(&selector, 0.0f,
        aCandidates, STD_ARRAYLEN(aCandidates)));

    // A hostile outside the 35 degree cone is never chosen.
    TEST_ASSERT_NULL(sithPlayerControls_SelectLockTarget(&selector, 0.0f,
        &aCandidates[0], 1));
}

TEST(sithModernControls, SelectLockTargetRejectsOutsideCone)
{
    SithThing selector;
    memset(&selector, 0, sizeof(selector));
    selector.type = SITH_THING_ACTOR;

    SithThing outside;
    memset(&outside, 0, sizeof(outside));
    outside.type = SITH_THING_ACTOR;
    outside.pos.x = -0.6427876f; // 40 degrees left of the camera
    outside.pos.y = 0.7660444f;

    TEST_ASSERT_NULL(sithPlayerControls_SelectLockTarget(&selector, 0.0f, (SithThing*[]){ &outside }, 1));
}

TEST(sithModernControls, ComputeLockFacesTargetWithoutTurnKeys)
{
    SithModernOutput out = sithModernControlsTest_ComputeLock(0.0f, 0.0f, 0.0f, 0.0f, true, 30.0f);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, out.targetYaw);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);

    out = sithModernControlsTest_ComputeLock(0.0f, 0.0f, 0.0f, 0.0f, true, -30.0f);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -30.0f, out.targetYaw);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);

    out = sithModernControlsTest_ComputeLock(0.0f, 0.0f, 0.0f, 0.0f, true, 2.0f);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, out.targetYaw);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeLockHoldsSidestepKeys)
{
    SithModernOutput out = sithModernControlsTest_ComputeLock(0.0f, -1.0f, 0.0f, 0.0f, true, 0.0f);
    TEST_ASSERT_TRUE(out.back.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.back.amount);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.strafeLeft.bPressed);
    TEST_ASSERT_FALSE(out.strafeRight.bPressed);

    out = sithModernControlsTest_ComputeLock(1.0f, 0.0f, 0.0f, 0.0f, true, 0.0f);
    TEST_ASSERT_TRUE(out.strafeRight.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.strafeRight.amount);
    TEST_ASSERT_FALSE(out.strafeLeft.bPressed);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);

    out = sithModernControlsTest_ComputeLock(-1.0f, 0.0f, 0.0f, 0.0f, true, 0.0f);
    TEST_ASSERT_TRUE(out.strafeLeft.bPressed);

    // A diagonal that is not sideways falls back to forward or back.
    out = sithModernControlsTest_ComputeLock(0.4f, -0.8f, 0.0f, 0.0f, true, 0.0f);
    TEST_ASSERT_FALSE(out.strafeLeft.bPressed);
    TEST_ASSERT_FALSE(out.strafeRight.bPressed);
    TEST_ASSERT_TRUE(out.back.bPressed);
}

TEST(sithModernControls, ComputeLockWithoutTargetUsesIndyFacing)
{
    SithModernOutput out = sithModernControlsTest_ComputeLock(0.0f, 1.0f, 90.0f, 0.0f, false, 0.0f);
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.strafeLeft.bPressed);
    TEST_ASSERT_FALSE(out.strafeRight.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.bTurnTowardTarget);
}

#ifdef J3D_QOL_IMPROVEMENTS
TEST(sithModernControls, UpdateSnapsCameraOnceWhenLockHasNoTarget)
{
    sithControl_SetModernControlStyle(true);
    sithModernCamera_ResetGlobals();
    sithModernCamera_SetYawPitch(90.0f, 0.0f);

    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_NULL(sithModernControls_GetLockedTarget());
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());

    // The press starts the snap; finishing it centers the camera behind Indy.
    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sithModernCamera_GetYaw());

    // Holding the lock does not snap again.
    sithModernCamera_SetYawPitch(45.0f, 0.0f);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, sithModernCamera_GetYaw());

    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_FALSE(sithModernControls_IsLockActive());
}

TEST(sithModernControls, UpdateIgnoresTargetLockWhenClassic)
{
    sithControl_SetModernControlStyle(false);
    sithModernCamera_ResetGlobals();
    sithModernCamera_SetYawPitch(90.0f, 0.0f);

    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, 0.3f);

    TEST_ASSERT_NULL(sithModernControls_GetLockedTarget());
    TEST_ASSERT_FALSE(sithModernControls_IsLockActive());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, sithModernCamera_GetYaw());

    sithModernControlsTest_ReadKey(STDKEY_F, 0);
}
#endif

TEST_GROUP_RUNNER(sithModernControls)
{
    RUN_TEST_CASE(sithModernControls, ComputeCameraBehindPressingUpMovesForward);
    RUN_TEST_CASE(sithModernControls, ComputePressingBackFacesBackAndMovesForward);
    RUN_TEST_CASE(sithModernControls, ComputePressingRightFacesRightAndMovesForward);
    RUN_TEST_CASE(sithModernControls, ComputeCameraYawRotatesWorldDirection);
    RUN_TEST_CASE(sithModernControls, ComputeUsesHeadingForAnyStickAngle);
    RUN_TEST_CASE(sithModernControls, ComputeWalkAndRunFromMagnitude);
    RUN_TEST_CASE(sithModernControls, ComputeRelativeModeMapsAxesLikeClassic);
    RUN_TEST_CASE(sithModernControls, ComputeNoneSetsNothing);
    RUN_TEST_CASE(sithModernControls, ClassifierKeepsOriginalCameraStatesRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierLeavesJumpAndForcedStatesToRealKeys);
    RUN_TEST_CASE(sithModernControls, ClassifierMakesTransitionsAndUnknownsCameraRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierUsesWaterAndFloorFallback);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetPrefersTheCameraDirection);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetRejectsOutsideCone);
    RUN_TEST_CASE(sithModernControls, ComputeLockFacesTargetWithoutTurnKeys);
    RUN_TEST_CASE(sithModernControls, ComputeLockHoldsSidestepKeys);
    RUN_TEST_CASE(sithModernControls, ComputeLockWithoutTargetUsesIndyFacing);
#ifdef J3D_QOL_IMPROVEMENTS
    RUN_TEST_CASE(sithModernControls, UpdateAppliesOverridesWhenModern);
    RUN_TEST_CASE(sithModernControls, UpdateBackRunsAtOnceAndTurnsDirectly);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesRealKeysWhenClassic);
    RUN_TEST_CASE(sithModernControls, UpdateQuickStepCountsOnlyThePressEdge);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesQuickStepHoldInClassic);
    RUN_TEST_CASE(sithModernControls, UpdateAppliesStickDeadzone);
    RUN_TEST_CASE(sithModernControls, UpdateLockedSidewaysHoldsSidestepKey);
    RUN_TEST_CASE(sithModernControls, UpdateQuickStepRuleIsOffWhileLocked);
    RUN_TEST_CASE(sithModernControls, UpdateSnapsCameraOnceWhenLockHasNoTarget);
    RUN_TEST_CASE(sithModernControls, UpdateIgnoresTargetLockWhenClassic);
#endif
}
