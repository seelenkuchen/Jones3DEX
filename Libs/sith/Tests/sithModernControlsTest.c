// Added: the Modern control style's camera-relative move translation (ADR-0029). The Compute
// cases are pure; the Update cases drive the virtual key layer with a stubbed camera and player.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithCollision.h>
#include <sith/Engine/sithModernCamera.h>
#include <sith/Engine/sithPhysics.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Gameplay/sithModernControls.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerActions.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/Null/stdControlNull.h>

#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>

TEST_GROUP(sithModernControls);

static tHostServices sithModernControlsTest_hostServices;
static size_t sithModernControlsTest_assertCount;

static SithThing sithModernControlsTest_player;
static SithCamera sithModernControlsTest_camera;
static SithSector sithModernControlsTest_sector;
static SithPuppetState sithModernControlsTest_puppetState;

static void sithModernControlsTest_CleanupStepWorld(void); // Added

// Added: a sight test that models a wall which the camera can see past but Indy cannot: only an
// eye behind Indy (negative Y) has line of sight.
static bool J3DAPI sithModernControlsTest_SightFromBehindOnly(SithSector* pEyeSector, const rdVector3* pEyePos, const rdVector3* pTargetPos)
{
    (void)pEyeSector;
    (void)pTargetPos;
    return pEyePos->y < 0.0f;
}

static bool J3DAPI sithModernControlsTest_SightBlocked(SithSector* pEyeSector, const rdVector3* pEyePos, const rdVector3* pTargetPos)
{
    (void)pEyeSector;
    (void)pEyePos;
    (void)pTargetPos;
    return false;
}

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

static SithModernOutput sithModernControlsTest_ComputeQuickStep(float x, float y,
    float cameraYaw, float playerYaw, bool bAllowed)
{
    SithModernInput in = { 0 };
    in.x                = x;
    in.y                = y;
    in.cameraYaw        = cameraYaw;
    in.playerYaw        = playerYaw;
    in.mode             = SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    in.bQuickStepHeld   = true;
    in.bQuickStepAllowed = bAllowed;

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);
    return out;
}

static SithModernOutput sithModernControlsTest_ComputeQuickStepLocked(float x, float y)
{
    SithModernInput in = { 0 };
    in.x                 = x;
    in.y                 = y;
    in.mode              = SITHMODERNCONTROL_MODE_CAMERA_RELATIVE;
    in.bLocked           = true;
    in.bHasLockTarget    = true;
    in.lockTargetYaw     = 0.0f;
    in.bQuickStepHeld    = true;
    in.bQuickStepAllowed = true;

    SithModernOutput out;
    sithModernControls_Compute(&in, &out);
    return out;
}

static float sithModernControlsTest_PlayerYaw(void)
{
    const rdVector3* pLvec = &sithModernControlsTest_player.orient.lvec;
    return STDMATH_TODEGREESF(atan2f(-pLvec->x, pLvec->y));
}

static float sithModernControlsTest_PlayerPitch(void)
{
    rdVector3 pyr;
    rdMatrix_ExtractAngles34(&sithModernControlsTest_player.orient, &pyr);
    return pyr.pitch;
}

static SithModernOutput sithModernControlsTest_ComputeSwim(float x, float y, float cameraYaw, float cameraPitch)
{
    SithModernInput in = { 0 };
    in.x           = x;
    in.y           = y;
    in.cameraYaw   = cameraYaw;
    in.cameraPitch = cameraPitch;
    in.mode        = SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM;

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
    sithControl_RegisterAxisFunction(SITHCONTROL_JUMP, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_RUNFWD, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_ACT1, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TARGETLOCK, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_STPLEFT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_STPRIGHT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_QUICKSTEP, SITHCONTROLFUNCTION_KEY);

    TEST_ASSERT_EQUAL_INT(1, sithControl_Open());

    sithControl_g_controlOptions = 0x04;

    sithControl_BindControl(SITHCONTROL_FORWARD, STDKEY_UP, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_BACK, STDKEY_DOWN, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNLEFT, STDKEY_LEFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TURNRIGHT, STDKEY_RIGHT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_JUMP, STDKEY_X, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_RUNFWD, STDKEY_LSHIFT, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_ACT1, STDKEY_SPACE, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_TARGETLOCK, STDKEY_F, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_STPLEFT, STDKEY_Q, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_STPRIGHT, STDKEY_E, (SithControlBindFlag)0);
    sithControl_BindControl(SITHCONTROL_QUICKSTEP, STDKEY_LMENU, (SithControlBindFlag)0);

    sithModernControlsTest_ResetWorld();
}

TEST_TEAR_DOWN(sithModernControls)
{
    sithModernControlsTest_CleanupStepWorld();
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

TEST(sithModernControls, ComputeSwimUsesCameraPitchAndSwimForward)
{
    // The camera looks 30 degrees down, so stick up dives 30 degrees, not straight ahead.
    SithModernOutput out = sithModernControlsTest_ComputeSwim(0.0f, 1.0f, 0.0f, -30.0f);

    TEST_ASSERT_TRUE(out.swimForward.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.swimForward.amount);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, out.targetYaw);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -30.0f, out.targetPitch);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.run.bPressed);

    // Stick right stays horizontal: it uses the camera's right vector and no pitch.
    out = sithModernControlsTest_ComputeSwim(1.0f, 0.0f, 0.0f, -30.0f);
    TEST_ASSERT_TRUE(out.swimForward.bPressed);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -90.0f, out.targetYaw);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, out.targetPitch);

    // The camera yaw rotates the whole direction; camera pitch -30 still dives 30.
    out = sithModernControlsTest_ComputeSwim(0.0f, 1.0f, 90.0f, -30.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 90.0f, out.targetYaw);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -30.0f, out.targetPitch);
}

TEST(sithModernControls, ComputeSwimClampsPitchToTheSwimLimit)
{
    SithModernOutput out = sithModernControlsTest_ComputeSwim(0.0f, 1.0f, 0.0f, -89.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -75.0f, out.targetPitch);
    TEST_ASSERT_TRUE(out.swimForward.bPressed);

    out = sithModernControlsTest_ComputeSwim(0.0f, 1.0f, 0.0f, 89.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 75.0f, out.targetPitch);
}

TEST(sithModernControls, ComputeSwimNeutralSetsNothing)
{
    SithModernOutput out = sithModernControlsTest_ComputeSwim(0.0f, 0.0f, 0.0f, -30.0f);

    TEST_ASSERT_FALSE(out.swimForward.bPressed);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.back.bPressed);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
    TEST_ASSERT_FALSE(out.run.bPressed);
    TEST_ASSERT_FALSE(out.bTurnTowardTarget);
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

TEST(sithModernControls, ComputeQuickStepMapsFourDirections)
{
    SithModernOutput out = sithModernControlsTest_ComputeQuickStep(0.0f, 1.0f, 0.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_FORWARD, out.quickStep);
    TEST_ASSERT_FALSE(out.forward.bPressed);
    TEST_ASSERT_FALSE(out.run.bPressed);

    out = sithModernControlsTest_ComputeQuickStep(0.0f, -1.0f, 0.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_BACK, out.quickStep);
    TEST_ASSERT_FALSE(out.back.bPressed);

    out = sithModernControlsTest_ComputeQuickStep(-1.0f, 0.0f, 0.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_LEFT, out.quickStep);
    TEST_ASSERT_FALSE(out.turnLeft.bPressed);

    out = sithModernControlsTest_ComputeQuickStep(1.0f, 0.0f, 0.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_RIGHT, out.quickStep);
    TEST_ASSERT_FALSE(out.turnRight.bPressed);
}

TEST(sithModernControls, ComputeQuickStepRotatesWithTheCamera)
{
    // A camera turned 90 degrees makes stick up Indy's left, and stick right his forward.
    SithModernOutput out = sithModernControlsTest_ComputeQuickStep(0.0f, 1.0f, 90.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_LEFT, out.quickStep);

    out = sithModernControlsTest_ComputeQuickStep(1.0f, 0.0f, 90.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_FORWARD, out.quickStep);

    // Indy facing right relative to the camera: stick forward is his right.
    out = sithModernControlsTest_ComputeQuickStep(0.0f, 1.0f, 0.0f, -90.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_RIGHT, out.quickStep);
}

TEST(sithModernControls, ComputeQuickStepNeedsThresholdAndTheEdge)
{
    SithModernOutput out = sithModernControlsTest_ComputeQuickStep(0.4f, 0.0f, 0.0f, 0.0f, true);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_NONE, out.quickStep);

    // Another step waits until the press edge re-arms.
    out = sithModernControlsTest_ComputeQuickStep(1.0f, 0.0f, 0.0f, 0.0f, false);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_NONE, out.quickStep);
}

TEST(sithModernControls, ComputeQuickStepLockedUsesIndysFacing)
{
    SithModernOutput out = sithModernControlsTest_ComputeQuickStepLocked(0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_FORWARD, out.quickStep);

    out = sithModernControlsTest_ComputeQuickStepLocked(1.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_RIGHT, out.quickStep);

    out = sithModernControlsTest_ComputeQuickStepLocked(0.0f, -1.0f);
    TEST_ASSERT_EQUAL_INT(SITHMODERNSTEP_BACK, out.quickStep);
    TEST_ASSERT_FALSE(out.back.bPressed);
}

TEST(sithModernControls, ComputeLockedBackWalksBack)
{
    SithModernOutput out = sithModernControlsTest_ComputeLock(0.0f, -1.0f, 0.0f, 0.0f, true, 0.0f);

    TEST_ASSERT_TRUE(out.back.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.back.amount);
    TEST_ASSERT_TRUE(out.bTurnTowardTarget);

    // Any held backward stick walks back; there is no back hop.
    out = sithModernControlsTest_ComputeLock(0.0f, -0.4f, 0.0f, 0.0f, true, 0.0f);
    TEST_ASSERT_TRUE(out.back.bPressed);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, out.back.amount);
}

TEST(sithModernControls, QuickStepBackStartsTheTimedStep)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    SithSurface surface;
    memset(&surface, 0, sizeof(surface));
    thing.type         = SITH_THING_PLAYER;
    thing.moveType     = SITH_MT_PHYSICS;
    thing.moveStatus   = SITHPLAYERMOVE_STILL;
    thing.attach.flags = SITH_ATTACH_SURFACE;
    thing.attach.attachedToStructure.pSurfaceAttached = &surface;
    thing.orient.lvec  = (rdVector3){ { 0.0f }, { 1.0f }, { 0.0f } };

    sithPlayerActions_StartTimedStep(&thing, /*bBackward=*/true);

    // The step uses the walking state, stays on the ground and never becomes the back hop.
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, thing.moveStatus);
    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_FALSE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(SITH_ATTACH_SURFACE, thing.attach.flags);
    TEST_ASSERT_TRUE(rdVector_IsZero3(&thing.moveInfo.physics.velocity));

    sithPlayerActions_StopTimedStep(&thing);

    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, thing.moveStatus);
    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
}

TEST(sithModernControls, LockedSidestepDropsFinishedStepForChaining)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    memset(&sithModernControlsTest_puppetState, 0, sizeof(sithModernControlsTest_puppetState));

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    SithPuppetTrack* pTrack = (SithPuppetTrack*)STDMALLOC(sizeof(*pTrack));
    TEST_ASSERT_NOT_NULL(pTrack);
    memset(pTrack, 0, sizeof(*pTrack));
    pTrack->trackNum = 3;
    pTrack->submode  = SITHPUPPETSUBMODE_STRAFELEFT;

    sithModernControlsTest_puppetState.pFirstTrack = pTrack;
    thing.pPuppetState        = &sithModernControlsTest_puppetState;
    thing.renderData.pPuppet  = &puppet;

    // The rdPuppet track is still playing, so the step must not be dropped.
    puppet.aTracks[3].pKFTrack = (rdKeyframe*)pTrack;
    puppet.aTracks[3].status   = RDPUPPET_TRACK_PLAYING;
    sithPuppet_DropFinishedMode(&thing, SITHPUPPETSUBMODE_STRAFELEFT);
    TEST_ASSERT_EQUAL_PTR(pTrack, sithModernControlsTest_puppetState.pFirstTrack);

    // Once the rdPuppet track finished, dropping it lets the held key start the next step at
    // once instead of waiting for the next puppet update.
    puppet.aTracks[3].pKFTrack = NULL;
    puppet.aTracks[3].status   = 0;
    sithPuppet_DropFinishedMode(&thing, SITHPUPPETSUBMODE_STRAFELEFT);
    TEST_ASSERT_NULL(sithModernControlsTest_puppetState.pFirstTrack);
}

TEST(sithModernControls, SelectLockTargetUsesTheCameraEyeForSight)
{
    SithThing selector;
    memset(&selector, 0, sizeof(selector));
    selector.type = SITH_THING_ACTOR;

    SithThing candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type  = SITH_THING_ACTOR;
    candidate.pos.y = 2.0f;

    SithThing* aCandidates[] = { &candidate };
    SithSector eyeSector;
    memset(&eyeSector, 0, sizeof(eyeSector));

    // Indy is at the origin. The fake sight only sees targets from an eye behind him, so this is
    // the "hidden from Indy, visible from the camera" case.
    rdVector3 cameraEye = { 0.0f, -2.0f, 0.0f };
    rdVector3 indyEye   = { 0.0f, 0.0f, 0.0f };

    TEST_ASSERT_EQUAL_PTR(&candidate, sithPlayerControls_SelectLockTargetEx(&selector, 0.0f,
        &eyeSector, &cameraEye, sithModernControlsTest_SightFromBehindOnly,
        aCandidates, STD_ARRAYLEN(aCandidates)));

    TEST_ASSERT_NULL(sithPlayerControls_SelectLockTargetEx(&selector, 0.0f,
        &eyeSector, &indyEye, sithModernControlsTest_SightFromBehindOnly,
        aCandidates, STD_ARRAYLEN(aCandidates)));
}

TEST(sithModernControls, SelectLockTargetRejectsHiddenFromCamera)
{
    SithThing selector;
    memset(&selector, 0, sizeof(selector));
    selector.type = SITH_THING_ACTOR;

    SithThing candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type  = SITH_THING_ACTOR;
    candidate.pos.y = 2.0f;

    SithThing* aCandidates[] = { &candidate };
    SithSector eyeSector;
    memset(&eyeSector, 0, sizeof(eyeSector));
    rdVector3 cameraEye = { 0.0f, -2.0f, 0.0f };

    TEST_ASSERT_NULL(sithPlayerControls_SelectLockTargetEx(&selector, 0.0f,
        &eyeSector, &cameraEye, sithModernControlsTest_SightBlocked,
        aCandidates, STD_ARRAYLEN(aCandidates)));
}

TEST(sithModernControls, SelectLockTargetKeepsRangeFromIndy)
{
    SithThing selector;
    memset(&selector, 0, sizeof(selector));
    selector.type = SITH_THING_ACTOR;

    SithThing candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type  = SITH_THING_ACTOR;
    candidate.pos.y = 9.0f;

    SithThing* aCandidates[] = { &candidate };
    SithSector eyeSector;
    memset(&eyeSector, 0, sizeof(eyeSector));

    // The camera is far behind; the range is still measured from Indy, so 9 m is in range.
    rdVector3 cameraEye = { 0.0f, -100.0f, 0.0f };
    TEST_ASSERT_EQUAL_PTR(&candidate, sithPlayerControls_SelectLockTargetEx(&selector, 0.0f,
        &eyeSector, &cameraEye, sithModernControlsTest_SightFromBehindOnly,
        aCandidates, STD_ARRAYLEN(aCandidates)));

    candidate.pos.y = 11.0f;
    TEST_ASSERT_NULL(sithPlayerControls_SelectLockTargetEx(&selector, 0.0f,
        &eyeSector, &cameraEye, sithModernControlsTest_SightFromBehindOnly,
        aCandidates, STD_ARRAYLEN(aCandidates)));
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

TEST(sithModernControls, UpdateSwimUsesCameraPitchAndSwimForward)
{
    sithControl_SetModernControlStyle(true);
    sithModernCamera_ResetGlobals();
    sithModernCamera_SetYawPitch(0.0f, -30.0f);
    sithModernControlsTest_camera.type = SITHCAMERA_EXTERNAL;
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_SWIMIDLE;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    for ( int i = 0; i < 200; ++i )
    {
        sithModernControls_Update(&sithModernControlsTest_player, 0.02f);
    }

    // The original forward/turn keys are suppressed and the JUMP override swims.
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_JUMP, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));

    // Stick up with the camera looking 30 degrees down settles at that dive, without overshoot.
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, sithModernControlsTest_PlayerYaw());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -30.0f, sithModernControlsTest_PlayerPitch());
    TEST_ASSERT_TRUE(sithModernControlsTest_PlayerPitch() >= -30.0f - 0.1f);

    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithModernControls, UpdateSwimRespectsTheOriginalPitchClamp)
{
    sithControl_SetModernControlStyle(true);

    // The original camera (not the free one) looks 80 degrees down; the swim limit keeps Indy
    // from following it past 75.
    rdVector3 cameraPYR = { -80.0f, 0.0f, 0.0f };
    sithModernControlsTest_camera.type = SITHCAMERA_INTERNAL;
    rdMatrix_BuildRotate34(&sithModernControlsTest_camera.orient, &cameraPYR);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_SWIMIDLE;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);

    float lowestPitch = 0.0f;
    for ( int i = 0; i < 200; ++i )
    {
        sithModernControls_Update(&sithModernControlsTest_player, 0.02f);
        float pitch = sithModernControlsTest_PlayerPitch();
        if ( pitch < lowestPitch )
        {
            lowestPitch = pitch;
        }
    }

    TEST_ASSERT_TRUE(lowestPitch >= -75.0f - 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -75.0f, sithModernControlsTest_PlayerPitch());

    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithModernControls, UpdateSwimNeutralLeavesRealKeys)
{
    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_SWIMIDLE;

    // With no movement input the real JUMP and RUN keys reach the original swim code unchanged.
    sithModernControlsTest_ReadKey(STDKEY_X, 1);
    sithModernControlsTest_ReadKey(STDKEY_LSHIFT, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_JUMP, NULL));
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_RUNFWD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernControlsTest_PlayerPitch());

    sithModernControlsTest_ReadKey(STDKEY_X, 0);
    sithModernControlsTest_ReadKey(STDKEY_LSHIFT, 0);
}

TEST(sithModernControls, UpdateSurfaceSwimKeepsJumpForTheLedgeGrab)
{
    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_SWIMIDLE;
    sithModernControlsTest_player.moveInfo.physics.flags = SITH_PF_ONWATERSURFACE;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_JUMP, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));

    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithModernControls, UpdateSwimLeavesClassicUnchanged)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(false);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_SWIMIDLE;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.05f);

    // Classic keeps the real forward key and never presses JUMP or turns Indy itself.
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_JUMP, NULL));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernControlsTest_PlayerPitch());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sithModernControlsTest_PlayerYaw());

    sithModernControlsTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithModernControls, UpdateStartSnapsFacingAtOnce)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);

    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));

    // From standing the 180-degree change snaps in this one update.
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 180.0f, fabsf(sithModernControlsTest_PlayerYaw()));

    sithModernControlsTest_ReadKey(STDKEY_DOWN, 0);
}

TEST(sithModernControls, UpdateRotatesSmallChangesAt1080)
{
    size_t axisX = STDCONTROL_GET_JOYSTICK_AXIS_X(0);
    size_t axisY = STDCONTROL_GET_JOYSTICK_AXIS_Y(0);

    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_RUNNING;

    stdControl_RegisterAxis(axisX, -32768, 32767, 0.0f);
    stdControl_RegisterAxis(axisY, -32768, 32767, 0.0f);
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisX));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axisY));

    // A full stick 60 degrees to the left. Y is down positive, so forward is -Y.
    stdControl_SetAxisState(axisX, (int)(-0.8660254f * 32767.0f));
    stdControl_SetAxisState(axisY, (int)(-0.5f * 32767.0f));

    sithModernControls_Update(&sithModernControlsTest_player, 0.05f);

    // 0.05 s at 1080 deg/s covers 54 of the 60 degrees; standing alone snaps.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 54.0f, sithModernControlsTest_PlayerYaw());

    stdControl_SetAxisState(axisX, 0);
    stdControl_SetAxisState(axisY, 0);
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

TEST(sithModernControls, UpdateQuickStepModifierTakesOneStepPerPress)
{
    int pressed = -1;

    sithControl_SetModernControlStyle(true);

    // A floor-less test world keeps the original step action from touching real sector data;
    // the quick-step key edge is what this case observes.
    static SithWorld testWorld;
    memset(&testWorld, 0, sizeof(testWorld));
    SithSector testSector;
    memset(&testSector, 0, sizeof(testSector));
    testSector.flags = SITH_SECTOR_HASCOLLIDEBOX;
    testSector.collideBox.v0 = (rdVector3){ -100.0f, -100.0f, -100.0f };
    testSector.collideBox.v1 = (rdVector3){ 100.0f, 100.0f, 100.0f };
    sithWorld_g_pCurrentWorld = &testWorld;
    sithModernControlsTest_player.pInSector = &testSector;

    // Left Alt plus a direction is the Modern quick-step. A left direction resolves to Indy's
    // left and reaches the original sidestep key once.
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 1);
    sithModernControlsTest_ReadKey(STDKEY_LEFT, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_RUNFWD, NULL));

    // The held modifier and direction do not start another step.
    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    // Returning the direction below the re-arm magnitude allows a new step to the other side.
    sithModernControlsTest_ReadKey(STDKEY_LEFT, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    sithModernControlsTest_ReadKey(STDKEY_RIGHT, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPRIGHT, &pressed));
    TEST_ASSERT_TRUE(pressed != 0);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_RIGHT, 0);
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    sithModernControlsTest_player.pInSector = NULL;
    sithWorld_g_pCurrentWorld = NULL;
}

TEST(sithModernControls, UpdateQuickStepModifierSuppressesMovementWithoutADirection)
{
    sithControl_SetModernControlStyle(true);

    // A held modifier alone walks or runs nowhere.
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_RUNFWD, NULL));

    sithModernControlsTest_ReadKey(STDKEY_LMENU, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
}

TEST(sithModernControls, UpdateLockedBackStartsContinuousBackpedal)
{
    sithControl_SetModernControlStyle(true);

    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());

    // A held backward stick while locked starts the continuous backpedal, not the back hop.
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_TRUE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, sithModernControlsTest_player.moveStatus);
    TEST_ASSERT_EQUAL_INT(SITH_ATTACH_SURFACE, sithModernControlsTest_player.attach.flags);
    TEST_ASSERT_TRUE(rdVector_IsZero3(&sithModernControlsTest_player.moveInfo.physics.velocity));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));

    // Releasing the stick stops it at once and returns to standing.
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_FALSE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, sithModernControlsTest_player.moveStatus);

    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
}

TEST(sithModernControls, UpdateQuickStepBackStartsTheTimedStep)
{
    sithControl_SetModernControlStyle(true);

    SithSurface surface;
    memset(&surface, 0, sizeof(surface));
    sithModernControlsTest_player.attach.attachedToStructure.pSurfaceAttached = &surface;

    // Down plus the modifier resolves to a backward step that never leaves the ground.
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 1);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);

    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_FALSE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, sithModernControlsTest_player.moveStatus);
    TEST_ASSERT_EQUAL_INT(SITH_ATTACH_SURFACE, sithModernControlsTest_player.attach.flags);
    TEST_ASSERT_TRUE(rdVector_IsZero3(&sithModernControlsTest_player.moveInfo.physics.velocity));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));

    // The held modifier and direction cannot start another step until this one finishes.
    sithControl_ReadControls();
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());

    sithPlayerActions_StopTimedStep(&sithModernControlsTest_player);
    sithModernControlsTest_ReadKey(STDKEY_DOWN, 0);
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 0);
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

TEST(sithModernControls, ModernMoveTransitionSkipOnlyForCameraRelativeStates)
{
    sithControl_SetModernControlStyle(true);

    sithModernControlsTest_player.moveStatus  = SITHPLAYERMOVE_STILL;
    sithModernControlsTest_player.attach.flags = SITH_ATTACH_SURFACE;
    TEST_ASSERT_TRUE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));

    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_WALKING;
    TEST_ASSERT_TRUE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));

    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_RUNNING;
    TEST_ASSERT_TRUE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));

    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_JUMPFWD;
    TEST_ASSERT_FALSE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));

    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_PUSHING;
    TEST_ASSERT_FALSE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));

    // Classic always goes through the original transitions.
    sithControl_SetModernControlStyle(false);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    TEST_ASSERT_FALSE(sithModernControls_IsCameraRelative(&sithModernControlsTest_player));
}

TEST(sithModernControls, StartModernMoveSkipsTransitionStatus)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    memset(&sithModernControlsTest_puppetState, 0, sizeof(sithModernControlsTest_puppetState));

    thing.type         = SITH_THING_PLAYER;
    thing.moveType     = SITH_MT_PHYSICS;
    thing.moveStatus   = SITHPLAYERMOVE_STILL;
    thing.pPuppetState = &sithModernControlsTest_puppetState;
    thing.thingInfo.actorInfo.maxThrust = 1.0f;

    TEST_ASSERT_TRUE(sithPlayerControls_StartModernMove(&thing, true, 2.5f));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_RUNNING, thing.moveStatus);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bControlsDisabled);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bForceMovePlay);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.25f, thing.moveInfo.physics.thrust.y);

    TEST_ASSERT_TRUE(sithPlayerControls_StartModernMove(&thing, false, 1.0f));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, thing.moveStatus);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bControlsDisabled);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bForceMovePlay);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, thing.moveInfo.physics.thrust.y);
}

TEST(sithModernControls, StopModernMoveSkipsTransitionStatus)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    memset(&sithModernControlsTest_puppetState, 0, sizeof(sithModernControlsTest_puppetState));

    thing.type         = SITH_THING_PLAYER;
    thing.moveType     = SITH_MT_PHYSICS;
    thing.moveStatus   = SITHPLAYERMOVE_RUNNING;
    thing.pPuppetState = &sithModernControlsTest_puppetState;
    thing.thingInfo.actorInfo.bControlsDisabled = 1;
    thing.moveInfo.physics.velocity = (rdVector3){ 1.0f, 2.0f, 3.0f };

    sithPlayerControls_StopModernMove(&thing);

    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, thing.moveStatus);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bControlsDisabled);
    TEST_ASSERT_EQUAL_INT(0, thing.thingInfo.actorInfo.bForceMovePlay);
    TEST_ASSERT_TRUE(rdVector_IsZero3(&thing.moveInfo.physics.velocity));
}

TEST(sithModernControls, LockedSidestepPlaysAtDoubleSpeed)
{
    sithControl_SetModernControlStyle(true);
    sithModernControls_ResetLock();

    // Unlocked quick-step and any non-local thing keep the original rate.
    TEST_ASSERT_EQUAL_FLOAT(1.0f,
        sithPlayerActions_GetSidestepPlaybackSpeed(&sithModernControlsTest_player));

    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());
    TEST_ASSERT_EQUAL_FLOAT(2.0f,
        sithPlayerActions_GetSidestepPlaybackSpeed(&sithModernControlsTest_player));

    SithThing other = sithModernControlsTest_player;
    other.type = SITH_THING_ACTOR;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, sithPlayerActions_GetSidestepPlaybackSpeed(&other));

    // Releasing the lock restores the original step speed.
    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_FALSE(sithModernControls_IsLockActive());
    TEST_ASSERT_EQUAL_FLOAT(1.0f,
        sithPlayerActions_GetSidestepPlaybackSpeed(&sithModernControlsTest_player));

    // Classic never speeds the step up.
    sithControl_SetModernControlStyle(false);
    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.01f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f,
        sithPlayerActions_GetSidestepPlaybackSpeed(&sithModernControlsTest_player));
    sithModernControlsTest_ReadKey(STDKEY_F, 0);
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
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM,
        sithModernControls_GetMode(&sithModernControlsTest_player));

    sithModernControlsTest_sector.flags = SITH_SECTOR_AETHERIUM;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM,
        sithModernControls_GetMode(&sithModernControlsTest_player));

    // Airborne and not swimming keeps the real keys.
    sithModernControlsTest_sector.flags = 0;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_NONE,
        sithModernControls_GetMode(&sithModernControlsTest_player));
}

TEST(sithModernControls, ClassifierKeepsSurfaceSwimmingCameraRelative)
{
    sithModernControlsTest_player.moveStatus  = SITHPLAYERMOVE_SWIMIDLE;
    sithModernControlsTest_player.attach.flags = 0;
    sithModernControlsTest_player.pInSector    = &sithModernControlsTest_sector;
    sithModernControlsTest_player.moveInfo.physics.flags = SITH_PF_ONWATERSURFACE;

    sithModernControlsTest_sector.flags = SITH_SECTOR_UNDERWATER;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE,
        sithModernControls_GetMode(&sithModernControlsTest_player));

    // Below the surface the same state becomes 3D camera-relative.
    sithModernControlsTest_player.moveInfo.physics.flags = 0;
    TEST_ASSERT_EQUAL_INT(SITHMODERNCONTROL_MODE_CAMERA_RELATIVE_SWIM,
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
    // A clear backward stick walks back instead of hopping.
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

    // A small diagonal that is not sideways still walks back.
    out = sithModernControlsTest_ComputeLock(0.2f, -0.4f, 0.0f, 0.0f, true, 0.0f);
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

TEST(sithModernControls, UpdateTurnsHorizontalMomentumWithTheFacing)
{
    sithControl_SetModernControlStyle(true);
    sithModernControlsTest_player.moveStatus = SITHPLAYERMOVE_WALKING;
    sithModernControlsTest_player.orient.lvec = (rdVector3){ 1.0f, 0.0f, 0.0f };
    sithModernControlsTest_player.orient.rvec = (rdVector3){ 0.0f, -1.0f, 0.0f };
    sithModernControlsTest_player.orient.uvec = (rdVector3){ 0.0f, 0.0f, 1.0f };
    sithModernControlsTest_player.moveInfo.physics.velocity = (rdVector3){ 1.0f, 0.0f, 3.0f };

    // The camera faces +Y, so a forward press turns the +X facing to +Y over this update.
    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(&sithModernControlsTest_player, 0.1f);
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);

    const rdVector3* pVelocity = &sithModernControlsTest_player.moveInfo.physics.velocity;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pVelocity->x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pVelocity->y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, pVelocity->z);
}

TEST(sithModernControls, ApplyGroundAccelerationUsesTheOriginalSurfaceTarget)
{
    sithControl_SetModernControlStyle(true);

    SithThing* pPlayer = &sithModernControlsTest_player;
    pPlayer->moveStatus = SITHPLAYERMOVE_WALKING;
    pPlayer->moveInfo.physics.thrust.y = 0.9f;
    pPlayer->moveInfo.physics.velocity = (rdVector3){ 0 };

    // Attached surface drag without crouch: 0.9 / 2.
    pPlayer->attach.flags = SITH_ATTACH_SURFACE;
    pPlayer->moveInfo.physics.surfDrag = 2.0f;
    sithModernControls_ApplyGroundAcceleration(pPlayer, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, pPlayer->moveInfo.physics.velocity.y);

    // Crawling uses the original crouch thrust scale: 0.9 * 0.8 / 2.
    pPlayer->moveInfo.physics.flags |= SITH_PF_CROUCHING;
    pPlayer->moveInfo.physics.velocity = (rdVector3){ 0 };
    sithModernControls_ApplyGroundAcceleration(pPlayer, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.36f, pPlayer->moveInfo.physics.velocity.y);

    // A detached thing uses the player physics' air drag and ground thrust scale.
    pPlayer->attach.flags = 0;
    pPlayer->moveInfo.physics.flags &= ~SITH_PF_CROUCHING;
    pPlayer->moveInfo.physics.airDrag = 5.0f;
    pPlayer->moveInfo.physics.velocity = (rdVector3){ 0 };
    sithModernControls_ApplyGroundAcceleration(pPlayer, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.054f, pPlayer->moveInfo.physics.velocity.y);
}

TEST(sithModernControls, MomentumAndAccelerationLeaveClassicAndWaterAndAirAlone)
{
    SithThing* pPlayer = &sithModernControlsTest_player;
    pPlayer->orient.lvec = (rdVector3){ 1.0f, 0.0f, 0.0f };
    pPlayer->orient.rvec = (rdVector3){ 0.0f, -1.0f, 0.0f };
    pPlayer->orient.uvec = (rdVector3){ 0.0f, 0.0f, 1.0f };
    pPlayer->moveInfo.physics.airDrag = 5.0f;
    pPlayer->moveInfo.physics.thrust.y = 0.9f;

    const rdVector3 velocity = { 1.0f, 0.0f, 3.0f };

    // Classic never runs the Modern facing or acceleration code.
    sithControl_SetModernControlStyle(false);
    pPlayer->moveStatus = SITHPLAYERMOVE_WALKING;
    pPlayer->moveInfo.physics.velocity = velocity;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(pPlayer, 0.1f);
    sithModernControls_ApplyGroundAcceleration(pPlayer, 0.02f);
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);

    TEST_ASSERT_EQUAL_FLOAT(velocity.x, pPlayer->moveInfo.physics.velocity.x);
    TEST_ASSERT_EQUAL_FLOAT(velocity.y, pPlayer->moveInfo.physics.velocity.y);
    TEST_ASSERT_EQUAL_FLOAT(velocity.z, pPlayer->moveInfo.physics.velocity.z);

    // Swimming turns the camera-relative facing but keeps the water momentum.
    sithControl_SetModernControlStyle(true);
    pPlayer->moveStatus = SITHPLAYERMOVE_SWIMIDLE;
    pPlayer->moveInfo.physics.velocity = velocity;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(pPlayer, 0.1f);
    sithModernControls_ApplyGroundAcceleration(pPlayer, 0.02f);
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);

    TEST_ASSERT_EQUAL_FLOAT(velocity.x, pPlayer->moveInfo.physics.velocity.x);
    TEST_ASSERT_EQUAL_FLOAT(velocity.y, pPlayer->moveInfo.physics.velocity.y);
    TEST_ASSERT_EQUAL_FLOAT(velocity.z, pPlayer->moveInfo.physics.velocity.z);

    // Airborne states keep the real keys and their momentum.
    pPlayer->moveStatus = SITHPLAYERMOVE_FALLING;
    pPlayer->moveInfo.physics.velocity = velocity;

    sithModernControlsTest_ReadKey(STDKEY_UP, 1);
    sithModernControls_Update(pPlayer, 0.1f);
    sithModernControls_ApplyGroundAcceleration(pPlayer, 0.02f);
    sithModernControlsTest_ReadKey(STDKEY_UP, 0);

    TEST_ASSERT_EQUAL_FLOAT(velocity.x, pPlayer->moveInfo.physics.velocity.x);
    TEST_ASSERT_EQUAL_FLOAT(velocity.y, pPlayer->moveInfo.physics.velocity.y);
    TEST_ASSERT_EQUAL_FLOAT(velocity.z, pPlayer->moveInfo.physics.velocity.z);
}
#endif

// Added: a small flat sector for the timed-step travel and ledge cases. The player sits at the
// origin, and the floor's +Y edge can be moved to model a ledge.
#define SITHMODERNCONTROLS_TEST_STEP_EDGE 0.06f

static SithWorld sithModernControlsTest_stepWorld;
static SithThing sithModernControlsTest_stepThings[1];
static SithSector sithModernControlsTest_stepSector;
static SithSurface sithModernControlsTest_stepFloor;
static rdVector3 sithModernControlsTest_stepVertices[4];
static int sithModernControlsTest_stepFaceVerts[4] = { 0, 1, 2, 3 };
static rdPuppet sithModernControlsTest_stepPuppet;
static rdThing sithModernControlsTest_stepRdThing;
static rdModel3 sithModernControlsTest_stepModel;
static SithPlayer sithModernControlsTest_stepPlayer;
static SithPuppetState sithModernControlsTest_stepPuppetState;
static SithPuppetClass sithModernControlsTest_stepPuppetClass;
static rdKeyframe sithModernControlsTest_stepStrafeKf;
static rdKeyframe sithModernControlsTest_stepWalkKf;
static rdKeyframe sithModernControlsTest_stepWalkBackKf;
static rdKeyframe sithModernControlsTest_stepStandKf;
static rdKeyframe sithModernControlsTest_stepHopBackKf;
static bool sithModernControlsTest_bStepWorldInit;

static void sithModernControlsTest_CleanupStepWorld(void)
{
    if ( !sithModernControlsTest_bStepWorldInit )
    {
        return;
    }

    SithThing* pThing = &sithModernControlsTest_stepThings[0];
    if ( pThing->pPuppetState && pThing->renderData.pPuppet )
    {
        sithPuppet_RemoveAllTracks(pThing);
    }

    sithPlayerActions_CancelTimedStep();
    sithPlayer_g_pLocalPlayerThing = NULL;
    sithWorld_g_pCurrentWorld      = NULL;
    sithModernControlsTest_bStepWorldInit = false;
}

static SithThing* sithModernControlsTest_InitStepWorld(float floorEdgeY)
{
    if ( !sithModernControlsTest_bStepWorldInit )
    {
        sithCollision_Startup();
    }

    memset(&sithModernControlsTest_stepWorld, 0, sizeof(sithModernControlsTest_stepWorld));
    memset(&sithModernControlsTest_stepThings, 0, sizeof(sithModernControlsTest_stepThings));
    memset(&sithModernControlsTest_stepSector, 0, sizeof(sithModernControlsTest_stepSector));
    memset(&sithModernControlsTest_stepFloor, 0, sizeof(sithModernControlsTest_stepFloor));
    memset(&sithModernControlsTest_stepPuppet, 0, sizeof(sithModernControlsTest_stepPuppet));
    memset(&sithModernControlsTest_stepRdThing, 0, sizeof(sithModernControlsTest_stepRdThing));
    memset(&sithModernControlsTest_stepModel, 0, sizeof(sithModernControlsTest_stepModel));
    memset(&sithModernControlsTest_stepPlayer, 0, sizeof(sithModernControlsTest_stepPlayer));
    memset(&sithModernControlsTest_stepPuppetState, 0, sizeof(sithModernControlsTest_stepPuppetState));
    memset(&sithModernControlsTest_stepPuppetClass, 0, sizeof(sithModernControlsTest_stepPuppetClass));
    memset(&sithModernControlsTest_stepStrafeKf, 0, sizeof(sithModernControlsTest_stepStrafeKf));
    memset(&sithModernControlsTest_stepWalkKf, 0, sizeof(sithModernControlsTest_stepWalkKf));
    memset(&sithModernControlsTest_stepWalkBackKf, 0, sizeof(sithModernControlsTest_stepWalkBackKf));
    memset(&sithModernControlsTest_stepStandKf, 0, sizeof(sithModernControlsTest_stepStandKf));
    memset(&sithModernControlsTest_stepHopBackKf, 0, sizeof(sithModernControlsTest_stepHopBackKf));

    sithModernControlsTest_stepVertices[0] = (rdVector3){ -1.0f, -1.0f, 0.0f };
    sithModernControlsTest_stepVertices[1] = (rdVector3){ 1.0f, -1.0f, 0.0f };
    sithModernControlsTest_stepVertices[2] = (rdVector3){ 1.0f, floorEdgeY, 0.0f };
    sithModernControlsTest_stepVertices[3] = (rdVector3){ -1.0f, floorEdgeY, 0.0f };

    sithModernControlsTest_stepFloor.pSector       = &sithModernControlsTest_stepSector;
    sithModernControlsTest_stepFloor.flags         = SITH_SURFACE_ISFLOOR | SITH_SURFACE_COLLISION;
    sithModernControlsTest_stepFloor.face.numVertices = 4;
    sithModernControlsTest_stepFloor.face.aVertices   = sithModernControlsTest_stepFaceVerts;
    sithModernControlsTest_stepFloor.face.normal      = (rdVector3){ 0.0f, 0.0f, 1.0f };

    // Added: no collide box, so the surface search always checks the floor face (a collide box
    // makes the search skip surfaces for points inside it).
    sithModernControlsTest_stepSector.flags         = 0;
    sithModernControlsTest_stepSector.collideBox.v0 = (rdVector3){ -10.0f, -10.0f, -1.0f };
    sithModernControlsTest_stepSector.collideBox.v1 = (rdVector3){ 10.0f, 10.0f, 1.0f };
    sithModernControlsTest_stepSector.numSurfaces   = 1;
    sithModernControlsTest_stepSector.pFirstSurface = &sithModernControlsTest_stepFloor;
    sithModernControlsTest_stepSector.radius        = 10.0f;

    sithModernControlsTest_stepWorld.numVertices = 4;
    sithModernControlsTest_stepWorld.aVertices   = sithModernControlsTest_stepVertices;
    sithModernControlsTest_stepWorld.numSectors  = 1;
    sithModernControlsTest_stepWorld.aSectors    = &sithModernControlsTest_stepSector;
    sithModernControlsTest_stepWorld.numSurfaces = 1;
    sithModernControlsTest_stepWorld.aSurfaces   = &sithModernControlsTest_stepFloor;
    sithModernControlsTest_stepWorld.numThings   = 1;
    sithModernControlsTest_stepWorld.aThings     = sithModernControlsTest_stepThings;
    sithModernControlsTest_stepWorld.gravity     = 0.5f;

    SithThing* pThing = &sithModernControlsTest_stepThings[0];
    pThing->type         = SITH_THING_PLAYER;
    pThing->moveType     = SITH_MT_PHYSICS;
    pThing->moveStatus   = SITHPLAYERMOVE_STILL;
    pThing->idx          = 0;
    pThing->pInSector    = &sithModernControlsTest_stepSector;
    pThing->pos          = (rdVector3){ 0.0f, 0.0f, 0.09f };
    pThing->collide.movesize = 0.04f;
    pThing->moveInfo.physics.height = 0.09f;
    pThing->moveInfo.physics.flags  = SITH_PF_USETHRUST | SITH_PF_ALIGNUP;
    pThing->moveInfo.physics.surfDrag = 2.0f;
    pThing->thingInfo.actorInfo.maxThrust = 1.0f;
    sithModernControlsTest_stepPlayer.curWeaponID = SITHWEAPON_NO_WEAPON;
    pThing->thingInfo.actorInfo.pPlayer = &sithModernControlsTest_stepPlayer;
    pThing->orient.lvec = (rdVector3){ 0.0f, 1.0f, 0.0f };
    pThing->orient.rvec = (rdVector3){ 1.0f, 0.0f, 0.0f };
    pThing->orient.uvec = (rdVector3){ 0.0f, 0.0f, 1.0f };
    pThing->attach.flags = SITH_ATTACH_SURFACE;
    pThing->attach.pFace = &sithModernControlsTest_stepFloor.face;
    pThing->attach.attachedToStructure.pSurfaceAttached = &sithModernControlsTest_stepFloor;

    // Added: 10 frames at 25 fps is a 0.4 s sidestep in this fixture.
    sithModernControlsTest_stepStrafeKf.numFrames = 10;
    sithModernControlsTest_stepStrafeKf.fps       = 25.0f;
    sithModernControlsTest_stepWalkKf.numFrames   = 20;
    sithModernControlsTest_stepWalkKf.fps         = 25.0f;
    sithModernControlsTest_stepWalkKf.flags       = RDKEYFRAME_PUPPET_CONTROLLED | RDKEYFRAME_DISABLE_FADEIN;
    sithModernControlsTest_stepWalkBackKf.numFrames = 20;
    sithModernControlsTest_stepWalkBackKf.fps       = 25.0f;
    sithModernControlsTest_stepWalkBackKf.flags     = RDKEYFRAME_PUPPET_CONTROLLED | RDKEYFRAME_DISABLE_FADEIN;
    sithModernControlsTest_stepStandKf.numFrames  = 20;
    sithModernControlsTest_stepStandKf.fps        = 25.0f;
    sithModernControlsTest_stepHopBackKf.numFrames = 20;
    sithModernControlsTest_stepHopBackKf.fps       = 25.0f;

    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_STRAFELEFT].pKeyframe = &sithModernControlsTest_stepStrafeKf;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_WALK].pKeyframe        = &sithModernControlsTest_stepWalkKf;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_WALK].flags            = sithModernControlsTest_stepWalkKf.flags;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_WALKBACK].pKeyframe    = &sithModernControlsTest_stepWalkBackKf;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_WALKBACK].flags        = sithModernControlsTest_stepWalkBackKf.flags;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_STAND].pKeyframe       = &sithModernControlsTest_stepStandKf;
    sithModernControlsTest_stepPuppetClass.aModes[0][SITHPUPPETSUBMODE_HOPBACK].pKeyframe     = &sithModernControlsTest_stepHopBackKf;

    sithModernControlsTest_stepRdThing.data.pModel3 = &sithModernControlsTest_stepModel;
    sithModernControlsTest_stepRdThing.pThing       = pThing;
    sithModernControlsTest_stepPuppet.pThing        = &sithModernControlsTest_stepRdThing;

    pThing->pPuppetClass     = &sithModernControlsTest_stepPuppetClass;
    pThing->pPuppetState     = &sithModernControlsTest_stepPuppetState;
    pThing->renderData.pPuppet = &sithModernControlsTest_stepPuppet;

    sithWorld_g_pCurrentWorld      = &sithModernControlsTest_stepWorld;
    sithPlayer_g_pLocalPlayerThing = pThing;
    sithControl_SetModernControlStyle(true);
    sithModernControls_ResetLock();
    sithPlayerActions_CancelTimedStep();
    sithModernControlsTest_bStepWorldInit = true;

    return pThing;
}

// Added: drive the step and move the thing exactly as the physics step would after it. The
// collision move keeps the walking ledge checks and any world collision.
static int sithModernControlsTest_AdvanceStep(SithThing* pThing, float secDeltaTime, int maxFrames)
{
    int frames = 0;
    while ( sithPlayerActions_IsTimedStepActive() && frames < maxFrames )
    {
        sithPlayerActions_UpdateTimedStep(pThing, secDeltaTime);
        if ( !sithPlayerActions_IsTimedStepActive() )
        {
            break;
        }

        rdVector3 moveNorm = pThing->moveInfo.physics.velocity;
        float moveDist     = rdVector_Len3(&moveNorm) * secDeltaTime;
        if ( moveDist > 0.0f )
        {
            rdVector_Normalize3Acc(&moveNorm);
            sithCollision_MoveThing(pThing, &moveNorm, moveDist, 0);
        }

        ++frames;
    }

    return frames;
}

#ifdef J3D_QOL_IMPROVEMENTS
TEST(sithModernControls, TimedStepTimeAndSpeedComeFromTheStrafeAnimation)
{
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    // The fixture's StrafeLeft animation is 10 frames at 25 fps.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, sithPlayerActions_GetSidestepTime(pThing));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, sithPlayerActions_GetTimedStepSpeed(pThing));

    // Under the lock the animation, and therefore the step, runs at twice the rate.
    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(pThing, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, sithPlayerActions_GetTimedStepSpeed(pThing));

    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(pThing, 0.01f);
    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, TimedStepForwardTravelsTheSidestepDistanceInSeveralFrames)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    sithPlayerActions_StartTimedStep(pThing, /*bBackward=*/false);

    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, pThing->moveStatus);
    TEST_ASSERT_TRUE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_WALK));

    int frames = sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, 200);

    TEST_ASSERT_GREATER_THAN_INT(1, frames);
    TEST_ASSERT_FLOAT_WITHIN(secDeltaTime, 0.4f, (float)frames * secDeltaTime);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.1f, pThing->pos.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pThing->pos.x);
    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, pThing->moveStatus);
    TEST_ASSERT_FALSE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_WALK));

    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, TimedStepBackTravelsTheSidestepDistanceInSeveralFrames)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    sithPlayerActions_StartTimedStep(pThing, /*bBackward=*/true);

    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_TRUE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_WALKBACK));
    TEST_ASSERT_FALSE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_HOPBACK));

    int frames = sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, 200);

    TEST_ASSERT_GREATER_THAN_INT(1, frames);
    TEST_ASSERT_FLOAT_WITHIN(secDeltaTime, 0.4f, (float)frames * secDeltaTime);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, -0.1f, pThing->pos.y);
    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, pThing->moveStatus);

    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, TimedStepStopsAtALedge)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(SITHMODERNCONTROLS_TEST_STEP_EDGE);

    sithPlayerActions_StartTimedStep(pThing, /*bBackward=*/false);
    sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, 200);

    // The walking ledge check stops Indy before the floor ends, so he never takes the full step.
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pThing->pos.y);
    TEST_ASSERT_LESS_THAN_FLOAT(SITHMODERNCONTROLS_TEST_STEP_EDGE, pThing->pos.y);
    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, pThing->moveStatus);

    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, TimedStepBlocksOtherMoves)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    sithPlayerActions_StartTimedStep(pThing, /*bBackward=*/false);
    sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, 2);
    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());

    // A step key during the step must not start another move; the step's state is untouched.
    sithModernControlsTest_ReadKey(STDKEY_Q, 1);
    sithModernControls_Update(pThing, secDeltaTime);
    TEST_ASSERT_TRUE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_WALKING, pThing->moveStatus);
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_Q, 0);
    sithPlayerActions_StopTimedStep(pThing);
    sithModernControlsTest_CleanupStepWorld();
}

// A save calls StopTimedStep(NULL) before writing things: it must end any running step and give
// the controls back, or the saved player would load with its controls disabled.
TEST(sithModernControls, StopTimedStepWithoutThingEndsTheRunningStep)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    sithPlayerActions_StartBackpedal(pThing);
    sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, 2);
    TEST_ASSERT_TRUE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(1, pThing->thingInfo.actorInfo.bControlsDisabled);

    sithPlayerActions_StopTimedStep(NULL);

    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(0, pThing->thingInfo.actorInfo.bControlsDisabled);
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, pThing->moveStatus);

    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, LockedBackpedalUsesTheLockedSidestepSpeed)
{
    const float secDeltaTime = 1.0f / 30.0f;
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    // Hold the lock, then start the continuous backpedal the locked stick would start.
    sithModernControlsTest_ReadKey(STDKEY_F, 1);
    sithModernControls_Update(pThing, 0.01f);
    TEST_ASSERT_TRUE(sithModernControls_IsLockActive());

    sithPlayerActions_StartBackpedal(pThing);
    TEST_ASSERT_TRUE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_TRUE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_WALKBACK));
    TEST_ASSERT_FALSE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_HOPBACK));

    float expectedSpeed = sithPlayerActions_GetTimedStepSpeed(pThing);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, expectedSpeed);

    const int numFrames = 10;
    int frames = sithModernControlsTest_AdvanceStep(pThing, secDeltaTime, numFrames);
    TEST_ASSERT_EQUAL_INT(numFrames, frames);

    float measuredSpeed = fabsf(pThing->pos.y) / ((float)numFrames * secDeltaTime);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedSpeed, measuredSpeed);
    TEST_ASSERT_TRUE(sithPlayerActions_IsBackpedalActive());

    // Releasing the lock ends the continuous walk at once.
    sithModernControlsTest_ReadKey(STDKEY_F, 0);
    sithModernControls_Update(pThing, secDeltaTime);
    TEST_ASSERT_FALSE(sithPlayerActions_IsBackpedalActive());
    TEST_ASSERT_EQUAL_INT(SITHPLAYERMOVE_STILL, pThing->moveStatus);
    TEST_ASSERT_FALSE(sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_WALKBACK));

    sithModernControlsTest_CleanupStepWorld();
}

TEST(sithModernControls, QuickStepLeftAndRightStayOriginalSidesteps)
{
    SithThing* pThing = sithModernControlsTest_InitStepWorld(10.0f);

    sithModernControlsTest_ReadKey(STDKEY_LMENU, 1);
    sithModernControlsTest_ReadKey(STDKEY_LEFT, 1);
    sithModernControls_Update(pThing, 0.01f);

    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));

    // The original sidestep is a force move; end it before checking the other side.
    pThing->thingInfo.actorInfo.bForceMovePlay = 0;
    sithPuppet_RemoveAllTracks(pThing);
    pThing->moveStatus = SITHPLAYERMOVE_STILL;

    sithModernControlsTest_ReadKey(STDKEY_LEFT, 0);
    sithModernControls_Update(pThing, 0.01f); // re-arm the press edge

    sithModernControlsTest_ReadKey(STDKEY_RIGHT, 1);
    sithModernControls_Update(pThing, 0.01f);
    TEST_ASSERT_FALSE(sithPlayerActions_IsTimedStepActive());
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_STPRIGHT, NULL));

    sithModernControlsTest_ReadKey(STDKEY_RIGHT, 0);
    sithModernControlsTest_ReadKey(STDKEY_LMENU, 0);
    sithModernControls_Update(pThing, 0.01f);
    sithModernControlsTest_CleanupStepWorld();
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
    RUN_TEST_CASE(sithModernControls, ComputeSwimUsesCameraPitchAndSwimForward);
    RUN_TEST_CASE(sithModernControls, ComputeSwimClampsPitchToTheSwimLimit);
    RUN_TEST_CASE(sithModernControls, ComputeSwimNeutralSetsNothing);
    RUN_TEST_CASE(sithModernControls, ComputeRelativeModeMapsAxesLikeClassic);
    RUN_TEST_CASE(sithModernControls, ComputeNoneSetsNothing);
    RUN_TEST_CASE(sithModernControls, ClassifierKeepsOriginalCameraStatesRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierLeavesJumpAndForcedStatesToRealKeys);
    RUN_TEST_CASE(sithModernControls, ClassifierMakesTransitionsAndUnknownsCameraRelative);
    RUN_TEST_CASE(sithModernControls, ClassifierUsesWaterAndFloorFallback);
    RUN_TEST_CASE(sithModernControls, ClassifierKeepsSurfaceSwimmingCameraRelative);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetPrefersTheCameraDirection);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetRejectsOutsideCone);
    RUN_TEST_CASE(sithModernControls, ComputeLockFacesTargetWithoutTurnKeys);
    RUN_TEST_CASE(sithModernControls, ComputeLockHoldsSidestepKeys);
    RUN_TEST_CASE(sithModernControls, ComputeLockedBackWalksBack);
    RUN_TEST_CASE(sithModernControls, ComputeLockWithoutTargetUsesIndyFacing);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetUsesTheCameraEyeForSight);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetRejectsHiddenFromCamera);
    RUN_TEST_CASE(sithModernControls, SelectLockTargetKeepsRangeFromIndy);
    RUN_TEST_CASE(sithModernControls, LockedSidestepDropsFinishedStepForChaining);
    RUN_TEST_CASE(sithModernControls, QuickStepBackStartsTheTimedStep);
#ifdef J3D_QOL_IMPROVEMENTS
    RUN_TEST_CASE(sithModernControls, UpdateAppliesOverridesWhenModern);
    RUN_TEST_CASE(sithModernControls, UpdateSwimUsesCameraPitchAndSwimForward);
    RUN_TEST_CASE(sithModernControls, UpdateSwimRespectsTheOriginalPitchClamp);
    RUN_TEST_CASE(sithModernControls, UpdateSwimNeutralLeavesRealKeys);
    RUN_TEST_CASE(sithModernControls, UpdateSurfaceSwimKeepsJumpForTheLedgeGrab);
    RUN_TEST_CASE(sithModernControls, UpdateSwimLeavesClassicUnchanged);
    RUN_TEST_CASE(sithModernControls, UpdateStartSnapsFacingAtOnce);
    RUN_TEST_CASE(sithModernControls, UpdateRotatesSmallChangesAt1080);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesRealKeysWhenClassic);
    RUN_TEST_CASE(sithModernControls, UpdateQuickStepCountsOnlyThePressEdge);
    RUN_TEST_CASE(sithModernControls, UpdateLeavesQuickStepHoldInClassic);
    RUN_TEST_CASE(sithModernControls, UpdateAppliesStickDeadzone);
    RUN_TEST_CASE(sithModernControls, UpdateLockedSidewaysHoldsSidestepKey);
    RUN_TEST_CASE(sithModernControls, UpdateQuickStepRuleIsOffWhileLocked);
    RUN_TEST_CASE(sithModernControls, ModernMoveTransitionSkipOnlyForCameraRelativeStates);
    RUN_TEST_CASE(sithModernControls, StartModernMoveSkipsTransitionStatus);
    RUN_TEST_CASE(sithModernControls, StopModernMoveSkipsTransitionStatus);
    RUN_TEST_CASE(sithModernControls, LockedSidestepPlaysAtDoubleSpeed);
    RUN_TEST_CASE(sithModernControls, UpdateSnapsCameraOnceWhenLockHasNoTarget);
    RUN_TEST_CASE(sithModernControls, UpdateIgnoresTargetLockWhenClassic);
    RUN_TEST_CASE(sithModernControls, UpdateTurnsHorizontalMomentumWithTheFacing);
    RUN_TEST_CASE(sithModernControls, ApplyGroundAccelerationUsesTheOriginalSurfaceTarget);
    RUN_TEST_CASE(sithModernControls, MomentumAndAccelerationLeaveClassicAndWaterAndAirAlone);
    RUN_TEST_CASE(sithModernControls, UpdateLockedBackStartsContinuousBackpedal);
    RUN_TEST_CASE(sithModernControls, UpdateQuickStepBackStartsTheTimedStep);
    RUN_TEST_CASE(sithModernControls, TimedStepTimeAndSpeedComeFromTheStrafeAnimation);
    RUN_TEST_CASE(sithModernControls, TimedStepForwardTravelsTheSidestepDistanceInSeveralFrames);
    RUN_TEST_CASE(sithModernControls, TimedStepBackTravelsTheSidestepDistanceInSeveralFrames);
    RUN_TEST_CASE(sithModernControls, TimedStepStopsAtALedge);
    RUN_TEST_CASE(sithModernControls, TimedStepBlocksOtherMoves);
    RUN_TEST_CASE(sithModernControls, StopTimedStepWithoutThingEndsTheRunningStep);
    RUN_TEST_CASE(sithModernControls, LockedBackpedalUsesTheLockedSidestepSpeed);
    RUN_TEST_CASE(sithModernControls, QuickStepLeftAndRightStayOriginalSidesteps);
#endif
}
