#include <unity_fixture.h>

void TEST_JonesMain_GROUP_RUNNER(void);
#ifdef JONES3D_CONTROL_SCHEMES_TEST
void TEST_JonesControlSchemes_GROUP_RUNNER(void);
#endif

// Altered: the JonesCapture group runs in its own executable (jonesCaptureTests, JonesCaptureTestRunner.c).
static void Jones3DTest_RunAllTests(void)
{
    TEST_JonesMain_GROUP_RUNNER();
#ifdef JONES3D_CONTROL_SCHEMES_TEST
    TEST_JonesControlSchemes_GROUP_RUNNER();
#endif
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, Jones3DTest_RunAllTests);
}
