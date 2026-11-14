#include <unity_fixture.h>

#include <j3dcore/j3d.h>

void TEST_JonesDataLocate_GROUP_RUNNER(void);

static void JonesDataLocateTest_RunAllTests(void)
{
    TEST_JonesDataLocate_GROUP_RUNNER();
}

// Added: the entry name is overridable so the Babylon column's test main can install the adapter
// context before this runner (J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main); plain builds keep main.
#ifndef J3D_TEST_RUNNER_ENTRY
#define J3D_TEST_RUNNER_ENTRY main
#endif

int J3D_TEST_RUNNER_ENTRY(int argc, const char* argv[])
{
    return UnityMain(argc, argv, JonesDataLocateTest_RunAllTests);
}
