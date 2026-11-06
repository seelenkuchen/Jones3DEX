// Added: the Babylon column's entry of the render capture replay (Jones3D-replay). It installs the
// adapter context the replayed calls need, the SDL file system and clock, a NullPlatform, the game's
// cvars and the render device chosen with --render, so a capture replays into the Babylon std3D
// without a GPU or on the SDL GPU device, then calls the C entry of Tools/stdRenderReplayMain.c and
// uninstalls before the objects die. With --frames-out the replay writes each frame to a BMP.
#include <cstdio>
#include <memory>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

#include "stdBabylon.h"
#include "stdBabylonCvars.h"
#include <std/General/stdRenderReplay.h>

#include "stdRenderBabylon.h"
#include "stdRenderReplayBabylon.h"

// Tools/stdRenderReplayMain.c compiled with STDRENDERREPLAYMAIN_ENTRY=stdRenderReplayMain_Run.
extern "C" int stdRenderReplayMain_RunWithFrameCallback(
    int argc,
    char** argv,
    StdRenderReplayFrameCallback pFrameCallback,
    void* pUserData
);

int main(int argc, char** argv)
{
    StdRenderBabylonDeviceType renderType = StdRenderBabylonDeviceType::Null;
    if ( !stdRenderBabylon_TakeRenderOption(&argc, argv, &renderType) )
    {
        fprintf(stderr, "ERROR: Jones3D-replay: --render needs null or gpu!\n");
        return 2;
    }

    StdRenderReplayBabylonFrameOutput frameOutput;
    const char* pError = nullptr;
    if ( !stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, renderType == StdRenderBabylonDeviceType::Gpu,
        &frameOutput, &pError) )
    {
        fprintf(stderr, "ERROR: %s!\n", pError ? pError : "Jones3D-replay: invalid options");
        return 2;
    }

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

    std::unique_ptr<babylon::render::Device> pRenderDevice;
    if ( renderType == StdRenderBabylonDeviceType::Gpu )
    {
        pRenderDevice = babylon::render::createSdlGpuDevice(nullptr);
        if ( !pRenderDevice )
        {
            fprintf(stderr, "ERROR: Jones3D-replay: --render gpu: could not create the SDL GPU device!\n");
            return 1;
        }
    }
    else
    {
        // The replay checks the calls, not the device's record: a long capture keeps none.
        babylon::render::NullDeviceDesc nullDesc;
        nullDesc.record = false;
        pRenderDevice   = babylon::render::createNullDevice(nullDesc);
    }

    if ( !stdRenderReplayBabylon_BeginFrames(&frameOutput) )
    {
        return 1;
    }

    cvars.finishStartup();
    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform, &platform, nullptr, &cvars,
        pRenderDevice.get() };
    stdBabylon_SetContext(&context);
    int exitCode = stdRenderReplayMain_RunWithFrameCallback(argc, argv,
        frameOutput.pDirectory ? stdRenderReplayBabylon_CaptureFrame : nullptr, &frameOutput);
    stdBabylon_SetContext(nullptr);

    if ( frameOutput.pDirectory )
    {
        printf("replay: %zu frames replayed, %zu images written\n", frameOutput.frames, frameOutput.images);
    }

    return exitCode;
}
