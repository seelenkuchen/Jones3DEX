#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <Jones3D/Main/JonesData.h>
#include <Jones3D/Main/JonesDataLocate.h>
#include <Jones3D/Main/JonesInstall.h>
#include <Jones3D/Platform/Babylon/JonesGogInstaller.h>
#include <Jones3D/Platform/Babylon/JonesSetupWindow.h>
#include <Jones3D/Platform/Babylon/JonesSetupWizard.h>

#include <Jones3D/Platform/Babylon/JonesUtf8Path.h>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/process/NullProcess.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace
{
    tHostServices g_hostServices;

    int J3DAPI TestPrintf(const char* pFormat, ...)
    {
        va_list args;
        va_start(args, pFormat);
        int result = vfprintf(stderr, pFormat, args);
        va_end(args);
        return result;
    }

    std::atomic<unsigned int> g_tempCounter{ 0 };

    std::filesystem::path MakeTempRoot()
    {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        for ( unsigned int attempt = 0; attempt < 1000; ++attempt )
        {
            const std::filesystem::path path = base / ("jones3d-setup-test-"
                + std::to_string(++g_tempCounter));
            if ( std::filesystem::create_directory(path, ec) )
            {
                return path;
            }
            ec.clear();
        }

        return std::filesystem::path();
    }

    void MakeDirs(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::create_directories(JonesUtf8Path_From(path), ec);
    }

    void WriteText(const std::string& dir, const char* pName, const std::string& text)
    {
        MakeDirs(dir);
        std::ofstream file(JonesUtf8Path_From(dir) / JonesUtf8Path_From(pName),
            std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        REQUIRE(file.good());
    }

    void WriteRequiredSet(const std::string& dir)
    {
        WriteText(dir, "Jones3D.GOB", "jones");
        WriteText(dir, "CD1.GOB", "cd1");
        WriteText(dir, "CD2.GOB", "cd2");
    }

    std::string Join(const std::string& left, const std::string& right)
    {
        if ( left.empty() )
        {
            return right;
        }

        return left + "/" + right;
    }

    bool Exists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::exists(JonesUtf8Path_From(path), ec) && !ec;
    }

    std::string NormalizeSlashes(std::string path)
    {
        for ( char& chr : path )
        {
            if ( chr == '\\' )
            {
                chr = '/';
            }
        }
        return path;
    }

    bool SamePath(const std::string& left, const std::string& right)
    {
        return NormalizeSlashes(left) == NormalizeSlashes(right);
    }

    std::size_t FindRow(const JonesSetupWizard& wizard, JonesSetupRowKind kind)
    {
        const std::vector<JonesSetupRow>& rows = wizard.Rows();
        for ( std::size_t i = 0; i < rows.size(); ++i )
        {
            if ( rows[i].kind == kind )
            {
                return i;
            }
        }

        return rows.size();
    }

    std::size_t FindSourceRow(const JonesSetupWizard& wizard, JonesInstallSourceKind kind,
        int discNumber, const std::string& path)
    {
        const std::vector<JonesSetupRow>& rows = wizard.Rows();
        for ( std::size_t i = 0; i < rows.size(); ++i )
        {
            if ( rows[i].kind != JonesSetupRowKind::Source )
            {
                continue;
            }

            if ( rows[i].source.kind == kind
                && (discNumber == 0 || rows[i].source.discNumber == discNumber)
                && (path.empty() || SamePath(rows[i].source.aPath, path)) )
            {
                return i;
            }
        }

        return rows.size();
    }

    constexpr float kWizardTestCanvasWidth = 768.0f;

    babylon::ui::Screen BuildTestScreen(const JonesSetupWizard& wizard,
        babylon::ui::NullPainter& painter, babylon::ui::Skin& skin,
        std::size_t initialFocus = babylon::ui::kNoWidget)
    {
        skin = JonesSetupWindow_CreateSkin(0, 0, &painter);
        return JonesSetupWindow_BuildScreen(wizard, painter, kWizardTestCanvasWidth, skin,
            initialFocus);
    }

    void CheckScreenLayout(const JonesSetupWizard& wizard, const std::string& title)
    {
        babylon::ui::NullPainter painter;
        babylon::ui::Skin skin;
        const babylon::ui::Screen screen = BuildTestScreen(wizard, painter, skin);

        CHECK_FALSE(screen.title.empty());
        CHECK(screen.title == title);
        CHECK_FALSE(screen.widgets.empty());

        babylon::cvars::Registry cvars;
        babylon::ui::Ui ui(painter, cvars);
        REQUIRE(ui.setSkin(skin));
        REQUIRE(ui.setViewport(1280, 800));
        const babylon::ui::ScreenId screenId = ui.push(screen);
        REQUIRE(screenId != babylon::ui::kInvalidScreen);
        ui.draw();

        bool bFoundTitle = false;
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const babylon::ui::RecordedDrawText* pText =
                std::get_if<babylon::ui::RecordedDrawText>(&call) )
            {
                if ( pText->text == title )
                {
                    bFoundTitle = true;
                }
            }
        }

        CHECK(bFoundTitle);
    }

    struct SetupWizardTest
    {
        std::filesystem::path root;
        std::unique_ptr<babylon::files::FileSystem> fs;
        StdBabylonContext context = {};
        babylon::platform::NullPlatform platform;
        babylon::process::NullProcess process;

        std::string home;
        std::string userDir;
        std::string exeDir;
        std::string downloads;
        // Added: the Windows Steam and GOG folders live under %ProgramFiles(x86)%, not the home
        // directory; the fixture points the host at these temp folders so the search stays sandboxed.
        std::string programFiles;
        std::string programFilesX86;
        JonesInstallPathList volumeRoots = {};

        SetupWizardTest()
        {
            root = MakeTempRoot();
            REQUIRE_FALSE(root.empty());
            home = (root / "home").string();
            userDir = (root / "user").string();
            exeDir = (root / "exe").string();
            downloads = Join(home, "Downloads");
            programFiles = (root / "programfiles").string();
            programFilesX86 = (root / "programfilesx86").string();

            fs = babylon::files::createSdlFileSystem();
            REQUIRE(fs);
            context.pFileSystem = fs.get();
            stdBabylon_SetContext(&context);

            std::memset(&g_hostServices, 0, sizeof(g_hostServices));
            g_hostServices.pStatusPrint  = TestPrintf;
            g_hostServices.pMessagePrint = TestPrintf;
            g_hostServices.pWarningPrint = TestPrintf;
            g_hostServices.pErrorPrint   = TestPrintf;
            g_hostServices.pDebugPrint   = TestPrintf;
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
            stdStartup(&g_hostServices);
        }

        ~SetupWizardTest()
        {
            stdShutdown();
            stdBabylon_SetContext(nullptr);
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        JonesInstallHost MakeHost(const std::vector<std::string>& pVolumeRoots = {})
        {
            JonesInstallHost host = {};
            host.platform = JONESINSTALL_PLATFORM_LINUX;
#ifdef _WIN32
            host.platform = JONESINSTALL_PLATFORM_WINDOWS;
            host.pProgramFiles = programFiles.c_str();
            host.pProgramFilesX86 = programFilesX86.c_str();
#endif
            host.pHome = home.c_str();
            host.pXdgDownloadDir = downloads.c_str();
            volumeRoots = JonesInstallPathList();
            for ( const std::string& rootPath : pVolumeRoots )
            {
                if ( volumeRoots.numPaths >= JONESINSTALL_MAX_HOST_PATHS )
                {
                    break;
                }

                std::snprintf(volumeRoots.aPaths[volumeRoots.numPaths++],
                    JONESINSTALL_MAX_PATH, "%s", rootPath.c_str());
            }
            if ( volumeRoots.numPaths > 0 )
            {
                host.pVolumeRoots = &volumeRoots;
            }

            return host;
        }

        std::string CompleteFolder(const std::string& name)
        {
            const std::string game = Join(root.string(), name);
            WriteRequiredSet(Join(game, "Resource"));
            return game;
        }

        void MakeDisc1(const std::string& disc)
        {
            MakeDirs(Join(disc, "install"));
            WriteText(Join(disc, "install"), "license.txt", "This is the disc licence.");
            MakeDirs(Join(disc, "Resource"));
            WriteText(Join(disc, "Resource"), "Jones3D.GOB", "jones");
            WriteText(Join(disc, "Resource"), "CD1.GOB", "cd1");
        }

        void MakeDisc2(const std::string& disc)
        {
            MakeDirs(Join(disc, "Resource"));
            WriteText(Join(disc, "Resource"), "CD2.GOB", "cd2");
        }
    };
}

TEST_CASE_FIXTURE(SetupWizardTest, "Sources appear in the model under the fake home")
{
    // The finder looks for the retail folder name, so place the complete set there. Windows
    // searches %ProgramFiles(x86)%\Steam and %ProgramFiles%\Steam; Linux searches ~/.steam.
#ifdef _WIN32
    const std::string steamGame = Join(programFilesX86,
        "Steam/steamapps/common/Indiana Jones and the Infernal Machine");
#else
    const std::string steamGame = Join(home,
        ".steam/steam/steamapps/common/Indiana Jones and the Infernal Machine");
#endif
    WriteRequiredSet(Join(steamGame, "Resource"));

    JonesInstallHost host = MakeHost();
    JonesSetupWizard wizard(platform, process, host, userDir, exeDir);
    wizard.Start();

    CHECK(FindSourceRow(wizard, JONESINSTALL_SOURCE_STEAM, 0, steamGame)
        != wizard.Rows().size());

    WriteText(downloads, "setup_indiana_jones_and_the_infernal_machine_1.2.exe", "setup");

    // Without innoextract no GOG installer source and no installer picker row appear.
    process.setExists("innoextract", false);
    JonesSetupWizard withoutInno(platform, process, host, userDir, exeDir);
    withoutInno.Start();
    CHECK(FindSourceRow(withoutInno, JONESINSTALL_SOURCE_GOG_INSTALLER, 0, "")
        == withoutInno.Rows().size());
    CHECK(FindRow(withoutInno, JonesSetupRowKind::ChooseInstaller)
        == withoutInno.Rows().size());

    // With innoextract both appear.
    process.setExists("innoextract", true);
    JonesSetupWizard withInno(platform, process, host, userDir, exeDir);
    withInno.Start();
    CHECK(FindSourceRow(withInno, JONESINSTALL_SOURCE_GOG_INSTALLER, 0, "")
        != withInno.Rows().size());
    CHECK(FindRow(withInno, JonesSetupRowKind::ChooseInstaller)
        != withInno.Rows().size());

}

TEST_CASE_FIXTURE(SetupWizardTest, "An incomplete folder stays on Source with the missing names")
{
    const std::string folder = Join(root.string(), "incomplete");
    WriteText(folder, "Jones3D.GOB", "jones");

    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();
    CHECK(wizard.Step() == JonesSetupStep::Source);

    CHECK_FALSE(wizard.ChooseFolder(folder));
    CHECK(wizard.Step() == JonesSetupStep::Source);
    CHECK(wizard.Message().find("doesn't contain the game files") != std::string::npos);
    CHECK(wizard.Message().find("CD1.GOB") != std::string::npos);
    CHECK(wizard.Message().find("CD2.GOB") != std::string::npos);

    CheckScreenLayout(wizard, "Where is your copy of the game?");
}

TEST_CASE_FIXTURE(SetupWizardTest, "Picker Cancelled keeps Source and Unavailable reports it")
{
    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();

    const std::size_t row = FindRow(wizard, JonesSetupRowKind::ChooseFolder);
    REQUIRE(row != wizard.Rows().size());

    platform.setFolderAnswer({ babylon::platform::PathChoiceResult::Cancelled, std::string() });
    CHECK(wizard.SelectRow(row));
    CHECK(wizard.Step() == JonesSetupStep::Source);
    CHECK(wizard.Message().empty());

    platform.setFolderAnswer({ babylon::platform::PathChoiceResult::Unavailable, std::string() });
    CHECK(wizard.SelectRow(row));
    CHECK(wizard.Step() == JonesSetupStep::Source);
    CHECK(wizard.Message() == "No file dialog is available on this system.");
}

TEST_CASE_FIXTURE(SetupWizardTest, "A folder installs into the user data folder and finishes")
{
    const std::string folder = CompleteFolder("folder-game");

    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();
    REQUIRE(wizard.ChooseFolder(folder));
    CHECK(wizard.Step() == JonesSetupStep::Location);
    CHECK(SamePath(wizard.SelectedUserDir(), userDir));
    CheckScreenLayout(wizard, "Where should Jones3D EX keep its files?");

    wizard.StartInstall();

    CHECK(wizard.Step() == JonesSetupStep::Done);
    CHECK(wizard.Report().bComplete);
    const std::string dataDir = Join(userDir, JONESDATALOCATE_GAMEDATA_DIR);
    CHECK(SamePath(wizard.TargetDataDir(), dataDir));
    CHECK(Exists(Join(Join(dataDir, "Resource"), "Jones3D.GOB")));
    CHECK(Exists(Join(Join(dataDir, "Resource"), "CD1.GOB")));
    CHECK(Exists(Join(Join(dataDir, "Resource"), "CD2.GOB")));
    CHECK(Exists(Join(userDir, "datacheck.txt")));

    CheckScreenLayout(wizard, "Setup complete");
}

TEST_CASE_FIXTURE(SetupWizardTest, "A folder install with non-ASCII source and user paths")
{
    const std::string source = JonesUtf8Path_To(
        root / JonesUtf8Path_From("Quelle-\xC3\xBC"));
    WriteRequiredSet(Join(source, "Resource"));

    const std::string nonAsciiUser = JonesUtf8Path_To(
        root / JonesUtf8Path_From("Benutzer-\xC3\xBC"));

    JonesSetupWizard wizard(platform, process, MakeHost(), nonAsciiUser, exeDir);
    wizard.Start();
    wizard.Continue();
    REQUIRE(wizard.ChooseFolder(source));
    CHECK(wizard.Step() == JonesSetupStep::Location);

    wizard.StartInstall();

    CHECK(wizard.Step() == JonesSetupStep::Done);
    CHECK(wizard.Report().bComplete);
    const std::string dataDir = Join(nonAsciiUser, JONESDATALOCATE_GAMEDATA_DIR);
    CHECK(Exists(Join(Join(dataDir, "Resource"), "Jones3D.GOB")));
    CHECK(Exists(Join(nonAsciiUser, "datacheck.txt")));
}

TEST_CASE_FIXTURE(SetupWizardTest, "A disc copy reads the licence, asks for disc 2 and finishes")
{
    const std::string media = Join(root.string(), "media");
    const std::string disc1 = Join(media, "disc1");
    const std::string disc2 = Join(media, "disc2");
    MakeDisc1(disc1);
    MakeDisc2(disc2);

    // Windows only finds a disc at a volume root itself; Linux also checks the immediate
    // subdirectories of a mount base, so the shared media folder works there.
#ifdef _WIN32
    JonesSetupWizard wizard(platform, process, MakeHost({ disc1, disc2 }), userDir, exeDir);
#else
    JonesSetupWizard wizard(platform, process, MakeHost({ media }), userDir, exeDir);
#endif
    wizard.Start();
    wizard.Continue();

    const std::size_t disc1Row = FindSourceRow(wizard, JONESINSTALL_SOURCE_DISC, 1, disc1);
    REQUIRE(disc1Row != wizard.Rows().size());
    REQUIRE(wizard.SelectRow(disc1Row));

    CHECK(wizard.Step() == JonesSetupStep::Licence);
    CHECK(wizard.LicenceText().find("disc licence") != std::string::npos);
    CheckScreenLayout(wizard, "Licence agreement");

    wizard.AcceptLicence();
    CHECK(wizard.Step() == JonesSetupStep::Location);

    wizard.StartInstall();
    CHECK(wizard.Step() == JonesSetupStep::Disc2);
    CheckScreenLayout(wizard, "Insert disc 2, then choose it.");

    const std::size_t disc2Row = FindSourceRow(wizard, JONESINSTALL_SOURCE_DISC, 2, disc2);
    REQUIRE(disc2Row != wizard.Rows().size());
    CHECK(wizard.SelectRow(disc2Row));
    CHECK(wizard.TakeDisc2Run());

    wizard.StartInstall();
    CHECK(wizard.Step() == JonesSetupStep::Done);
    CHECK(wizard.Report().bComplete);
    CHECK(Exists(Join(Join(wizard.TargetDataDir(), "Resource"), "CD2.GOB")));
}

TEST_CASE_FIXTURE(SetupWizardTest, "A GOG installer extracts into setup-tmp and removes it")
{
    WriteText(downloads, "setup_indiana_jones_and_the_infernal_machine_1.2.exe", "setup");
    process.setExists("innoextract", true);

    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();

    const std::size_t installerRow = FindSourceRow(wizard,
        JONESINSTALL_SOURCE_GOG_INSTALLER, 0, "");
    REQUIRE(installerRow != wizard.Rows().size());

    {
        babylon::ui::NullPainter sourcePainter;
        babylon::ui::Skin sourceSkin;
        const babylon::ui::Screen sourceScreen = BuildTestScreen(wizard, sourcePainter,
            sourceSkin);
        bool bFoundInstaller = false;
        for ( const babylon::ui::Widget& widget : sourceScreen.widgets )
        {
            const babylon::ui::Button* pButton = std::get_if<babylon::ui::Button>(&widget);
            if ( !pButton || pButton->text.rfind("GOG installer - ", 0) != 0 )
            {
                continue;
            }

            bFoundInstaller = true;
            CHECK(pButton->text.find("...") != std::string::npos);
            CHECK(pButton->text.find(".exe") != std::string::npos);
            CHECK(sourcePainter.measureText(sourceSkin.textFont, pButton->text).width
                <= 655.0f);
        }
        CHECK(bFoundInstaller);
    }

    std::string extractDir;
    JonesSetupWizard::TestHooks hooks;
    hooks.readEula = [](const std::string&, const std::string&, std::string& outText,
        std::string&) -> bool
    {
        outText = "Disney Terms of Use\n"
            "http://disneytermsofuse.com/\n"
            "\n\n\n"
            "Line four has enough words to wrap in the wizard's text column.\n";
        return true;
    };
    hooks.extract = [&extractDir](const std::string& dir,
        const JonesGogProgressFunc& progress, const babylon::process::CancelCheck&,
        std::string&) -> JonesGogExtractResult
    {
        extractDir = dir;
        WriteRequiredSet(Join(dir, "Resource"));
        if ( progress )
        {
            progress(100.0, "done");
        }
        return JonesGogExtractResult::Ok;
    };
    wizard.SetTestHooks(hooks);

    REQUIRE(wizard.SelectRow(installerRow));
    CHECK(wizard.Step() == JonesSetupStep::Licence);
    CHECK(wizard.LicenceText().find("Disney Terms of Use") != std::string::npos);
    {
        babylon::ui::NullPainter licencePainter;
        babylon::ui::Skin licenceSkin = JonesSetupWindow_CreateSkin(0, 0, &licencePainter);
        const std::vector<std::string> lines = JonesSetupWindow_WrapLicenceText(
            wizard.LicenceText(), licencePainter, licenceSkin, kWizardTestCanvasWidth);
        for ( std::size_t i = 1; i < lines.size(); ++i )
        {
            const bool bTwoBlanks = lines[i].empty() && lines[i - 1].empty();
            CHECK_FALSE(bTwoBlanks);
        }

        const babylon::ui::Screen licenceScreen = BuildTestScreen(wizard, licencePainter,
            licenceSkin);
        const std::size_t visible = JonesSetupWindow_LicenceVisibleLines(licenceSkin);
        REQUIRE(licenceScreen.widgets.size() >= visible + 4);
        const babylon::ui::Label* pHint =
            std::get_if<babylon::ui::Label>(&licenceScreen.widgets[visible]);
        REQUIRE(pHint != nullptr);
        CHECK(pHint->text.rfind("Lines 1-", 0) == 0);
        CHECK(pHint->text.find(" of ") != std::string::npos);
    }
    wizard.AcceptLicence();
    CHECK(wizard.Step() == JonesSetupStep::Location);

    wizard.StartInstall();
    CHECK(wizard.Step() == JonesSetupStep::Done);
    CHECK(wizard.Report().bComplete);

    const std::string expectedExtract = Join(Join(userDir, "setup-tmp"), "extract");
    CHECK(SamePath(extractDir, expectedExtract));
    CHECK_FALSE(Exists(Join(userDir, "setup-tmp")));
    CHECK(Exists(Join(Join(wizard.TargetDataDir(), "Resource"), "Jones3D.GOB")));
}

TEST_CASE_FIXTURE(SetupWizardTest, "A cancel during the copy removes setup-tmp")
{
    const std::string folder = CompleteFolder("cancel-game");
    const std::string setupTmp = Join(userDir, "setup-tmp");
    WriteText(setupTmp, "leftover.txt", "leftover");

    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();
    REQUIRE(wizard.ChooseFolder(folder));
    CHECK(wizard.Step() == JonesSetupStep::Location);

    int numPumps = 0;
    wizard.SetPump([&wizard, &numPumps]()
    {
        ++numPumps;
        wizard.RequestCancel();
    });

    wizard.StartInstall();

    CHECK(numPumps > 0);
    CHECK(wizard.Step() == JonesSetupStep::Cancelled);
    CHECK(wizard.Message() == "Setup was cancelled.");
    CHECK_FALSE(Exists(setupTmp));
    CheckScreenLayout(wizard, "Setup cancelled");
}

TEST_CASE_FIXTURE(SetupWizardTest, "Next to the game is offered only for a writable different exe folder")
{
    MakeDirs(userDir);
    MakeDirs(exeDir);

    JonesSetupWizard offered(platform, process, MakeHost(), userDir, exeDir);
    offered.Start();
    CHECK(offered.NextToGameAvailable());
    offered.SetNextToGame(true);
    CHECK(offered.NextToGame());
    CHECK(SamePath(offered.TargetBase(), exeDir));
    CHECK(SamePath(offered.SelectedUserDir(), exeDir));

    JonesSetupWizard same(platform, process, MakeHost(), userDir, userDir);
    same.Start();
    CHECK_FALSE(same.NextToGameAvailable());

    JonesSetupWizard unknown(platform, process, MakeHost(), userDir, std::string());
    unknown.Start();
    CHECK_FALSE(unknown.NextToGameAvailable());

    JonesSetupWizard missing(platform, process, MakeHost(), userDir,
        Join(root.string(), "does-not-exist"));
    missing.Start();
    CHECK_FALSE(missing.NextToGameAvailable());

    JonesSetupWizard workingDirectory(platform, process, MakeHost(), std::string(), exeDir);
    workingDirectory.Start();
    CHECK_FALSE(workingDirectory.UserDirForDisplay().empty());
    CHECK(workingDirectory.UserDirForDisplay() != ".");
}

TEST_CASE_FIXTURE(SetupWizardTest, "A rebuilt screen can keep the activated row focused")
{
    MakeDirs(exeDir);
    const std::string folder = CompleteFolder("focus-game");

    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    wizard.Continue();
    REQUIRE(wizard.ChooseFolder(folder));
    REQUIRE(wizard.Step() == JonesSetupStep::Location);
    REQUIRE(wizard.NextToGameAvailable());

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    const babylon::ui::Screen screen = BuildTestScreen(wizard, painter, skin, 2);

    CHECK(screen.initialFocus == 2);
    REQUIRE(screen.widgets.size() > 2);
    const babylon::ui::Button* pButton =
        std::get_if<babylon::ui::Button>(&screen.widgets[2]);
    REQUIRE(pButton != nullptr);
    CHECK(pButton->text.rfind("Next to the game", 0) == 0);
}

TEST_CASE_FIXTURE(SetupWizardTest, "Welcome text wraps by the measured column width")
{
    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    const babylon::ui::Screen screen = BuildTestScreen(wizard, painter, skin);

    std::string allText;
    for ( const babylon::ui::Widget& widget : screen.widgets )
    {
        const babylon::ui::Label* pLabel = std::get_if<babylon::ui::Label>(&widget);
        if ( !pLabel )
        {
            continue;
        }

        if ( pLabel->text.find(' ') != std::string::npos )
        {
            CHECK(painter.measureText(skin.textFont, pLabel->text).width <= 655.0f);
        }

        allText += pLabel->text;
        allText += ' ';
    }

    CHECK(allText.find("Infernal Machine") != std::string::npos);
    const bool bHasSteam = allText.find("steam or gog") != std::string::npos
        || allText.find("Steam or GOG") != std::string::npos;
    CHECK(bHasSteam);
}

TEST_CASE_FIXTURE(SetupWizardTest, "Every screen lays out with a NullPainter")
{
    JonesSetupWizard wizard(platform, process, MakeHost(), userDir, exeDir);
    wizard.Start();
    CheckScreenLayout(wizard, "Jones3D EX Setup");

    wizard.Continue();
    CheckScreenLayout(wizard, "Where is your copy of the game?");

    const std::string folder = CompleteFolder("layout-game");
    REQUIRE(wizard.ChooseFolder(folder));
    CheckScreenLayout(wizard, "Where should Jones3D EX keep its files?");

    wizard.BeginInstall();
    CheckScreenLayout(wizard, "Installing...");
    wizard.RunInstall();
    CheckScreenLayout(wizard, "Setup complete");

    JonesSetupWizard empty(platform, process, MakeHost(), userDir, exeDir);
    empty.Start();
    empty.BeginInstall();
    CHECK(empty.Step() == JonesSetupStep::Error);
    CheckScreenLayout(empty, "Setup failed");
}

TEST_CASE("The progress bar fill matches the fraction and contrasts with its track")
{
    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin = JonesSetupWindow_CreateSkin(0, 0, &painter);
    babylon::cvars::Registry cvars;
    babylon::ui::Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(1280, 800));

    const double fractions[] = { 0.0, 0.5, 1.0 };
    for ( const double fraction : fractions )
    {
        painter.clearRecorded();
        JonesSetupWindow_DrawProgressBar(painter, ui, skin, fraction);

        std::vector<babylon::ui::RecordedFillRect> fills;
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const babylon::ui::RecordedFillRect* pFill =
                std::get_if<babylon::ui::RecordedFillRect>(&call) )
            {
                fills.push_back(*pFill);
            }
        }

        REQUIRE(fills.size() >= 2);
        const babylon::ui::RecordedFillRect& track = fills[fills.size() - 2];
        const babylon::ui::RecordedFillRect& fill = fills[fills.size() - 1];
        CHECK(track.rect.width > 0.0f);
        CHECK(fill.rect.width == doctest::Approx(
            static_cast<double>(track.rect.width) * fraction));
        CHECK_FALSE(fill.color == track.color);
    }
}

int main(int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
