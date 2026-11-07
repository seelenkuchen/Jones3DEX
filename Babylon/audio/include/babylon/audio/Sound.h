// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace babylon::audio
{

    // Sample encodings a sound or a stream accepts. Both are integer PCM,
    // interleaved frame by frame:
    // - Unsigned8: one byte per sample, 0..255, with 128 as silence.
    // - Signed16: two bytes per sample, little-endian, -32768..32767.
    enum class SampleFormat : std::uint8_t
    {
        Unsigned8,
        Signed16,
    };

    // The highest sample rate createSound() and createStreamVoice() accept,
    // in frames per second. It is above what any current output device
    // offers, so callers don't lose data before the backend's own
    // conversion.
    inline constexpr std::uint32_t kMaxSampleRate = 384000;

    // Bytes of one sample in format: 1 for Unsigned8, 2 for Signed16.
    constexpr std::uint32_t bytesPerSample(SampleFormat format)
    {
        return format == SampleFormat::Unsigned8 ? 1 : 2;
    }

    // Bytes of one frame: channels samples.
    constexpr std::uint32_t bytesPerFrame(SampleFormat format, std::uint32_t channels)
    {
        return bytesPerSample(format) * channels;
    }

    // Immutable PCM data for one or more voices. Device::createSound() copies
    // data before it returns, so the caller may free or reuse the memory at
    // once.
    //
    // A sound is refused unless sampleRate is 1..kMaxSampleRate, channels is
    // 1 (mono) or 2 (stereo), and data holds at least one whole frame: a
    // non-empty size that is a multiple of bytesPerFrame(format, channels).
    // The data is interleaved, so one stereo frame is left then right.
    struct SoundDesc
    {
        SampleFormat format = SampleFormat::Signed16;
        std::uint32_t sampleRate = 0;    // frames per second
        std::uint32_t channels = 1;      // 1 or 2
        std::span<const std::byte> data; // interleaved frames, copied
    };

    // What a sound holds and how long it is. frameCount is
    // data.size() / bytesPerFrame(). Device::soundInfo() returns it.
    struct SoundInfo
    {
        SampleFormat format = SampleFormat::Signed16;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 1;
        std::uint64_t frameCount = 0;

        bool operator==(const SoundInfo&) const = default;
    };

}
