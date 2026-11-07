#include <unity_fixture.h>

#include <stdint.h>
#include <string.h>

#include <Jones3D/Display/JonesConsole.h>
#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Play/jonesCog.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/World/sithWorld.h>
#include <std/General/stdUtil.h>
#include <std/types.h>

TEST_GROUP(JonesMain);

TEST_SETUP(JonesMain)
{
    sithPlayer_g_pLocalPlayerThing = NULL;
    sithPlayerControls_g_bCutsceneMode = 0;
    sithWorld_g_bLoading              = 0;
    jonesCog_g_bMenuVisible           = 0;
    JonesConsole_g_bVisible           = 0;
    JonesHud_EnableMenu(1);
}

TEST_TEAR_DOWN(JonesMain)
{
}

static SithThing JonesMainTest_player;

static void JonesMainTest_SetPlayer(unsigned int flags)
{
    memset(&JonesMainTest_player, 0, sizeof(JonesMainTest_player));
    JonesMainTest_player.flags     = flags;
    sithPlayer_g_pLocalPlayerThing = &JonesMainTest_player;
}

static void JonesMainTest_BuildMode(StdVideoMode* pMode, uint32_t width, uint32_t height, uint32_t bpp, uint32_t refreshRate)
{
    STD_ZEROMEM(pMode, sizeof(StdVideoMode));
    pMode->rasterInfo.width         = width;
    pMode->rasterInfo.height        = height;
    pMode->rasterInfo.colorInfo.bpp = bpp;
    pMode->refreshRate              = refreshRate;
}

static void JonesMainTest_BuildDisplayInfo(StdDisplayInfo* pDisplay, StdVideoMode* aModes, size_t numModes)
{
    STD_ZEROMEM(pDisplay, sizeof(StdDisplayInfo));
    pDisplay->numModes = numModes;
    pDisplay->aModes   = aModes;
}

TEST(JonesMain, TestExactMatchAtRequestedRateAfterOtherRate)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 75);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 60);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestZeroRefreshRateCountsAsExact)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 75);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 0);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestReportedMissingRefreshRateBug)
{
    StdVideoMode aModes[3] = { 0 };
    JonesMainTest_BuildMode(&aModes[0], 320, 200, 16, 165);
    JonesMainTest_BuildMode(&aModes[1], 640, 480, 32, 165);
    JonesMainTest_BuildMode(&aModes[2], 2560, 1440, 32, 165);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 2560, 1440, 32, 60);

    TEST_ASSERT_EQUAL_size_t(2u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestSameSizeClosestRate)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 144);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 75);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestClosestRateNoUnsignedWraparound)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 240);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 30);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 200);

    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestClosestRateNoUnsignedWraparound_LowFirst)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 30);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 100);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    // A naive mode - requested unsigned distance wraps huge for the 30 Hz mode, so the 100 Hz mode wins.
    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestClosestRateTieKeepsFirstListed)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 30);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 90);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestSameSizeOtherRateBeatsOtherSizeMatchRate)
{
    StdVideoMode aModes[3] = { 0 };
    JonesMainTest_BuildMode(&aModes[0], 800, 600, 32, 75);
    JonesMainTest_BuildMode(&aModes[1], 640, 480, 32, 60);
    JonesMainTest_BuildMode(&aModes[2], 640, 480, 16, 60);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestSizeAbsentRateAvailable)
{
    StdVideoMode aModes[3] = { 0 };
    JonesMainTest_BuildMode(&aModes[0], 640, 480, 32, 165);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 60);
    JonesMainTest_BuildMode(&aModes[2], 800, 600, 16, 60);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 1024, 768, 32, 60);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestSizeAbsentRateAbsent)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 640, 480, 32, 85);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 32, 165);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 1024, 768, 32, 120);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestOtherBppNotSelected)
{
    StdVideoMode aModes[3] = { 0 };
    JonesMainTest_BuildMode(&aModes[0], 640, 480, 32, 60);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 16, 60);
    JonesMainTest_BuildMode(&aModes[2], 1024, 768, 32, 60);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(2u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestNoModeAtRequestedBpp)
{
    StdVideoMode aModes[2];
    JonesMainTest_BuildMode(&aModes[0], 640, 480, 16, 60);
    JonesMainTest_BuildMode(&aModes[1], 800, 600, 16, 60);

    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, aModes, STD_ARRAYLEN(aModes));

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestEmptyModeList)
{
    StdDisplayInfo displayInfo;
    JonesMainTest_BuildDisplayInfo(&displayInfo, NULL, 0);

    StdDisplayInfo aDisplayInfos[1] = { displayInfo };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(0u, JonesMain_FindClosestVideoMode(&env, &requested, 0));
}

TEST(JonesMain, TestNullColumnNeverDefersStartModes)
{
    // Added: the Null column keeps every start-mode dialog where it always ran (ADR-0026).
    for ( int startMode = JONES_STARTMODE_STARTGAME; startMode <= JONES_STARTMODE_MAINMENU;
        ++startMode )
    {
        TEST_ASSERT_EQUAL_INT(0, jonesConfig_DefersStartMode(startMode));
    }
}

TEST(JonesMain, TestUsesRequestedDeviceModes)
{
    StdVideoMode aModesFirst[1];
    JonesMainTest_BuildMode(&aModesFirst[0], 800, 600, 32, 165);

    StdVideoMode aModesSecond[2] = { 0 };
    JonesMainTest_BuildMode(&aModesSecond[0], 640, 480, 32, 60);
    JonesMainTest_BuildMode(&aModesSecond[1], 800, 600, 32, 60);

    StdDisplayInfo displayInfoFirst;
    JonesMainTest_BuildDisplayInfo(&displayInfoFirst, aModesFirst, STD_ARRAYLEN(aModesFirst));
    StdDisplayInfo displayInfoSecond;
    JonesMainTest_BuildDisplayInfo(&displayInfoSecond, aModesSecond, STD_ARRAYLEN(aModesSecond));

    StdDisplayInfo aDisplayInfos[2] = { displayInfoFirst, displayInfoSecond };
    StdDisplayEnvironment env;
    env.numInfos      = STD_ARRAYLEN(aDisplayInfos);
    env.aDisplayInfos = aDisplayInfos;

    StdVideoMode requested;
    JonesMainTest_BuildMode(&requested, 800, 600, 32, 60);

    TEST_ASSERT_EQUAL_size_t(1u, JonesMain_FindClosestVideoMode(&env, &requested, 1));
}

TEST(JonesMain, TestPauseMenuNeedsAPlayer)
{
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
}

TEST(JonesMain, TestPauseMenuOpensInPlay)
{
    JonesMainTest_SetPlayer(0);
    TEST_ASSERT_EQUAL_INT(1, JonesMain_CanOpenPauseMenu());
}

TEST(JonesMain, TestPauseMenuStaysClosedInEachBlockingState)
{
    JonesMainTest_SetPlayer(0);

    sithPlayerControls_g_bCutsceneMode = 1;
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
    sithPlayerControls_g_bCutsceneMode = 0;

    sithWorld_g_bLoading = 1;
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
    sithWorld_g_bLoading = 0;

    jonesCog_g_bMenuVisible = 1;
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
    jonesCog_g_bMenuVisible = 0;

    JonesConsole_g_bVisible = 1;
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
    JonesConsole_g_bVisible = 0;

    JonesHud_EnableMenu(0);
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
    JonesHud_EnableMenu(1);

    TEST_ASSERT_EQUAL_INT(1, JonesMain_CanOpenPauseMenu());
}

TEST(JonesMain, TestNullPauseMenuAnswersAtOnce)
{
    TEST_ASSERT_EQUAL_INT(2, jonesConfig_ShowPauseMenu(NULL));
}

TEST(JonesMain, TestPauseMenuStaysClosedForDeadOrDyingPlayer)
{
    JonesMainTest_SetPlayer(SITH_TF_DYING);
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());

    JonesMainTest_SetPlayer(SITH_TF_DESTROYED);
    TEST_ASSERT_EQUAL_INT(0, JonesMain_CanOpenPauseMenu());
}

TEST_GROUP_RUNNER(JonesMain)
{
    RUN_TEST_CASE(JonesMain, TestExactMatchAtRequestedRateAfterOtherRate);
    RUN_TEST_CASE(JonesMain, TestZeroRefreshRateCountsAsExact);
    RUN_TEST_CASE(JonesMain, TestReportedMissingRefreshRateBug);
    RUN_TEST_CASE(JonesMain, TestSameSizeClosestRate);
    RUN_TEST_CASE(JonesMain, TestClosestRateNoUnsignedWraparound);
    RUN_TEST_CASE(JonesMain, TestClosestRateNoUnsignedWraparound_LowFirst);
    RUN_TEST_CASE(JonesMain, TestClosestRateTieKeepsFirstListed);
    RUN_TEST_CASE(JonesMain, TestSameSizeOtherRateBeatsOtherSizeMatchRate);
    RUN_TEST_CASE(JonesMain, TestSizeAbsentRateAvailable);
    RUN_TEST_CASE(JonesMain, TestSizeAbsentRateAbsent);
    RUN_TEST_CASE(JonesMain, TestOtherBppNotSelected);
    RUN_TEST_CASE(JonesMain, TestNoModeAtRequestedBpp);
    RUN_TEST_CASE(JonesMain, TestEmptyModeList);
    RUN_TEST_CASE(JonesMain, TestNullColumnNeverDefersStartModes);
    RUN_TEST_CASE(JonesMain, TestUsesRequestedDeviceModes);
    RUN_TEST_CASE(JonesMain, TestPauseMenuNeedsAPlayer);
    RUN_TEST_CASE(JonesMain, TestPauseMenuOpensInPlay);
    RUN_TEST_CASE(JonesMain, TestPauseMenuStaysClosedInEachBlockingState);
    RUN_TEST_CASE(JonesMain, TestNullPauseMenuAnswersAtOnce);
    RUN_TEST_CASE(JonesMain, TestPauseMenuStaysClosedForDeadOrDyingPlayer);
}
