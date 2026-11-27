// Added: the Babylon column's composition root. It builds the Babylon clocks, file system, platform,
// cvar registry and render device, installs them as the adapter's context, runs the shared headless
// loop, then uninstalls before the objects die.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <j3dcore/j3d.h>

#include <babylon/audio/NullDevice.h>
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
#include <std/Babylon/stdBabylonTour.h>
#include <std/Babylon/stdRenderBabylon.h>

#include <Jones3D/Main/JonesDataLocate.h>
#include <Jones3D/Platform/Null/JonesHeadless.h>

#ifdef _WIN32
// Added: windows.h stays out of this composition root (the platform boundary), so name the one
// kernel32 entry point the UTF-8 startup check needs. Windows builds are 64-bit, where every
// function uses one calling convention.
extern "C" __declspec(dllimport) unsigned int __stdcall GetACP(void);
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Added: the executable's manifest turns on the UTF-8 code page; warn once when it did not
    // take (Windows before 10 1903), because the game passes non-ASCII char* paths to the CRT.
    if ( GetACP() != 65001 )
    {
        fprintf(stderr,
            "WARNING: UTF-8 code page not active; paths with non-ASCII characters may fail\n");
    }
#endif

    // Added: --version prints the build's version and exits before any engine code runs.
    for ( int i = 1; i < argc; ++i )
    {
        if ( strcmp(argv[i], "--version") == 0 )
        {
            printf("%s\n", J3D_VERSION_FULL);
            return 0;
        }
    }

    // Added: --render null|gpu picks the render device std3D draws with (the usage text is in
    // JonesHeadless.c). It is taken out of the arguments here, so the shared runner never sees it.
    StdRenderBabylonDeviceType renderType = StdRenderBabylonDeviceType::Null;
    if ( !stdRenderBabylon_TakeRenderOption(&argc, argv, &renderType) )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: --render needs null or gpu!\n");
        return 1;
    }

    // Added: the screenshots of chosen presents use the windowed build's scripted tour. Only its two
    // screenshot options belong here: its full parser also owns --input, which the headless runner
    // reads as its own script. So build a command line of only those pairs, let the tour validate
    // them, then take the same pairs out of the real arguments before the runner sees them.
    std::vector<char*> tourArgs;
    tourArgs.push_back(argv[0]);
    for ( int i = 1; i < argc; ++i )
    {
        if ( strcmp(argv[i], "--screenshot-dir") == 0 || strcmp(argv[i], "--screenshot-at") == 0 )
        {
            tourArgs.push_back(argv[i]);
            if ( i + 1 < argc )
            {
                tourArgs.push_back(argv[++i]);
            }
        }
    }

    tourArgs.push_back(nullptr);

    StdBabylonTourOptions tourOptions;
    std::string tourError;
    int tourArgc = static_cast<int>(tourArgs.size()) - 1;
    if ( !stdBabylonTour_TakeOptions(&tourArgc, tourArgs.data(), tourOptions, tourError) )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: %s\n", tourError.c_str());
        return 1;
    }

    int numKept = 1;
    for ( int i = 1; i < argc; ++i )
    {
        if ( strcmp(argv[i], "--screenshot-dir") == 0 || strcmp(argv[i], "--screenshot-at") == 0 )
        {
            ++i;
            continue;
        }

        argv[numKept++] = argv[i];
    }

    argv[numKept] = nullptr;
    argc          = numKept;

    // Added: screenshots read the rendered frame back from the present path, which only the SDL GPU
    // device draws; the Null device has nothing to write.
    if ( !tourOptions.screenshotDir.empty() && renderType != StdRenderBabylonDeviceType::Gpu )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: --screenshot-dir needs --render gpu!\n");
        return 1;
    }

    // Added: the Null device by default: it draws nothing and keeps no record of the frames, so a run
    // needs no GPU and doesn't grow. --render gpu is the SDL GPU device without a window, for image
    // checks; a run that asked for it fails when there is none.
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
        babylon::render::NullDeviceDesc nullDesc;
        nullDesc.record = false;
        pRenderDevice   = babylon::render::createNullDevice(nullDesc);
    }

    std::unique_ptr<babylon::files::FileSystem> pFileSystem = babylon::files::createSdlFileSystem();
    // Added: the game data search takes the host paths from here, so the game layer no longer reads
    // platform paths itself.
    JonesDataLocate_SetHostDirs(pFileSystem->basePath().c_str(), pFileSystem->currentPath().c_str());
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
    // Added: the headless run always plays on a Null audio device, so traces stay device-free.
    std::unique_ptr<babylon::audio::NullDevice> pAudioDevice = babylon::audio::createNullDevice();

    // Added: the headless run has no game window; the windowed composition root sets pWindow.
    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform, &platform, nullptr, &cvars,
        pRenderDevice.get(), pAudioDevice.get() };
    stdBabylon_SetContext(&context);

    // Added: the headless run has no window, so the tour gets the invalid WindowId (0) and only its
    // screenshot side is used; the present count and the frame-<n>.bmp writer need no window.
    if ( !tourOptions.screenshotDir.empty()
        && !stdBabylonTour_Configure(tourOptions, babylon::platform::WindowId{}, tourError) )
    {
        fprintf(stderr, "ERROR: Jones3D-headless: %s\n", tourError.c_str());
        stdBabylon_SetContext(nullptr);
        return 1;
    }

    int exitCode = JonesHeadless_Run(argc, argv);
    stdBabylonTour_Shutdown();
    stdBabylon_SetContext(nullptr);
    return exitCode;
}
