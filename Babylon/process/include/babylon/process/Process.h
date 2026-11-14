// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace babylon::process
{

    // Receives one complete line of the child's combined standard output and
    // standard error, without the terminating '\n' or '\r'. A "\r\n" pair
    // ends one line, not two. Lines are delivered on the thread that called
    // run(), in the order the child wrote them, once per terminator; a
    // progress bar that redraws with '\r' therefore gives one line per update.
    // The view is valid only for the duration of the call. An unterminated
    // final line is delivered when the child exits.
    using LineCallback = std::function<void(std::string_view line)>;

    // Asked whether the running child should be stopped. Returning true
    // requests termination; run() then kills the child, consumes what is left
    // of its output and returns with cancelled set. Called on the thread that
    // called run(), never after run() returns.
    using CancelCheck = std::function<bool()>;

    // What run() learned. started is false when the child could not be
    // started (an empty argument list or program name, or the operating
    // system refused), in which case exitCode is 0, cancelled is false and no
    // line was delivered. When started is true, exitCode is the child's exit
    // code, or a negative value when the backend reports a signal instead.
    // cancelled is true when the child was stopped because cancelled() asked
    // for it; the exit code is then whatever the backend reports for a killed
    // child.
    struct RunResult
    {
        bool started = false;
        int exitCode = 0;
        bool cancelled = false;
    };

    // Runs external programs and answers whether one can be run. Not
    // thread-safe: call from one thread at a time.
    class Process
    {
    public:
        virtual ~Process() = default;

        // Runs argv[0] with argv[1...] as its arguments and no shell, so
        // spaces and metacharacters in an argument are passed unchanged.
        // argv[0] is resolved through the search path when it names no
        // directory, as a command line would; otherwise it is used as given,
        // relative to the current directory unless it is absolute. The
        // child's standard input is empty and its standard output and
        // standard error are combined into one stream.
        //
        // Starts the child, then reads its output until it exits, delivering
        // each line to onLine on the calling thread. Polls cancelled at least
        // every 100 ms while the child runs, and at least once before the
        // first line; when it returns true, stops the child and returns with
        // cancelled set. Blocks until the child has exited and its output has
        // been consumed.
        //
        // Returns the result described by RunResult. A failure to start is
        // logged as one warning and reported as started == false. onLine and
        // cancelled may be empty, in which case that step is skipped. Never
        // throws; onLine and cancelled must not throw.
        virtual RunResult run(const std::vector<std::string>& argv,
            const LineCallback& onLine, const CancelCheck& cancelled) = 0;

        // Whether name names a program that run() could start: an existing
        // absolute path to an executable file, or a bare name with no
        // directory separator found as an executable in the search path,
        // trying the platform's executable extensions on Windows. Answers
        // false for an empty name, a directory, or a missing or non-executable
        // file. Never throws.
        virtual bool exists(std::string_view name) = 0;
    };

}
