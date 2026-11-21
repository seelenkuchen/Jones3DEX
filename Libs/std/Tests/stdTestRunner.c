#include <unity_fixture.h>

#include <j3dcore/j3d.h>

#include <string.h>

void TEST_std_GROUP_RUNNER(void);
void TEST_stdMemory_GROUP_RUNNER(void);
void TEST_stdUtil_GROUP_RUNNER(void);
void TEST_stdMath_GROUP_RUNNER(void);
void TEST_stdFnames_GROUP_RUNNER(void);
void TEST_stdLinkList_GROUP_RUNNER(void);
void TEST_stdHashtbl_GROUP_RUNNER(void);
void TEST_stdEffect_GROUP_RUNNER(void);
void TEST_stdColor_GROUP_RUNNER(void);
void TEST_stdCircBuf_GROUP_RUNNER(void);
void TEST_stdConffile_GROUP_RUNNER(void);
void TEST_stdConfig_GROUP_RUNNER(void);
void TEST_stdRenderCapture_GROUP_RUNNER(void);
void TEST_stdFileUtil_GROUP_RUNNER(void);
void TEST_stdJSON_GROUP_RUNNER(void);
void TEST_stdStrTable_GROUP_RUNNER(void);
void TEST_stdUserDir_GROUP_RUNNER(void);
// Added: the non-Windows columns' simulation cvar seam (ADR-0020), stdCvarNull.c; the Babylon
// column's replacement is covered by stdCvarBabylon.cpp in stdBabylonTests.
void TEST_stdCvarNull_GROUP_RUNNER(void);
// Added: the renderer forwarding layer recording; it is only built on the Null platform columns.
void TEST_stdRenderCaptureForward_GROUP_RUNNER(void);
// Added: the replay of those captures; it is only built on the Null platform columns.
void TEST_stdRenderReplay_GROUP_RUNNER(void);
// Added: the suites that only the Win32/DirectX build ran (M6.3f), now on the Null backends.
void TEST_stdGob_GROUP_RUNNER(void);
void TEST_stdPlatform_GROUP_RUNNER(void);
void TEST_std3D_GROUP_RUNNER(void);
void TEST_stdDisplay_GROUP_RUNNER(void);
void TEST_stdControl_GROUP_RUNNER(void);
void TEST_stdComm_GROUP_RUNNER(void);
void TEST_stdWin95_GROUP_RUNNER(void);

static void runAllTests(void)
{
    TEST_std_GROUP_RUNNER();
    TEST_stdMemory_GROUP_RUNNER();
    TEST_stdUtil_GROUP_RUNNER();
    TEST_stdMath_GROUP_RUNNER();
    TEST_stdFnames_GROUP_RUNNER();
    TEST_stdLinkList_GROUP_RUNNER();
    TEST_stdHashtbl_GROUP_RUNNER();
    TEST_stdEffect_GROUP_RUNNER();
    TEST_stdColor_GROUP_RUNNER();
    TEST_stdCircBuf_GROUP_RUNNER();
    TEST_stdConffile_GROUP_RUNNER();
    TEST_stdConfig_GROUP_RUNNER();
    TEST_stdRenderCapture_GROUP_RUNNER();
    TEST_stdFileUtil_GROUP_RUNNER();
    TEST_stdJSON_GROUP_RUNNER();
    TEST_stdCvarNull_GROUP_RUNNER();
    TEST_stdRenderCaptureForward_GROUP_RUNNER();
    TEST_stdRenderReplay_GROUP_RUNNER();
    TEST_stdStrTable_GROUP_RUNNER();
    TEST_stdUserDir_GROUP_RUNNER();
    TEST_stdGob_GROUP_RUNNER();
    TEST_stdPlatform_GROUP_RUNNER();
    TEST_std3D_GROUP_RUNNER();
    TEST_stdDisplay_GROUP_RUNNER();
    TEST_stdControl_GROUP_RUNNER();
    TEST_stdComm_GROUP_RUNNER();
    TEST_stdWin95_GROUP_RUNNER();
}

// Added: the entry name is overridable so the Babylon column's test main can call this runner
// after it has installed the platform context (J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main); plain
// builds keep main.
#ifndef J3D_TEST_RUNNER_ENTRY
#define J3D_TEST_RUNNER_ENTRY main
#endif

int J3D_TEST_RUNNER_ENTRY(int argc, const char* argv[])
{
    return UnityMain(argc, argv, runAllTests);
}
