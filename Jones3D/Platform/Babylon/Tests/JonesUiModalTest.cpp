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

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Play/jonesInventory.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

#include <cstring>
#include <string>
#include <vector>

// The wrapper's string lookup, stubbed so this executable need not link the game layer. The
// string-key cases fall back to the readable text JonesUiModalMessage.cpp supplies. A case may
// inject one table entry to exercise lookup behavior.
namespace
{
    const char* g_pTestStringKey   = nullptr;
    const char* g_pTestStringValue = nullptr;
}

extern "C" void JonesUiTest_SetString(const char* pKey, const char* pValue)
{
    g_pTestStringKey   = pKey;
    g_pTestStringValue = pValue;
}

extern "C" const char* J3DAPI jonesString_GetString(const char* pKey)
{
    if ( g_pTestStringKey && std::strcmp(pKey, g_pTestStringKey) == 0 )
    {
        return g_pTestStringValue;
    }

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

extern "C" const char* JonesPlatformNull_GetLoadPath(void)
{
    return g_testLoadPath;
}

extern "C" void JonesPlatformNull_ClearLoadPath(void)
{
    g_testLoadPath = NULL;
}

// Added: the HUD sound cue the modal loop forwarded, standing in for the real JonesHud.c that this
// executable does not link. The real function plays the cue through sithSoundMixer_PlaySound.
namespace
{
    constexpr int kMaxRecordedMenuCues = 8;
    int g_aMenuSoundCues[kMaxRecordedMenuCues] = { 0 };
    int g_numMenuSoundCues = 0;
}

extern "C" void J3DAPI JonesHud_PlayMenuSoundCue(int cueIdx)
{
    if ( g_numMenuSoundCues < kMaxRecordedMenuCues )
    {
        g_aMenuSoundCues[g_numMenuSoundCues] = cueIdx;
    }
    ++g_numMenuSoundCues;
}

extern "C" void JonesHud_InitializeMenuSounds(void)
{
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

    std::vector<std::string> PromptLabels(const ui::Ui& ui)
    {
        std::vector<std::string> labels;
        for ( const ui::Prompt& prompt : ui.prompts() )
        {
            labels.push_back(prompt.label);
        }
        return labels;
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

TEST_CASE("the Jones skin uses the HUD item menu's sound cues")
{
    const ui::Skin skin = JonesUiSkin_Create(0, nullptr);

    // Focus is slot 2 (inv_move.wav); accepting and going back are slot 3 (inv_expand.wav), the
    // sounds the HUD item menu plays when it opens, selects and closes.
    CHECK(kJonesUiSoundCueFocus == 2);
    CHECK(kJonesUiSoundCueAccept == 3);
    CHECK(kJonesUiSoundCueBack == 3);
    CHECK(skin.focusSound == kJonesUiSoundCueFocus);
    CHECK(skin.acceptSound == kJonesUiSoundCueAccept);
    CHECK(skin.backSound == kJonesUiSoundCueBack);
    // With no font the default metrics are kept.
    CHECK(skin.rowHeight == 24.0f);
    CHECK(skin.titleHeight == 32.0f);
}

TEST_CASE("the modal loop forwards the skin's focus and back cues to the HUD")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(JonesUiSkin_Create(0, &painter)));

    ui::Screen screen;
    screen.title   = "Cues";
    screen.widgets = {
        ui::Button{ "One", 1, true },
        ui::Button{ "Two", 2, true },
    };
    screen.backResult = 2;

    // Moving to the second button and accepting it: focus then accept.
    g_numMenuSoundCues = 0;
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Down, true, false });
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Enter, true, false });

    CHECK(JonesUiModal_Run(ui, &painter, screen, Identity) == 2);
    REQUIRE(g_numMenuSoundCues == 2);
    CHECK(g_aMenuSoundCues[0] == (int)kJonesUiSoundCueFocus);
    CHECK(g_aMenuSoundCues[1] == (int)kJonesUiSoundCueAccept);

    // A fresh screen closed with back reports the back cue.
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Down, false, false });
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Enter, false, false });
    testContext.platform.pushEvent(platform::KeyChanged{ 1, platform::Key::Escape, true, false });

    g_numMenuSoundCues = 0;
    CHECK(JonesUiModal_Run(ui, &painter, screen, Identity) == 2);
    REQUIRE(g_numMenuSoundCues == 1);
    CHECK(g_aMenuSoundCues[0] == (int)kJonesUiSoundCueBack);
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

TEST_CASE("the string resolver strips the string table's dialog mnemonics")
{
    JonesUiTest_SetString("JONES_STR_TEST_MNEMONIC", "&Load Game && &More");
    CHECK(JonesUiContext_ResolveString("JONES_STR_TEST_MNEMONIC") == "Load Game & More");
    JonesUiTest_SetString(nullptr, nullptr);

    // Text without a table entry passes through unchanged.
    CHECK(JonesUiContext_ResolveString("Back") == "Back");
}

TEST_CASE("the message dialogs answer the Null value headless")
{
    TestContext testContext;

    // No window: the wrapper answers the Null stub's cancel value and never enters the loop.
    CHECK(jonesConfig_ShowMessageDialog(NULL, "Title", "Text", 0) == 2);
    CHECK(jonesConfig_ShowDialogInsertCD(NULL, 2) == 2);
    CHECK(jonesConfig_ShowDialogInsertCD(NULL, 1) == 2);
}

TEST_CASE("the modal loop sets Select and Back for a plain menu")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(JonesUiSkin_Create(0, &painter)));

    ui::Screen screen;
    screen.title   = "Plain";
    screen.widgets = {
        ui::Button{ "One", 1, true },
        ui::Button{ "Two", 2, true },
    };
    screen.backResult = 2;

    std::vector<std::string> labels;
    const JonesUiModalEventHandler handler = [&](ui::Ui& uiRef, ui::ScreenId,
        const ui::UiEvent&) -> bool
    {
        if ( labels.empty() )
        {
            labels = PromptLabels(uiRef);
            // Close on the next frame so the loop has already filled the bar.
            testContext.platform.pushEvent(
                platform::KeyChanged{ 1, platform::Key::Escape, true, false });
        }
        return false;
    };

    testContext.platform.pushEvent(
        platform::KeyChanged{ 1, platform::Key::Down, true, false });
    CHECK(JonesUiModal_RunHandled(ui, &painter, screen, Identity, handler) == 2);
    CHECK(labels == std::vector<std::string>{ "Select", "Back" });
}

TEST_CASE("the modal loop adds Change for a focused Choice row")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(JonesUiSkin_Create(0, &painter)));

    ui::Screen screen;
    screen.title   = "Choice";
    screen.widgets = {
        ui::Button{ "One", 1, true },
        ui::Choice{ "Mode", "", { "First", "Second" }, true, 0 },
    };
    screen.backResult   = 2;
    screen.initialFocus = 1;

    std::vector<std::string> labels;
    const JonesUiModalEventHandler handler = [&](ui::Ui& uiRef, ui::ScreenId,
        const ui::UiEvent&) -> bool
    {
        if ( labels.empty() )
        {
            labels = PromptLabels(uiRef);
            testContext.platform.pushEvent(
                platform::KeyChanged{ 1, platform::Key::Escape, true, false });
        }
        return false;
    };

    // Right on the Choice changes its value and reports an event, so the
    // handler runs with the Change prompt already in the bar.
    testContext.platform.pushEvent(
        platform::KeyChanged{ 1, platform::Key::Right, true, false });
    CHECK(JonesUiModal_RunHandled(ui, &painter, screen, Identity, handler) == 2);
    CHECK(labels == std::vector<std::string>{ "Select", "Back", "Change" });
}

TEST_CASE("the modal loop adds Tabs for a tabbed screen")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(JonesUiSkin_Create(0, &painter)));

    ui::Screen screen;
    screen.title   = "Tabbed";
    screen.tabs    = { "Gameplay", "Sound" };
    screen.widgets = {
        ui::Button{ "One", 1, true },
        ui::Button{ "Two", 2, true },
    };
    screen.backResult = 2;

    std::vector<std::string> labels;
    const JonesUiModalEventHandler handler = [&](ui::Ui& uiRef, ui::ScreenId,
        const ui::UiEvent&) -> bool
    {
        if ( labels.empty() )
        {
            labels = PromptLabels(uiRef);
            testContext.platform.pushEvent(
                platform::KeyChanged{ 1, platform::Key::Escape, true, false });
        }
        return false;
    };

    testContext.platform.pushEvent(
        platform::KeyChanged{ 1, platform::Key::Down, true, false });
    CHECK(JonesUiModal_RunHandled(ui, &painter, screen, Identity, handler) == 2);
    CHECK(labels == std::vector<std::string>{ "Select", "Back", "Tabs" });
}

TEST_CASE("the modal loop omits Back when the screen has no back result")
{
    TestContext testContext;
    ui::NullPainter painter;

    cvars::Registry cvars;
    ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(JonesUiSkin_Create(0, &painter)));

    ui::Screen screen;
    screen.title   = "No Back";
    screen.widgets = {
        ui::Button{ "One", 1, true },
        ui::Button{ "Two", 2, true },
    };
    screen.backResult = 0;

    std::vector<std::string> labels;
    const JonesUiModalEventHandler handler = [&](ui::Ui& uiRef, ui::ScreenId,
        const ui::UiEvent&) -> bool
    {
        if ( labels.empty() )
        {
            labels = PromptLabels(uiRef);
            testContext.platform.pushEvent(
                platform::KeyChanged{ 1, platform::Key::Escape, true, false });
        }
        return false;
    };

    testContext.platform.pushEvent(
        platform::KeyChanged{ 1, platform::Key::Down, true, false });
    CHECK(JonesUiModal_RunHandled(ui, &painter, screen, Identity, handler) == 0);
    CHECK(labels == std::vector<std::string>{ "Select" });
}
