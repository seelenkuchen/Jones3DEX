#include <unity_fixture.h>

void TEST_JonesMain_GROUP_RUNNER(void);
void TEST_JonesCapture_GROUP_RUNNER(void);

static void Jones3DTest_RunAllTests(void)
{
    TEST_JonesMain_GROUP_RUNNER();
    TEST_JonesCapture_GROUP_RUNNER();
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, Jones3DTest_RunAllTests);
}
