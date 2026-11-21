// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/process/Process.h>
#include <babylon/process/SdlProcess.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>
#include <babylon/log/Sink.h>

#include <doctest.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using babylon::process::CancelCheck;
    using babylon::process::Process;
    using babylon::process::RunResult;
    using namespace std::chrono_literals;

    // CMake itself is the program under test: it exists on every machine that
    // can build this project, so the test needs no system-specific tool.
    constexpr const char* kCmake = BABYLON_TEST_CMAKE_COMMAND;

    std::unique_ptr<Process> makeProcess()
    {
        std::unique_ptr<Process> process = babylon::process::createSdlProcess();
        REQUIRE(process != nullptr);
        return process;
    }

    RunResult runWithLines(Process& process, const std::vector<std::string>& argv,
        std::vector<std::string>& lines)
    {
        return process.run(argv,
            [&lines](std::string_view line) { lines.emplace_back(line); },
            CancelCheck{});
    }
}

TEST_CASE("The SDL process delivers cmake -E echo output as a line")
{
    const std::unique_ptr<Process> process = makeProcess();
    std::vector<std::string> lines;

    const RunResult result = runWithLines(*process,
        { kCmake, "-E", "echo", "process line" }, lines);

    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 0);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "process line");
}

TEST_CASE("The SDL process splits output at a carriage return")
{
    const std::unique_ptr<Process> process = makeProcess();
    std::vector<std::string> lines;

    // cmake -E echo prints its argument unchanged, so the embedded '\r' is
    // exactly what a progress bar that redraws in place writes.
    const RunResult result = runWithLines(*process,
        { kCmake, "-E", "echo", "first\rsecond" }, lines);

    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 0);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "first");
    CHECK(lines[1] == "second");
}

TEST_CASE("The SDL process reports a nonzero exit code")
{
    const std::unique_ptr<Process> process = makeProcess();

    const RunResult result = process->run({ kCmake, "-E", "false" },
        babylon::process::LineCallback{}, CancelCheck{});

    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode != 0);
}

TEST_CASE("The SDL process combines standard error into the line stream")
{
    const std::unique_ptr<Process> process = makeProcess();
    std::vector<std::string> lines;

    // An unknown -E command writes its error to standard error and exits
    // nonzero, so a delivered line proves the two streams are combined.
    const RunResult result = runWithLines(*process,
        { kCmake, "-E", "no_such_arbitrary_command" }, lines);

    CHECK(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode != 0);
    CHECK_FALSE(lines.empty());
}

TEST_CASE("The SDL process stops a long child when cancellation asks")
{
    const std::unique_ptr<Process> process = makeProcess();
    const auto deadline = std::chrono::steady_clock::now() + 100ms;
    const CancelCheck cancelled = [&deadline]()
    {
        return std::chrono::steady_clock::now() >= deadline;
    };

    const auto start = std::chrono::steady_clock::now();
    const RunResult result = process->run({ kCmake, "-E", "sleep", "10" },
        babylon::process::LineCallback{}, cancelled);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(result.started);
    CHECK(result.cancelled);
    CHECK(elapsed < 5s);
}

TEST_CASE("The SDL process answers exists for an executable path and refuses others")
{
    const std::unique_ptr<Process> process = makeProcess();

    CHECK(process->exists(kCmake));
    CHECK_FALSE(process->exists("babylon-no-such-program-anywhere"));
    CHECK_FALSE(process->exists(""));
}

TEST_CASE("The SDL process refuses an empty argument list with one warning")
{
    const std::unique_ptr<Process> process = makeProcess();
    babylon::log::CaptureSink sink;

    const RunResult result = process->run({}, babylon::process::LineCallback{},
        CancelCheck{});

    CHECK_FALSE(result.started);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 0);
    REQUIRE(sink.records().size() == 1);
    CHECK(sink.records()[0].level == babylon::log::Level::Warning);
}
