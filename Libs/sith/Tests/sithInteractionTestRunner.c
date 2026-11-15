#include <unity_fixture.h>

#include <stdio.h>

void TEST_sithInteraction_GROUP_RUNNER(void);

int main(int argc, const char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    return UnityMain(argc, argv, TEST_sithInteraction_GROUP_RUNNER);
}
