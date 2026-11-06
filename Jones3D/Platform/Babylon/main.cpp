// Added: the Babylon column's composition root. It builds the Babylon clocks, file system, platform,
// cvar registry and render device, installs them as the adapter's context, runs the shared headless
// loop, then uninstalls before the objects die.
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

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonCvars.h>
#include <std/Babylon/stdRenderBabylon.h>

#include <Jones3D/Platform/Null/JonesHeadless.h>

int main(int argc, char** argv)
{
    // Added: --render null|gpu picks the render device std3D draws with (the usage text is in
    // JonesHeadless.c). It is taken out of the arguments here, so the shared runner never sees it.
    StdRenderBabylonDeviceType renderType = StdRenderBabylonDeviceType::Null;
    if ( !stdRenderBabylon_TakeRenderOption(&argc, argv, &renderType) )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: --render needs null or gpu!\n");
        return 1;
    }

    // Added: the Null device by default: it records the frames and draws nothing, so a run needs no
    // GPU. --render gpu is the SDL GPU device without a window, for image checks; a run that asked
    // for it fails when there is none.
    std::unique_ptr<babylon::render::Device> pRenderDevice;
    if ( renderType == StdRenderBabylonDeviceType::Gpu )
    {
        pRenderDevice = babylon::render::createSdlGpuDevice(nullptr);
        if ( !pRenderDevice )
        {
            fprintf(stderr, "ERROR: Jones3D-headless: --render gpu: could not create the SDL GPU device!\n");
            return 1;
        }
    }
    else
    {
        pRenderDevice = babylon::render::createNullDevice();
    }

    std::unique_ptr<babylon::files::FileSystem> pFileSystem = babylon::files::createSdlFileSystem();
    std::unique_ptr<babylon::time::Clock> pClock = babylon::time::createSdlClock();
    babylon::time::NullClock virtualClock;
    // Added: the headless run has no OS input, so the platform is a NullPlatform whose queue the
    // runner's --input script feeds; the windowed entry (mainWindowed.cpp) uses the SDL platform.
    // Both context pointers name it: stdControl_ReadDevices() pumps it and stdControl_QueueKeyEvent()
    // pushes into it.
    babylon::platform::NullPlatform platform;
    // Added: the run's cvars (ADR-0020): the game's own are registered here, before finishStartup(),
    // so the headless state file can name the simulation profile that produced the trace.
    babylon::cvars::Registry cvars;
    if ( !stdBabylonCvars_RegisterGame(cvars) )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: could not register the game cvars!\n");
        return 1;
    }

    cvars.finishStartup();
    // Added: the headless run has no game window; the windowed composition root sets pWindow.
    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform, &platform, nullptr, &cvars,
        pRenderDevice.get() };
    stdBabylon_SetContext(&context);
    int exitCode = JonesHeadless_Run(argc, argv);
    stdBabylon_SetContext(nullptr);
    return exitCode;
}
