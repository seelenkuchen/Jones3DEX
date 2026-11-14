// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/process/Process.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::process
{

    // Process that starts nothing: it records the arguments of the last
    // run(), replays a scripted list of output lines through the callback and
    // reports a scripted exit code. exists() answers only for scripted names.
    // Public because tests and headless runs drive it.
    class NullProcess final : public Process
    {
    public:
        RunResult run(const std::vector<std::string>& argv,
            const LineCallback& onLine, const CancelCheck& cancelled) override;
        bool exists(std::string_view name) override;

        // The lines the next run() delivers, in order and without
        // terminators. Replaces any earlier script; empty by default.
        void setOutputLines(std::vector<std::string> lines);

        // The exit code the next run() reports; 0 by default. A cancelled run
        // still reports it.
        void setExitCode(int exitCode);

        // Scripts whether exists() answers true for name. Names match
        // exactly and the last script for a name wins; nothing exists by
        // default.
        void setExists(std::string_view name, bool exists);

        // The argv of the last run(), or nullopt if no run() was made. A run
        // that was refused for an empty argument list still records it.
        std::optional<std::vector<std::string>> lastArgv() const;

        // How often the last run() asked its cancelled check, or 0 if no run
        // was made. A run asks once before the first line and once after each
        // line that is followed by another; a run whose check answers true
        // stops there and delivers no further lines.
        std::size_t lastCancelPolls() const;

    private:
        std::vector<std::string> m_outputLines;
        std::vector<std::pair<std::string, bool>> m_existsAnswers;
        int m_exitCode = 0;
        std::optional<std::vector<std::string>> m_lastArgv;
        std::size_t m_lastCancelPolls = 0;
    };

}
