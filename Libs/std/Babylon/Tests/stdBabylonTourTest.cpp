// Added: Unity Fixture suite for the windowed build's scripted tour (stdBabylonTour.cpp): the
// option parser, the input script replacing the platform's input events for exactly the present
// counts it names, and --quit-after-script. No display is needed: the suite reports presents
// through stdBabylonTour_OnPresent directly, the way stdDisplay_Update would.
#include <unity_fixture.h>

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/time/NullClock.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdBabylonTour.h>

TEST_GROUP(stdBabylonTour);

namespace
{
    babylon::time::NullClock*        pClock            = nullptr;
    babylon::time::NullClock*        pVirtualClock     = nullptr;
    babylon::platform::NullPlatform* pPlatform         = nullptr;
    babylon::cvars::Registry         cvars;
    StdBabylonContext                context;
    StdBabylonContext*               pPreviousContext = nullptr;

    const char* const kScriptPath = "stdBabylonTourTest_input.txt";
    const char* const kLogPath    = "stdBabylonTourTest_present.log";

    void WriteScript(const char* pText)
    {
        std::ofstream file(kScriptPath, std::ios::binary | std::ios::trunc);
        file << pText;
    }

    // Runs stdBabylonTour_TakeOptions on a mutable copy of the arguments.
    bool ParseOptions(std::initializer_list<std::string> args, StdBabylonTourOptions& rOptions,
        std::string& rError)
    {
        std::vector<std::string> storage(args);
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1u);
        for ( std::string& arg : storage )
        {
            argv.push_back(arg.data());
        }

        argv.push_back(nullptr);
        int argc = static_cast<int>(storage.size());
        return stdBabylonTour_TakeOptions(&argc, argv.data(), rOptions, rError);
    }

    bool ConfigureScript(const char* pText, bool bQuitAfterScript, std::string& rError)
    {
        WriteScript(pText);

        StdBabylonTourOptions options;
        options.inputPath       = kScriptPath;
        options.presentLogPath  = kLogPath;
        options.bQuitAfterScript = bQuitAfterScript;
        return stdBabylonTour_Configure(options, 0, rError);
    }

    // The key of the only event the pump produced, or Unknown when there is none or it is not a key.
    babylon::platform::Key LastKey()
    {
        const std::vector<babylon::platform::Event>& events = stdBabylonEvents_LastEvents();
        if ( events.size() != 1u )
        {
            return babylon::platform::Key::Unknown;
        }

        const babylon::platform::KeyChanged* pKey =
            std::get_if<babylon::platform::KeyChanged>(&events.front());
        return pKey ? pKey->key : babylon::platform::Key::Unknown;
    }
}

TEST_SETUP(stdBabylonTour)
{
    stdBabylonTour_Shutdown();
    stdBabylonEvents_ResetForTest();
    std::remove(kScriptPath);
    std::remove(kLogPath);

    pPreviousContext = stdBabylon_GetContext();
    pClock           = new babylon::time::NullClock();
    pVirtualClock    = new babylon::time::NullClock();
    pPlatform        = new babylon::platform::NullPlatform();

    context                = StdBabylonContext{};
    context.pClock         = pClock;
    context.pVirtualClock  = pVirtualClock;
    context.pPlatform      = pPlatform;
    context.pCvars         = &cvars;
    stdBabylon_SetContext(&context);
}

TEST_TEAR_DOWN(stdBabylonTour)
{
    stdBabylonEvents_ResetForTest();
    stdBabylonTour_Shutdown();
    stdBabylon_SetContext(pPreviousContext);

    delete pPlatform;
    delete pVirtualClock;
    delete pClock;
    pPlatform         = nullptr;
    pVirtualClock     = nullptr;
    pClock            = nullptr;
    pPreviousContext  = nullptr;

    std::remove(kScriptPath);
    std::remove(kLogPath);
}

TEST(stdBabylonTour, NoOptionsLeavesTheRunUntouched)
{
    StdBabylonTourOptions options;
    std::string error;
    TEST_ASSERT_TRUE(stdBabylonTour_Configure(options, 0, error));
    TEST_ASSERT_FALSE(stdBabylonTour_IsEnabled());

    stdBabylonTour_OnPresent();
    TEST_ASSERT_EQUAL_UINT64(0u, stdBabylonTour_GetPresentCount());
}

TEST(stdBabylonTour, TakesEveryOptionOutOfTheCommandLine)
{
    StdBabylonTourOptions options;
    std::string error;

    TEST_ASSERT_TRUE(ParseOptions(
        { "Jones3D", "--load", "save.nds", "--input", "tour.txt", "--screenshot-dir", "shots",
            "--screenshot-at", "30,1,30", "--present-log", "log.txt", "--quit-after-script" },
        options, error));

    // Only the game's own arguments are left.
    TEST_ASSERT_EQUAL_STRING("tour.txt", options.inputPath.c_str());
    TEST_ASSERT_EQUAL_STRING("shots", options.screenshotDir.c_str());
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)options.screenshotFrames.size());
    TEST_ASSERT_EQUAL_UINT64(1u, options.screenshotFrames[0]);
    TEST_ASSERT_EQUAL_UINT64(30u, options.screenshotFrames[1]);
    TEST_ASSERT_EQUAL_STRING("log.txt", options.presentLogPath.c_str());
    TEST_ASSERT_TRUE(options.bQuitAfterScript);
}

TEST(stdBabylonTour, KeepsTheArgumentsTheTourDoesNotOwn)
{
    std::vector<std::string> storage = { "Jones3D", "--load", "save.nds", "--input", "tour.txt" };
    std::vector<char*> argv;
    for ( std::string& arg : storage )
    {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    int argc = static_cast<int>(storage.size());

    StdBabylonTourOptions options;
    std::string error;
    TEST_ASSERT_TRUE(stdBabylonTour_TakeOptions(&argc, argv.data(), options, error));
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_STRING("Jones3D", argv[0]);
    TEST_ASSERT_EQUAL_STRING("--load", argv[1]);
    TEST_ASSERT_EQUAL_STRING("save.nds", argv[2]);
    TEST_ASSERT_NULL(argv[3]);
}

TEST(stdBabylonTour, RejectsEveryBadValueWithoutChangingTheOptions)
{
    const std::vector<std::vector<std::string>> badArguments = {
        { "Jones3D", "--input" },
        { "Jones3D", "--input", "" },
        { "Jones3D", "--screenshot-dir" },
        { "Jones3D", "--screenshot-at" },
        { "Jones3D", "--present-log" },
        { "Jones3D", "--present-log", "" },
        { "Jones3D", "--screenshot-dir", "shots" },
        { "Jones3D", "--screenshot-at", "1" },
        { "Jones3D", "--quit-after-script" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "0" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "-1" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "1," },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", ",1" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "1,,2" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "1,a" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "10000001" },
        { "Jones3D", "--screenshot-dir", "shots", "--screenshot-at", "999999999999999999999" },
    };

    for ( const std::vector<std::string>& arguments : badArguments )
    {
        StdBabylonTourOptions options;
        options.inputPath = "sentinel";
        std::string error;

        std::vector<std::string> storage = arguments;
        std::vector<char*> argv;
        for ( std::string& arg : storage )
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        int argc = static_cast<int>(storage.size());

        TEST_ASSERT_FALSE_MESSAGE(
            stdBabylonTour_TakeOptions(&argc, argv.data(), options, error), error.c_str());
        TEST_ASSERT_EQUAL_STRING("sentinel", options.inputPath.c_str());
        TEST_ASSERT_FALSE(error.empty());
    }
}

TEST(stdBabylonTour, ReadsTheScriptAndRejectsAMalformedOneWithItsLine)
{
    WriteScript("this is not a script line\n");

    StdBabylonTourOptions options;
    options.inputPath = kScriptPath;
    std::string error;
    TEST_ASSERT_FALSE(stdBabylonTour_Configure(options, 0, error));
    TEST_ASSERT_TRUE(error.find("stdBabylonTourTest_input.txt:1:") != std::string::npos);
}

TEST(stdBabylonTour, TheScriptReplacesRealInputForItsPresentCounts)
{
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(ConfigureScript(
        "frame 0: key W down\n"
        "frame 1: key A down\n", false, error), error.c_str());
    TEST_ASSERT_TRUE(stdBabylonTour_IsEnabled());
    TEST_ASSERT_TRUE(stdBabylonTour_InputActive());

    // A real key pressed now is replaced by the script's frame 0 event. The pump reports both the
    // queue event it took (and dropped) and the script event it injected.
    pPlatform->pushEvent(babylon::platform::KeyChanged{ 0, babylon::platform::Key::Q, true, false });
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(babylon::platform::Key::W == LastKey());
    TEST_ASSERT_TRUE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::W));
    TEST_ASSERT_FALSE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::Q));

    // The pump may run again at the same present; the frame's events are delivered only once.
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(stdBabylonEvents_LastEvents().empty());

    // The next present delivers frame 1.
    stdBabylonTour_OnPresent();
    TEST_ASSERT_EQUAL_UINT64(1u, stdBabylonTour_GetPresentCount());
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(babylon::platform::Key::A == LastKey());
    TEST_ASSERT_TRUE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::A));

    // Past the last script frame, the platform's own input is read again.
    stdBabylonTour_OnPresent();
    TEST_ASSERT_FALSE(stdBabylonTour_InputActive());
    pPlatform->pushEvent(babylon::platform::KeyChanged{ 0, babylon::platform::Key::Q, true, false });
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(babylon::platform::Key::Q == LastKey());
    TEST_ASSERT_TRUE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::Q));
}

TEST(stdBabylonTour, KeepsWindowEventsWhileTheScriptRuns)
{
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(ConfigureScript("frame 0: key W down\n", false, error), error.c_str());

    pPlatform->pushEvent(babylon::platform::WindowResized{
        0, babylon::platform::Size{ 800, 600 } });
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)stdBabylonEvents_Pump());

    bool bSawResize = false;
    for ( const babylon::platform::Event& event : stdBabylonEvents_LastEvents() )
    {
        if ( std::holds_alternative<babylon::platform::WindowResized>(event) )
        {
            bSawResize = true;
        }
    }
    TEST_ASSERT_TRUE(bSawResize);
}

TEST(stdBabylonTour, QuitAfterScriptFiresOnceAfterTheLastFrame)
{
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(ConfigureScript("frame 0: key W down\n", true, error), error.c_str());

    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_FALSE(stdBabylonEvents_TakeQuitRequest());

    // The frame after the script's last one ends the run.
    stdBabylonTour_OnPresent();
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(stdBabylonEvents_TakeQuitRequest());
    TEST_ASSERT_FALSE(stdBabylonEvents_TakeQuitRequest());
}

TEST(stdBabylonTour, AnEmptyScriptEndsAtOnceForQuitAfterScript)
{
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(ConfigureScript("# only a comment\n", true, error), error.c_str());
    TEST_ASSERT_FALSE(stdBabylonTour_InputActive());

    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)stdBabylonEvents_Pump());
    TEST_ASSERT_TRUE(stdBabylonEvents_TakeQuitRequest());
}

TEST(stdBabylonTour, LogsEveryPresentWithTheInnermostScreenTitle)
{
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(ConfigureScript("", false, error), error.c_str());

    stdBabylonTour_OnPresent();
    stdBabylonTour_PushScreenTitle("Settings");
    stdBabylonTour_OnPresent();
    stdBabylonTour_PushScreenTitle("Display");
    stdBabylonTour_OnPresent();
    stdBabylonTour_PopScreenTitle();
    stdBabylonTour_OnPresent();
    stdBabylonTour_PopScreenTitle();
    stdBabylonTour_OnPresent();

    stdBabylonTour_Shutdown();

    std::ifstream file(kLogPath, std::ios::binary);
    std::string line;
    const char* const aExpected[] = {
        "frame 1: game",
        "frame 2: Settings",
        "frame 3: Display",
        "frame 4: Settings",
        "frame 5: game",
    };
    for ( const char* pExpected : aExpected )
    {
        TEST_ASSERT_TRUE(std::getline(file, line));
        TEST_ASSERT_EQUAL_STRING(pExpected, line.c_str());
    }

    TEST_ASSERT_FALSE(std::getline(file, line));
}

TEST_GROUP_RUNNER(stdBabylonTour)
{
    RUN_TEST_CASE(stdBabylonTour, NoOptionsLeavesTheRunUntouched);
    RUN_TEST_CASE(stdBabylonTour, TakesEveryOptionOutOfTheCommandLine);
    RUN_TEST_CASE(stdBabylonTour, KeepsTheArgumentsTheTourDoesNotOwn);
    RUN_TEST_CASE(stdBabylonTour, RejectsEveryBadValueWithoutChangingTheOptions);
    RUN_TEST_CASE(stdBabylonTour, ReadsTheScriptAndRejectsAMalformedOneWithItsLine);
    RUN_TEST_CASE(stdBabylonTour, TheScriptReplacesRealInputForItsPresentCounts);
    RUN_TEST_CASE(stdBabylonTour, KeepsWindowEventsWhileTheScriptRuns);
    RUN_TEST_CASE(stdBabylonTour, QuitAfterScriptFiresOnceAfterTheLastFrame);
    RUN_TEST_CASE(stdBabylonTour, AnEmptyScriptEndsAtOnceForQuitAfterScript);
    RUN_TEST_CASE(stdBabylonTour, LogsEveryPresentWithTheInnermostScreenTitle);
}
