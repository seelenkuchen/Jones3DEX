// Added: the F12 screenshot hotkey. The module is compiled with a counting
// JonesMain_OnScreenshotKey, and the real sithControl key state comes from the column's scripted
// stdControl device, so a press, a held key and a release are checked against the real bindings.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <Jones3D/Main/JonesScreenshot.h>

#include <sith/Devices/sithControl.h>
#include <sith/Main/sithMain.h>

#include <std/General/std.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(JonesScreenshot);

static tHostServices JonesScreenshotTest_hostServices;
static size_t JonesScreenshotTest_numScreenshots;

void JonesScreenshotTest_OnScreenshotKey(void); // Added: the counting seam of JonesMain_OnScreenshotKey

void JonesScreenshotTest_OnScreenshotKey(void)
{
    ++JonesScreenshotTest_numScreenshots;
}

static int J3DAPI JonesScreenshotTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI JonesScreenshotTest_Assert(const char* pCondition, const char* pFile, int line)
{
    (void)pCondition;
    (void)pFile;
    (void)line;
}

static void* J3DAPI JonesScreenshotTest_Malloc(size_t size)
{
    return malloc(size ? size : 1u);
}

static void J3DAPI JonesScreenshotTest_Free(void* pMemory)
{
    free(pMemory);
}

static void JonesScreenshotTest_ReadKey(int bPressed)
{
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_F12, bPressed));
    sithControl_ReadControls();
}

TEST_SETUP(JonesScreenshot)
{
    memset(&JonesScreenshotTest_hostServices, 0, sizeof(JonesScreenshotTest_hostServices));
    JonesScreenshotTest_hostServices.pAssert       = JonesScreenshotTest_Assert;
    JonesScreenshotTest_hostServices.pStatusPrint  = JonesScreenshotTest_Print;
    JonesScreenshotTest_hostServices.pMessagePrint = JonesScreenshotTest_Print;
    JonesScreenshotTest_hostServices.pWarningPrint = JonesScreenshotTest_Print;
    JonesScreenshotTest_hostServices.pErrorPrint   = JonesScreenshotTest_Print;
    JonesScreenshotTest_hostServices.pDebugPrint   = JonesScreenshotTest_Print;
    JonesScreenshotTest_hostServices.pMalloc       = JonesScreenshotTest_Malloc;
    JonesScreenshotTest_hostServices.pFree         = JonesScreenshotTest_Free;
    sith_g_pHS = &JonesScreenshotTest_hostServices;
    std_g_pHS  = &JonesScreenshotTest_hostServices;

    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();
    JonesScreenshot_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, sithControl_Startup(0));
    TEST_ASSERT_EQUAL_INT(1, sithControl_Open());
    sithControl_BindControl(SITHCONTROL_SCREENSHOT, STDKEY_F12, (SithControlBindFlag)0);

    JonesScreenshotTest_numScreenshots = 0;
}

TEST_TEAR_DOWN(JonesScreenshot)
{
    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();
    JonesScreenshot_ResetGlobals();

    sith_g_pHS = NULL;
    std_g_pHS  = NULL;
}

TEST(JonesScreenshot, PressEdgeMakesOneScreenshotAndHoldingDoesNotRepeat)
{
    JonesScreenshotTest_ReadKey(1);
    JonesScreenshot_ProcessKey();
    TEST_ASSERT_EQUAL_size_t(1u, JonesScreenshotTest_numScreenshots);

    // The key stays down over the next updates: no new press, so no screenshot.
    JonesScreenshot_ProcessKey();
    JonesScreenshot_ProcessKey();
    TEST_ASSERT_EQUAL_size_t(1u, JonesScreenshotTest_numScreenshots);

    // The poll does not consume the control: another reader still sees it held.
    int numPressed = 0;
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_SCREENSHOT, &numPressed));
    TEST_ASSERT_EQUAL_INT(1, numPressed);

    // Release, then press again: a new edge writes the second screenshot.
    JonesScreenshotTest_ReadKey(0);
    JonesScreenshot_ProcessKey();
    TEST_ASSERT_EQUAL_size_t(1u, JonesScreenshotTest_numScreenshots);

    JonesScreenshotTest_ReadKey(1);
    JonesScreenshot_ProcessKey();
    TEST_ASSERT_EQUAL_size_t(2u, JonesScreenshotTest_numScreenshots);
}

TEST_GROUP_RUNNER(JonesScreenshot)
{
    RUN_TEST_CASE(JonesScreenshot, PressEdgeMakesOneScreenshotAndHoldingDoesNotRepeat);
}
