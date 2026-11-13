// Added: entry point of the game-side rumble trigger suite (sithRumbleTest.c).
#include <unity_fixture.h>

#include <stdio.h>

void TEST_sithRumble_GROUP_RUNNER(void);

static void SithRumbleTest_RunAllTests(void)
{
    TEST_sithRumble_GROUP_RUNNER();
}

int main(int argc, const char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    return UnityMain(argc, argv, SithRumbleTest_RunAllTests);
}
