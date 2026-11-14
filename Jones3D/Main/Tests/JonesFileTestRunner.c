#include <unity_fixture.h>

void TEST_JonesFile_GROUP_RUNNER(void);

static void JonesFileTest_RunAllTests(void)
{
    TEST_JonesFile_GROUP_RUNNER();
}

int main(int argc, const char* argv[])
{
    return UnityMain(argc, argv, JonesFileTest_RunAllTests);
}
