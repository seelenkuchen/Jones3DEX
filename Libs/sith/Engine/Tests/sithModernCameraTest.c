#include <unity_fixture.h>

#include <rdroid/Math/rdMatrix.h>
#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithModernCamera.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <std/General/stdMath.h>
#include <std/General/stdUtil.h>

static SithCamera sithModernCameraTest_camera;
static SithThing sithModernCameraTest_player;
static SithThing sithModernCameraTest_other;

static SithThing* sithModernCameraTest_pSavedPlayer;
static SithCamera* sithModernCameraTest_pSavedCamera;
static int sithModernCameraTest_savedCutscene;
static SithCameraState sithModernCameraTest_savedFlags;
static int sithModernCameraTest_savedLookMode;
static bool sithModernCameraTest_savedModern;

static void sithModernCameraTest_MakeActive(void)
{
    sithModernCameraTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    sithModernCameraTest_camera.type       = SITHCAMERA_EXTERNAL;
    sithModernCameraTest_camera.pPrimaryFocusThing = &sithModernCameraTest_player;
    sithPlayer_g_pLocalPlayerThing = &sithModernCameraTest_player;
    sithCamera_g_pCurCamera        = &sithModernCameraTest_camera;
    sithControl_SetModernControlStyle(true);
    sithPlayerControls_g_bCutsceneMode = 0;
    sithCamera_g_stateFlags            = (SithCameraState)0;
    sithCamera_g_bExtCameraLookMode    = 0;
}

TEST_GROUP(sithModernCamera);

TEST_SETUP(sithModernCamera)
{
    sithModernCameraTest_pSavedPlayer  = sithPlayer_g_pLocalPlayerThing;
    sithModernCameraTest_pSavedCamera  = sithCamera_g_pCurCamera;
    sithModernCameraTest_savedCutscene = sithPlayerControls_g_bCutsceneMode;
    sithModernCameraTest_savedFlags    = sithCamera_g_stateFlags;
    sithModernCameraTest_savedLookMode = sithCamera_g_bExtCameraLookMode;
    sithModernCameraTest_savedModern   = sithControl_IsModernControlStyle();

    STD_ZEROMEM(&sithModernCameraTest_camera, sizeof(sithModernCameraTest_camera));
    STD_ZEROMEM(&sithModernCameraTest_player, sizeof(sithModernCameraTest_player));
    STD_ZEROMEM(&sithModernCameraTest_other, sizeof(sithModernCameraTest_other));

    sithModernCameraTest_player.type = SITH_THING_PLAYER;
    sithModernCameraTest_other.type  = SITH_THING_ACTOR;

    sithModernCamera_ResetGlobals();
    sithModernCameraTest_MakeActive();
}

TEST_TEAR_DOWN(sithModernCamera)
{
    sithModernCamera_ResetGlobals();
    sithPlayer_g_pLocalPlayerThing     = sithModernCameraTest_pSavedPlayer;
    sithCamera_g_pCurCamera            = sithModernCameraTest_pSavedCamera;
    sithPlayerControls_g_bCutsceneMode = sithModernCameraTest_savedCutscene;
    sithCamera_g_stateFlags            = sithModernCameraTest_savedFlags;
    sithCamera_g_bExtCameraLookMode    = sithModernCameraTest_savedLookMode;
    sithControl_SetModernControlStyle(sithModernCameraTest_savedModern);
}

TEST(sithModernCamera, TestRadialDeadzoneRescalesStick)
{
    sithModernCamera_SetYawPitch(0.0f, 0.0f);

    sithModernCamera_Advance(SITHMODERNCAMERA_DEADZONE, 0.0f, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernCamera_GetYaw());

    sithModernCamera_Advance(0.6f, 0.0f, 0.0f, 0.0f, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 315.0f, sithModernCamera_GetYaw());
}

TEST(sithModernCamera, TestStickYawSpeed)
{
    sithModernCamera_SetYawPitch(0.0f, 0.0f);

    sithModernCamera_Advance(1.0f, 0.0f, 0.0f, 0.0f, 0.5f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 270.0f, sithModernCamera_GetYaw());
}

TEST(sithModernCamera, TestStickPitchSpeedAndClamp)
{
    sithModernCamera_SetYawPitch(0.0f, 0.0f);

    sithModernCamera_Advance(0.0f, -1.0f, 0.0f, 0.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, sithModernCamera_GetPitch());

    sithModernCamera_Advance(0.0f, -1.0f, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(SITHMODERNCAMERA_PITCHMAX, sithModernCamera_GetPitch());

    sithModernCamera_Advance(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(SITHMODERNCAMERA_PITCHMIN, sithModernCamera_GetPitch());
}

TEST(sithModernCamera, TestMouseYawAndPitch)
{
    sithModernCamera_SetYawPitch(0.0f, 0.0f);

    sithModernCamera_Advance(0.0f, 0.0f, -2.0f, 0.0f, 1.0f / 60.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 350.0f, sithModernCamera_GetYaw());

    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 2.0f, 1.0f / 60.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.0f, sithModernCamera_GetPitch());
}

TEST(sithModernCamera, TestSnapReachesTargetInTime)
{
    sithModernCamera_SetYawPitch(90.0f, 20.0f);
    sithModernCamera_SnapTo(0.0f);

    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, SITHMODERNCAMERA_SNAPSECONDS * 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, sithModernCamera_GetYaw());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, sithModernCamera_GetPitch());

    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, SITHMODERNCAMERA_SNAPSECONDS * 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernCamera_GetYaw());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernCamera_GetPitch());
}

TEST(sithModernCamera, TestSnapTakesShortestYawPath)
{
    sithModernCamera_SetYawPitch(350.0f, 10.0f);
    sithModernCamera_SnapTo(10.0f);

    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, SITHMODERNCAMERA_SNAPSECONDS * 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sithModernCamera_GetYaw());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, sithModernCamera_GetPitch());

    sithModernCamera_Advance(0.0f, 0.0f, 0.0f, 0.0f, SITHMODERNCAMERA_SNAPSECONDS * 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, sithModernCamera_GetYaw());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithModernCamera_GetPitch());
}

TEST(sithModernCamera, TestExternalOffsetOrbitsWithYawAndPitch)
{
    rdVector3 focus  = { 10.0f, 20.0f, 30.0f };
    rdVector3 offset = { 0.0f, -0.2f, 0.065f };
    rdVector3 eye;

    sithModernCamera_ComputeEyePosition(&eye, &focus, &offset, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, eye.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.8f, eye.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 30.065f, eye.z);

    sithModernCamera_ComputeEyePosition(&eye, &focus, &offset, 90.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.2f, eye.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 20.0f, eye.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 30.065f, eye.z);

    sithModernCamera_ComputeEyePosition(&eye, &focus, &offset, 0.0f, 45.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, eye.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.8126f, eye.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 29.9045f, eye.z);
}

TEST(sithModernCamera, TestActiveRequiresModernPlayerExternalAndFreeState)
{
#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));

    sithControl_SetModernControlStyle(false);
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithControl_SetModernControlStyle(true);

    sithPlayerControls_g_bCutsceneMode = 1;
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithPlayerControls_g_bCutsceneMode = 0;

    sithCamera_g_stateFlags = SITHCAMERA_STATE_CUTSCENE;
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithCamera_g_stateFlags = (SithCameraState)0;

    sithCamera_g_bExtCameraLookMode = 1;
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithCamera_g_bExtCameraLookMode = 0;

    sithModernCameraTest_camera.type = SITHCAMERA_INTERNAL;
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithModernCameraTest_camera.type = SITHCAMERA_EXTERNAL;

    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_other));

    sithModernCameraTest_player.moveStatus = SITHPLAYERMOVE_HANGING;
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    TEST_ASSERT_TRUE(sithModernCamera_IsPlayerExternalCamera(&sithModernCameraTest_camera, &sithModernCameraTest_player));
#else
    sithControl_SetModernControlStyle(true);
    TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
#endif
}

TEST(sithModernCamera, TestActiveAnchorsTransitionsAndNoneStates)
{
    static const SithThingMoveStatus aTransition[] =
    {
        SITHPLAYERMOVE_STAND2WALK,
        SITHPLAYERMOVE_TURNING_LEFT,
        SITHPLAYERMOVE_TURNING_RIGHT,
        SITHPLAYERMOVE_WALK2STAND,
        SITHPLAYERMOVE_STAND2RUN,
        SITHPLAYERMOVE_UNKNOWN_78,
        SITHPLAYERMOVE_UNKNOWN_80,
        SITHPLAYERMOVE_UNKNOWN_81,
        SITHPLAYERMOVE_UNKNOWN_82,
        SITHPLAYERMOVE_UNKNOWN_83,
        SITHPLAYERMOVE_UNKNOWN_84,
        SITHPLAYERMOVE_ACTIVATING,
        SITHPLAYERMOVE_STAND_TO_CRAWL,
        SITHPLAYERMOVE_CRAWL_TO_STAND,
        SITHPLAYERMOVE_ROLLING_LEFT,
        SITHPLAYERMOVE_ROLLING_RIGHT,
        SITHPLAYERMOVE_STRAFING_LEFT,
        SITHPLAYERMOVE_STRAFING_RIGHT,
    };

    static const SithThingMoveStatus aNone[] =
    {
        SITHPLAYERMOVE_JUMPFWD,
        SITHPLAYERMOVE_FALLING,
        SITHPLAYERMOVE_LAND,
        SITHPLAYERMOVE_KNOCKEDOUT,
    };

    static const SithThingMoveStatus aRelative[] =
    {
        SITHPLAYERMOVE_HANGING,
        SITHPLAYERMOVE_CLIMBING_UP,
        SITHPLAYERMOVE_SLIDEDOWNFORWARD,
        SITHPLAYERMOVE_JEEP_IDLE,
    };

    sithModernCameraTest_player.attach.flags = SITH_ATTACH_SURFACE;

    for ( size_t i = 0; i < STD_ARRAYLEN(aTransition); ++i )
    {
        sithModernCameraTest_player.moveStatus = aTransition[i];
        TEST_ASSERT_TRUE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    }

    // None states keep the free camera anchored too; only the explicit original-camera set
    // releases it back to the original camera.
    for ( size_t i = 0; i < STD_ARRAYLEN(aNone); ++i )
    {
        sithModernCameraTest_player.moveStatus = aNone[i];
        TEST_ASSERT_TRUE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(aRelative); ++i )
    {
        sithModernCameraTest_player.moveStatus = aRelative[i];
        TEST_ASSERT_FALSE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    }

    sithModernCameraTest_player.moveStatus = SITHPLAYERMOVE_STILL;
    sithModernCameraTest_player.thingInfo.actorInfo.bForceMovePlay = 1;
    TEST_ASSERT_TRUE(sithModernCamera_IsActive(&sithModernCameraTest_camera, &sithModernCameraTest_player));
    sithModernCameraTest_player.thingInfo.actorInfo.bForceMovePlay = 0;
}

TEST(sithModernCamera, TestNonFreeStateSyncsYaw)
{
    sithModernCamera_SetYawPitch(10.0f, 5.0f);

    rdVector3 pyr = { 0.0f, 90.0f, 0.0f };
    rdMatrix34 orient;
    rdMatrix_BuildRotate34(&orient, &pyr);

    sithModernCamera_SyncYawFromOrient(&orient);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 90.0f, sithModernCamera_GetYaw());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, sithModernCamera_GetPitch());
}

TEST_GROUP_RUNNER(sithModernCamera)
{
    RUN_TEST_CASE(sithModernCamera, TestRadialDeadzoneRescalesStick);
    RUN_TEST_CASE(sithModernCamera, TestStickYawSpeed);
    RUN_TEST_CASE(sithModernCamera, TestStickPitchSpeedAndClamp);
    RUN_TEST_CASE(sithModernCamera, TestMouseYawAndPitch);
    RUN_TEST_CASE(sithModernCamera, TestSnapReachesTargetInTime);
    RUN_TEST_CASE(sithModernCamera, TestSnapTakesShortestYawPath);
    RUN_TEST_CASE(sithModernCamera, TestExternalOffsetOrbitsWithYawAndPitch);
    RUN_TEST_CASE(sithModernCamera, TestActiveRequiresModernPlayerExternalAndFreeState);
    RUN_TEST_CASE(sithModernCamera, TestActiveAnchorsTransitionsAndNoneStates);
    RUN_TEST_CASE(sithModernCamera, TestNonFreeStateSyncsYaw);
}
