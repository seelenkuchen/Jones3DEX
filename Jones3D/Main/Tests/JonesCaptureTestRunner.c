#include <unity_fixture.h>

void TEST_JonesCapture_GROUP_RUNNER(void);

static void JonesCaptureTest_RunAllTests(void)
{
    TEST_JonesCapture_GROUP_RUNNER();
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, JonesCaptureTest_RunAllTests);
}
