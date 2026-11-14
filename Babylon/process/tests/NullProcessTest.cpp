// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/process/NullProcess.h>
#include <babylon/process/Process.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>
#include <babylon/log/Sink.h>

#include <doctest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using babylon::process::CancelCheck;
    using babylon::process::NullProcess;
    using babylon::process::Process;
    using babylon::process::RunResult;
}

TEST_CASE("A Null process records its argv and replays scripted lines and exit code")
{
    NullProcess nullProcess;
    Process& process = nullProcess;

    // Nothing was run yet.
    CHECK_FALSE(nullProcess.lastArgv().has_value());
    CHECK(nullProcess.lastCancelPolls() == 0);

    const std::vector<std::string> argv{ "innoextract", "--gog", "setup.exe" };
    std::vector<std::string> lines;
    nullProcess.setOutputLines({ "Extracting", "done" });
    nullProcess.setExitCode(7);

    const RunResult result = process.run(argv,
        [&lines](std::string_view line) { lines.emplace_back(line); }, CancelCheck{});

    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 7);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Extracting");
    CHECK(lines[1] == "done");

    const std::optional<std::vector<std::string>> recorded = nullProcess.lastArgv();
    REQUIRE(recorded.has_value());
    CHECK(*recorded == argv);
    CHECK(nullProcess.lastCancelPolls() == 0);   // no check was given
}

TEST_CASE("A Null process stops delivering lines when cancellation answers true")
{
    NullProcess nullProcess;
    Process& process = nullProcess;
    nullProcess.setOutputLines({ "one", "two", "three" });
    nullProcess.setExitCode(3);

    int polls = 0;
    std::vector<std::string> lines;
    const CancelCheck cancelled = [&polls]()
    {
        ++polls;
        return polls >= 2;
    };

    const RunResult result = process.run({ "innoextract", "--version" },
        [&lines](std::string_view line) { lines.emplace_back(line); }, cancelled);

    CHECK(result.started);
    CHECK(result.cancelled);
    CHECK(result.exitCode == 3);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "one");
    CHECK(polls == 2);
    CHECK(nullProcess.lastCancelPolls() == 2);
}

TEST_CASE("A Null process refuses an empty argument list with one warning")
{
    NullProcess nullProcess;
    Process& process = nullProcess;
    babylon::log::CaptureSink sink;

    const RunResult result = process.run({}, babylon::process::LineCallback{},
        CancelCheck{});

    CHECK_FALSE(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 0);
    REQUIRE(sink.records().size() == 1);
    CHECK(sink.records()[0].level == babylon::log::Level::Warning);
    CHECK(nullProcess.lastArgv().has_value());
    CHECK(nullProcess.lastArgv()->empty());

    // A name with no characters is the same refusal.
    const RunResult emptyName = process.run({ "" }, babylon::process::LineCallback{},
        CancelCheck{});
    CHECK_FALSE(emptyName.started);
    REQUIRE(sink.records().size() == 2);
    CHECK(sink.records()[1].level == babylon::log::Level::Warning);
}

TEST_CASE("A Null process answers exists only for scripted names")
{
    NullProcess nullProcess;
    Process& process = nullProcess;

    CHECK_FALSE(process.exists("innoextract"));
    CHECK_FALSE(process.exists(""));

    nullProcess.setExists("innoextract", true);
    CHECK(process.exists("innoextract"));
    CHECK_FALSE(process.exists("other-tool"));

    // The last script for a name wins.
    nullProcess.setExists("innoextract", false);
    CHECK_FALSE(process.exists("innoextract"));
}

TEST_CASE("A Null process with no scripted lines still reports the scripted exit")
{
    NullProcess nullProcess;
    Process& process = nullProcess;
    nullProcess.setExitCode(-1);

    const RunResult result = process.run({ "tool" }, babylon::process::LineCallback{},
        CancelCheck{});
    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == -1);
}
