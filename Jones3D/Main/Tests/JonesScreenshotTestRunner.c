#include <unity_fixture.h>

void TEST_JonesScreenshot_GROUP_RUNNER(void);

static void JonesScreenshotTest_RunAllTests(void)
{
    TEST_JonesScreenshot_GROUP_RUNNER();
}

// Added: the Babylon column's test main installs the adapter context and calls the renamed entry;
// plain columns keep main.
#ifndef J3D_TEST_RUNNER_ENTRY
#define J3D_TEST_RUNNER_ENTRY main
#endif

int J3D_TEST_RUNNER_ENTRY(int argc, const char* argv[])
{
    return UnityMain(argc, argv, JonesScreenshotTest_RunAllTests);
}
