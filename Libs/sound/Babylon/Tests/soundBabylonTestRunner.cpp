#include <unity_fixture.h>

void TEST_SoundDriverBabylon_GROUP_RUNNER(void);

static void SoundDriverBabylonTest_RunAllTests(void)
{
    TEST_SoundDriverBabylon_GROUP_RUNNER();
}

// The entry name is overridable so the test main can call this runner after it has installed the
// adapter context (J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main).
#ifndef J3D_TEST_RUNNER_ENTRY
#define J3D_TEST_RUNNER_ENTRY main
#endif

extern "C" int J3D_TEST_RUNNER_ENTRY(int argc, const char* argv[])
{
    return UnityMain(argc, argv, SoundDriverBabylonTest_RunAllTests);
}
