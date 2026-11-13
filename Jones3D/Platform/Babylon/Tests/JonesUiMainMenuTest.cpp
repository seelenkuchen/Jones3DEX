// Added: the logic tests of the Babylon column's main menu and start-mode mapping (ADR-0026).
// They drive the runner with a Null painter and a scripted platform, so every menu action and the
// nested Settings screen run without a window, display or game asset. The drawing sample lives in
// JonesUiPainterTest.cpp.
#include <doctest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/JonesMainSys.h>
#include <Jones3D/Platform/Babylon/JonesBabylon.h>
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/types.h>

#include <rdroid/Primitives/rdWallpaper.h>

#include <sith/Main/sithMain.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>
#include <std/General/stdUtil.h>

#include <wkernel/wkernel.h>

// The game-layer and platform symbols the main menu wrapper names. The cases call the runner with
// hooks, so these stubs only satisfy the linker of jonesConfig_ShowMainMenu.
extern "C" StdDisplayEnvironment* JonesMain_GetDisplayEnvironment(void)
{
    return NULL;
}

extern "C" const JonesLevelInfo* J3DAPI JonesMain_GetMainMenuBackgroundLevel(void)
{
    return NULL;
}

extern "C" void JonesPlatformNull_SetLoadPath(const char* pPath);
extern "C" void JonesPlatformNull_ClearLoadPath(void);

namespace
{
    using namespace babylon;

    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
    }

    tHostServices g_hostServices;

    // The modal loop logs its background state; a case that runs alone needs the print hooks.
    void SetupHostServices()
    {
        std::memset(&g_hostServices, 0, sizeof(g_hostServices));
        g_hostServices.pStatusPrint  = TestPrint;
        g_hostServices.pMessagePrint = TestPrint;
        g_hostServices.pWarningPrint = TestPrint;
        g_hostServices.pErrorPrint   = TestPrint;
        g_hostServices.pDebugPrint   = TestPrint;
        g_hostServices.pMalloc       = malloc;
        g_hostServices.pFree         = free;
        g_hostServices.pRealloc      = realloc;

        std_g_pHS  = &g_hostServices;
        sith_g_pHS = &g_hostServices;
    }

    // Delivers one event per modal frame, like JonesUiSaveLoadTest's platform. If a screen never
    // closes, a quit request ends it with its back result instead of hanging the suite.
    class ScriptedPlatform final : public platform::Platform
    {
    public:
        std::unique_ptr<platform::Window> createWindow(const platform::WindowDesc&) override
        {
            return nullptr;
        }

        std::optional<platform::Event> pollEvent() override
        {
            if ( m_delivering )
            {
                m_delivering = false;
                return std::nullopt;
            }

            if ( m_events.empty() )
            {
                if ( ++m_emptyPolls > 200 )
                {
                    m_emptyPolls = 0;
                    ++m_quitFallbacks;
                    return platform::Event{ platform::QuitRequested{} };
                }

                return std::nullopt;
            }

            m_emptyPolls = 0;
            m_delivering = true;
            platform::Event event = m_events.front();
            m_events.pop_front();
            return event;
        }

        bool showMessageBox(std::string_view title, std::string_view text) override
        {
            m_messageBoxTitles.emplace_back(title);
            m_messageBoxTexts.emplace_back(text);
            return true;
        }

        size_t MessageBoxCount() const
        {
            return m_messageBoxTitles.size();
        }

        const std::string& LastMessageBoxTitle() const
        {
            return m_messageBoxTitles.back();
        }

        const std::string& LastMessageBoxText() const
        {
            return m_messageBoxTexts.back();
        }

        bool rumbleGamepad(platform::GamepadId, float, float, std::uint32_t) override
        {
            return false;
        }

        void PushKey(platform::Key key)
        {
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, true, false } });
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, false, false } });
        }

        int QuitFallbacks() const
        {
            return m_quitFallbacks;
        }

    private:
        std::deque<platform::Event> m_events;
        std::vector<std::string> m_messageBoxTitles;
        std::vector<std::string> m_messageBoxTexts;
        bool m_delivering = false;
        int m_emptyPolls  = 0;
        int m_quitFallbacks = 0;
    };

    struct MenuTestContext
    {
        ScriptedPlatform platform;
        time::NullClock clock;
        time::NullClock virtualClock;
        cvars::Registry cvars;
        StdBabylonContext context{};

        MenuTestContext()
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
            if ( stdBabylonEvents_PeekQuitRequest() )
            {
                static_cast<void>(stdBabylonEvents_TakeQuitRequest());
            }

            SetupHostServices();
        }

        ~MenuTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    struct StartModeTestContext
    {
        platform::NullPlatform platform;
        time::NullClock clock;
        time::NullClock virtualClock;
        cvars::Registry cvars;
        StdBabylonContext context{};

        StartModeTestContext()
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
            SetupHostServices();
        }

        ~StartModeTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };
}

TEST_CASE("the windowed build opens the main menu for a missing or developer start mode")
{
    StartModeTestContext testContext;
    std::unique_ptr<platform::Window> pWindow =
        testContext.platform.createWindow({ "menu test", { 640, 480 }, false });
    REQUIRE(pWindow);
    testContext.context.pWindow = pWindow.get();

    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DEVELOPERDIALOG, 1)
        == JONES_STARTMODE_MAINMENU);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DEVELOPERDIALOG, 0)
        == JONES_STARTMODE_MAINMENU);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_STARTGAME, 1)
        == JONES_STARTMODE_STARTGAME);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_LOADGAME, 1)
        == JONES_STARTMODE_LOADGAME);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_SOUNDSETTINGS, 1)
        == JONES_STARTMODE_SOUNDSETTINGS);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DISPLAYSETTINGS, 1)
        == JONES_STARTMODE_DISPLAYSETTINGS);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_MAINMENU, 1)
        == JONES_STARTMODE_MAINMENU);

    // A --load path still loads that save directly.
    JonesPlatformNull_SetLoadPath("save.nds");
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_STARTGAME, 0)
        == JONES_STARTMODE_LOADGAME);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DEVELOPERDIALOG, 1)
        == JONES_STARTMODE_LOADGAME);
    JonesPlatformNull_ClearLoadPath();
}

TEST_CASE("the windowed build defers the Load and settings start-mode screens")
{
    StartModeTestContext testContext;
    std::unique_ptr<platform::Window> pWindow =
        testContext.platform.createWindow({ "defer test", { 640, 480 }, false });
    REQUIRE(pWindow);
    testContext.context.pWindow = pWindow.get();

    CHECK(jonesConfig_DefersStartMode(JONES_STARTMODE_LOADGAME));
    CHECK(jonesConfig_DefersStartMode(JONES_STARTMODE_SOUNDSETTINGS));
    CHECK(jonesConfig_DefersStartMode(JONES_STARTMODE_DISPLAYSETTINGS));
    CHECK_FALSE(jonesConfig_DefersStartMode(JONES_STARTMODE_STARTGAME));
    CHECK_FALSE(jonesConfig_DefersStartMode(JONES_STARTMODE_DEVELOPERDIALOG));
    CHECK_FALSE(jonesConfig_DefersStartMode(JONES_STARTMODE_MAINMENU));
}

TEST_CASE("the headless column keeps the start-mode screens where they are")
{
    StartModeTestContext testContext;

    for ( int startMode = JONES_STARTMODE_STARTGAME; startMode <= JONES_STARTMODE_MAINMENU;
        ++startMode )
    {
        CHECK_FALSE(jonesConfig_DefersStartMode(startMode));
    }
}

TEST_CASE("the headless start mode keeps its configured value")
{
    StartModeTestContext testContext;

    // No window: the Babylon headless run behaves like the Null column.
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DEVELOPERDIALOG, 0)
        == JONES_STARTMODE_DEVELOPERDIALOG);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_DEVELOPERDIALOG, 1)
        == JONES_STARTMODE_DEVELOPERDIALOG);
    CHECK(jonesConfig_ResolveStartMode(JONES_STARTMODE_STARTGAME, 1)
        == JONES_STARTMODE_STARTGAME);
}

TEST_CASE("the Babylon close message makes the next loop check report quit")
{
    StartModeTestContext testContext;
    while ( stdBabylonEvents_TakeQuitRequest() )
    {
    }

    // The Null stub returned 1 without ending the game; the Babylon version records the same
    // quit request a window close does, which is the WM_QUIT the game loop ends on.
    CHECK(JonesMainSys_PostCloseMessage() == 1);
    CHECK(wkernel_PeekProcessEvents() == 1);
    CHECK(wkernel_PeekProcessEvents() == 0);
}

TEST_CASE("a main-menu quit during startup exits cleanly")
{
    StartModeTestContext testContext;

    // The main menu reports its own result, so the windowed entry exits 0 without an error.
    CHECK(JONESMAIN_STARTUP_USERQUIT != 0);
    CHECK(JONESMAIN_STARTUP_USERQUIT != 1);
    CHECK(JonesBabylon_StartupExitCode(JONESMAIN_STARTUP_USERQUIT) == 0);

    // A real startup failure keeps the recovered nonzero result and stays an error.
    CHECK(JonesBabylon_StartupExitCode(1) == 1);

    // A successful startup is 0.
    CHECK(JonesBabylon_StartupExitCode(0) == 0);
}

TEST_CASE("a fatal message shows the windowed platform message box")
{
    MenuTestContext testContext;
    platform::NullPlatform windowPlatform;
    std::unique_ptr<platform::Window> pWindow =
        windowPlatform.createWindow({ "fatal test", { 640, 480 }, false });
    REQUIRE(pWindow);
    testContext.context.pWindow = pWindow.get();

    CHECK(JonesMainSys_ShowFatalMessage("Out of memory", "Jones3D", true) == 1);
    CHECK(testContext.platform.MessageBoxCount() == 1u);
    CHECK(testContext.platform.LastMessageBoxTitle() == "Jones3D");
    CHECK(testContext.platform.LastMessageBoxText() == "Out of memory");
}

TEST_CASE("a fatal message keeps stderr only without a game window")
{
    MenuTestContext testContext;

    CHECK(JonesMainSys_ShowFatalMessage("Out of memory", "Jones3D", true) == 1);
    CHECK(testContext.platform.MessageBoxCount() == 0u);
}

TEST_CASE("the main menu uses the load screen's finished wallpaper state")
{
    // The load screen advances one cel step at each of its three completion stages; the main
    // menu must build the same finished wallpaper state.
    rdWallpaper loadScreenWallpaper;
    rdWallpaper mainMenuWallpaper;
    std::memset(&loadScreenWallpaper, 0, sizeof(loadScreenWallpaper));
    std::memset(&mainMenuWallpaper, 0, sizeof(mainMenuWallpaper));

    const size_t aCels[] = { 2u, 3u, 4u, 6u, 1u, 5u };
    static_assert(STD_ARRAYLEN(aCels) == STD_ARRAYLEN(loadScreenWallpaper.aMaterials),
        "one cel count for every wallpaper tile");

    std::snprintf(loadScreenWallpaper.aName, sizeof(loadScreenWallpaper.aName), "mat\\synthetic");
    std::snprintf(mainMenuWallpaper.aName, sizeof(mainMenuWallpaper.aName), "mat\\synthetic");
    for ( size_t i = 0; i < STD_ARRAYLEN(aCels); ++i )
    {
        loadScreenWallpaper.aMaterials[i].numCels = aCels[i];
        mainMenuWallpaper.aMaterials[i].numCels   = aCels[i];
    }

    for ( size_t step = 0; step < 3; ++step )
    {
        rdWallpaper_AdvanceCels(&loadScreenWallpaper);
    }

    JonesUiMainMenu_SetFinishedWallpaper(&mainMenuWallpaper);

    CHECK(std::strcmp(loadScreenWallpaper.aName, mainMenuWallpaper.aName) == 0);
    CHECK(loadScreenWallpaper.pCamera == mainMenuWallpaper.pCamera);
    for ( size_t i = 0; i < STD_ARRAYLEN(aCels); ++i )
    {
        CHECK(mainMenuWallpaper.aMaterials[i].numCels == loadScreenWallpaper.aMaterials[i].numCels);
        CHECK(mainMenuWallpaper.aMatCelNums[i] == loadScreenWallpaper.aMatCelNums[i]);

        const int expected = aCels[i] > 3u ? 3 : (int)aCels[i] - 1;
        CHECK(mainMenuWallpaper.aMatCelNums[i] == expected);
    }
}

TEST_CASE("New Game closes the main menu with the new-game action")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool newGameCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.newGame = [&newGameCalled]() { newGameCalled = true; };
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    // No saves: CONTINUE is absent, so NEW GAME has the initial focus.
    testContext.platform.PushKey(platform::Key::Enter);
    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, false, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::NewGame);
    CHECK(newGameCalled);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("Back closes the main menu with the quit action")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiMainMenuHooks hooks;
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);
    CHECK(ui.screenCount() == 0u);

    // Back closes the screen at once; the platform's empty-poll quit fallback is never needed.
    CHECK(testContext.platform.QuitFallbacks() == 0);
}

TEST_CASE("the Quit button closes the main menu with the quit action")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiMainMenuHooks hooks;
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    // Focus starts on CONTINUE; four downs reach EXIT GAME and Enter accepts it.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);
    CHECK(ui.screenCount() == 0u);

    // A zero action id would report nothing and only the empty-poll quit fallback would close
    // the menu; a real action id closes it on the Enter itself.
    CHECK(testContext.platform.QuitFallbacks() == 0);
}

TEST_CASE("a disabled Load row is skipped")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool loadCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.loadGame = [&loadCalled](char*) { loadCalled = true; return true; };
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    // Down skips the disabled LOAD GAME row and lands on OPTIONS; Enter opens it, then Escape
    // closes the settings screen and the menu.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, false, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);
    CHECK_FALSE(loadCalled);
}

TEST_CASE("Load returns the load-game action when a save is chosen")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool loadCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.loadGame = [&loadCalled](char* pOutPath)
    {
        loadCalled = true;
        std::snprintf(pOutPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s", "chosen.nds");
        return true;
    };
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::LoadGame);
    CHECK(loadCalled);
    CHECK(std::string(aOutLoadPath) == "chosen.nds");
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("a cancelled Load keeps the main menu open")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool loadCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.loadGame = [&loadCalled](char* pOutPath)
    {
        loadCalled = true;
        std::snprintf(pOutPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s", "cancelled.nds");
        return false;
    };
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    // The cancelled Load leaves the menu open, so this Escape is what quits it.
    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);
    CHECK(loadCalled);
    CHECK(aOutLoadPath[0] == '\0');
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("OPTIONS opens the tabbed Options screen and returns to the menu")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // The test has no display, so the Options wrapper answers cancel without blocking; what is
    // checked here is that the menu accepts OPTIONS and stays usable afterwards.
    JonesUiMainMenuHooks hooks;
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // OPTIONS
    testContext.platform.PushKey(platform::Key::Escape); // back to the main menu
    testContext.platform.PushKey(platform::Key::Escape); // quit

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("the main menu model shows CONTINUE first only when saves exist")
{
    const ui::Screen withSaves = JonesUiMainMenu_BuildScreenForTest(true);
    REQUIRE(!withSaves.widgets.empty());
    const ui::Button* pFirst = std::get_if<ui::Button>(&withSaves.widgets[0]);
    REQUIRE(pFirst != nullptr);
    CHECK(pFirst->text == "CONTINUE");
    CHECK(pFirst->enabled);

    const ui::Screen withoutSaves = JonesUiMainMenu_BuildScreenForTest(false);
    REQUIRE(!withoutSaves.widgets.empty());
    pFirst = std::get_if<ui::Button>(&withoutSaves.widgets[0]);
    REQUIRE(pFirst != nullptr);
    CHECK(pFirst->text == "NEW GAME");

    bool hasContinue = false;
    for ( const ui::Widget& widget : withoutSaves.widgets )
    {
        if ( const ui::Button* pButton = std::get_if<ui::Button>(&widget) )
        {
            hasContinue = hasContinue || pButton->text == "CONTINUE";
            for ( char chr : pButton->text )
            {
                const bool lower = chr >= 'a' && chr <= 'z';
                CHECK_FALSE(lower);
            }
        }
    }
    CHECK_FALSE(hasContinue);

    // The exit row keeps its own action and remains the last item.
    REQUIRE(!withSaves.widgets.empty());
    const ui::Button* pLast = std::get_if<ui::Button>(&withSaves.widgets.back());
    REQUIRE(pLast != nullptr);
    CHECK(pLast->text == "EXIT GAME");
}

TEST_CASE("Continue loads the newest save's path")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool continueCalled = false;
    bool loadCalled     = false;
    JonesUiMainMenuHooks hooks;
    hooks.continueGame = [&continueCalled](char* pOutPath)
    {
        continueCalled = true;
        std::snprintf(pOutPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s", "newest.nds");
        return true;
    };
    hooks.loadGame = [&loadCalled](char*)
    {
        loadCalled = true;
        return true;
    };
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };

    // Focus starts on CONTINUE and Enter continues it.
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::LoadGame);
    CHECK(continueCalled);
    CHECK_FALSE(loadCalled);
    CHECK(std::string(aOutLoadPath) == "newest.nds");
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("the main menu skin is a lower-left list with a bullet and the given font")
{
    ui::NullPainter painter;

    // The Cinzel id when it loaded...
    const ui::Skin ttf = JonesUiSkin_CreateMainMenu(42, &painter);
    CHECK(ttf.placement == ui::PanelPlacement::BottomLeft);
    CHECK(ttf.focusMarker == "\xE2\x80\xA2");
    CHECK(ttf.markerSpacing > 0.0f);
    CHECK(ttf.textFont == 42);
    CHECK(ttf.titleFont == 42);
    CHECK(ttf.panel.a == 0.0f);
    CHECK(ttf.panelBorder.a == 0.0f);
    CHECK(ttf.borderWidth == 0.0f);
    CHECK(ttf.highlight.a == 0.0f);
    CHECK(ttf.text.a > 0.0f);
    CHECK(ttf.text.a < 1.0f);
    const ui::Color focused = { 1.0f, 1.0f, 1.0f, 1.0f };
    CHECK(ttf.textFocused == focused);
    CHECK(ttf.textShadow.a > 0.0f);
    CHECK(ttf.textShadowOffset > 0.0f);
    CHECK(ttf.rowHeight > 0.0f);

    // ...and the shared rdFont id when Cinzel did not load.
    const ui::Skin fallback = JonesUiSkin_CreateMainMenu(7, &painter);
    CHECK(fallback.textFont == 7);
    CHECK(fallback.placement == ui::PanelPlacement::BottomLeft);

    const ui::Skin noPainter = JonesUiSkin_CreateMainMenu(9, nullptr);
    CHECK(noPainter.textFont == 9);
    CHECK(noPainter.rowHeight > 0.0f);
}
