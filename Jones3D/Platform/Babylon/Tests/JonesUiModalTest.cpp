// Added: the logic tests of the Babylon column's modal screen loop (ADR-0026). They drive
// JonesUiModal_Run with a Null painter and a Null platform whose event queue the cases script, so
// no window, display or game asset is needed. The GPU drawing case lives in JonesUiPainterTest.cpp,
// next to its synthetic font.
#include <doctest.h>

#include <memory>
#include <optional>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Play/jonesInventory.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

// The wrapper's string lookup, stubbed so this executable need not link the game layer. The
// string-key cases fall back to the readable text JonesUiModalMessage.cpp supplies.
extern "C" const char* J3DAPI jonesString_GetString(const char* pKey)
{
    return pKey;
}

// The game-layer and Null-platform symbols the public screen wrappers name. The cases call the
// explicit-directory runners, so only the linker needs these.
extern "C" int J3DAPI JonesMain_HasStarted(void)
{
    return 0;
}

// Added: a settable pending --load path, so the main menu test can exercise the direct load. The
// real Null helper lives in Platform/Null and is not linked into this test executable.
static const char* g_testLoadPath = NULL;

extern "C" void JonesPlatformNull_SetLoadPath(const char* pPath)
{
    g_testLoadPath = pPath;
}

extern "C" size_t JonesMain_GetCurrentLevelNum(void)
{
    return 0;
}

extern "C" int J3DAPI jonesInventory_GetTotalIQPoints(void)
{
    return 0;
}

extern "C" const char* JonesPlatformNull_GetLoadPath(void)
{
    return g_testLoadPath;
}

extern "C" void JonesPlatformNull_ClearLoadPath(void)
{
    g_testLoadPath = NULL;
}

namespace
{
    using namespace babylon;

    // A context with the Null platform and clocks, installed for the life of one case.
    struct TestContext
    {
        time::NullClock clock;
        time::NullClock virtualClock;
        platform::NullPlatform platform;
        cvars::Registry cvars;
        StdBabylonContext context{};

        TestContext()
        {
            context.pClock          = &clock;
            context.pVirtualClock   = &virtualClock;
            context.pFileSystem     = nullptr;
            context.pPlatform       = &platform;
            context.pScriptPlatform = nullptr;
            context.pWindow         = nullptr;
            context.pCvars          = &cvars;
            context.pRenderDevice   = nullptr;

            stdBabylon_SetContext(&context);
            // A previous case may have left a quit request recorded; the loop would see it.
            if ( stdBabylonEvents_PeekQuitRequest() )
            {
                static_cast<void>(stdBabylonEvents_TakeQuitRequest());
            }
        }

        ~TestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    ui::Screen MakeMessageScreen()
    {
        ui::Screen screen;
        screen.title   = "Message";
        screen.widgets = {
            ui::Label{ "The level file is missing.", "" },
            ui::Button{ "OK", 1, true },
        };
        screen.backResult = 2;
        return screen;
    }

    int Identity(babylon::ui::ResultCode code)
    {
        return (int)code;
    }
}

TEST_CASE("the modal loop maps OK and back")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);

    // Enter accepts the focused OK button, whose action id is the dialog's IDOK.
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Enter, true, false });
    CHECK(JonesUiModal_Run(ui, &painter, MakeMessageScreen(), Identity) == 1);
    CHECK(ui.screenCount() == 0u);

    // Escape is back, whose result is the screen's back result (IDCANCEL).
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Escape, true, false });
    CHECK(JonesUiModal_Run(ui, &painter, MakeMessageScreen(), Identity) == 2);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("the modal loop applies a window resize to the viewport")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);

    testContext.platform.pushEvent(platform::WindowResized{ 1, platform::Size{ 800, 600 } });
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Escape, true, false });

    CHECK(JonesUiModal_Run(ui, &painter, MakeMessageScreen(), Identity) == 2);
    // The canvas is 480 high and as wide as the 800x600 window's aspect: 640.
    CHECK(ui.canvasSize() == ui::Size{ 640.0f, 480.0f });
}

TEST_CASE("the modal loop closes on a window close and leaves the request")
{
    TestContext testContext;
    std::unique_ptr<platform::Window> pWindow =
        testContext.platform.createWindow({ "modal test", { 640, 480 }, false });
    REQUIRE(pWindow);
    testContext.context.pWindow = pWindow.get();

    ui::NullPainter painter;
    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);

    testContext.platform.pushEvent(platform::WindowCloseRequested{ pWindow->id() });

    CHECK(JonesUiModal_Run(ui, &painter, MakeMessageScreen(), Identity) == 2);
    CHECK(ui.screenCount() == 0u);

    // The request is still flagged for the game loop, which turns it into its quit.
    CHECK(stdBabylonEvents_PeekQuitRequest());
    CHECK(stdBabylonEvents_TakeQuitRequest());
    CHECK_FALSE(stdBabylonEvents_PeekQuitRequest());
}

TEST_CASE("a nested modal returns to the outer screen")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);

    ui::Screen outer;
    outer.title   = "Outer";
    outer.widgets = { ui::Label{ "outer", "" } };
    const ui::ScreenId outerId = ui.push(outer);
    REQUIRE(outerId != ui::kInvalidScreen);

    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Escape, true, false });
    CHECK(JonesUiModal_Run(ui, &painter, MakeMessageScreen(), Identity) == 2);

    // The inner screen closed; the outer one is still the top screen and keeps its state.
    CHECK(ui.screenCount() == 1u);
    CHECK(ui.topScreen() == outerId);
    CHECK(ui.isOpen(outerId));
}

TEST_CASE("the message dialogs answer the Null value headless")
{
    TestContext testContext;

    // No window: the wrapper answers the Null stub's cancel value and never enters the loop.
    CHECK(jonesConfig_ShowMessageDialog(NULL, "Title", "Text", 0) == 2);
    CHECK(jonesConfig_ShowDialogInsertCD(NULL, 2) == 2);
    CHECK(jonesConfig_ShowDialogInsertCD(NULL, 1) == 2);
}
