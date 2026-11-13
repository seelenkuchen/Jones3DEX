// Added: the Modern control style's camera-relative move translation (ADR-0029). The Compute
// cases are pure; the Update cases drive the virtual key layer with a stubbed camera and player.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithModernControls.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>

#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(sithModernControls);

static tHostServices sithModernControlsTest_hostServices;
static size_t sithModernControlsTest_assertCount;

static SithThing sithModernControlsTest_player;
static SithCamera sithModernControlsTest_camera;

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
    memset(&sithModernControlsTest_player, 0, sizeof(sithModernControlsTest_player));
    sithModernControlsTest_player.type       = SITH_THING_PLAYER;
    sithModernControlsTest_player.moveType   = SITH_MT_PHYSICS;
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    sithModernControlsTest_player.orient.lvec  = (rdVector3){ { 0.0f }, { 1.0f }, { 0.0f } };

    memset(&sithModernControlsTest_camera, 0, sizeof(sithModernControlsTest_camera));
    sithModernControlsTest_camera.orient.lvec = (rdVector3){ { 0.0f }, { 1.0f }, { 0.0f } };

    sithPlayer_g_pLocalPlayerThing = &sithModernControlsTest_player;
    sithCamera_g_pCurCamera        = &sithModernControlsTest_camera;
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

    TEST_ASSERT_EQUAL_INT(1, sithControl_Open());

    sithControl_g_controlOptions = 0x04;

    sithControl_BindControl(SITHCONTROL_FORWARD, STDKEY_UP, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_BACK, STDKEY_DOWN, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNLEFT, STDKEY_LEFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNRIGHT, STDKEY_RIGHT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_RUNFWD, STDKEY_LSHIFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_ACT1, STDKEY_SPACE, (SithControlBindFlag)0);

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
    sithControl_SetModernControlStyle(J3D_QOL_VALUE(true, false));

    sithPlayer_g_pLocalPlayerThing = NULL;
    sithCamera_g_pCurCamera        = NULL;

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

TEST(sithModernControls, ComputePressingBackTurnsWithoutBack)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, -1.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_TRUE(out.turnLeft.bPressed || out.turnRight.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f,
        out.turnLeft.bPressed ? out.turnLeft.amount : out.turnRight.amount);
}

TEST(sithModernControls, ComputePressingRightTurnsThenMovesWhenAligned)
{
    SithModernOutput out = sithModernControlsTest_Compute(1.0f, 0.0f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);

    // Facing +X, the same stick input now points along Indy's facing.
    out = sithModernControlsTest_Compute(1.0f, 0.0f, 0.0f, -90.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeCameraYawRotatesWorldDirection)
{
    SithModernOutput out = sithModernControlsTest_Compute(0.0f, 1.0f, 90.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    // A camera yawed 90 degrees makes stick up point to world -X, which is Indy's left.
    TEST_ASSERT_TRUE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.forward.bPressed);

    out = sithModernControlsTest_Compute(0.0f, 1.0f, 90.0f, 90.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeTurnAmountScalesWithAngle)
{
    // A target 30 degrees to the left turns and still moves forward.
    SithModernOutput out = sithModernControlsTest_Compute(-0.5f, 0.8660254f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.turnLeft.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, out.turnLeft.amount);
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);

    // A target 10 degrees to the right stays above the turn threshold but uses the minimum amount.
    out = sithModernControlsTest_Compute(0.1736482f, 0.9848078f, 0.0f, 0.0f,
        SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

    TEST_ASSERT_TRUE(out.turnRight.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, out.turnRight.amount);
    TEST_ASSERT_TRUE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
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

    // A real BACK press is suppressed and translated into a turn, not a backpedal.
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_TRUE(sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL)
        || sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));

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
#endif

TEST_GROUP_RUNNER(sithModernControls)
{
    RUN_TEST_CASE(sithModernControls, ComputeCameraBehindPressingUpMovesForward);
    RUN_TEST_CASE(sithModernControls, ComputePressingBackTurnsWithoutBack);
    RUN_TEST_CASE(sithModernControls, ComputePressingRightTurnsThenMovesWhenAligned);
    RUN_TEST_CASE(sithModernControls, ComputeCameraYawRotatesWorldDirection);
    RUN_TEST_CASE(sithModernControls, ComputeTurnAmountScalesWithAngle);
    RUN_TEST_CASE(sithModernControls, ComputeWalkAndRunFromMagnitude);
    RUN_TEST_CASE(sithModernControls, ComputeRelativeModeMapsAxesLikeClassic);
    RUN_TEST_CASE(sithModernControls, ComputeNoneSetsNothing);
#ifdef J3D_QOL_IMPROVEMENTS
    RUN_TEST_CASE(sithModernControls, UpdateAppliesOverridesWhenModern);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesRealKeysWhenClassic);
    RUN_TEST_CASE(sithModernControls, UpdateAppliesStickDeadzone);
#endif
}
