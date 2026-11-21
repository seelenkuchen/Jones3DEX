#include <unity_fixture.h>

void TEST_JonesData_GROUP_RUNNER(void);

static void JonesDataTest_RunAllTests(void)
{
    TEST_JonesData_GROUP_RUNNER();
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, JonesDataTest_RunAllTests);
}
