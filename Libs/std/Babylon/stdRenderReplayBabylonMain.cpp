// Added: the Babylon column's entry of the render capture replay (Jones3D-replay). It installs the
// adapter context the replayed calls need, the SDL file system and clock, a NullPlatform, the game's
// cvars and a Null render device, so a capture replays into the Babylon std3D without a GPU, then
// calls the C entry of Tools/stdRenderReplayMain.c and uninstalls before the objects die.
#include <cstdio>
#include <memory>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/render/NullDevice.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

#include "stdBabylon.h"
#include "stdBabylonCvars.h"

// Tools/stdRenderReplayMain.c compiled with STDRENDERREPLAYMAIN_ENTRY=stdRenderReplayMain_Run.
extern "C" int stdRenderReplayMain_Run(int argc, char** argv);

int main(int argc, char** argv)
{
    std::unique_ptr<babylon::files::FileSystem> pFileSystem = babylon::files::createSdlFileSystem();
    std::unique_ptr<babylon::time::Clock> pClock = babylon::time::createSdlClock();
    babylon::time::NullClock virtualClock;
    babylon::platform::NullPlatform platform;
    babylon::cvars::Registry cvars;
    if ( !stdBabylonCvars_RegisterGame(cvars) )
    {
        fprintf(stderr, "ERROR: Jones3D-replay: could not register the game cvars!\n");
        return 1;
    }

    cvars.finishStartup();
    std::unique_ptr<babylon::render::NullDevice> pRenderDevice = babylon::render::createNullDevice();
    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform, &platform, nullptr, &cvars,
        pRenderDevice.get() };
    stdBabylon_SetContext(&context);
    int exitCode = stdRenderReplayMain_Run(argc, argv);
    stdBabylon_SetContext(nullptr);
    return exitCode;
}
