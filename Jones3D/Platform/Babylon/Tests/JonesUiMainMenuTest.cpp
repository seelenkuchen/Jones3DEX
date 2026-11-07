// Added: the logic tests of the Babylon column's main menu and start-mode mapping (ADR-0026).
// They drive the runner with a Null painter and a scripted platform, so every menu action and the
// nested Settings screen run without a window, display or game asset. The drawing sample lives in
// JonesUiPainterTest.cpp.
#include <doctest.h>

#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string>

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
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>
#include <Jones3D/types.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

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

extern "C" int J3DAPI jonesConfig_ShowGamePlayOptions(tSysWindow hWnd)
{
    (void)hWnd;
    return 2;
}

extern "C" int J3DAPI jonesConfig_ShowControlOptions(tSysWindow hWnd)
{
    (void)hWnd;
    return 2;
}

extern "C" void JonesPlatformNull_SetLoadPath(const char* pPath);
extern "C" void JonesPlatformNull_ClearLoadPath(void);

namespace
{
    using namespace babylon;

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

        bool showMessageBox(std::string_view, std::string_view) override
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

    private:
        std::deque<platform::Event> m_events;
        bool m_delivering = false;
        int m_emptyPolls  = 0;
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

TEST_CASE("New Game closes the main menu with the new-game action")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool newGameCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.newGame = [&newGameCalled]() { newGameCalled = true; };

    testContext.platform.PushKey(platform::Key::Enter);
    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks)
        == JonesUiMainMenuAction::NewGame);
    CHECK(newGameCalled);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("Quit closes the main menu without any action")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiMainMenuHooks hooks;
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks)
        == JonesUiMainMenuAction::Quit);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("a disabled Load row is skipped")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool loadCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.loadGame = [&loadCalled](char*) { loadCalled = true; return true; };

    // Down skips the disabled Load row and lands on Settings; Enter opens it, then Escape closes
    // the settings screen and the menu.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, false, hooks)
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

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks)
        == JonesUiMainMenuAction::LoadGame);
    CHECK(loadCalled);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("a cancelled Load keeps the main menu open")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    bool loadCalled = false;
    JonesUiMainMenuHooks hooks;
    hooks.loadGame = [&loadCalled](char*) { loadCalled = true; return false; };

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks)
        == JonesUiMainMenuAction::Quit);
    CHECK(loadCalled);
    CHECK(ui.screenCount() == 0u);
}

TEST_CASE("the Settings screen calls every settings hook")
{
    MenuTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int gameplay = 0;
    int controls = 0;
    int display  = 0;
    int sound    = 0;
    JonesUiMainMenuHooks hooks;
    hooks.gameplay = [&gameplay]() { ++gameplay; };
    hooks.controls = [&controls]() { ++controls; };
    hooks.display  = [&display]() { ++display; };
    hooks.sound    = [&sound]() { ++sound; };

    // Down twice to Settings, Enter opens it; then accept each row in turn and Back.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiMainMenu_Run(ui, &painter, JonesUiModalBackground{}, true, hooks)
        == JonesUiMainMenuAction::Quit);
    CHECK(gameplay == 1);
    CHECK(controls == 1);
    CHECK(display == 1);
    CHECK(sound == 1);
    CHECK(ui.screenCount() == 0u);
}
