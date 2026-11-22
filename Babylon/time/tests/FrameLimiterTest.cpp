// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/time/FrameLimiter.h>

#include <babylon/time/NullClock.h>

#include <doctest.h>

#include <chrono>

namespace
{
    using babylon::time::FrameLimiter;
    using babylon::time::Duration;
    using babylon::time::NullClock;

    Duration period(int rateHz)
    {
        return std::chrono::nanoseconds(1'000'000'000LL / rateHz);
    }
}

TEST_CASE("a frame limiter waits for one period per frame at 60 Hz")
{
    NullClock clock;
    FrameLimiter limiter(clock);
    limiter.setRate(60);

    CHECK(limiter.enabled());
    CHECK(limiter.rate() == 60);

    limiter.wait();
    CHECK(clock.now() == period(60));

    limiter.wait();
    CHECK(clock.now() == period(60) * 2);

    // The schedule is drift-free: three more frames end exactly on the fifth period
    // even though the period is not a whole number of milliseconds.
    limiter.wait();
    limiter.wait();
    limiter.wait();
    CHECK(clock.now() == period(60) * 5);
}

TEST_CASE("a frame limiter keeps the 144 Hz deadline")
{
    NullClock clock;
    FrameLimiter limiter(clock);
    limiter.setRate(144);

    limiter.wait();
    CHECK(clock.now() == period(144));

    limiter.wait();
    CHECK(clock.now() == period(144) * 2);
}

TEST_CASE("a frame limiter does not wait when V-Sync or unlimited is selected")
{
    NullClock clock;
    FrameLimiter limiter(clock);

    limiter.setRate(0);
    CHECK_FALSE(limiter.enabled());
    limiter.wait();
    limiter.wait();
    CHECK(clock.now() == Duration::zero());

    limiter.setRate(-1);
    CHECK_FALSE(limiter.enabled());
    CHECK(limiter.rate() == 0);
    limiter.wait();
    CHECK(clock.now() == Duration::zero());

    // Changing back starts a fresh schedule from the current time.
    REQUIRE(clock.advance(std::chrono::milliseconds(100)));
    limiter.setRate(60);
    limiter.wait();
    CHECK(clock.now() == std::chrono::milliseconds(100) + period(60));
}

TEST_CASE("a frame limiter drops a backlog after a long stall")
{
    NullClock clock;
    FrameLimiter limiter(clock);
    limiter.setRate(60);

    limiter.wait();
    CHECK(clock.now() == period(60));

    // A 50 ms stall leaves the next deadline more than a period behind. The
    // limiter starts a fresh one period from now instead of bursting to catch up.
    REQUIRE(clock.advance(std::chrono::milliseconds(50)));
    const Duration stalled = clock.now();
    limiter.wait();
    CHECK(clock.now() == stalled + period(60));
}
