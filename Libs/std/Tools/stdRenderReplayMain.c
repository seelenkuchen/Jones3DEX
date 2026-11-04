// Added: M6.0c command-line entry point of the render capture replay. It sets up the std host
// services like the game does, replays the input capture and exits with the replay result.

#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdPlatform.h>

#include <std/Win95/stdRenderReplay.h>

static void StdRenderReplayMain_Usage(void)
{
    fprintf(stderr, "usage: Jones3D-replay <in.rcap> [--capture <out.rcap>]\n");
}

int main(int argc, char** argv)
{
    const char* pInPath = NULL;
    const char* pOutPath = NULL;
    tHostServices hostServices;
    int result;

    if ( argc == 2 )
    {
        pInPath = argv[1];
    }
    else if ( argc == 4 && strcmp(argv[2], "--capture") == 0 )
    {
        pInPath  = argv[1];
        pOutPath = argv[3];
    }
    else
    {
        StdRenderReplayMain_Usage();
        return 2;
    }

    stdPlatform_InitServices(&hostServices);
    stdStartup(&hostServices);

    result = stdRenderReplay_Run(pInPath, pOutPath);

    stdShutdown();
    stdPlatform_ClearServices(&hostServices);
    return result;
}
