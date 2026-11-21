#include <unity_fixture.h>

#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWorld.h>
#include <std/General/stdUtil.h>

#define SITHCAMERA_TEST_PRIMARY_X 10.0f
#define SITHCAMERA_TEST_PRIMARY_Y 20.0f
#define SITHCAMERA_TEST_PRIMARY_Z 30.0f
#define SITHCAMERA_TEST_START_X   7.0f
#define SITHCAMERA_TEST_START_Y   8.0f
#define SITHCAMERA_TEST_START_Z   9.0f

static tHostServices sithCameraTest_hostServices;
static SithWorld sithCameraTest_world;
static SithSector sithCameraTest_sector;
static SithThing sithCameraTest_aThings[2];
static SithCamera sithCameraTest_camera;

static tHostServices* sithCameraTest_pSavedHostServices;
static SithWorld* sithCameraTest_pSavedWorld;

static int sithCameraTest_numAsserts;

TEST_GROUP(sithCamera);

static int J3DAPI sithCameraTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithCameraTest_RecordAssert(const char* pText, const char* pFile, int line)
{
    (void)pText;
    (void)pFile;
    (void)line;
    ++sithCameraTest_numAsserts;
}

static void sithCameraTest_InitCamera(SithThing* pPrimary, SithThing* pSecondary)
{
    STD_ZEROMEM(&sithCameraTest_camera, sizeof(sithCameraTest_camera));
    sithCameraTest_camera.type              = SITHCAMERA_CINEMATIC;
    sithCameraTest_camera.interpSpeed       = 1.0f;
    sithCameraTest_camera.pPrimaryFocusThing   = pPrimary;
    sithCameraTest_camera.pSecondaryFocusThing = pSecondary;
    sithCameraTest_camera.pos.x             = SITHCAMERA_TEST_START_X;
    sithCameraTest_camera.pos.y             = SITHCAMERA_TEST_START_Y;
    sithCameraTest_camera.pos.z             = SITHCAMERA_TEST_START_Z;
}

TEST_SETUP(sithCamera)
{
    STD_ZEROMEM(&sithCameraTest_hostServices, sizeof(sithCameraTest_hostServices));
    sithCameraTest_hostServices.pStatusPrint  = sithCameraTest_Print;
    sithCameraTest_hostServices.pMessagePrint = sithCameraTest_Print;
    sithCameraTest_hostServices.pWarningPrint = sithCameraTest_Print;
    sithCameraTest_hostServices.pErrorPrint   = sithCameraTest_Print;
    sithCameraTest_hostServices.pDebugPrint   = sithCameraTest_Print;
    sithCameraTest_hostServices.pAssert       = sithCameraTest_RecordAssert;

    sithCameraTest_pSavedHostServices = sith_g_pHS;
    sithCameraTest_pSavedWorld        = sithWorld_g_pCurrentWorld;
    sith_g_pHS                        = &sithCameraTest_hostServices;
    sithCameraTest_numAsserts         = 0;

    sithCamera_ResetGlobals();
    sithTime_g_frameTimeFlex = 1.0f / 15.0f;

    STD_ZEROMEM(&sithCameraTest_world, sizeof(sithCameraTest_world));
    STD_ZEROMEM(&sithCameraTest_sector, sizeof(sithCameraTest_sector));
    STD_ZEROMEM(sithCameraTest_aThings, sizeof(sithCameraTest_aThings));

    sithCameraTest_world.aThings    = sithCameraTest_aThings;
    sithCameraTest_world.numThings  = STD_ARRAYLEN(sithCameraTest_aThings);
    sithCameraTest_world.aSectors   = &sithCameraTest_sector;
    sithCameraTest_world.numSectors = 1;
    sithWorld_g_pCurrentWorld       = &sithCameraTest_world;

    for ( size_t i = 0; i < STD_ARRAYLEN(sithCameraTest_aThings); ++i )
    {
        sithCameraTest_aThings[i].idx       = (int)i;
        sithCameraTest_aThings[i].pInSector = &sithCameraTest_sector;
    }

    sithCameraTest_aThings[0].pos.x = SITHCAMERA_TEST_PRIMARY_X;
    sithCameraTest_aThings[0].pos.y = SITHCAMERA_TEST_PRIMARY_Y;
    sithCameraTest_aThings[0].pos.z = SITHCAMERA_TEST_PRIMARY_Z;
}

TEST_TEAR_DOWN(sithCamera)
{
    sithCamera_ResetGlobals();
    sithWorld_g_pCurrentWorld = sithCameraTest_pSavedWorld;
    sith_g_pHS                = sithCameraTest_pSavedHostServices;
}

TEST(sithCamera, TestCinematicWithoutSecondaryFocusKeepsPosition)
{
    sithCameraTest_InitCamera(&sithCameraTest_aThings[0], NULL);

    sithCamera_Update(&sithCameraTest_camera);

    TEST_ASSERT_EQUAL_INT(0, sithCameraTest_numAsserts);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_START_X, sithCameraTest_camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_START_Y, sithCameraTest_camera.pos.y);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_START_Z, sithCameraTest_camera.pos.z);
}

TEST(sithCamera, TestCinematicWithBothFocusesUpdatesPosition)
{
    sithCameraTest_InitCamera(&sithCameraTest_aThings[0], &sithCameraTest_aThings[1]);

    sithCamera_Update(&sithCameraTest_camera);

    TEST_ASSERT_EQUAL_INT(0, sithCameraTest_numAsserts);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_PRIMARY_X, sithCameraTest_camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_PRIMARY_Y, sithCameraTest_camera.pos.y);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_TEST_PRIMARY_Z, sithCameraTest_camera.pos.z);
}

// Added: the Field of view option drives the two normal gameplay cameras and leaves the
// cutscene camera, an authored FOV and the global range alone.
TEST(sithCamera, TestGameplayFOVAppliesToGameplayCamerasOnly)
{
    sithCamera_g_aCameras[SITHCAMERA_INTCAMERANUM].type    = SITHCAMERA_INTERNAL;
    sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM].type    = SITHCAMERA_EXTERNAL;
    sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM].type = SITHCAMERA_CINEMATIC;
    sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM].fov  = SITHCAMERA_FOVDEFAULT;

    // ResetAllCameras applies the option to the gameplay cameras and flags them.
    sithCamera_ResetAllCameras();
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_FOVDEFAULT,
        sithCamera_g_aCameras[SITHCAMERA_INTCAMERANUM].fov);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_FOVDEFAULT,
        sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM].fov);

    sithCamera_SetGameplayFOV(110.0f);
    TEST_ASSERT_EQUAL_FLOAT(110.0f, sithCamera_g_aCameras[SITHCAMERA_INTCAMERANUM].fov);
    TEST_ASSERT_EQUAL_FLOAT(110.0f, sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM].fov);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_FOVDEFAULT,
        sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM].fov);
    TEST_ASSERT_TRUE(sithCamera_IsGameplayFOV(&sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM]));
    TEST_ASSERT_FALSE(sithCamera_IsGameplayFOV(&sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM]));

    // A COG-set (authored) FOV keeps priority on the camera it changes.
    sithCamera_SetCameraFOV(&sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM], 70.0f);
    sithCamera_SetGameplayFOV(60.0f);
    TEST_ASSERT_EQUAL_FLOAT(70.0f, sithCamera_g_aCameras[SITHCAMERA_CINEMACAMERANUM].fov);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM].fov);

    // 0 means the default, and the option stays inside its own range.
    sithCamera_SetGameplayFOV(200.0f);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_GAMEPLAYFOVMAX, sithCamera_GetGameplayFOV());
    sithCamera_SetGameplayFOV(10.0f);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_GAMEPLAYFOVMIN, sithCamera_GetGameplayFOV());
    sithCamera_SetGameplayFOV(0.0f);
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_FOVDEFAULT, sithCamera_GetGameplayFOV());
    TEST_ASSERT_EQUAL_FLOAT(SITHCAMERA_FOVDEFAULT,
        sithCamera_g_aCameras[SITHCAMERA_EXTCAMERANUM].fov);
}

TEST_GROUP_RUNNER(sithCamera)
{
    RUN_TEST_CASE(sithCamera, TestCinematicWithoutSecondaryFocusKeepsPosition);
    RUN_TEST_CASE(sithCamera, TestCinematicWithBothFocusesUpdatesPosition);
    RUN_TEST_CASE(sithCamera, TestGameplayFOVAppliesToGameplayCamerasOnly);
}
