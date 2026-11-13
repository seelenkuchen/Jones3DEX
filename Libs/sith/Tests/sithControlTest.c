// Added: the Modern control style's virtual key layer (ADR-0029). The real bindings come from
// the shared Null stdControl, whose scripted device lets a case hold a bound key while the
// override and suppression paths are checked.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sith/Devices/sithControl.h>
#include <sith/Main/sithMain.h>

#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(sithControl);

static tHostServices sithControlTest_hostServices;
static size_t sithControlTest_assertCount;

static int J3DAPI sithControlTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithControlTest_Assert(const char* pCondition, const char* pFile, int line)
{
    (void)pCondition;
    (void)pFile;
    (void)line;
    ++sithControlTest_assertCount;
}

static void* J3DAPI sithControlTest_Malloc(size_t size)
{
    return malloc(size ? size : 1u);
}

static void J3DAPI sithControlTest_Free(void* pMemory)
{
    free(pMemory);
}

static void sithControlTest_ReadKey(SithControlFunction key, int keyDown)
{
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(key, keyDown));
    sithControl_ReadControls();
}

TEST_SETUP(sithControl)
{
    memset(&sithControlTest_hostServices, 0, sizeof(sithControlTest_hostServices));
    sithControlTest_hostServices.pAssert       = sithControlTest_Assert;
    sithControlTest_hostServices.pStatusPrint  = sithControlTest_Print;
    sithControlTest_hostServices.pMessagePrint = sithControlTest_Print;
    sithControlTest_hostServices.pWarningPrint = sithControlTest_Print;
    sithControlTest_hostServices.pErrorPrint   = sithControlTest_Print;
    sithControlTest_hostServices.pDebugPrint   = sithControlTest_Print;
    sithControlTest_hostServices.pMalloc       = sithControlTest_Malloc;
    sithControlTest_hostServices.pFree         = sithControlTest_Free;
    sith_g_pHS  = &sithControlTest_hostServices;
    std_g_pHS   = &sithControlTest_hostServices;
    sithControlTest_assertCount = 0;

    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, sithControl_Startup(0));

    // JonesControl_Startup registers every function as an axis as well as a key, so the movement
    // functions accept GetKeyAsAxis. PITCH stays axis-only for the rejection case.
    sithControl_RegisterAxisFunction(SITHCONTROL_FORWARD, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_BACK, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TURNLEFT, SITHCONTROLFUNCTION_KEY);
    sithControl_RegisterAxisFunction(SITHCONTROL_TURNRIGHT, SITHCONTROLFUNCTION_KEY);

    TEST_ASSERT_EQUAL_INT(1, sithControl_Open());
    sithControl_BindControl(SITHCONTROL_FORWARD, STDKEY_UP, (SithControlBindFlag)0);
}

TEST_TEAR_DOWN(sithControl)
{
    sithControl_ClearKeyOverrides();

    if ( sithControl_IsOpen() )
    {
        sithControl_Close();
    }

    sithControl_Shutdown();
    sithControl_ResetGlobals();
    sithControl_SetModernControlStyle(J3D_QOL_VALUE(true, false));

    sith_g_pHS = NULL;
    std_g_pHS  = NULL;
}

TEST(sithControl, OverrideReportsPressEdgesAndAxes)
{
    int pressed = -1;

    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 0.75f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(1, pressed);
    TEST_ASSERT_EQUAL_FLOAT(0.75f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, sithControl_GetKeyAsAxis(SITHCONTROL_FORWARD));

    // Held over the next frame: still down, but no new press.
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 0.75f);
    pressed = -1;
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 0, 0.75f);
    pressed = -1;
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithControl_GetKeyAsAxis(SITHCONTROL_FORWARD));

    // Released again: the next press is a new edge.
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 0.5f);
    pressed = -1;
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(1, pressed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));
}

TEST(sithControl, OverrideWinsOverARealBinding)
{
    int pressed = 0;

    sithControlTest_ReadKey(STDKEY_UP, 1);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));

    // A released override hides a physically held real key.
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 0, 0.0f);
    pressed = 0;
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    // A pressed override supplies the amount, independent of the real key.
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 0.25f);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, sithControl_GetKeyAsAxis(SITHCONTROL_FORWARD));

    // Clear restores the binding, and the real key is still held.
    sithControl_ClearKeyOverrides();
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));

    sithControlTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithControl, SuppressionHidesABoundPressedKey)
{
    int pressed = 0;

    sithControlTest_ReadKey(STDKEY_UP, 1);
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));

    sithControl_ClearKeyOverrides();
    sithControl_SuppressKey(SITHCONTROL_FORWARD);
    pressed = 5;
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,
        sithControl_GetKeyAsAxisNormalized(SITHCONTROL_FORWARD));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithControl_GetKeyAsAxis(SITHCONTROL_FORWARD));

    // The suppression lasts one frame; Clear brings the binding back while the key is held.
    sithControl_ClearKeyOverrides();
    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));

    sithControlTest_ReadKey(STDKEY_UP, 0);
}

TEST(sithControl, SuppressionDoesNotHideAnOverride)
{
    int pressed = 0;

    // The Modern layer suppresses the real movement keys while it drives them through overrides.
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 0.6f);
    sithControl_SuppressKey(SITHCONTROL_FORWARD);

    TEST_ASSERT_EQUAL_INT(1, sithControl_GetKey(SITHCONTROL_FORWARD, &pressed));
    TEST_ASSERT_EQUAL_FLOAT(0.6f, sithControl_GetKeyAsAxis(SITHCONTROL_FORWARD));
}

TEST(sithControl, ClearDropsEveryOverrideAndSuppression)
{
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride(SITHCONTROL_FORWARD, 1, 1.0f);
    sithControl_SetKeyOverride(SITHCONTROL_TURNLEFT, 1, 1.0f);
    sithControl_SuppressKey(SITHCONTROL_BACK);
    sithControl_SuppressKey(SITHCONTROL_TURNRIGHT);

    sithControl_ClearKeyOverrides();

    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_FORWARD, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNLEFT, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_BACK, NULL));
    TEST_ASSERT_EQUAL_INT(0, sithControl_GetKey(SITHCONTROL_TURNRIGHT, NULL));
}

TEST(sithControl, OutOfRangeAndAxisOnlyIdsAreIgnored)
{
    sithControl_ClearKeyOverrides();
    sithControl_SetKeyOverride((SithControlFunction)-1, 1, 1.0f);
    sithControl_SetKeyOverride((SithControlFunction)SITHCONTROL_MAXFUNCTIONS, 1, 1.0f);
    sithControl_SuppressKey((SithControlFunction)-1);
    sithControl_SuppressKey((SithControlFunction)SITHCONTROL_MAXFUNCTIONS);

    // An axis-only function has no key meaning, so its override is rejected.
    sithControl_SetKeyOverride(SITHCONTROL_PITCH, 1, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithControl_GetKeyAsAxis(SITHCONTROL_PITCH));
}

TEST(sithControl, ControlStyleIsModernInQolBuildsOnly)
{
#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(sithControl_IsModernControlStyle());

    sithControl_SetModernControlStyle(false);
    TEST_ASSERT_FALSE(sithControl_IsModernControlStyle());

    sithControl_SetModernControlStyle(true);
    TEST_ASSERT_TRUE(sithControl_IsModernControlStyle());
#else
    TEST_ASSERT_FALSE(sithControl_IsModernControlStyle());

    sithControl_SetModernControlStyle(true);
    TEST_ASSERT_FALSE(sithControl_IsModernControlStyle());
#endif
}

TEST_GROUP_RUNNER(sithControl)
{
    RUN_TEST_CASE(sithControl, OverrideReportsPressEdgesAndAxes);
    RUN_TEST_CASE(sithControl, OverrideWinsOverARealBinding);
    RUN_TEST_CASE(sithControl, SuppressionHidesABoundPressedKey);
    RUN_TEST_CASE(sithControl, SuppressionDoesNotHideAnOverride);
    RUN_TEST_CASE(sithControl, ClearDropsEveryOverrideAndSuppression);
    RUN_TEST_CASE(sithControl, OutOfRangeAndAxisOnlyIdsAreIgnored);
    RUN_TEST_CASE(sithControl, ControlStyleIsModernInQolBuildsOnly);
}
