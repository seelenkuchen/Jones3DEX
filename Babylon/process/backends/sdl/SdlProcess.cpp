// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/process/SdlProcess.h>

#include <babylon/log/Log.h>
#include <babylon/process/Process.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace babylon::process
{
namespace
{

    // The interval at which run() asks the cancellation check.
    constexpr Uint64 kCancelPollIntervalNs = 100000000ULL;   // 100 ms

    // Turns a byte stream into lines at '\n' or '\r'. A "\r\n" pair ends one
    // line only, including when the two bytes arrive in different reads.
    class LineSplitter
    {
    public:
        explicit LineSplitter(const LineCallback& onLine) : m_onLine(onLine)
        {
        }

        void feed(const char* data, std::size_t size)
        {
            for ( std::size_t i = 0; i < size; ++i )
            {
                const char character = data[i];
                if ( character == '\r' )
                {
                    emit();
                    m_afterCarriageReturn = true;
                }
                else if ( character == '\n' )
                {
                    if ( m_afterCarriageReturn )
                    {
                        m_afterCarriageReturn = false;   // the second half of "\r\n"
                    }
                    else
                    {
                        emit();
                    }
                }
                else
                {
                    m_afterCarriageReturn = false;
                    m_pending.push_back(character);
                }
            }
        }

        // Delivers a final line that ended at end of file rather than at a
        // terminator. An empty remainder is no line at all.
        void finish()
        {
            if ( !m_pending.empty() )
            {
                emit();
            }
        }

    private:
        void emit()
        {
            if ( m_onLine )
            {
                m_onLine(m_pending);
            }
            m_pending.clear();
        }

        const LineCallback& m_onLine;
        std::string m_pending;
        bool m_afterCarriageReturn = false;
    };

    std::vector<std::string> splitList(std::string_view text, char separator)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for ( ; ; )
        {
            const std::size_t end = text.find(separator, start);
            if ( end == std::string_view::npos )
            {
                parts.emplace_back(text.substr(start));
                return parts;
            }
            parts.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }
    }

#ifdef _WIN32
    char pathListSeparator()
    {
        return ';';
    }
#else
    char pathListSeparator()
    {
        return ':';
    }
#endif

    // True when the name itself names a directory, so it must not be searched
    // in PATH. On Windows a drive colon counts too.
    bool hasDirectorySeparator(std::string_view name)
    {
#ifdef _WIN32
        return name.find('/') != std::string_view::npos
            || name.find('\\') != std::string_view::npos
            || name.find(':') != std::string_view::npos;
#else
        return name.find('/') != std::string_view::npos;
#endif
    }

    std::string joinPath(std::string_view directory, std::string_view name)
    {
        if ( directory.empty() )
        {
            return std::string(name);
        }
#ifdef _WIN32
        const char separator = '\\';
        const bool endsWithSeparator =
            directory.back() == '/' || directory.back() == '\\';
#else
        const char separator = '/';
        const bool endsWithSeparator = directory.back() == '/';
#endif
        std::string path(directory);
        if ( !endsWithSeparator )
        {
            path.push_back(separator);
        }
        path.append(name);
        return path;
    }

#ifdef _WIN32
    // The extensions considered executable, from PATHEXT or the Windows
    // default. Each keeps its leading dot.
    std::vector<std::string> executableExtensions()
    {
        const char* const pathExt =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PATHEXT");
        std::vector<std::string> extensions = splitList(
            pathExt != nullptr && pathExt[0] != '\0' ? pathExt : ".COM;.EXE;.BAT;.CMD",
            ';');
        for ( std::string& extension : extensions )
        {
            if ( !extension.empty() && extension.front() != '.' )
            {
                extension.insert(extension.begin(), '.');
            }
        }
        return extensions;
    }

    bool endsWithIgnoringCase(std::string_view text, std::string_view suffix)
    {
        if ( suffix.size() > text.size() )
        {
            return false;
        }
        const std::size_t offset = text.size() - suffix.size();
        for ( std::size_t i = 0; i < suffix.size(); ++i )
        {
            const auto a = static_cast<unsigned char>(text[offset + i]);
            const auto b = static_cast<unsigned char>(suffix[i]);
            if ( std::tolower(a) != std::tolower(b) )
            {
                return false;
            }
        }
        return true;
    }
#endif

    bool isExecutableFile(const std::string& path)
    {
        SDL_PathInfo info;
        if ( !SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE )
        {
            return false;
        }
#ifdef _WIN32
        for ( const std::string& extension : executableExtensions() )
        {
            if ( endsWithIgnoringCase(path, extension) )
            {
                return true;
            }
        }
        return false;
#else
        // SDL_GetPathInfo knows the file exists but not whether it may be
        // executed; the standard library reports the permission bits.
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(path, error);
        if ( error )
        {
            return false;
        }
        const std::filesystem::perms executable = std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;
        return (status.permissions() & executable) != std::filesystem::perms::none;
#endif
    }

    // Process on SDL3's process API. SDL does the PATH lookup of argv[0] in
    // the spawn call, so arguments are passed exactly as given.
    class SdlProcess final : public Process
    {
    public:
        RunResult run(const std::vector<std::string>& argv,
            const LineCallback& onLine, const CancelCheck& cancelled) override;
        bool exists(std::string_view name) override;
    };

    // One level of indirection so the arguments' const char pointers stay
    // valid for the duration of SDL_CreateProcessWithProperties().
    std::vector<const char*> argumentPointers(const std::vector<std::string>& argv)
    {
        std::vector<const char*> arguments;
        arguments.reserve(argv.size() + 1);
        for ( const std::string& argument : argv )
        {
            arguments.push_back(argument.c_str());
        }
        arguments.push_back(nullptr);
        return arguments;
    }

}

    RunResult SdlProcess::run(const std::vector<std::string>& argv,
        const LineCallback& onLine, const CancelCheck& cancelled)
    {
        RunResult result;
        if ( argv.empty() || argv[0].empty() )
        {
            log::warning("process: cannot run an empty argument list");
            return result;
        }

        std::vector<const char*> arguments = argumentPointers(argv);
        const SDL_PropertiesID props = SDL_CreateProperties();
        if ( props == 0 )
        {
            log::warning("process: could not create the properties to run '{}': {}",
                argv[0], SDL_GetError());
            return result;
        }
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
            arguments.data());
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
            SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
            true);
        SDL_Process* const child = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if ( child == nullptr )
        {
            log::warning("process: could not run '{}': {}", argv[0], SDL_GetError());
            return result;
        }
        result.started = true;

        SDL_IOStream* const output = SDL_GetProcessOutput(child);
        if ( output == nullptr )
        {
            log::warning("process: reading the output of '{}' failed: {}", argv[0],
                SDL_GetError());
        }

        LineSplitter splitter(onLine);
        bool outputDone = output == nullptr;
        bool childDone = false;
        int exitCode = 0;
        Uint64 nextCancelPoll = SDL_GetTicksNS();
        while ( !outputDone || !childDone )
        {
            bool progressed = false;
            if ( !outputDone )
            {
                char buffer[4096];
                const std::size_t count = SDL_ReadIO(output, buffer, sizeof buffer);
                if ( count > 0 )
                {
                    splitter.feed(buffer, count);
                    progressed = true;
                }
                else
                {
                    const SDL_IOStatus status = SDL_GetIOStatus(output);
                    if ( status == SDL_IO_STATUS_EOF )
                    {
                        outputDone = true;
                        progressed = true;
                    }
                    else if ( status == SDL_IO_STATUS_ERROR )
                    {
                        log::warning("process: reading the output of '{}' failed: {}",
                            argv[0], SDL_GetError());
                        outputDone = true;
                        progressed = true;
                    }
                }
            }
            if ( outputDone && !childDone )
            {
                // All output has been consumed, so the child cannot block on
                // a full pipe; a non-blocking wait reports its exit code.
                childDone = SDL_WaitProcess(child, false, &exitCode);
                progressed = progressed || childDone;
            }

            // A killed child can take a moment to close its end of the pipe,
            // so the drain above continues after the kill.
            const Uint64 now = SDL_GetTicksNS();
            if ( !result.cancelled && cancelled && now >= nextCancelPoll )
            {
                nextCancelPoll = now + kCancelPollIntervalNs;
                if ( cancelled() )
                {
                    result.cancelled = true;
                    // The child may have exited between the last read and this
                    // check; reaping it first keeps a kill of an already dead
                    // process out of the log.
                    if ( SDL_WaitProcess(child, false, &exitCode) )
                    {
                        childDone = true;
                    }
                    else if ( !SDL_KillProcess(child, true) )
                    {
                        log::warning("process: stopping '{}' failed: {}", argv[0],
                            SDL_GetError());
                    }
                }
            }

            if ( !progressed )
            {
                // Keep the wait from spinning while the child produces no
                // output; the cancellation check above runs between sleeps.
                SDL_Delay(1);
            }
        }

        splitter.finish();
        result.exitCode = exitCode;
        SDL_DestroyProcess(child);
        return result;
    }

    bool SdlProcess::exists(std::string_view name)
    {
        if ( name.empty() )
        {
            return false;
        }
        const std::string text(name);
        if ( hasDirectorySeparator(name) )
        {
            return isExecutableFile(text);
        }

        const char* const path =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PATH");
        if ( path == nullptr || path[0] == '\0' )
        {
            return false;
        }
#ifdef _WIN32
        const std::vector<std::string> extensions = executableExtensions();
#endif
        for ( const std::string& directory : splitList(path, pathListSeparator()) )
        {
            if ( isExecutableFile(joinPath(directory, text)) )
            {
                return true;
            }
#ifdef _WIN32
            for ( const std::string& extension : extensions )
            {
                if ( endsWithIgnoringCase(text, extension) )
                {
                    continue;   // the plain name above already tried it
                }
                if ( isExecutableFile(joinPath(directory, text + extension)) )
                {
                    return true;
                }
            }
#endif
        }
        return false;
    }

    std::unique_ptr<Process> createSdlProcess()
    {
        return std::make_unique<SdlProcess>();
    }

}
