// Added: the logic tests of the Babylon column's pause menu (ADR-0026). They drive the screen with
// one scripted platform event per frame, check that every entry calls the shared JonesHud system
// item, and check the thumbnail capture the save reuses.
#include <doctest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiPauseMenu.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>

namespace
{
    using namespace babylon;

    int g_saveCalls      = 0;
    int g_loadCalls      = 0;
    int g_statisticsCalls = 0;
    int g_gamePlayCalls  = 0;
    int g_controlCalls   = 0;
    int g_displayCalls   = 0;
    int g_soundCalls     = 0;
    int g_exitCalls      = 0;

    int g_saveResult     = 1;
    int g_loadResult     = 1;
    int g_exitResult     = 2;

    void ResetCalls()
    {
        g_saveCalls       = 0;
        g_loadCalls       = 0;
        g_statisticsCalls = 0;
        g_gamePlayCalls   = 0;
        g_controlCalls    = 0;
        g_displayCalls    = 0;
        g_soundCalls      = 0;
        g_exitCalls       = 0;

        g_saveResult = 1;
        g_loadResult = 1;
        g_exitResult = 2;
    }

    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
    }

    tHostServices g_hostServices;

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

    // Delivers one event per modal frame. The pump drains a platform until pollEvent returns
    // empty, so each event is followed by one empty answer to end that frame.
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

    struct ScreenTestContext
    {
        ScriptedPlatform platform;
        time::NullClock clock;
        time::NullClock virtualClock;
        cvars::Registry cvars;
        StdBabylonContext context{};

        ScreenTestContext()
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
            ResetCalls();
        }

        ~ScreenTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    tVBuffer* CreateFrame(std::uint8_t blue, std::uint8_t green, std::uint8_t red)
    {
        tRasterInfo rasterInfo;
        std::memset(&rasterInfo, 0, sizeof(rasterInfo));
        rasterInfo.width     = 256u;
        rasterInfo.height    = 192u;
        rasterInfo.rowSize   = 256u * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL;
        rasterInfo.rowWidth  = 256u;
        rasterInfo.size      = rasterInfo.rowSize * 192u;
        rasterInfo.colorInfo = stdColor_cfRGB888;

        tVBuffer* pFrame = stdDisplay_VBufferNew(&rasterInfo, 0, 0);
        REQUIRE(pFrame != nullptr);
        REQUIRE(pFrame->pPixels != nullptr);

        for ( std::uint32_t y = 0; y < rasterInfo.height; ++y )
        {
            for ( std::uint32_t x = 0; x < rasterInfo.width; ++x )
            {
                std::uint8_t* pPixel = &pFrame->pPixels[(std::size_t)rasterInfo.rowSize * y
                    + (std::size_t)x * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL];
                pPixel[0] = blue;
                pPixel[1] = green;
                pPixel[2] = red;
            }
        }

        return pFrame;
    }
}

extern "C" int JonesHud_ShowSaveGame(void)
{
    ++g_saveCalls;
    return g_saveResult;
}

extern "C" int JonesHud_ShowLoadGame(void)
{
    ++g_loadCalls;
    return g_loadResult;
}

extern "C" int JonesHud_ShowStatistics(void)
{
    ++g_statisticsCalls;
    return 2;
}

extern "C" int JonesHud_ShowGamePlaySettings(void)
{
    ++g_gamePlayCalls;
    return 2;
}

extern "C" int JonesHud_ShowControlSettings(void)
{
    ++g_controlCalls;
    return 2;
}

extern "C" int JonesHud_ShowDisplaySettings(void)
{
    ++g_displayCalls;
    return 2;
}

extern "C" int JonesHud_ShowSoundSettings(void)
{
    ++g_soundCalls;
    return 2;
}

extern "C" int JonesHud_ShowExitGame(void)
{
    ++g_exitCalls;
    return g_exitResult;
}

TEST_CASE("the pause menu has the original six entries and back closes it")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    const ui::Screen screen = JonesUiPauseMenu_BuildScreen();
    REQUIRE(screen.widgets.size() == 6u);
    CHECK(std::get<ui::Button>(screen.widgets[0]).action == 1);
    CHECK(std::get<ui::Button>(screen.widgets[1]).action == 2);
    CHECK(std::get<ui::Button>(screen.widgets[2]).action == 3);
    CHECK(std::get<ui::Button>(screen.widgets[3]).action == 4);
    CHECK(std::get<ui::Button>(screen.widgets[4]).action == 5);
    CHECK(std::get<ui::Button>(screen.widgets[5]).action == 6);

    for ( const ui::Widget& widget : screen.widgets )
    {
        CHECK(std::get<ui::Button>(widget).text.rfind("JONES_STR_", 0) == 0);
    }

    {
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_saveCalls == 0);
        CHECK(g_loadCalls == 0);
        CHECK(g_statisticsCalls == 0);
        CHECK(g_exitCalls == 0);
    }

    {
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_saveCalls == 0);
        CHECK(g_loadCalls == 0);
        CHECK(g_statisticsCalls == 0);
        CHECK(g_exitCalls == 0);
    }
}

TEST_CASE("the pause menu entries call the shared HUD system item functions")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_saveCalls == 1);
    }

    ResetCalls();
    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_loadCalls == 1);
    }

    ResetCalls();
    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_statisticsCalls == 1);
    }

    ResetCalls();
    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
        CHECK(g_exitCalls == 1);
    }
}

TEST_CASE("pause menu load closes only when its screen accepts")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    g_loadResult = 2;
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
    CHECK(g_loadCalls == 1);

    ResetCalls();
    g_loadResult = 1;
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
    CHECK(g_loadCalls == 1);
}

TEST_CASE("pause menu exit closes when the exit screen activates and stays on cancel")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    g_exitResult = 2;
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
    CHECK(g_exitCalls == 1);

    ResetCalls();
    g_exitResult = 1;
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);
    CHECK(g_exitCalls == 1);
}

TEST_CASE("the nested settings screen offers every original options entry")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    const ui::Screen screen = JonesUiPauseMenu_BuildSettingsScreen();
    REQUIRE(screen.widgets.size() == 5u);
    for ( const ui::Widget& widget : screen.widgets )
    {
        CHECK(std::get<ui::Button>(widget).text.rfind("JONES_STR_", 0) == 0);
    }

    for ( int entry = 0; entry < 4; ++entry )
    {
        ResetCalls();
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        for ( int step = 0; step < entry; ++step )
        {
            testContext.platform.PushKey(platform::Key::Down);
        }
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiPauseMenu_RunPauseMenu(ui, &painter) == 1);

        switch ( entry )
        {
            case 0: CHECK(g_gamePlayCalls == 1); break;
            case 1: CHECK(g_controlCalls == 1); break;
            case 2: CHECK(g_displayCalls == 1); break;
            default: CHECK(g_soundCalls == 1); break;
        }
    }
}

TEST_CASE("the pause menu thumbnail captures the frame once and replaces an earlier one")
{
    ScreenTestContext testContext;

    tVBuffer* pFirst = CreateFrame(10u, 20u, 30u);
    CHECK(JonesUiPauseMenu_CaptureThumbnail(pFirst));
    CHECK(sithGamesave_bThumbnail == 1);
    REQUIRE(sithGamesave_hBmpThumbnail != NULL);

    const std::uint8_t* pThumbnail = (const std::uint8_t*)sithGamesave_hBmpThumbnail;
    CHECK(pThumbnail[0] == 10u);
    CHECK(pThumbnail[1] == 20u);
    CHECK(pThumbnail[2] == 30u);

    tVBuffer* pSecond = CreateFrame(40u, 50u, 60u);
    CHECK(JonesUiPauseMenu_CaptureThumbnail(pSecond));
    CHECK(sithGamesave_bThumbnail == 1);
    CHECK(sithGamesave_hBmpThumbnail != NULL);
    CHECK(pThumbnail[0] == 40u);
    CHECK(pThumbnail[1] == 50u);
    CHECK(pThumbnail[2] == 60u);

    // This is the guard sithGamesave_Process tests before it captures again: a held thumbnail is
    // written as it is, so the save made from the menu keeps the game frame.
    CHECK(sithGamesave_bThumbnail == 1);
    CHECK(sithGamesave_hBmpThumbnail != NULL);

    stdDisplay_VBufferFree(pFirst);
    stdDisplay_VBufferFree(pSecond);
    sithGamesaveSys_ReleaseThumbnail();
}

TEST_CASE("the headless pause menu answers the closed value")
{
    ScreenTestContext testContext;
    CHECK(jonesConfig_ShowPauseMenu(NULL) == 2);
}
