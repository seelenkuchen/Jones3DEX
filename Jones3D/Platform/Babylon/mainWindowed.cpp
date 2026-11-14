// Added: the windowed composition root of the Babylon column. It builds the SDL platform and the
// game window, the SDL clock, the Null virtual clock (never latched: the windowed run uses the
// real clock), the SDL file system and the run's cvar registry, installs them as the adapter context
// together with the window, runs the windowed game loop, then uninstalls before the objects die.
// Added: std3D draws and stdDisplay presents on the render device made here, the SDL GPU device on
// the game window (M6.6, M6.7).
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <j3dcore/j3d.h>

#include <babylon/audio/Device.h>
#include <babylon/audio/MiniaudioDevice.h>
#include <babylon/audio/NullDevice.h>
#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/Path.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonCvars.h>
#include <std/Babylon/stdBabylonTour.h>

#include <Jones3D/Platform/Babylon/JonesBabylon.h>

namespace
{
    // Added: the windowed run's options beyond the game's own command line. The game command line
    // itself has no usage text; these options are printed when the scripted tour rejects one.
    void PrintUsage(const char* pProgramName)
    {
        fprintf(stderr,
            "usage: %s [--version] [--user-dir <dir>] [--load <save.nds>] [--input <script>] "
            "[--quit-after-script] [--screenshot-dir <dir> --screenshot-at <n>[,<n>...]] "
            "[--present-log <file>]\n",
            pProgramName ? pProgramName : "Jones3D");
    }

    // Added: --user-dir is taken out before the game command line is built, like --load. It is
    // validated here so a missing value stops the run before any engine code.
    bool TakeUserDirOption(int* pArgc, char** argv, std::string& rUserDir, bool& rHasUserDir,
        std::string& rError)
    {
        for ( int i = 1; i < *pArgc; ++i )
        {
            if ( strcmp(argv[i], "--user-dir") != 0 )
            {
                continue;
            }

            if ( i + 1 >= *pArgc )
            {
                rError = "--user-dir needs a value";
                return false;
            }

            rUserDir    = argv[++i];
            rHasUserDir = true;
        }

        int numKept = 1;
        for ( int i = 1; i < *pArgc; ++i )
        {
            if ( strcmp(argv[i], "--user-dir") == 0 )
            {
                ++i;
                continue;
            }

            argv[numKept++] = argv[i];
        }

        *pArgc = numKept;
        return true;
    }

    // Added: decides the windowed run's writable directory once, before JonesMain_Startup: an
    // explicit --user-dir, portable.txt beside the working directory (the working directory
    // itself, kept as "") or beside the executable (its directory), else the platform user path.
    // A failed platform lookup falls back to the working directory.
    std::string ResolveUserDir(babylon::files::FileSystem& fs, bool bHasUserDir,
        const std::string& userDir)
    {
        if ( bHasUserDir )
        {
            return userDir;
        }

        if ( fs.exists("portable.txt") )
        {
            return {};
        }

        const std::string basePath = fs.basePath();
        if ( !basePath.empty() && fs.exists(babylon::files::joinPath(basePath, "portable.txt")) )
        {
            std::string directory = basePath;
            while ( directory.size() > 1 && directory.back() == '/' )
            {
                directory.pop_back();
            }

            return directory;
        }

        std::string path = fs.userPath("", "Jones3DEX");
        if ( path.empty() )
        {
            fprintf(stderr,
                "WARNING: Jones3D: could not find a per-user data directory, using the working "
                "directory!\n");
            return {};
        }

        return path;
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

    // Added: the user directory option comes out first, so neither the tour parser nor the game
    // command line sees it.
    std::string userDir;
    bool bHasUserDir = false;
    std::string userDirError;
    if ( !TakeUserDirOption(&argc, argv, userDir, bHasUserDir, userDirError) )
    {
        fprintf(stderr, "ERROR: Jones3D: %s\n", userDirError.c_str());
        PrintUsage(argv[0]);
        return 1;
    }

    // Added: the scripted tour's options are taken out before the game command line is built, so
    // they never reach JonesMain_Startup. Parsing the whole tour command line up front means a bad
    // option exits before any engine code runs, like the headless runner's --input.
    int tourArgc = argc;
    StdBabylonTourOptions tourOptions;
    std::string tourError;
    if ( !stdBabylonTour_TakeOptions(&tourArgc, argv, tourOptions, tourError) )
    {
        fprintf(stderr, "ERROR: Jones3D: %s\n", tourError.c_str());
        PrintUsage(argv[0]);
        return 1;
    }

    std::unique_ptr<babylon::platform::Platform> pPlatform = babylon::platform::createSdlPlatform();
    if ( !pPlatform )
    {
        fprintf(stderr, "ERROR: Jones3D: could not create the SDL platform!\n");
        return 1;
    }

    const babylon::platform::WindowDesc desc{ "Jones3D EX", { 640, 480 }, false };
    std::unique_ptr<babylon::platform::Window> pWindow = pPlatform->createWindow(desc);
    if ( !pWindow )
    {
        fprintf(stderr, "ERROR: Jones3D: could not create the game window!\n");
        return 1;
    }

    // Added: read the tour's input script and open its files now, so a bad script or path stops the
    // run before any engine code. The script's events carry the game window id.
    if ( !stdBabylonTour_Configure(tourOptions, pWindow->id(), tourError) )
    {
        fprintf(stderr, "ERROR: Jones3D: %s\n", tourError.c_str());
        return 1;
    }

    std::unique_ptr<babylon::files::FileSystem> pFileSystem = babylon::files::createSdlFileSystem();
    // Added: the user directory decision needs the file system (portable.txt, base path, user path).
    const std::string userDirPath = ResolveUserDir(*pFileSystem, bHasUserDir, userDir);
    std::unique_ptr<babylon::time::Clock> pClock = babylon::time::createSdlClock();
    babylon::time::NullClock virtualClock;
    // Added: the run's cvars (ADR-0020), registered before finishStartup() like in the headless root,
    // so every run of this column has a cvar registry in its context.
    babylon::cvars::Registry cvars;
    if ( !stdBabylonCvars_RegisterGame(cvars) )
    {
        fprintf(stderr, "ERROR: Jones3D: could not register the game cvars!\n");
        return 1;
    }

    cvars.finishStartup();

    // Added: the render device draws to the window, so it is made after the window and, declared
    // later, destroyed before it. Without a GPU the game still runs, on the Null device.
    std::unique_ptr<babylon::render::Device> pRenderDevice = babylon::render::createSdlGpuDevice(pWindow.get());
    if ( !pRenderDevice )
    {
        fprintf(stderr, "WARNING: Jones3D: no SDL GPU device for the window, nothing will be drawn!\n");
        babylon::render::NullDeviceDesc nullDesc;
        nullDesc.record = false; // a whole game session: keep no record of its frames
        pRenderDevice   = babylon::render::createNullDevice(nullDesc);
    }

    // Added: the game plays on miniaudio (ADR-0010). createMiniaudioDevice() always returns a device,
    // so it is a trial open() that tells whether miniaudio can start; when it cannot, the run falls
    // back to a Null device and plays silently. The sound driver opens the device itself later.
    std::unique_ptr<babylon::audio::Device> pAudioDevice = babylon::audio::createMiniaudioDevice();
    if ( !pAudioDevice || !pAudioDevice->open() )
    {
        fprintf(stderr, "WARNING: Jones3D: miniaudio could not start, sound is silent!\n");
        pAudioDevice = babylon::audio::createNullDevice();
    }

    // The command line behind argv[0] and the --load option belong to the run: JonesBabylon_Run
    // takes main's arguments directly, like the headless entry. The windowed run has no script
    // platform: every event comes from the SDL platform itself.
    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), pPlatform.get(),
        nullptr, pWindow.get(), &cvars, pRenderDevice.get(), pAudioDevice.get() };
    stdBabylon_SetContext(&context);
    int exitCode = JonesBabylon_Run(tourArgc, argv, userDirPath.c_str());
    stdBabylon_SetContext(nullptr);

    // Added: close the present log and forget the tour after the run that used it.
    stdBabylonTour_Shutdown();

    // Scope end destroys pWindow before pPlatform, the order the Platform interface requires.
    // Added: and pRenderDevice before pWindow, which the SDL GPU device's swapchain needs.
    return exitCode;
}
