// Added: the Babylon column's composition root. It builds the Babylon clocks, file system, platform,
// cvar registry and render device, installs them as the adapter's context, runs the shared headless
// loop, then uninstalls before the objects die.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

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
#include <std/Babylon/stdRenderBabylon.h>

#include <Jones3D/Main/JonesDataLocate.h>
#include <Jones3D/Platform/Null/JonesHeadless.h>

namespace
{
    // Added: _dupenv_s avoids MSVC's getenv deprecation; other hosts keep std::getenv.
    std::string ReadEnvironment(const char* pName)
    {
#ifdef _WIN32
        char* pValue = nullptr;
        size_t size = 0;
        if ( _dupenv_s(&pValue, &size, pName) != 0 || !pValue )
        {
            return {};
        }

        std::string value = pValue;
        std::free(pValue);
        return value;
#else
        const char* pValue = std::getenv(pName);
        return pValue ? pValue : "";
#endif
    }

    // Added: the home the store search uses. Windows keeps it in USERPROFILE, or HOMEDRIVE plus
    // HOMEPATH; other hosts use HOME.
    std::string HomeDirectory()
    {
        const std::string userProfile = ReadEnvironment("USERPROFILE");
        if ( !userProfile.empty() )
        {
            return userProfile;
        }

        const std::string drive = ReadEnvironment("HOMEDRIVE");
        const std::string path  = ReadEnvironment("HOMEPATH");
        if ( !drive.empty() && !path.empty() )
        {
            return drive + path;
        }

        return ReadEnvironment("HOME");
    }
}

int main(int argc, char** argv)
{
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
    // Added: the game data search takes the host paths and environment from here, so the game layer
    // no longer reads platform paths or getenv itself.
    const std::string homeDir        = HomeDirectory();
    const std::string programFiles   = ReadEnvironment("ProgramFiles");
    const std::string programFilesX86 = ReadEnvironment("ProgramFiles(x86)");
    JonesDataLocate_SetHostDirs(pFileSystem->basePath().c_str(), pFileSystem->currentPath().c_str());
    JonesDataLocate_SetHostEnvironment(homeDir.c_str(), programFiles.c_str(),
        programFilesX86.c_str());
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
    int exitCode = JonesHeadless_Run(argc, argv);
    stdBabylon_SetContext(nullptr);
    return exitCode;
}
