// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests only for the miniaudio backend, run on miniaudio's null device so
// they work without a sound card. The shared Device contract is checked in
// tests/AudioContractTest.cpp, which runs the miniaudio backend there too;
// these cases cover the parts only a real, clock-driven output exercises:
// opening by an enumerated id and playback reaching its end by itself.

#include <babylon/audio/Handle.h>
#include <babylon/audio/MiniaudioDevice.h>
#include <babylon/audio/Sound.h>
#include <babylon/audio/Stream.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>

#include "../../../tests/TestHelpers.h"

#include <doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    using namespace babylon::audio;
    using babylon::log::CaptureSink;
    using namespace std::chrono_literals;

    std::unique_ptr<MiniaudioDevice> nullBackendDevice()
    {
        MiniaudioOptions options;
        options.useNullBackend = true;
        return createMiniaudioDevice(options);
    }

    // Sleeps until done() is true, at most timeout. The null device mixes on
    // the host clock, so tests wait on real time; the timeouts are far above
    // the sounds' lengths and only fail if the device stops advancing.
    bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while ( !done() )
        {
            if ( std::chrono::steady_clock::now() >= deadline )
            {
                return false;
            }
            std::this_thread::sleep_for(5ms);
        }
        return true;
    }
}

TEST_CASE("The miniaudio backend opens its default output on the null device")
{
    CaptureSink sink;
    std::unique_ptr<MiniaudioDevice> device = nullBackendDevice();
    REQUIRE(device != nullptr);
    CHECK(device->info().backend == "miniaudio");
    CHECK_FALSE(device->isOpen());
    CHECK(device->open());
    babylon::audio::test::noWarning(sink);
    CHECK(device->isOpen());
    CHECK_FALSE(device->pollEvent().has_value());
    device->close();
    babylon::audio::test::noWarning(sink);
    CHECK_FALSE(device->isOpen());
}

TEST_CASE("The miniaudio backend lists output devices with hex ids")
{
    CaptureSink sink;
    std::unique_ptr<MiniaudioDevice> device = nullBackendDevice();
    const std::vector<OutputDevice> outputs = device->listOutputDevices();
    babylon::audio::test::noWarning(sink);
    REQUIRE_FALSE(outputs.empty());
    for ( const OutputDevice& output : outputs )
    {
        CHECK_FALSE(output.id.empty());
        CHECK_FALSE(output.name.empty());
        CHECK(output.id.size() % 2 == 0);
        for ( const char c : output.id )
        {
            CHECK(( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ));
        }
    }

    // Reopening through an enumerated id succeeds.
    CHECK(device->open(outputs.front().id));
    babylon::audio::test::noWarning(sink);
    CHECK(device->isOpen());
    CHECK(device->open()); // back to the default
    babylon::audio::test::noWarning(sink);
    device->close();
}

TEST_CASE("A short sound plays to its end on the null device")
{
    CaptureSink sink;
    std::unique_ptr<MiniaudioDevice> device = nullBackendDevice();
    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);

    const std::vector<std::byte> data = babylon::audio::test::silence(4800,
        SampleFormat::Signed16, 1);
    const SoundHandle sound = device->createSound(babylon::audio::test::descOf(data, 48000));
    REQUIRE_FALSE(sound.isNull());
    const VoiceHandle voice = device->createVoice(sound);
    REQUIRE_FALSE(voice.isNull());
    CHECK(device->play(voice));
    babylon::audio::test::noWarning(sink);
    CHECK(device->isPlaying(voice));

    // 100 ms of audio on the host clock; 30 s is slack for a loaded machine.
    CHECK(waitFor([&device, voice] { return !device->isPlaying(voice); }, 30000ms));
    CHECK(babylon::audio::test::positionOf(*device, voice) == 4800);
    CHECK_FALSE(device->pollEvent().has_value());
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(voice);
    device->destroySound(sound);
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("A looping sound keeps playing past its end")
{
    CaptureSink sink;
    std::unique_ptr<MiniaudioDevice> device = nullBackendDevice();
    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);

    const std::vector<std::byte> data = babylon::audio::test::silence(2400,
        SampleFormat::Signed16, 1);
    const SoundHandle sound = device->createSound(babylon::audio::test::descOf(data, 48000));
    REQUIRE_FALSE(sound.isNull());
    const VoiceHandle voice = device->createVoice(sound);
    REQUIRE_FALSE(voice.isNull());
    CHECK(device->play(voice, true));
    babylon::audio::test::noWarning(sink);

    CHECK(waitFor([&device, voice] {
        const std::optional<std::uint64_t> position = device->voicePosition(voice);
        return position.has_value() && *position >= 4800;
    }, 30000ms));
    CHECK(device->isPlaying(voice));
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(voice);
    device->destroySound(sound);
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("A stream underrun keeps the voice playing on the null device")
{
    CaptureSink sink;
    std::unique_ptr<MiniaudioDevice> device = nullBackendDevice();
    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);

    const VoiceHandle stream = device->createStreamVoice(StreamFormat{ SampleFormat::Signed16,
        48000, 1 });
    REQUIRE_FALSE(stream.isNull());
    CHECK(device->play(stream));
    babylon::audio::test::noWarning(sink);

    const std::vector<std::byte> block = babylon::audio::test::silence(4800,
        SampleFormat::Signed16, 1);
    CHECK(device->pushStream(stream, block));
    babylon::audio::test::noWarning(sink);

    // Wait until the queue has drained into silence, then the voice must
    // still play and, per the contract, an underrun advances nothing.
    CHECK(waitFor([&device, stream]
        {
            return babylon::audio::test::streamStateOf(*device, stream).queuedFrames == 0;
        }, 30000ms));
    CHECK(device->isPlaying(stream));
    const StreamState drained = babylon::audio::test::streamStateOf(*device, stream);
    CHECK(drained.playedFrames > 0);
    CHECK(babylon::audio::test::positionOf(*device, stream) == drained.playedFrames);
    babylon::audio::test::noWarning(sink);

    std::this_thread::sleep_for(200ms);
    CHECK(device->isPlaying(stream));
    CHECK(babylon::audio::test::streamStateOf(*device, stream) == drained);
    babylon::audio::test::noWarning(sink);

    // New data refills the queue and consumption resumes.
    CHECK(device->pushStream(stream, block));
    babylon::audio::test::noWarning(sink);
    CHECK(babylon::audio::test::streamStateOf(*device, stream).queuedFrames > 0);
    CHECK(waitFor([&device, stream, drained]
        {
            return babylon::audio::test::streamStateOf(*device, stream).playedFrames
                > drained.playedFrames;
        }, 30000ms));
    CHECK(device->isPlaying(stream));
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(stream);
    babylon::audio::test::noWarning(sink);
}
