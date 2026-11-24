// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/time/Clock.h>

namespace babylon::time
{

    // Paces a presentation loop to a wall-clock rate: after a frame has been
    // presented, wait() sleeps until the next frame's deadline. It reads only
    // the injected clock, never game time or simulation state.
    //
    // A rate of 0 (V-Sync) or less (unlimited) disables it: wait() returns at
    // once and forgets any schedule. A positive rate is a cap in frames per
    // second.
    //
    // The deadlines are drift-free from the first wait() after setRate(): a
    // frame that is on time waits until the previous deadline plus one period,
    // so rounding and short sleeps do not accumulate. A frame more than one
    // period late drops the backlog and starts a fresh schedule one period
    // from now instead of running without a pause to catch up.
    //
    // Most of the wait is sleepFor(); the final short window is spun on the
    // clock, so the wait returns close to the deadline. A clock that only
    // moves in sleepFor() (babylon::time::NullClock) finishes that window with
    // a sleep instead, so injected-clock tests need no real waiting.
    class FrameLimiter
    {
    public:
        explicit FrameLimiter(time::Clock& clock);

        // Sets the cap: 0 or less turns the limiter off, a positive value is
        // frames per second. The next wait() starts a fresh schedule.
        void setRate(int rateHz);

        // The configured cap, 0 when disabled.
        int rate() const;

        // True when a positive rate is set.
        bool enabled() const;

        // Waits until the current frame's deadline and schedules the next one.
        // Call after the frame has been presented. Returns at once when
        // disabled.
        void wait();

        // Forgets the schedule; the next wait() starts a fresh one. setRate()
        // calls it.
        void reset();

    private:
        time::Clock& m_clock;
        int m_rate = 0;
        bool m_hasDeadline = false;
        time::Duration m_nextDeadline{};
    };

}
