// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/process/NullProcess.h>

#include <babylon/log/Log.h>
#include <babylon/process/Process.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::process
{

    RunResult NullProcess::run(const std::vector<std::string>& argv,
        const LineCallback& onLine, const CancelCheck& cancelled)
    {
        m_lastArgv = argv;
        m_lastCancelPolls = 0;

        RunResult result;
        if ( argv.empty() || argv[0].empty() )
        {
            log::warning("process: cannot run an empty argument list");
            return result;
        }

        // A Null child starts immediately and would exit at once, so there is
        // no clock to watch: the scripted output is its whole life. Asking the
        // check before each line gives a test a deterministic cancellation
        // point.
        result.started = true;
        result.exitCode = m_exitCode;
        const auto isCancelled = [this, &cancelled]()
        {
            if ( !cancelled )
            {
                return false;
            }
            ++m_lastCancelPolls;
            return cancelled();
        };

        if ( isCancelled() )
        {
            result.cancelled = true;
            return result;
        }
        for ( std::size_t i = 0; i < m_outputLines.size(); ++i )
        {
            if ( onLine )
            {
                onLine(m_outputLines[i]);
            }
            // Asking between two lines gives cancellation the same effect as
            // stopping a real child: what was already written stays delivered,
            // the rest is not.
            if ( i + 1 < m_outputLines.size() && isCancelled() )
            {
                result.cancelled = true;
                return result;
            }
        }
        return result;
    }

    bool NullProcess::exists(std::string_view name)
    {
        if ( name.empty() )
        {
            return false;
        }
        for ( auto answer = m_existsAnswers.rbegin(); answer != m_existsAnswers.rend();
            ++answer )
        {
            if ( answer->first == name )
            {
                return answer->second;
            }
        }
        return false;
    }

    void NullProcess::setOutputLines(std::vector<std::string> lines)
    {
        m_outputLines = std::move(lines);
    }

    void NullProcess::setExitCode(int exitCode)
    {
        m_exitCode = exitCode;
    }

    void NullProcess::setExists(std::string_view name, bool exists)
    {
        for ( auto answer = m_existsAnswers.rbegin(); answer != m_existsAnswers.rend();
            ++answer )
        {
            if ( answer->first == name )
            {
                answer->second = exists;
                return;
            }
        }
        m_existsAnswers.emplace_back(std::string(name), exists);
    }

    std::optional<std::vector<std::string>> NullProcess::lastArgv() const
    {
        return m_lastArgv;
    }

    std::size_t NullProcess::lastCancelPolls() const
    {
        return m_lastCancelPolls;
    }

}
