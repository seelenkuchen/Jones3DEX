// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/audio/Device.h>
#include <babylon/audio/Handle.h>
#include <babylon/audio/Sound.h>
#include <babylon/audio/Stream.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace babylon::audio::test
{

    inline std::string joined(const log::CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    // Exactly one warning since the last check, then forgets it.
    inline void oneWarning(log::CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        REQUIRE(sink.records().size() == 1);
        CHECK(sink.records()[0].level == log::Level::Warning);
        sink.clear();
    }

    // Nothing logged since the last check.
    inline void noWarning(log::CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());
        sink.clear();
    }

    // Zero samples: frames frames of the given format and channel count.
    inline std::vector<std::byte> silence(std::size_t frames,
        SampleFormat format = SampleFormat::Signed16, std::uint32_t channels = 1)
    {
        return std::vector<std::byte>(frames * bytesPerFrame(format, channels), std::byte{ 0 });
    }

    // A sound desc whose data spans data. The caller keeps data alive until
    // the device call that copies it returns.
    inline SoundDesc descOf(const std::vector<std::byte>& data, std::uint32_t sampleRate,
        SampleFormat format = SampleFormat::Signed16, std::uint32_t channels = 1)
    {
        return SoundDesc{ format, sampleRate, channels, std::span<const std::byte>{ data } };
    }

    // Queries that must succeed for a live handle; a failure aborts the test.
    inline std::uint64_t positionOf(Device& device, VoiceHandle voice)
    {
        const std::optional<std::uint64_t> position = device.voicePosition(voice);
        REQUIRE(position.has_value());
        return *position;
    }

    inline StreamState streamStateOf(Device& device, VoiceHandle voice)
    {
        const std::optional<StreamState> state = device.streamState(voice);
        REQUIRE(state.has_value());
        return *state;
    }

    inline SoundInfo soundInfoOf(Device& device, SoundHandle sound)
    {
        const std::optional<SoundInfo> info = device.soundInfo(sound);
        REQUIRE(info.has_value());
        return *info;
    }

}
