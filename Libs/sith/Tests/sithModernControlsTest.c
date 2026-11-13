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
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/Main/sithMain.h>

#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
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

TEST(sithModernControls, ComputeStickLeftConvergesWithAnchoredCamera)
{
    const float cameraYaw = 30.0f;
    const float step      = 10.0f;
    float playerYaw       = cameraYaw;
    bool bForward         = false;
    int steps             = 0;

    for ( ; steps < 60; ++steps )
    {
        SithModernOutput out = sithModernControlsTest_Compute(-1.0f, 0.0f, cameraYaw,
            playerYaw, SITHMODERNCONTROL_MODE_CAMERA_RELATIVE, false);

        if ( out.forward.bPressed )
        {
            bForward = true;
        }

        if ( !out.turnLeft.bPressed && !out.turnRight.bPressed )
        {
            break;
        }

        TEST_ASSERT_TRUE(out.turnLeft.bPressed || out.turnRight.bPressed);
        playerYaw += out.turnLeft.bPressed ? step : -step;
    }

    TEST_ASSERT_TRUE(bForward);
    TEST_ASSERT_TRUE(steps < 60);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, cameraYaw + 90.0f, playerYaw);
}

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
    RUN_TEST_CASE(sithModernControls, ClassifierKeepsOriginalCameraStatesRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierLeavesJumpAndForcedStatesToRealKeys);
    RUN_TEST_CASE(sithModernControls, ClassifierMakesTransitionsAndUnknownsCameraRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierUsesWaterAndFloorFallback);
    RUN_TEST_CASE(sithModernControls, ComputeStickLeftConvergesWithAnchoredCamera);
#ifdef J3D_QOL_IMPROVEMENTS
    RUN_TEST_CASE(sithModernControls, UpdateAppliesOverridesWhenModern);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesRealKeysWhenClassic);
    RUN_TEST_CASE(sithModernControls, UpdateAppliesStickDeadzone);
#endif
}
