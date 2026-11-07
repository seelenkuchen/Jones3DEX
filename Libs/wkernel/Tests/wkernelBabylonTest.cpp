// Added: Unity Fixture suite for the Babylon column's platform side of wkernel
// (Libs/wkernel/wkernelBabylon.cpp). There is no test executable of the Babylon column that
// already links wkernel (Jones3D/Main/Tests is not built in the native columns), so this small
// suite covers wkernel_PeekProcessEvents' activation-callback order and its WM_QUIT answer,
// wkernel_ProcessEvents' first-event wake-up and wkernel_SetWindowSize through a fresh
// StdBabylonContext whose platform is a babylon::platform::NullPlatform with one created window
// as pWindow. The pump behind it is the real stdBabylonEvents.cpp, compiled into this executable
// with J3D_TEST so its ResetForTest is available.
// Adapter test written in C++; the C runner reaches the group through the extern "C" entry.
#include <unity_fixture.h>

#include <memory>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

#include <wkernel/wkernel.h>

TEST_GROUP(wkernelBabylon);

// The linkage the C runner's declaration expects; the TEST_GROUP_RUNNER definition below
// inherits it from this declaration.
extern "C" void TEST_wkernelBabylon_GROUP_RUNNER(void);

namespace
{
    babylon::time::NullClock*        pClock        = nullptr;
    babylon::time::NullClock*        pVirtualClock = nullptr;
    babylon::platform::NullPlatform* pPlatform     = nullptr;
    std::unique_ptr<babylon::platform::Window> pWindow;
    // The context contract wants a registry; wkernel reads no cvar, so this one stays empty.
    babylon::cvars::Registry       cvars;
    StdBabylonContext                context;
    StdBabylonContext*               pSavedContext = nullptr;

    // The activations wkernel handed to the callback, oldest first.
    constexpr int kActivateLogMax = 16;
    int aActivateLog[kActivateLogMax];
    int numActivateLog = 0;
}

// The intro skip callback under test: counts the calls.
namespace
{
    int numIntroSkips = 0;
}

extern "C" void RecordIntroSkip(void)
{
    ++numIntroSkips;
}

// The activation callback under test: it records what the game window's focus changes arrive as.
extern "C" void J3DAPI RecordActivate(int bActive)
{
    if ( numActivateLog < kActivateLogMax )
    {
        aActivateLog[numActivateLog] = bActive;
    }

    ++numActivateLog;
}

TEST_SETUP(wkernelBabylon)
{
    pSavedContext = stdBabylon_GetContext();

    pClock        = new babylon::time::NullClock();
    pVirtualClock = new babylon::time::NullClock();
    pPlatform     = new babylon::platform::NullPlatform();
    pWindow       = pPlatform->createWindow({ "wkernel test", { 640, 480 }, false });
    TEST_ASSERT_NOT_NULL(pWindow.get());

    context.pClock          = pClock;
    context.pVirtualClock   = pVirtualClock;
    context.pFileSystem     = nullptr;
    context.pPlatform       = pPlatform;
    context.pScriptPlatform = nullptr;
    context.pWindow         = pWindow.get();
    context.pCvars          = &cvars;
    stdBabylon_SetContext(&context);

    numActivateLog = 0;
    numIntroSkips  = 0;
    stdBabylonEvents_ResetForTest();
    TEST_ASSERT_TRUE(wkernel_SetActivateCallback(RecordActivate) == nullptr);
}

TEST_TEAR_DOWN(wkernelBabylon)
{
    wkernel_SetActivateCallback(nullptr);
    wkernel_SetIntroSkipCallback(nullptr);
    stdBabylonEvents_ResetForTest();
    stdBabylon_SetContext(pSavedContext);

    // The window dies before the platform that made it, like the interface requires.
    pWindow.reset();
    delete pPlatform;
    delete pVirtualClock;
    delete pClock;
    pPlatform     = nullptr;
    pVirtualClock = nullptr;
    pClock        = nullptr;
    pSavedContext = nullptr;
}

TEST(wkernelBabylon, PeekProcessEventsDeliversFocusChangesOldestFirstAndAnswersQuit)
{
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), true });
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id() + 1, false });
    pPlatform->pushEvent(babylon::platform::KeyChanged{ pWindow->id(), babylon::platform::Key::W, true, false });
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), false });
    pPlatform->pushEvent(babylon::platform::QuitRequested{});

    TEST_ASSERT_EQUAL_INT(1, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(2, numActivateLog);
    TEST_ASSERT_EQUAL_INT(1, aActivateLog[0]);
    TEST_ASSERT_EQUAL_INT(0, aActivateLog[1]);

    // The quit answer is the WM_QUIT taken from the queue: the next peek says no, and the
    // focus changes were delivered once.
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(2, numActivateLog);
}

TEST(wkernelBabylon, GameWindowCloseAnswersQuitAndDeliversNoFocus)
{
    pPlatform->pushEvent(babylon::platform::WindowCloseRequested{ pWindow->id() });

    TEST_ASSERT_EQUAL_INT(1, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(0, numActivateLog);
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
}

TEST(wkernelBabylon, OtherWindowCloseAndInputAnswerNoQuit)
{
    pPlatform->pushEvent(babylon::platform::WindowCloseRequested{ pWindow->id() + 1 });
    pPlatform->pushEvent(babylon::platform::KeyChanged{ pWindow->id(), babylon::platform::Key::Escape, true, false });

    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(0, numActivateLog);
}

TEST(wkernelBabylon, ProcessEventsWakesOnTheFirstEvent)
{
    // The deactivated game's blocking pump: it answers on the first event taken, delivers its
    // focus change and keeps the quit answer for the event that carries one.
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), false });

    TEST_ASSERT_EQUAL_INT(0, wkernel_ProcessEvents());
    TEST_ASSERT_EQUAL_INT(1, numActivateLog);
    TEST_ASSERT_EQUAL_INT(0, aActivateLog[0]);

    pPlatform->pushEvent(babylon::platform::QuitRequested{});
    TEST_ASSERT_EQUAL_INT(1, wkernel_ProcessEvents());
}

TEST(wkernelBabylon, ProcessEventsDeliversFocusLossThenRegainInOrder)
{
    // The deactivated game blocks in wkernel_ProcessEvents; each focus event wakes it once, in the
    // order the window system produced them, so JonesMain_OnAppActivate sees the loss and then the
    // regain the way the Win32 WM_ACTIVATE handling delivered them.
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_ProcessEvents());
    TEST_ASSERT_EQUAL_INT(1, numActivateLog);
    TEST_ASSERT_EQUAL_INT(0, aActivateLog[0]);

    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), true });
    TEST_ASSERT_EQUAL_INT(0, wkernel_ProcessEvents());
    TEST_ASSERT_EQUAL_INT(2, numActivateLog);
    TEST_ASSERT_EQUAL_INT(1, aActivateLog[1]);

    // The focus loss released every held key through the pump, so no input sticks while another
    // window has focus.
    pPlatform->pushEvent(babylon::platform::KeyChanged{ pWindow->id(), babylon::platform::Key::W,
        true, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_TRUE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::W));
    pPlatform->pushEvent(babylon::platform::WindowFocusChanged{ pWindow->id(), false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_FALSE(stdBabylonEvents_InputState().keyDown(babylon::platform::Key::W));
}

TEST(wkernelBabylon, SetActivateCallbackReturnsThePreviousOne)
{
    // The same answer pattern as wkernel_SetProcessProc: the setter hands back what it replaced.
    TEST_ASSERT_TRUE(wkernel_SetActivateCallback(nullptr) == RecordActivate);
    TEST_ASSERT_TRUE(wkernel_SetActivateCallback(RecordActivate) == nullptr);
}

TEST(wkernelBabylon, SetWindowSizeResizesTheGameWindowAndAnswersSuccess)
{
    TEST_ASSERT_EQUAL_INT(1, wkernel_SetWindowSize(800, 600));

    const babylon::platform::Size size = pWindow->size();
    TEST_ASSERT_EQUAL_INT(800, size.width);
    TEST_ASSERT_EQUAL_INT(600, size.height);
}

TEST(wkernelBabylon, WindowStylesDriveTheGameWindowFullscreenMode)
{
    // The Win32 styles set WS_OVERLAPPEDWINDOW and WS_POPUP; the Babylon answer is the window's
    // windowed/fullscreen mode. The two calls are idempotent and leave the mode as asked.
    TEST_ASSERT_FALSE(pWindow->fullscreen());

    wkernel_SetFullscreenStyle();
    TEST_ASSERT_TRUE(pWindow->fullscreen());

    wkernel_SetFullscreenStyle();
    TEST_ASSERT_TRUE(pWindow->fullscreen());

    wkernel_SetWindowedStyle();
    TEST_ASSERT_FALSE(pWindow->fullscreen());

    wkernel_SetWindowedStyle();
    TEST_ASSERT_FALSE(pWindow->fullscreen());
}

TEST(wkernelBabylon, IntroSkipCallbackTakesTheIntroWindowProcSkipKeys)
{
    using babylon::platform::Key;
    using babylon::platform::KeyChanged;
    using babylon::platform::MouseButton;
    using babylon::platform::MouseButtonChanged;
    const babylon::platform::WindowId id = pWindow->id();

    // Not installed: a skip key calls nothing
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, true, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, false, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(0, numIntroSkips);

    TEST_ASSERT_TRUE(wkernel_SetIntroSkipCallback(RecordIntroSkip) == nullptr);

    // Other keys, a held key's repeat, a key release and a mouse button press don't skip; neither does an empty pump
    pPlatform->pushEvent(KeyChanged{ id, Key::Space, true, false });
    pPlatform->pushEvent(KeyChanged{ id, Key::F4, true, false });
    pPlatform->pushEvent(MouseButtonChanged{ id, MouseButton::Left, true });
    pPlatform->pushEvent(MouseButtonChanged{ id, MouseButton::Right, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(0, numIntroSkips);

    // The left button's release skips, once per pump
    pPlatform->pushEvent(MouseButtonChanged{ id, MouseButton::Left, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(1, numIntroSkips);
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(1, numIntroSkips);

    // Escape, Enter and keypad Enter presses skip; a pump with two of them calls once
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, true, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(2, numIntroSkips);
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, true, true }); // the repeat of the held key
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, false, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(2, numIntroSkips);
    pPlatform->pushEvent(KeyChanged{ id, Key::Enter, true, false });
    pPlatform->pushEvent(KeyChanged{ id, Key::KeypadEnter, true, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(3, numIntroSkips);

    // A skip and a quit in one pump: both answered
    pPlatform->pushEvent(KeyChanged{ id, Key::Enter, false, false });
    pPlatform->pushEvent(KeyChanged{ id, Key::Enter, true, false });
    pPlatform->pushEvent(babylon::platform::QuitRequested{});
    TEST_ASSERT_EQUAL_INT(1, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(4, numIntroSkips);

    // Removed again: the setter hands back what it replaced, and nothing is called
    TEST_ASSERT_TRUE(wkernel_SetIntroSkipCallback(nullptr) == RecordIntroSkip);
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, false, false });
    pPlatform->pushEvent(KeyChanged{ id, Key::Escape, true, false });
    TEST_ASSERT_EQUAL_INT(0, wkernel_PeekProcessEvents());
    TEST_ASSERT_EQUAL_INT(4, numIntroSkips);
}

TEST_GROUP_RUNNER(wkernelBabylon)
{
    RUN_TEST_CASE(wkernelBabylon, PeekProcessEventsDeliversFocusChangesOldestFirstAndAnswersQuit);
    RUN_TEST_CASE(wkernelBabylon, GameWindowCloseAnswersQuitAndDeliversNoFocus);
    RUN_TEST_CASE(wkernelBabylon, OtherWindowCloseAndInputAnswerNoQuit);
    RUN_TEST_CASE(wkernelBabylon, ProcessEventsWakesOnTheFirstEvent);
    RUN_TEST_CASE(wkernelBabylon, ProcessEventsDeliversFocusLossThenRegainInOrder);
    RUN_TEST_CASE(wkernelBabylon, SetActivateCallbackReturnsThePreviousOne);
    RUN_TEST_CASE(wkernelBabylon, SetWindowSizeResizesTheGameWindowAndAnswersSuccess);
    RUN_TEST_CASE(wkernelBabylon, WindowStylesDriveTheGameWindowFullscreenMode);
    RUN_TEST_CASE(wkernelBabylon, IntroSkipCallbackTakesTheIntroWindowProcSkipKeys);
}
