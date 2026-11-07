// Added: M6.0c command-line entry point of the render capture replay. It sets up the std host
// services like the game does, replays the input capture and exits with the replay result.

#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdPlatform.h>

#include <std/General/stdRenderReplay.h>

// Added: the Babylon column's entry (std/Babylon/stdRenderReplayBabylonMain.cpp) also takes the
// --render and --frames-out options before calling the shared run below.
#ifdef STDRENDERREPLAYMAIN_ENTRY
#define STDRENDERREPLAY_RENDER_USAGE " [--render null|gpu, default null] [--frames-out <dir> [--every <n>, default 1] [--normals]]"
#else
#define STDRENDERREPLAY_RENDER_USAGE ""
#endif

static void StdRenderReplayMain_Usage(void)
{
    fprintf(stderr, "usage: Jones3D-replay <in.rcap> [--capture <out.rcap>]" STDRENDERREPLAY_RENDER_USAGE "\n");
}

// Added: the Babylon column's C++ entry (std/Babylon/stdRenderReplayBabylonMain.cpp) installs the
// adapter context first and calls this one under the name STDRENDERREPLAYMAIN_ENTRY; other builds
// keep main.
#ifndef STDRENDERREPLAYMAIN_ENTRY
#define STDRENDERREPLAYMAIN_ENTRY main
#endif

int stdRenderReplayMain_RunWithFrameCallback(
    int argc,
    char** argv,
    StdRenderReplayFrameCallback pFrameCallback,
    void* pUserData
)
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

    result = stdRenderReplay_RunWithFrameCallback(pInPath, pOutPath, pFrameCallback, pUserData);

    stdShutdown();
    stdPlatform_ClearServices(&hostServices);
    return result;
}

int STDRENDERREPLAYMAIN_ENTRY(int argc, char** argv)
{
    return stdRenderReplayMain_RunWithFrameCallback(argc, argv, NULL, NULL);
}
