// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/audio/Sound.h>

#include <cstdint>

namespace babylon::audio
{

    // The PCM format of a stream voice. The layout is the one SoundDesc
    // describes, without data of its own.
    struct StreamFormat
    {
        SampleFormat format = SampleFormat::Signed16;
        std::uint32_t sampleRate = 0; // frames per second
        std::uint32_t channels = 1;   // 1 or 2

        bool operator==(const StreamFormat&) const = default;
    };

    // The largest number of frames one stream voice may have queued at once:
    // 1,048,576 frames, about 24 s at 44.1 kHz mono 16-bit and 4 MiB. This
    // bounds the memory a caller that pushes faster than playback can use.
    inline constexpr std::uint64_t kMaxQueuedStreamFrames = std::uint64_t{ 1 } << 20;

    // The state of a stream voice, as Device::streamState() returns it.
    // queuedFrames were pushed and not mixed yet. playedFrames are source
    // frames actually mixed since play() began, so time spent waiting for
    // data is not counted. A video player can pace itself with both: push
    // while queuedFrames is low, and compare playedFrames with what it
    // expected to have been consumed. Both are presentation data and must
    // not feed simulation state.
    struct StreamState
    {
        std::uint64_t queuedFrames = 0;
        std::uint64_t playedFrames = 0;

        bool operator==(const StreamState&) const = default;
    };

}
