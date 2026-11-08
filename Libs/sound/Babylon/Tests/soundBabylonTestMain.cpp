// Added: the Babylon sound driver tests' entry point. It installs the adapter context with a
// NullClock (so the driver's platform time and the one-second reopen delay are exact), a
// NullPlatform and a NullDevice, calls the Unity runner's renamed entry, then uninstalls.
#include <cstdio>
#include <memory>

#include <babylon/audio/NullDevice.h>
#include <babylon/cvars/Registry.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/time/NullClock.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonCvars.h>
#include <std/General/std.h>
#include <std/General/stdPlatform.h>

// The runner source compiled with J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main.
extern "C" int j3dTestRunner_Main(int argc, const char* argv[]);

int main(int argc, char** argv)
{
    // The driver's handles go through the std memory layer, which starts like the game's own does.
    tHostServices hostServices;
    stdPlatform_InitServices(&hostServices);
    stdStartup(&hostServices);

    babylon::time::NullClock realClock;
    babylon::time::NullClock virtualClock;
    babylon::platform::NullPlatform platform;
    std::unique_ptr<babylon::audio::NullDevice> pAudioDevice = babylon::audio::createNullDevice();

    babylon::cvars::Registry cvars;
    if ( !stdBabylonCvars_RegisterGame(cvars) )
    {
        fprintf(stderr, "ERROR: soundBabylonTestMain: could not register the game cvars!\n");
        return 1;
    }

    cvars.finishStartup();

    // The driver reads no file, so the context's file system stays null like the other adapter tests
    // that do not touch it.
    StdBabylonContext context{ &realClock, &virtualClock, nullptr, &platform, &platform, nullptr, &cvars,
        nullptr, pAudioDevice.get() };
    stdBabylon_SetContext(&context);

    // C++ refuses the char** to const char** qualification change that C accepts for main's argv.
    int exitCode = j3dTestRunner_Main(argc, const_cast<const char**>(argv));

    stdBabylon_SetContext(nullptr);
    stdShutdown();
    stdPlatform_ClearServices(&hostServices);
    return exitCode;
}
