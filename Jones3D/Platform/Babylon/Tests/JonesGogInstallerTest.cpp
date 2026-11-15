// The tests of the Babylon column's GOG offline installer driver. They drive the driver with
// babylon::process::NullProcess, so no installer or innoextract program is needed. The output lines
// and the argv shapes come from innoextract 1.9 with the GOG installer (see the brief's measured
// sample).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <babylon/process/NullProcess.h>
#include <babylon/process/Process.h>

#include <Jones3D/Platform/Babylon/JonesGogInstaller.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct TempDir
    {
        TempDir()
        {
            static unsigned counter = 0;
            std::random_device random;
            path = fs::temp_directory_path()
                / ("jones-gog-installer-test-" + std::to_string(random()) + "-"
                    + std::to_string(++counter));
            std::error_code error;
            fs::remove_all(path, error);
            fs::create_directories(path, error);
        }

        ~TempDir()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }

        fs::path path;
    };

    void WriteFile(const fs::path& path, std::string_view content)
    {
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
    }
}

TEST_CASE("FindInnoextract prefers the program beside the executable")
{
    TempDir temp;
    const fs::path exeDir = temp.path / "exe";

    for ( const bool bWindows : { false, true } )
    {
        babylon::process::NullProcess nullProcess;
        babylon::process::Process& process = nullProcess;
        const std::string name = bWindows ? "innoextract.exe" : "innoextract";
        const std::string fullPath = (exeDir / name).string();
        nullProcess.setExists(fullPath, true);
        nullProcess.setExists("innoextract", true);

        CHECK(JonesGogInstaller_FindInnoextract(process, exeDir.string(), bWindows) == fullPath);
    }
}

TEST_CASE("FindInnoextract falls back to the search path")
{
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    nullProcess.setExists("innoextract", true);

    CHECK(JonesGogInstaller_FindInnoextract(process, "/nowhere", false) == "innoextract");

    babylon::process::NullProcess emptyDirProcess;
    babylon::process::Process& emptyDir = emptyDirProcess;
    emptyDirProcess.setExists("innoextract", true);

    CHECK(JonesGogInstaller_FindInnoextract(emptyDir, "", true) == "innoextract");
}

TEST_CASE("FindInnoextract returns empty when nothing can be run")
{
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;

    CHECK(JonesGogInstaller_FindInnoextract(process, "", false).empty());
    CHECK(JonesGogInstaller_FindInnoextract(process, "/nowhere", true).empty());
}

TEST_CASE("ParseLine reads a file line")
{
    const JonesGogProgressLine line =
        JonesGogInstaller_ParseLine("\x1b[K - \"Resource/CD1.GOB\" [en-US]");

    CHECK(line.bHasFile);
    CHECK(line.file == "Resource/CD1.GOB");
    CHECK_FALSE(line.bHasPercent);
}

TEST_CASE("ParseLine reads a progress bar")
{
    const JonesGogProgressLine line =
        JonesGogInstaller_ParseLine("\x1b[K[=>     ]  0.9%   147 MiB/s");

    CHECK_FALSE(line.bHasFile);
    CHECK(line.bHasPercent);
    CHECK(line.percent == doctest::Approx(0.9));
}

TEST_CASE("ParseLine carries neither on header and done lines")
{
    const JonesGogProgressLine header = JonesGogInstaller_ParseLine(
        "Extracting \"Indiana Jones\xC2\xAE and the Infernal Machine\xE2\x84\xA2\""
        " - setup data version 5.6.2 (unicode)");
    CHECK_FALSE(header.bHasFile);
    CHECK_FALSE(header.bHasPercent);

    const JonesGogProgressLine done = JonesGogInstaller_ParseLine("\x1b[K\x1b[KDone.");
    CHECK_FALSE(done.bHasFile);
    CHECK_FALSE(done.bHasPercent);
}

TEST_CASE("ParseLine takes the percent and ignores a lone percent sign")
{
    const JonesGogProgressLine high = JonesGogInstaller_ParseLine(
        "\x1b[K[==========================================================>] 99.9%   144 MiB/s");
    CHECK(high.bHasPercent);
    CHECK(high.percent == doctest::Approx(99.9));

    const JonesGogProgressLine clamped = JonesGogInstaller_ParseLine("150%");
    CHECK(clamped.bHasPercent);
    CHECK(clamped.percent == doctest::Approx(100.0));

    const JonesGogProgressLine full = JonesGogInstaller_ParseLine("100.0%");
    CHECK(full.bHasPercent);
    CHECK(full.percent == doctest::Approx(100.0));

    const JonesGogProgressLine lone = JonesGogInstaller_ParseLine("%");
    CHECK_FALSE(lone.bHasPercent);

    const JonesGogProgressLine last = JonesGogInstaller_ParseLine("10% 20%");
    CHECK(last.bHasPercent);
    CHECK(last.percent == doctest::Approx(20.0));
}

TEST_CASE("ParseLine handles an empty line and an escape at the end")
{
    const JonesGogProgressLine empty = JonesGogInstaller_ParseLine("");
    CHECK_FALSE(empty.bHasFile);
    CHECK_FALSE(empty.bHasPercent);

    const JonesGogProgressLine bareEscape = JonesGogInstaller_ParseLine("\x1b[K");
    CHECK_FALSE(bareEscape.bHasFile);
    CHECK_FALSE(bareEscape.bHasPercent);

    const JonesGogProgressLine trailingEscape = JonesGogInstaller_ParseLine("Done.\x1b[");
    CHECK_FALSE(trailingEscape.bHasFile);
    CHECK_FALSE(trailingEscape.bHasPercent);
}

TEST_CASE("ReadEula runs the licence command and decodes the text")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;

    const std::string installerPath =
        (temp.path / "setup_indiana_jones_and_the_infernal_machine_1.0.exe").string();
    WriteFile(temp.path / "tmp" / "EULA_en-US.txt",
        std::string("\xEF\xBB\xBF") + "a\r\nb\r\n");

    std::string text;
    std::string error;
    REQUIRE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        temp.path.string(), text, error));

    CHECK(text == "a\nb\n");
    CHECK(error.empty());

    const std::vector<std::string> expected{
        "innoextract",
        "--color=0",
        "--progress=0",
        "-d",
        temp.path.string(),
        "-I",
        "tmp/EULA_en-US.txt",
        "-I",
        "tmp/EULA.txt",
        installerPath,
    };
    const std::optional<std::vector<std::string>> argv = nullProcess.lastArgv();
    REQUIRE(argv.has_value());
    CHECK(*argv == expected);
}

TEST_CASE("ReadEula reads the licence from a non-ASCII folder")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const fs::path folder = temp.path / fs::path(u8"Spiele-\u00FC");
    const fs::path installer = folder / "setup.exe";
    const std::u8string folderUtf8 = folder.u8string();
    const std::u8string installerUtf8 = installer.u8string();
    const std::string folderText(reinterpret_cast<const char*>(folderUtf8.data()),
        folderUtf8.size());
    const std::string installerText(reinterpret_cast<const char*>(installerUtf8.data()),
        installerUtf8.size());
    WriteFile(folder / "tmp" / "EULA_en-US.txt", "unicode folder");

    std::string text;
    std::string error;
    REQUIRE(JonesGogInstaller_ReadEula(process, "innoextract", installerText, folderText, text,
        error));

    CHECK(text == "unicode folder");
    CHECK(error.empty());

    const std::optional<std::vector<std::string>> argv = nullProcess.lastArgv();
    REQUIRE(argv.has_value());
    REQUIRE(argv->size() == 10);
    CHECK((*argv)[4] == folderText);
}

TEST_CASE("ReadEula falls back to tmp/EULA.txt")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup.exe").string();
    WriteFile(temp.path / "tmp" / "EULA.txt", "fallback text");

    std::string text;
    std::string error;
    REQUIRE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        temp.path.string(), text, error));

    CHECK(text == "fallback text");
    CHECK(error.empty());
}

TEST_CASE("ReadEula fails when the installer has no licence file")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup_indiana.exe").string();

    std::string text = "stale";
    std::string error;
    CHECK_FALSE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        temp.path.string(), text, error));

    CHECK(text.empty());
    CHECK(error.find("setup_indiana.exe") != std::string::npos);
    CHECK(error.find(installerPath) == std::string::npos);
}

TEST_CASE("ReadEula fails on a non-zero exit code")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    nullProcess.setExitCode(1);
    const std::string installerPath = (temp.path / "setup.exe").string();
    WriteFile(temp.path / "tmp" / "EULA_en-US.txt", "text");

    std::string text = "stale";
    std::string error;
    CHECK_FALSE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        temp.path.string(), text, error));

    CHECK(text.empty());
    CHECK(error.find("setup.exe") != std::string::npos);
}

TEST_CASE("ReadEula fails when innoextract cannot start")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup.exe").string();

    std::string text = "stale";
    std::string error;
    CHECK_FALSE(JonesGogInstaller_ReadEula(process, "", installerPath,
        temp.path.string(), text, error));

    CHECK(text.empty());
    CHECK_FALSE(error.empty());
    CHECK(error.find("setup.exe") != std::string::npos);
}

TEST_CASE("ReadEula refuses a licence file larger than 1 MiB")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup.exe").string();
    WriteFile(temp.path / "tmp" / "EULA_en-US.txt", std::string(1024 * 1024 + 1, 'x'));

    std::string text = "stale";
    std::string error;
    CHECK_FALSE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        temp.path.string(), text, error));

    CHECK(text.empty());
    CHECK(error.find("setup.exe") != std::string::npos);
}

TEST_CASE("ReadEula creates the temp directory")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup.exe").string();
    const fs::path nested = temp.path / "does" / "not" / "exist";

    std::string text;
    std::string error;
    CHECK_FALSE(JonesGogInstaller_ReadEula(process, "innoextract", installerPath,
        nested.string(), text, error));

    CHECK(fs::is_directory(nested));
}

TEST_CASE("Extract runs the data command and reports progress in order")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const fs::path extractDir = temp.path / "out";
    const std::string installerPath = (temp.path / "setup.exe").string();

    nullProcess.setOutputLines({
        "Extracting \"Indiana Jones\xC2\xAE and the Infernal Machine\xE2\x84\xA2\""
        " - setup data version 5.6.2 (unicode)",
        "\x1b[K - \"Jones3D.chm\" [en-US]",
        "\x1b[K[>                                                                     ]  0.0% ",
        "\x1b[K - \"Resource/CD1.GOB\" [en-US]",
        "\x1b[K[>                                                          ]  0.0%   126 MiB/s",
        "\x1b[K[=>                                                         ]  0.9%   147 MiB/s",
        "\x1b[K - \"KeySets/XBOX360.kfg\" [en-US]",
        "\x1b[K[==========================================================>] 99.9%   144 MiB/s",
        "\x1b[K\x1b[KDone.",
    });

    std::vector<std::pair<double, std::string>> calls;
    const JonesGogProgressFunc onProgress = [&calls](double percent, const std::string& file)
    {
        calls.emplace_back(percent, file);
    };

    std::string error;
    const JonesGogExtractResult result = JonesGogInstaller_Extract(process, "innoextract",
        installerPath, extractDir.string(), onProgress, babylon::process::CancelCheck{}, error);

    CHECK(result == JonesGogExtractResult::Ok);
    CHECK(error.empty());
    CHECK(fs::is_directory(extractDir));

    const std::vector<std::string> expected{
        "innoextract",
        "--color=0",
        "--progress=1",
        "-d",
        extractDir.string(),
        "-I",
        "/Resource",
        "-I",
        "/KeySets",
        "-I",
        "/Jones3D.chm",
        installerPath,
    };
    const std::optional<std::vector<std::string>> argv = nullProcess.lastArgv();
    REQUIRE(argv.has_value());
    CHECK(*argv == expected);

    const std::vector<std::pair<double, std::string>> expectedCalls{
        { 0.0, "Jones3D.chm" },
        { 0.0, "Jones3D.chm" },
        { 0.0, "Resource/CD1.GOB" },
        { 0.0, "Resource/CD1.GOB" },
        { 0.9, "Resource/CD1.GOB" },
        { 0.9, "KeySets/XBOX360.kfg" },
        { 99.9, "KeySets/XBOX360.kfg" },
    };
    REQUIRE(calls.size() == expectedCalls.size());
    for ( std::size_t i = 0; i < expectedCalls.size(); ++i )
    {
        CHECK(calls[i].first == doctest::Approx(expectedCalls[i].first));
        CHECK(calls[i].second == expectedCalls[i].second);
    }
}

TEST_CASE("Extract fails, removes the folder and reports the last error line")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const fs::path extractDir = temp.path / "out";
    const std::string installerPath = (temp.path / "setup.exe").string();

    nullProcess.setOutputLines({
        "Extracting \"Indiana Jones\xC2\xAE and the Infernal Machine\xE2\x84\xA2\"",
        "\x1b[K\x1b[KError: the installer is corrupt\x1b[K",
    });
    nullProcess.setExitCode(2);

    std::string error;
    const JonesGogExtractResult result = JonesGogInstaller_Extract(process, "innoextract",
        installerPath, extractDir.string(), JonesGogProgressFunc{},
        babylon::process::CancelCheck{}, error);

    CHECK(result == JonesGogExtractResult::Failed);
    CHECK_FALSE(fs::exists(extractDir));
    CHECK(error.find("exit code 2") != std::string::npos);
    CHECK(error.find("Error: the installer is corrupt") != std::string::npos);
    CHECK(error.find('\x1b') == std::string::npos);
}

TEST_CASE("Extract stops and removes the folder when cancelled")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const fs::path extractDir = temp.path / "out";
    const std::string installerPath = (temp.path / "setup.exe").string();
    nullProcess.setOutputLines({ "one", "two" });

    std::string error = "stale";
    const JonesGogExtractResult result = JonesGogInstaller_Extract(process, "innoextract",
        installerPath, extractDir.string(), JonesGogProgressFunc{},
        []() { return true; }, error);

    CHECK(result == JonesGogExtractResult::Cancelled);
    CHECK_FALSE(fs::exists(extractDir));
    CHECK(nullProcess.lastCancelPolls() == 1);
    CHECK(error.empty());
}

TEST_CASE("Extract reports a program that cannot start")
{
    TempDir temp;
    babylon::process::NullProcess nullProcess;
    babylon::process::Process& process = nullProcess;
    const std::string installerPath = (temp.path / "setup.exe").string();

    std::string error;
    const JonesGogExtractResult result = JonesGogInstaller_Extract(process, "", installerPath,
        (temp.path / "out").string(), JonesGogProgressFunc{},
        babylon::process::CancelCheck{}, error);

    CHECK(result == JonesGogExtractResult::NotStarted);
    CHECK_FALSE(error.empty());
}

TEST_CASE("RemoveDir removes a nested tree and ignores empty or missing paths")
{
    TempDir temp;
    const fs::path tree = temp.path / "tree";
    WriteFile(tree / "below" / "file.txt", "data");
    REQUIRE(fs::exists(tree / "below" / "file.txt"));

    JonesGogInstaller_RemoveDir(tree.string());
    CHECK_FALSE(fs::exists(tree));

    JonesGogInstaller_RemoveDir("");
    JonesGogInstaller_RemoveDir((temp.path / "missing").string());
    CHECK_FALSE(fs::exists(temp.path / "missing"));
}
