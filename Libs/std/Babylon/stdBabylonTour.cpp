// Added: the windowed build's scripted tour; see stdBabylonTour.h. Adapter code: no Fixed/Added
// markers inside. The module touches no render device: it tracks presents and input, and
// stdDisplayBabylon.cpp asks it for a screenshot path after a present and writes the BMP through
// stdDisplay_SaveScreen.
#include "stdBabylonTour.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    // The whole input script may be 4 MiB, as InputScript::parse enforces; reject a larger file
    // before reading it into memory.
    constexpr std::uint64_t kMaxScriptBytes = 4u * 1024u * 1024u;

    // The largest present count --screenshot-at accepts, the same bound InputScript uses for its
    // frame numbers.
    constexpr std::uint64_t kMaxPresentCount = 10'000'000u;

    struct StdBabylonTourState
    {
        bool bConfigured = false;

        std::optional<babylon::input::InputScript> script;
        std::uint64_t presentCount = 0;
        std::optional<std::uint64_t> lastDeliveredFrame;
        bool bQuitAfterScript = false;
        bool bQuitSent = false;

        std::string screenshotDir;
        std::vector<std::uint64_t> screenshotFrames; // sorted, unique
        std::string screenshotPath;
        bool bScreenshotDue = false;

        std::ofstream presentLog;

        std::vector<std::string> screenTitles;
    };

    StdBabylonTourState stdBabylonTour_state;

    // Parses "<n>[,<n>...]" of --screenshot-at into positive, sorted, unique counts.
    bool stdBabylonTour_ParseFrames(std::string_view text, std::vector<std::uint64_t>& rFrames,
        std::string& rError)
    {
        std::vector<std::uint64_t> frames;
        std::size_t begin = 0;
        for ( ;; )
        {
            const std::size_t end = text.find(',', begin);
            const std::string_view token = text.substr(begin,
                end == std::string_view::npos ? std::string_view::npos : end - begin);

            std::uint64_t value = 0;
            const std::from_chars_result result =
                std::from_chars(token.data(), token.data() + token.size(), value);
            if ( token.empty() || result.ec != std::errc{}
                || result.ptr != token.data() + token.size() )
            {
                rError = "--screenshot-at needs comma-separated present counts, got '"
                    + std::string(token) + "'";
                return false;
            }

            if ( value == 0 || value > kMaxPresentCount )
            {
                rError = "--screenshot-at present count " + std::string(token)
                    + " is outside 1.." + std::to_string(kMaxPresentCount);
                return false;
            }

            frames.push_back(value);

            if ( end == std::string_view::npos )
            {
                break;
            }

            begin = end + 1u;
        }

        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        rFrames = std::move(frames);
        return true;
    }

    // The present log label: the innermost screen's title, or "game". Control bytes become spaces
    // so a title can never split a log line.
    std::string stdBabylonTour_Label()
    {
        std::string label = stdBabylonTour_state.screenTitles.empty()
            ? std::string("game")
            : stdBabylonTour_state.screenTitles.back();
        for ( char& ch : label )
        {
            if ( static_cast<unsigned char>(ch) < 0x20u )
            {
                ch = ' ';
            }
        }

        return label;
    }

    // <dir>/<name>, with exactly one separator between them.
    std::string stdBabylonTour_JoinPath(const std::string& dir, const std::string& name)
    {
        if ( dir.empty() )
        {
            return name;
        }

        std::string path = dir;
        if ( path.back() != '/' && path.back() != '\\' )
        {
            path.push_back('/');
        }

        path += name;
        return path;
    }
}

bool stdBabylonTour_TakeOptions(int* pArgc, char** argv, StdBabylonTourOptions& rOptions,
    std::string& rError)
{
    if ( !pArgc || !argv )
    {
        rError = "no command line";
        return false;
    }

    StdBabylonTourOptions parsed;
    bool bHasInput        = false;
    bool bHasScreenshotDir = false;
    bool bHasScreenshotAt  = false;
    bool bHasQuitAfterScript = false;

    // Validate and gather first; argv changes only after every option is known to be good.
    for ( int i = 1; i < *pArgc; ++i )
    {
        const std::string_view arg = argv[i];
        if ( arg == "--input" )
        {
            if ( i + 1 >= *pArgc )
            {
                rError = "--input needs a value";
                return false;
            }

            parsed.inputPath = argv[++i];
            if ( parsed.inputPath.empty() )
            {
                rError = "--input needs a non-empty script path";
                return false;
            }

            bHasInput = true;
        }
        else if ( arg == "--screenshot-dir" )
        {
            if ( i + 1 >= *pArgc )
            {
                rError = "--screenshot-dir needs a value";
                return false;
            }

            parsed.screenshotDir = argv[++i];
            bHasScreenshotDir    = !parsed.screenshotDir.empty();
            if ( parsed.screenshotDir.empty() )
            {
                rError = "--screenshot-dir needs a non-empty directory";
                return false;
            }
        }
        else if ( arg == "--screenshot-at" )
        {
            if ( i + 1 >= *pArgc )
            {
                rError = "--screenshot-at needs a value";
                return false;
            }

            if ( !stdBabylonTour_ParseFrames(argv[++i], parsed.screenshotFrames, rError) )
            {
                return false;
            }

            bHasScreenshotAt = true;
        }
        else if ( arg == "--present-log" )
        {
            if ( i + 1 >= *pArgc )
            {
                rError = "--present-log needs a value";
                return false;
            }

            parsed.presentLogPath = argv[++i];
            if ( parsed.presentLogPath.empty() )
            {
                rError = "--present-log needs a non-empty file path";
                return false;
            }
        }
        else if ( arg == "--quit-after-script" )
        {
            parsed.bQuitAfterScript = true;
            bHasQuitAfterScript     = true;
        }
    }

    if ( bHasScreenshotAt != bHasScreenshotDir )
    {
        rError = "--screenshot-at and --screenshot-dir must be given together";
        return false;
    }

    if ( bHasQuitAfterScript && !bHasInput )
    {
        rError = "--quit-after-script needs --input";
        return false;
    }

    // Compact argv, dropping every recognized option and its value.
    int numKept = 1;
    for ( int i = 1; i < *pArgc; ++i )
    {
        const std::string_view arg = argv[i];
        if ( arg == "--input" || arg == "--screenshot-dir" || arg == "--screenshot-at"
            || arg == "--present-log" )
        {
            ++i;
            continue;
        }

        if ( arg == "--quit-after-script" )
        {
            continue;
        }

        argv[numKept++] = argv[i];
    }

    argv[numKept] = nullptr;
    *pArgc        = numKept;
    rOptions      = std::move(parsed);
    return true;
}

bool stdBabylonTour_Configure(const StdBabylonTourOptions& rOptions,
    babylon::platform::WindowId window, std::string& rError)
{
    stdBabylonTour_Shutdown();

    StdBabylonTourState newState;

    if ( !rOptions.inputPath.empty() )
    {
        std::ifstream file(std::filesystem::path(rOptions.inputPath), std::ios::binary);
        if ( !file )
        {
            rError = "cannot open input script '" + rOptions.inputPath + "'";
            return false;
        }

        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if ( length < 0 )
        {
            rError = "cannot read the size of input script '" + rOptions.inputPath + "'";
            return false;
        }

        if ( static_cast<std::uint64_t>(length) > kMaxScriptBytes )
        {
            rError = "input script '" + rOptions.inputPath + "' is larger than 4 MiB";
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::string text(static_cast<std::size_t>(length), '\0');
        if ( length > 0 )
        {
            file.read(text.data(), length);
        }

        if ( !file )
        {
            rError = "cannot read input script '" + rOptions.inputPath + "'";
            return false;
        }

        std::variant<babylon::input::InputScript, babylon::input::ScriptError> parsed =
            babylon::input::InputScript::parse(text, window);
        if ( const babylon::input::ScriptError* pScriptError =
                std::get_if<babylon::input::ScriptError>(&parsed) )
        {
            rError = babylon::input::formatScriptError(rOptions.inputPath, *pScriptError);
            return false;
        }

        newState.script.emplace(std::get<babylon::input::InputScript>(std::move(parsed)));
    }

    if ( !rOptions.screenshotFrames.empty() )
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(rOptions.screenshotDir), error);
        if ( error )
        {
            rError = "cannot create screenshot directory '" + rOptions.screenshotDir + "': "
                + error.message();
            return false;
        }

        newState.screenshotDir    = rOptions.screenshotDir;
        newState.screenshotFrames = rOptions.screenshotFrames;
        std::sort(newState.screenshotFrames.begin(), newState.screenshotFrames.end());
        newState.screenshotFrames.erase(
            std::unique(newState.screenshotFrames.begin(), newState.screenshotFrames.end()),
            newState.screenshotFrames.end());
    }

    if ( !rOptions.presentLogPath.empty() )
    {
        newState.presentLog.open(std::filesystem::path(rOptions.presentLogPath),
            std::ios::out | std::ios::trunc);
        if ( !newState.presentLog )
        {
            rError = "cannot open present log '" + rOptions.presentLogPath + "'";
            return false;
        }
    }

    newState.bQuitAfterScript = rOptions.bQuitAfterScript;
    newState.presentCount     = 0;
    newState.lastDeliveredFrame.reset();

    // With none of the options there is nothing to count or log; the run stays exactly as before.
    newState.bConfigured = newState.script.has_value()
        || !newState.screenshotFrames.empty() || newState.presentLog.is_open();

    stdBabylonTour_state = std::move(newState);
    return true;
}

void stdBabylonTour_Shutdown()
{
    if ( stdBabylonTour_state.presentLog.is_open() )
    {
        stdBabylonTour_state.presentLog.close();
    }

    stdBabylonTour_state = StdBabylonTourState{};
}

bool stdBabylonTour_IsEnabled()
{
    return stdBabylonTour_state.bConfigured;
}

std::uint64_t stdBabylonTour_GetPresentCount()
{
    return stdBabylonTour_state.presentCount;
}

bool stdBabylonTour_InputActive()
{
    if ( !stdBabylonTour_state.bConfigured || !stdBabylonTour_state.script )
    {
        return false;
    }

    const std::optional<std::uint64_t> lastFrame = stdBabylonTour_state.script->lastFrame();
    return lastFrame.has_value() && stdBabylonTour_state.presentCount <= *lastFrame;
}

std::span<const babylon::platform::Event> stdBabylonTour_TakeInputEvents()
{
    if ( !stdBabylonTour_InputActive() )
    {
        return {};
    }

    if ( stdBabylonTour_state.lastDeliveredFrame.has_value()
        && *stdBabylonTour_state.lastDeliveredFrame == stdBabylonTour_state.presentCount )
    {
        return {};
    }

    stdBabylonTour_state.lastDeliveredFrame = stdBabylonTour_state.presentCount;
    return stdBabylonTour_state.script->eventsForFrame(stdBabylonTour_state.presentCount);
}

bool stdBabylonTour_TakeQuitAfterScript()
{
    if ( !stdBabylonTour_state.bConfigured || !stdBabylonTour_state.bQuitAfterScript
        || !stdBabylonTour_state.script || stdBabylonTour_state.bQuitSent )
    {
        return false;
    }

    const std::optional<std::uint64_t> lastFrame = stdBabylonTour_state.script->lastFrame();
    const bool bEnded = !lastFrame.has_value()
        || stdBabylonTour_state.presentCount > *lastFrame;
    if ( !bEnded )
    {
        return false;
    }

    stdBabylonTour_state.bQuitSent = true;
    return true;
}

void stdBabylonTour_OnPresent()
{
    if ( !stdBabylonTour_state.bConfigured )
    {
        return;
    }

    ++stdBabylonTour_state.presentCount;
    const std::uint64_t count = stdBabylonTour_state.presentCount;

    if ( std::binary_search(stdBabylonTour_state.screenshotFrames.begin(),
            stdBabylonTour_state.screenshotFrames.end(), count) )
    {
        stdBabylonTour_state.screenshotPath = stdBabylonTour_JoinPath(
            stdBabylonTour_state.screenshotDir, "frame-" + std::to_string(count) + ".bmp");
        stdBabylonTour_state.bScreenshotDue = true;
    }

    if ( stdBabylonTour_state.presentLog.is_open() )
    {
        stdBabylonTour_state.presentLog << "frame " << count << ": "
            << stdBabylonTour_Label() << '\n';
        stdBabylonTour_state.presentLog.flush();
        if ( !stdBabylonTour_state.presentLog )
        {
            stdBabylonTour_state.presentLog.close();
        }
    }
}

const char* stdBabylonTour_TakeScreenshotPath()
{
    if ( !stdBabylonTour_state.bScreenshotDue )
    {
        return nullptr;
    }

    stdBabylonTour_state.bScreenshotDue = false;
    return stdBabylonTour_state.screenshotPath.c_str();
}

void stdBabylonTour_PushScreenTitle(const char* pTitle)
{
    stdBabylonTour_state.screenTitles.emplace_back(pTitle ? pTitle : "");
}

void stdBabylonTour_PopScreenTitle()
{
    if ( !stdBabylonTour_state.screenTitles.empty() )
    {
        stdBabylonTour_state.screenTitles.pop_back();
    }
}
