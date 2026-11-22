// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/time/FrameLimiter.h>

#include <chrono>
#include <thread>

namespace babylon::time
{
namespace
{

    // The last stretch before the deadline is spun on the clock instead of
    // slept, so a wake-up that overshoots does not slow the frame down.
    constexpr time::Duration kSpinWindow = std::chrono::microseconds(500);

    time::Duration periodFor(int rateHz)
    {
        return std::chrono::nanoseconds(1'000'000'000LL / rateHz);
    }

}

    FrameLimiter::FrameLimiter(time::Clock& clock) : m_clock(clock)
    {
    }

    void FrameLimiter::setRate(int rateHz)
    {
        m_rate = rateHz > 0 ? rateHz : 0;
        reset();
    }

    int FrameLimiter::rate() const
    {
        return m_rate;
    }

    bool FrameLimiter::enabled() const
    {
        return m_rate > 0;
    }

    void FrameLimiter::reset()
    {
        m_hasDeadline  = false;
        m_nextDeadline = time::Duration::zero();
    }

    void FrameLimiter::wait()
    {
        if ( m_rate <= 0 )
        {
            reset();
            return;
        }

        const time::Duration period = periodFor(m_rate);
        time::Duration now = m_clock.now();

        // Start a schedule, or drop one that fell more than a whole frame
        // behind, so a stall never turns into a burst of unthrottled frames.
        if ( !m_hasDeadline || now > m_nextDeadline + period )
        {
            m_nextDeadline = now + period;
            m_hasDeadline  = true;
        }

        const time::Duration deadline = m_nextDeadline;
        while ( now < deadline )
        {
            const time::Duration remaining = deadline - now;
            if ( remaining > kSpinWindow )
            {
                m_clock.sleepFor(remaining - kSpinWindow);
            }
            else
            {
                std::this_thread::yield();
                const time::Duration spun = m_clock.now();
                if ( spun == now )
                {
                    // A clock that only advances in sleepFor() cannot be spun
                    // out; finish the last window with a sleep.
                    m_clock.sleepFor(remaining);
                }
            }
            now = m_clock.now();
        }

        m_nextDeadline = deadline + period;
    }

}
