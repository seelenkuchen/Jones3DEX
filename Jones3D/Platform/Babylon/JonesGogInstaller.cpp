// The Babylon column's GOG offline installer driver: finds the bundled innoextract program, reads
// the installer's licence text and extracts the game data with progress and cancel. The setup
// wizard calls it; it has no UI of its own.
#include <Jones3D/Platform/Babylon/JonesGogInstaller.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uintmax_t kJonesGogMaxEulaSize = 1024 * 1024;
    constexpr std::size_t kJonesGogRecentLineCount = 20;

#ifdef _WIN32
    std::filesystem::path PathFromUtf8(const std::string& path)
    {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
#else
    std::filesystem::path PathFromUtf8(const std::string& path)
    {
        return std::filesystem::path(path);
    }

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        return path.string();
    }
#endif

    bool IsDigit(char ch)
    {
        return ch >= '0' && ch <= '9';
    }

    std::string StripAnsi(std::string_view line)
    {
        std::string out;
        out.reserve(line.size());
        for ( std::size_t i = 0; i < line.size(); )
        {
            if ( static_cast<unsigned char>(line[i]) == 0x1B
                && i + 1 < line.size() && line[i + 1] == '[' )
            {
                std::size_t end = i + 2;
                while ( end < line.size() )
                {
                    const unsigned char ch = static_cast<unsigned char>(line[end]);
                    ++end;
                    if ( ch >= 0x40 && ch <= 0x7E )
                    {
                        break;
                    }
                }
                i = end;
                continue;
            }
            out.push_back(line[i]);
            ++i;
        }
        return out;
    }

    double ParseDecimal(std::string_view number)
    {
        double value = 0.0;
        double scale = 0.1;
        bool bFraction = false;
        for ( const char ch : number )
        {
            if ( ch == '.' )
            {
                bFraction = true;
                continue;
            }
            if ( !IsDigit(ch) )
            {
                continue;
            }
            const int digit = ch - '0';
            if ( !bFraction )
            {
                value = value * 10.0 + digit;
            }
            else
            {
                value += digit * scale;
                scale *= 0.1;
            }
        }
        return value;
    }

    std::string InstallerFileName(const std::string& installerPath)
    {
        const std::string name = PathToUtf8(PathFromUtf8(installerPath).filename());
        return name.empty() ? installerPath : name;
    }

    bool ReadEulaFile(const std::filesystem::path& path, const std::string& installerName,
        std::string& outText, std::string& outError)
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if ( error )
        {
            outError = "The licence file in " + installerName + " could not be read.";
            return false;
        }
        if ( size > kJonesGogMaxEulaSize )
        {
            outError = "The licence file in " + installerName + " is larger than 1 MiB.";
            return false;
        }

        std::ifstream input(path, std::ios::binary);
        if ( !input )
        {
            outError = "The licence file in " + installerName + " could not be read.";
            return false;
        }
        std::string data((std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        if ( input.bad() )
        {
            outError = "The licence file in " + installerName + " could not be read.";
            return false;
        }

        std::size_t start = 0;
        if ( data.size() >= 3
            && static_cast<unsigned char>(data[0]) == 0xEF
            && static_cast<unsigned char>(data[1]) == 0xBB
            && static_cast<unsigned char>(data[2]) == 0xBF )
        {
            start = 3;
        }

        std::string text;
        text.reserve(data.size() - start);
        for ( std::size_t i = start; i < data.size(); ++i )
        {
            if ( data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n' )
            {
                text.push_back('\n');
                ++i;
            }
            else
            {
                text.push_back(data[i]);
            }
        }
        outText = std::move(text);
        return true;
    }
}

std::string JonesGogInstaller_FindInnoextract(babylon::process::Process& process,
    const std::string& exeDir, bool bWindows)
{
    if ( !exeDir.empty() )
    {
        std::filesystem::path candidate = PathFromUtf8(exeDir);
        candidate /= bWindows ? "innoextract.exe" : "innoextract";
        const std::string fullPath = PathToUtf8(candidate);
        if ( process.exists(fullPath) )
        {
            return fullPath;
        }
    }
    if ( process.exists("innoextract") )
    {
        return "innoextract";
    }
    return "";
}

bool JonesGogInstaller_ReadEula(babylon::process::Process& process, const std::string& innoPath,
    const std::string& installerPath, const std::string& tempDir, std::string& outText,
    std::string& outError)
{
    outText.clear();
    outError.clear();

    if ( !tempDir.empty() )
    {
        std::error_code error;
        std::filesystem::create_directories(PathFromUtf8(tempDir), error);
    }

    const std::string installerName = InstallerFileName(installerPath);
    const std::vector<std::string> argv{
        innoPath,
        "--color=0",
        "--progress=0",
        "-d",
        tempDir,
        "-I",
        "tmp/EULA_en-US.txt",
        "-I",
        "tmp/EULA.txt",
        installerPath,
    };
    const babylon::process::RunResult result = process.run(argv,
        babylon::process::LineCallback{}, babylon::process::CancelCheck{});
    if ( !result.started )
    {
        outError = "innoextract could not be started for " + installerName + ".";
        return false;
    }
    if ( result.exitCode != 0 )
    {
        outError = "innoextract failed for " + installerName + ".";
        return false;
    }

    std::filesystem::path licence = PathFromUtf8(tempDir);
    licence /= "tmp";
    licence /= "EULA_en-US.txt";
    std::error_code error;
    if ( !std::filesystem::exists(licence, error) || error )
    {
        licence = PathFromUtf8(tempDir);
        licence /= "tmp";
        licence /= "EULA.txt";
        error.clear();
        if ( !std::filesystem::exists(licence, error) || error )
        {
            outError = "The installer " + installerName + " has no licence file.";
            return false;
        }
    }

    return ReadEulaFile(licence, installerName, outText, outError);
}

JonesGogProgressLine JonesGogInstaller_ParseLine(std::string_view line)
{
    JonesGogProgressLine result;
    const std::string text = StripAnsi(line);
    std::size_t start = 0;
    while ( start < text.size() && text[start] == ' ' )
    {
        ++start;
    }
    const std::string_view trimmed(text.data() + start, text.size() - start);

    if ( trimmed.size() >= 3 && trimmed[0] == '-' && trimmed[1] == ' ' && trimmed[2] == '"' )
    {
        const std::size_t close = trimmed.find('"', 3);
        if ( close != std::string_view::npos )
        {
            result.bHasFile = true;
            result.file.assign(trimmed.data() + 3, close - 3);
        }
    }

    bool bHasPercent = false;
    double lastPercent = 0.0;
    for ( std::size_t percent = trimmed.find('%'); percent != std::string_view::npos;
        percent = trimmed.find('%', percent + 1) )
    {
        std::size_t numberStart = percent;
        std::size_t numAfterDot = 0;
        while ( numberStart > 0 && IsDigit(trimmed[numberStart - 1]) )
        {
            --numberStart;
            ++numAfterDot;
        }
        if ( numAfterDot == 0 )
        {
            continue;
        }
        if ( numberStart > 0 && trimmed[numberStart - 1] == '.' )
        {
            std::size_t beforeDot = numberStart - 1;
            std::size_t numBeforeDot = 0;
            while ( beforeDot > 0 && IsDigit(trimmed[beforeDot - 1]) )
            {
                --beforeDot;
                ++numBeforeDot;
            }
            if ( numBeforeDot > 0 )
            {
                numberStart = beforeDot;
            }
        }
        lastPercent = ParseDecimal(trimmed.substr(numberStart, percent - numberStart));
        bHasPercent = true;
    }

    if ( bHasPercent )
    {
        result.bHasPercent = true;
        result.percent = std::min(100.0, std::max(0.0, lastPercent));
    }
    return result;
}

JonesGogExtractResult JonesGogInstaller_Extract(babylon::process::Process& process,
    const std::string& innoPath, const std::string& installerPath, const std::string& extractDir,
    const JonesGogProgressFunc& onProgress, const babylon::process::CancelCheck& cancelled,
    std::string& outError)
{
    outError.clear();

    if ( !extractDir.empty() )
    {
        std::error_code error;
        std::filesystem::create_directories(PathFromUtf8(extractDir), error);
    }

    const std::vector<std::string> argv{
        innoPath,
        "--color=0",
        "--progress=1",
        "-d",
        extractDir,
        "-I",
        "/Resource",
        "-I",
        "/KeySets",
        "-I",
        "/Jones3D.chm",
        installerPath,
    };

    double lastPercent = 0.0;
    std::string currentFile;
    std::deque<std::string> recentLines;
    const babylon::process::LineCallback onLine =
        [&](std::string_view line)
        {
            const JonesGogProgressLine parsed = JonesGogInstaller_ParseLine(line);
            if ( parsed.bHasPercent )
            {
                lastPercent = parsed.percent;
            }
            if ( parsed.bHasFile )
            {
                currentFile = parsed.file;
            }
            if ( parsed.bHasPercent || parsed.bHasFile )
            {
                if ( onProgress )
                {
                    onProgress(lastPercent, currentFile);
                }
                return;
            }
            std::string text = StripAnsi(line);
            std::size_t start = 0;
            while ( start < text.size() && text[start] == ' ' )
            {
                ++start;
            }
            if ( start >= text.size() )
            {
                return;
            }
            if ( recentLines.size() >= kJonesGogRecentLineCount )
            {
                recentLines.pop_front();
            }
            recentLines.emplace_back(text.substr(start));
        };

    const babylon::process::RunResult result = process.run(argv, onLine, cancelled);
    if ( !result.started )
    {
        outError = "innoextract could not be started.";
        return JonesGogExtractResult::NotStarted;
    }
    if ( result.cancelled )
    {
        JonesGogInstaller_RemoveDir(extractDir);
        return JonesGogExtractResult::Cancelled;
    }
    if ( result.exitCode != 0 )
    {
        const std::string detail = recentLines.empty() ? std::string() : recentLines.back();
        outError = "innoextract failed (exit code " + std::to_string(result.exitCode) + "): "
            + detail;
        JonesGogInstaller_RemoveDir(extractDir);
        return JonesGogExtractResult::Failed;
    }
    return JonesGogExtractResult::Ok;
}

void JonesGogInstaller_RemoveDir(const std::string& dir)
{
    if ( dir.empty() )
    {
        return;
    }
    std::error_code error;
    std::filesystem::remove_all(PathFromUtf8(dir), error);
}
