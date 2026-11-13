// Added: the logic tests of the Babylon column's Statistics, Level Completed and Game Over
// screens (ADR-0026). They drive each screen with one scripted platform event per frame, check
// every return value and the outputs, and check the exact statistics strings of a synthetic
// SithGameStatistics. The drawing sample lives in JonesUiPainterTest.cpp.
#include <doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesLevel.h>
#include <Jones3D/Platform/Babylon/JonesUiEndScreens.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <sith/Devices/sithSoundMixer.h>
#include <sith/types.h>
#include <sound/Sound.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>

namespace
{
    using namespace babylon;

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
        g_hostServices.pFileOpen     = stdFileOpen;
        g_hostServices.pFileClose    = stdFileClose;
        g_hostServices.pFileRead     = stdFileRead;
        g_hostServices.pFileGets     = stdFileGets;
        g_hostServices.pFileWrite    = stdFileWrite;
        g_hostServices.pFileEOF      = stdFileEof;
        g_hostServices.pFileTell     = stdFileTell;
        g_hostServices.pFileSeek     = stdFileSeek;
        g_hostServices.pFileSize     = stdFileSize;
        g_hostServices.pFilePrintf   = stdFilePrintf;
        g_hostServices.pFileGetws    = stdFileGetws;

        std_g_pHS = &g_hostServices;
    }

    // Delivers one event per modal frame. The pump drains a platform until pollEvent returns
    // empty, so each event is followed by one empty answer to end that frame. If a screen never
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
        std::unique_ptr<files::FileSystem> pFileSystem;
        cvars::Registry cvars;
        StdBabylonContext context{};

        ScreenTestContext()
        {
            pFileSystem = files::createSdlFileSystem();

            context.pClock          = &clock;
            context.pVirtualClock   = &virtualClock;
            context.pFileSystem     = pFileSystem.get();
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

        ~ScreenTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    bool J3DAPI FakeConfigHasStarted(void)
    {
        return true;
    }

    int J3DAPI FakeConfigGetInt(const char* pKey, int defaultValue)
    {
        (void)pKey;
        return defaultValue;
    }

    float J3DAPI FakeConfigGetFloat(const char* pKey, float defaultValue)
    {
        (void)pKey;
        return defaultValue;
    }

    int J3DAPI FakeConfigGetStr(const char* pKey, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        (void)pKey;
        if ( pDstStr && size )
        {
            std::snprintf(pDstStr, size, "%s", pDefaultValue ? pDefaultValue : "");
        }

        return 0;
    }

    // Opens the sound layer on the column's silent driver (no Babylon audio device is installed in
    // these tests), gives it one synthetic sound and starts the mixer, so the Game Over screen's
    // play call returns a real channel without a device or a game asset.
    class SoundTestContext
    {
    public:
        SoundTestContext()
        {
            SetupHostServices();

            m_bJsonStarted = stdJSON_Startup();
            if ( m_bJsonStarted )
            {
                const StdConfigFallback fallback = {
                    FakeConfigHasStarted, FakeConfigGetInt, FakeConfigGetFloat, FakeConfigGetStr };
                stdConfig_SetFallback(&fallback);
                m_bConfigStarted = stdConfig_Startup(kConfigPath);
            }

            if ( m_bConfigStarted
                && Sound_Initialize(&g_hostServices) == 0
                && Sound_Startup() )
            {
                m_bSoundStarted = Sound_Open(SOUNDOPEN_NO3DSOUND, 16, nullptr, nullptr, NULL)
                    != 0;
            }

            if ( !m_bSoundStarted )
            {
                return;
            }

            std::memset(&m_soundInfo, 0, sizeof(m_soundInfo));
            m_soundInfo.hSnd           = kSoundHandle;
            m_soundInfo.bankNum        = 0;
            m_soundInfo.dataOffset     = 16;
            m_soundInfo.sampleRate     = 8000;
            m_soundInfo.sempleBitSize  = 16;
            m_soundInfo.numChannels    = 1;
            m_soundInfo.dataSize       = 160;
            m_soundInfo.bCompressed    = 0;

            std::memset(m_aSoundCache, 0, sizeof(m_aSoundCache));
            m_aSoundCache[m_soundInfo.dataOffset] = 0xA5;
            Sound_TestSetSoundBank(0, &m_soundInfo, 1, m_aSoundCache, sizeof(m_aSoundCache));

            m_bMixerStarted = sithSoundMixer_Startup() == 0;
        }

        ~SoundTestContext()
        {
            if ( m_bMixerStarted )
            {
                sithSoundMixer_Shutdown();
            }

            if ( m_bSoundStarted )
            {
                // The bank points at this object's storage; clear it so Sound_Close does not free
                // memory it did not allocate.
                Sound_TestSetSoundBank(0, nullptr, 0, nullptr, 0);
                Sound_Close();
                Sound_Shutdown();
            }

            Sound_Uninitialize();

            if ( m_bConfigStarted )
            {
                stdConfig_Shutdown();
            }

            if ( m_bJsonStarted )
            {
                stdJSON_Shutdown();
            }

            std::remove(kConfigPath);
        }

        SoundTestContext(const SoundTestContext&) = delete;
        SoundTestContext& operator=(const SoundTestContext&) = delete;

        bool Ready() const
        {
            return m_bMixerStarted;
        }

        tSoundHandle Handle() const
        {
            return kSoundHandle;
        }

    private:
        static constexpr const char* kConfigPath  = "ui-endscreens-test.json";
        static constexpr tSoundHandle kSoundHandle = 1234;

        bool m_bJsonStarted   = false;
        bool m_bConfigStarted = false;
        bool m_bSoundStarted  = false;
        bool m_bMixerStarted  = false;
        SoundInfo m_soundInfo;
        std::uint8_t m_aSoundCache[256];
    };

    SithGameStatistics SyntheticStatistics()
    {
        SithGameStatistics statistics;
        std::memset(&statistics, 0, sizeof(statistics));
        statistics.curLevelNum   = 3;
        statistics.totalIQPoints = 999;

        statistics.aLevelStatistic[0].elapsedTime       = 0x0205u; // 2 : 05
        statistics.aLevelStatistic[0].numFoundTreasures = 4;
        statistics.aLevelStatistic[0].numSeenHints      = 2;
        statistics.aLevelStatistic[0].difficultyPenalty = 7;
        statistics.aLevelStatistic[0].iqPoints          = 41;

        statistics.aLevelStatistic[1].elapsedTime       = 0x0A0Fu; // 10 : 15
        statistics.aLevelStatistic[1].numFoundTreasures = 8;
        statistics.aLevelStatistic[1].iqPoints          = 55;

        return statistics;
    }
}

TEST_CASE("the end screens share a centered table skin with a Cinzel title and Jost rows")
{
    ui::NullPainter painter;
    const ui::Skin skin = JonesUiSkin_CreateEndScreen(3, 4, &painter, 6);

    CHECK(skin.placement == ui::PanelPlacement::Center);
    CHECK(skin.titleFont == 3);
    CHECK(skin.textFont == 4);
    CHECK(skin.promptFont == 6);
    CHECK(skin.highlightUnfocused.a > 0.0f);
    CHECK(skin.highlightUnfocused.a <= 0.15f);
    CHECK(skin.panel.a > 0.0f);
    CHECK(skin.panel.a < 1.0f);
    CHECK(skin.borderWidth == 0.0f);
    CHECK(skin.panelBorder.a == 0.0f);
    CHECK(skin.highlight.a == 0.0f);
    CHECK(skin.textShadow.a > 0.0f);
    CHECK(skin.textShadowOffset > 0.0f);
    CHECK(skin.rowHeight > 0.0f);
    CHECK(skin.titleHeight > 0.0f);
}

TEST_CASE("statistics time keeps the original hours and minutes format")
{
    CHECK(JonesUiEndScreens_FormatTime(0x0205u) == "2 : 05 ");
    CHECK(JonesUiEndScreens_FormatTime(0x020Au) == "2 : 10 ");
    CHECK(JonesUiEndScreens_FormatTime(0x0009u) == "0 : 09 ");
    CHECK(JonesUiEndScreens_FormatTime(0x0000u) == "0 : 00 ");
}

TEST_CASE("the statistics view starts at the summary and lists the levels downward")
{
    SithGameStatistics statistics = SyntheticStatistics();
    statistics.totalLevelStats.elapsedTime       = 0x0A0Fu; // 10 : 15
    statistics.totalLevelStats.numFoundTreasures = 30;
    statistics.totalLevelStats.numSeenHints      = 12;
    statistics.totalLevelStats.difficultyPenalty = 9;

    const std::vector<JonesUiStatisticsRow> rows =
        JonesUiStatistics_BuildView(statistics, 123);
    REQUIRE(rows.size() == 5u);

    CHECK(rows[0].levelNum == JONESLEVEL_BONUSLEVELNUM);
    CHECK(rows[0].optionName == "JONES_STR_SUMMARY");
    CHECK(rows[0].levelName == "JONES_STR_SUMMARY");
    CHECK(rows[0].timePassed == "10 : 15 ");
    CHECK(rows[0].numLevelsFinished == "3 ");
    CHECK(rows[0].treasuresFound == "30 ");
    CHECK(rows[0].hintsSeen == "12 ");
    CHECK(rows[0].difficultyDrop == "9 ");
    CHECK(rows[0].iqPoints == "123 ");

    CHECK(rows[1].levelNum == 3);
    CHECK(rows[1].levelName == "JONES_STR_SHS");
    CHECK(rows[2].levelNum == 2);
    CHECK(rows[2].levelName == "JONES_STR_RIV");
    CHECK(rows[3].levelNum == 1);
    CHECK(rows[3].levelName == "JONES_STR_BAB");

    CHECK(rows[4].levelNum == 0);
    CHECK(rows[4].levelName == "JONES_STR_CYN");
    CHECK(rows[4].timePassed == "2 : 05 ");
    CHECK(rows[4].numLevelsFinished == "3 ");
    CHECK(rows[4].treasuresFound == "4 ");
    CHECK(rows[4].hintsSeen == "2 ");
    CHECK(rows[4].difficultyDrop == "7 ");
    CHECK(rows[4].iqPoints == "41 ");
}

TEST_CASE("the statistics screen offers every level and closes with the original answers")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    const SithGameStatistics statistics = SyntheticStatistics();
    const ui::Screen screen = JonesUiStatistics_BuildScreen(statistics, 123);
    REQUIRE(screen.widgets.size() == 9u);
    CHECK(screen.title == "JONES_STR_IQ");
    const ui::Choice& choice = std::get<ui::Choice>(screen.widgets[0]);
    CHECK(choice.text == "Chapter");
    REQUIRE(choice.options.size() == 5u);
    CHECK(choice.options[0] == "JONES_STR_SUMMARY");
    CHECK(choice.options[4] == "JONES_STR_CYN");

    // The IQ row uses its own key; JONES_STR_IQ is the title.
    CHECK(std::get<ui::Label>(screen.widgets[7]).text == "JONES_STR_LEVELIQ");
    CHECK(std::get<ui::Label>(screen.widgets[7]).value == "123 ");

    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiStatistics_RunStatisticsScreen(ui, &painter, statistics, 123) == 1);
    }

    {
        testContext.platform.PushKey(platform::Key::Right);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiStatistics_RunStatisticsScreen(ui, &painter, statistics, 123) == 1);
    }

    {
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiStatistics_RunStatisticsScreen(ui, &painter, statistics, 123) == 2);
    }
}

TEST_CASE("the level completed screen returns its answers and opens the store when the original did")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    SithGameStatistics statistics = SyntheticStatistics();
    statistics.curLevelNum        = 1;

    int balance     = 500;
    int aItems[14]  = { 1, 2, 3 };
    const ui::Screen screen = JonesUiLevelCompleted_BuildScreen(0x0205u, 41, 4, 100, 200);
    REQUIRE(screen.widgets.size() == 6u);
    CHECK(std::get<ui::Label>(screen.widgets[0]).value == "2 : 05 ");
    CHECK(std::get<ui::Label>(screen.widgets[1]).value == "4 ");
    CHECK(std::get<ui::Label>(screen.widgets[2]).value == "100 ");
    CHECK(std::get<ui::Label>(screen.widgets[3]).value == "200 ");
    CHECK(std::get<ui::Label>(screen.widgets[4]).text == "JONES_STR_LEVELIQ");
    CHECK(std::get<ui::Label>(screen.widgets[4]).value == "41 ");

    JonesUiEndScreens_ResetStoreCallCount();
    {
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiLevelCompleted_RunLevelCompletedScreen(ui, &painter, statistics, 0x0205u,
            41, 4, 100, 200, &balance, aItems, 11) == 1);
        CHECK(JonesUiEndScreens_GetStoreCallCount() == 1);
        CHECK(balance == 500);
        CHECK(aItems[0] == 1);
    }

    JonesUiEndScreens_ResetStoreCallCount();
    {
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiLevelCompleted_RunLevelCompletedScreen(ui, &painter, statistics, 0x0205u,
            41, 4, 100, 200, &balance, aItems, 11) == 2);
        CHECK(JonesUiEndScreens_GetStoreCallCount() == 1);
    }

    // The bonus level skips the store, as the original's curLevelNum + 1 test does.
    statistics.curLevelNum = JONESLEVEL_BONUSLEVELNUM;
    JonesUiEndScreens_ResetStoreCallCount();
    {
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiLevelCompleted_RunLevelCompletedScreen(ui, &painter, statistics, 0x0205u,
            41, 4, 100, 200, &balance, aItems, 11) == 2);
        CHECK(JonesUiEndScreens_GetStoreCallCount() == 0);
    }
}

TEST_CASE("the game over screen has the original buttons and return values")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    char aFilename[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    tSoundChannelHandle channel = 0;

    const ui::Screen screen = JonesUiGameOver_BuildScreen(true);
    REQUIRE(screen.widgets.size() == 3u);
    CHECK(std::get<ui::Button>(screen.widgets[0]).action == 1178);
    CHECK(std::get<ui::Button>(screen.widgets[1]).action == 1179);
    CHECK(std::get<ui::Button>(screen.widgets[2]).action == 1177);
    CHECK(std::get<ui::Button>(screen.widgets[2]).enabled);

    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "", false, 0, &channel,
            aFilename) == 1179);
        CHECK(channel == 0);
        CHECK(aFilename[0] == '\0');
    }

    {
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "", true, 0, &channel,
            aFilename) == 1177);
    }

    // The Restart button is skipped when the auto save does not exist.
    CHECK(!std::get<ui::Button>(JonesUiGameOver_BuildScreen(false).widgets[2]).enabled);

    {
        testContext.platform.PushKey(platform::Key::Escape);
        CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "SAVES\\AUTO_LEVEL.nds", false, 0,
            &channel, aFilename) == 0);
        CHECK(std::string(aFilename) == "SAVES\\AUTO_LEVEL.nds");
    }
}

TEST_CASE("game over plays the passed music and returns its channel")
{
    ScreenTestContext testContext;
    SoundTestContext sound;
    if ( !sound.Ready() )
    {
        MESSAGE("sound layer unavailable, skipping the game over music check");
        return;
    }

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    char aFilename[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    tSoundChannelHandle channel = 0;

    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "", false, sound.Handle(), &channel,
        aFilename) == 0);
    CHECK(channel != 0);

    tSoundChannelHandle silentChannel = 1;
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "", false, 0, &silentChannel,
        aFilename) == 0);
    CHECK(silentChannel == 0);
}

TEST_CASE("the game over load answer nests the load screen and keeps the original result")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    char aFilename[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    tSoundChannelHandle channel = 0;

    // A headless load answers 2, so the game over screen stays open. The original had already
    // stored 1178 in data.result, so the later Escape still reports 1178.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    CHECK(JonesUiGameOver_RunGameOverScreen(ui, &painter, "", false, 0, &channel, aFilename)
        == 1178);
}

TEST_CASE("the headless wrappers answer the Null stub values")
{
    ScreenTestContext testContext;

    SithGameStatistics statistics = SyntheticStatistics();
    int balance                    = 10;
    int aItems[14]                 = { 0 };
    char aFilename[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    tSoundChannelHandle channel = 0;

    CHECK(jonesConfig_ShowStatisticsDialog(NULL, &statistics) == 2);
    CHECK(jonesConfig_ShowLevelCompletedDialog(NULL, &balance, aItems, 11, 0, 0, 0, 0, 0) == 2);
    CHECK(jonesConfig_ShowGameOverDialog(NULL, aFilename, 0, &channel) == 2);
    CHECK(jonesConfig_ShowStoreDialog(NULL, &balance, aItems, 11) == 2);
}
