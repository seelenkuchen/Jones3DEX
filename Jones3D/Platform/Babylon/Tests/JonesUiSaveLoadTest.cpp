// Added: the logic tests of the Babylon column's Save, Load and Exit screens (ADR-0026). They use
// a temporary save directory with hand-built .nds files and a platform that delivers one scripted
// event per frame, so the screens run exactly as they do in the game loop without a window or a
// GPU. The drawing sample lives in JonesUiPainterTest.cpp.
#include <doctest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
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

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>
#include <sith/Main/sithMain.h>
#include <sith/types.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>
#include <std/General/stdPlatform.h>

namespace
{
    using namespace babylon;

    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
    }

    // The std host services stdConffile reads save headers through. stdFileOpen is the Babylon
    // adapter and resolves against the installed context's file system.
    tHostServices g_saveHostServices;

    void SetupSaveHostServices()
    {
        std::memset(&g_saveHostServices, 0, sizeof(g_saveHostServices));
        g_saveHostServices.pStatusPrint  = TestPrint;
        g_saveHostServices.pMessagePrint = TestPrint;
        g_saveHostServices.pWarningPrint = TestPrint;
        g_saveHostServices.pErrorPrint   = TestPrint;
        g_saveHostServices.pDebugPrint   = TestPrint;
        g_saveHostServices.pMalloc       = malloc;
        g_saveHostServices.pFree         = free;
        g_saveHostServices.pRealloc      = realloc;
        g_saveHostServices.pFileOpen     = stdFileOpen;
        g_saveHostServices.pFileClose    = stdFileClose;
        g_saveHostServices.pFileRead     = stdFileRead;
        g_saveHostServices.pFileGets     = stdFileGets;
        g_saveHostServices.pFileWrite    = stdFileWrite;
        g_saveHostServices.pFileEOF      = stdFileEof;
        g_saveHostServices.pFileTell     = stdFileTell;
        g_saveHostServices.pFileSeek     = stdFileSeek;
        g_saveHostServices.pFileSize     = stdFileSize;
        g_saveHostServices.pFilePrintf   = stdFilePrintf;
        g_saveHostServices.pFileGetws    = stdFileGetws;

        std_g_pHS  = &g_saveHostServices;
        sith_g_pHS = &g_saveHostServices;
    }

    // A temporary "ui-save-test" directory beside the test's working directory. The engine path
    // ends in '\' like sithGetSaveGamesDir() so the output paths have the game's shape.
    class SaveTestDir
    {
    public:
        SaveTestDir()
            : m_path("ui-save-test")
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
            std::filesystem::create_directories(m_path, error);
        }

        ~SaveTestDir()
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }

        SaveTestDir(const SaveTestDir&) = delete;
        SaveTestDir& operator=(const SaveTestDir&) = delete;

        std::string EngineDir() const
        {
            return m_path.generic_string() + "\\";
        }

        std::string File(const std::string& name) const
        {
            return (m_path / name).string();
        }

    private:
        std::filesystem::path m_path;
    };

    void WriteSave(const std::string& path, const char* pLevelName, int secondsAgo,
        std::uint8_t red, std::uint8_t green, std::uint8_t blue)
    {
        NdsHeader header;
        std::memset(&header, 0, sizeof(header));
        header.version    = SITHSAVEGAME_FILEVERSION;
        header.bThumbnail = 1;
        std::snprintf(header.aLevelFilename, sizeof(header.aLevelFilename), "%s", pLevelName);

        std::vector<std::uint8_t> thumbnail(SITHSAVEGAME_THUMBSIZE);
        for ( std::size_t cur = 0; cur < thumbnail.size(); cur += 3 )
        {
            thumbnail[cur + 0] = blue;
            thumbnail[cur + 1] = green;
            thumbnail[cur + 2] = red;
        }

        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(thumbnail.data()),
            (std::streamsize)thumbnail.size());
        file.close();
        REQUIRE(file);

        std::filesystem::last_write_time(std::filesystem::path(path),
            std::filesystem::file_time_type::clock::now() - std::chrono::seconds(secondsAgo));
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

            SetupSaveHostServices();
        }

        ~ScreenTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    std::tm FixedTime()
    {
        std::tm timeValue{};
        timeValue.tm_year = 126; // 2026
        timeValue.tm_mon  = 10;  // November
        timeValue.tm_mday = 7;
        timeValue.tm_hour = 13;
        timeValue.tm_min  = 5;
        timeValue.tm_sec  = 9;
        return timeValue;
    }
}

TEST_CASE("save names are usable on every desktop platform")
{
    CHECK(JonesUiSave_SanitizeName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(JonesUiSave_SanitizeName("name. ") == "name");
    CHECK(JonesUiSave_SanitizeName("") == "save");
    CHECK(JonesUiSave_SanitizeName(".") == "save");
    CHECK(JonesUiSave_SanitizeName("..") == "save");
    CHECK(JonesUiSave_SanitizeName("CON") == "_CON");
    CHECK(JonesUiSave_SanitizeName("con.nds") == "_con.nds");
    CHECK(JonesUiSave_SanitizeName("a\tb") == "a_b");
    CHECK(JonesUiSave_SanitizeName("H\xC3\xA9llo") == "H\xC3\xA9llo");
}

TEST_CASE("the default save name is the level, date and time")
{
    const std::tm timeValue = FixedTime();
    CHECK(JonesUiSave_MakeDefaultName("Test Level", timeValue)
        == "Test Level 2026-11-07 13-05-09");
}

TEST_CASE("the save list is newest first and carries level and date")
{
    ScreenTestContext testContext;
    SaveTestDir dir;

    WriteSave(dir.File("older.nds"), "OLDER_LEVEL", 100, 1, 1, 1);
    WriteSave(dir.File("newer.nds"), "NEWER_LEVEL", 10, 2, 2, 2);
    WriteSave(dir.File("ignored.txt"), "IGNORED", 1, 3, 3, 3);

    std::vector<JonesUiSaveEntry> entries;
    REQUIRE(JonesUiSave_CollectEntries(*testContext.pFileSystem, dir.EngineDir().c_str(), entries));
    REQUIRE(entries.size() == 2u);

    CHECK(entries[0].fileName == "newer.nds");
    CHECK(entries[0].displayName == "newer");
    CHECK(entries[0].levelName == "NEWER_LEVEL");
    CHECK(entries[0].dateText.size() == 16u);
    CHECK(entries[1].fileName == "older.nds");
    CHECK(entries[1].levelName == "OLDER_LEVEL");
    CHECK(entries[0].modifiedSeconds > entries[1].modifiedSeconds);
}

TEST_CASE("the save screen writes the default path without typing")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    const std::tm timeValue = FixedTime();

    testContext.platform.PushKey(platform::Key::Enter);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiSave_RunSaveScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), "Test Level", timeValue, aPath);
    CHECK(result == 1);
    CHECK(std::string(aPath) == dir.EngineDir() + "Test Level 2026-11-07 13-05-09.nds");
}

TEST_CASE("the save screen asks before overwriting an existing save")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    WriteSave(dir.File("existing.nds"), "EXISTING_LEVEL", 5, 4, 5, 6);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    const std::tm timeValue = FixedTime();

    // Move from the text entry to the list, accept the row, then Yes in the overwrite prompt.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiSave_RunSaveScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), "Test Level", timeValue, aPath);
    CHECK(result == 1);
    CHECK(std::string(aPath) == dir.EngineDir() + "existing.nds");
}

TEST_CASE("answering No to overwrite leaves the save screen open")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    WriteSave(dir.File("existing.nds"), "EXISTING_LEVEL", 5, 4, 5, 6);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    const std::tm timeValue = FixedTime();

    // List row, open the prompt, No, then cancel the save screen itself.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiSave_RunSaveScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), "Test Level", timeValue, aPath);
    CHECK(result == 2);
    CHECK(aPath[0] == '\0');
}

TEST_CASE("the save screen cancels without a path")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    const std::tm timeValue = FixedTime();

    testContext.platform.PushKey(platform::Key::Escape);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiSave_RunSaveScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), "Test Level", timeValue, aPath);
    CHECK(result == 2);
    CHECK(aPath[0] == '\0');
}

TEST_CASE("the load screen returns the selected path")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    WriteSave(dir.File("one.nds"), "LEVEL_ONE", 50, 1, 2, 3);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKey(platform::Key::Enter);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiLoad_RunLoadScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), false, aPath);
    CHECK(result == 1);
    CHECK(std::string(aPath) == dir.EngineDir() + "one.nds");
}

TEST_CASE("the load warning answers Yes and No as in the original dialog")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    WriteSave(dir.File("one.nds"), "LEVEL_ONE", 50, 1, 2, 3);
    ui::NullPainter painter;

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Enter);

        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        const int result = JonesUiLoad_RunLoadScreen(ui, &painter, nullptr,
            *testContext.pFileSystem, dir.EngineDir().c_str(), true, aPath);
        CHECK(result == 1);
        CHECK(std::string(aPath) == dir.EngineDir() + "one.nds");
    }

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        testContext.platform.PushKey(platform::Key::Escape);

        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        const int result = JonesUiLoad_RunLoadScreen(ui, &painter, nullptr,
            *testContext.pFileSystem, dir.EngineDir().c_str(), true, aPath);
        CHECK(result == 2);
        CHECK(aPath[0] == '\0');
    }
}

TEST_CASE("the load screen cancels without a path")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    WriteSave(dir.File("one.nds"), "LEVEL_ONE", 50, 1, 2, 3);
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKey(platform::Key::Escape);

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiLoad_RunLoadScreen(ui, &painter, nullptr, *testContext.pFileSystem,
        dir.EngineDir().c_str(), false, aPath);
    CHECK(result == 2);
    CHECK(aPath[0] == '\0');
}

TEST_CASE("the exit screen answers exit, save first and cancel")
{
    ScreenTestContext testContext;
    SaveTestDir dir;
    ui::NullPainter painter;
    const std::tm timeValue = FixedTime();

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Enter);
        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        CHECK(JonesUiExit_RunExitScreen(ui, &painter, nullptr, *testContext.pFileSystem,
            dir.EngineDir().c_str(), "Test Level", timeValue, aPath) == 2);
    }

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        CHECK(JonesUiExit_RunExitScreen(ui, &painter, nullptr, *testContext.pFileSystem,
            dir.EngineDir().c_str(), "Test Level", timeValue, aPath) == 1);
    }

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Enter);
        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        CHECK(JonesUiExit_RunExitScreen(ui, &painter, nullptr, *testContext.pFileSystem,
            dir.EngineDir().c_str(), "Test Level", timeValue, aPath) == 1187);
        CHECK(std::string(aPath) == dir.EngineDir() + "Test Level 2026-11-07 13-05-09.nds");
    }

    {
        ui::Ui ui(painter, testContext.cvars);
        testContext.platform.PushKey(platform::Key::Down);
        testContext.platform.PushKey(platform::Key::Enter);
        testContext.platform.PushKey(platform::Key::Escape);
        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        CHECK(JonesUiExit_RunExitScreen(ui, &painter, nullptr, *testContext.pFileSystem,
            dir.EngineDir().c_str(), "Test Level", timeValue, aPath) == 2);
    }
}

TEST_CASE("the headless wrappers answer the Null stub values")
{
    ScreenTestContext testContext;

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    CHECK(jonesConfig_GetSaveGameFilePath(NULL, aPath) == 2);
    CHECK(jonesConfig_GetLoadGameFilePath(NULL, aPath) == 2);
    CHECK(jonesConfig_ShowExitGameDialog(NULL, aPath) == 2);
}
