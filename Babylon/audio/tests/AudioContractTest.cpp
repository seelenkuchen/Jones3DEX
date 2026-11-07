// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/audio/Device.h>
#include <babylon/audio/Handle.h>
#include <babylon/audio/NullDevice.h>
#include <babylon/audio/Sound.h>
#include <babylon/audio/Spatial.h>
#include <babylon/audio/Stream.h>

#include <babylon/log/CaptureSink.h>

#include "TestHelpers.h"

#include <doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace babylon::audio;
    using babylon::log::CaptureSink;

    // One backend and, when it has one, a way to drive its output clock.
    struct DeviceUnderTest
    {
        const char* name = "";
        std::unique_ptr<Device> device;
        std::function<void(std::uint64_t)> advance;
    };

    using DeviceFactory = std::function<DeviceUnderTest()>;

    // The backends the Device contract runs against. M7.2 adds the miniaudio
    // backend here; a backend without a deterministic clock leaves advance
    // empty and the time-dependent cases report a skip.
    const DeviceFactory kDeviceFactories[] = {
        [] {
            std::unique_ptr<NullDevice> device = createNullDevice();
            NullDevice* const raw = device.get();
            return DeviceUnderTest{ "null", std::move(device),
                [raw](std::uint64_t frames) { raw->advanceFrames(frames); } };
        },
    };

    void forEachDevice(const std::function<void(Device&, const DeviceUnderTest&)>& check)
    {
        for ( const DeviceFactory& factory : kDeviceFactories )
        {
            DeviceUnderTest harness = factory();
            INFO("device: " << harness.name);
            REQUIRE(harness.device != nullptr);
            check(*harness.device, harness);
        }
    }

    // Opens the default output, or reports a skip when the backend has none
    // in this environment.
    bool openDefault(Device& device, CaptureSink& sink)
    {
        if ( device.open() )
        {
            babylon::audio::test::noWarning(sink);
            return true;
        }
        MESSAGE("skipped: no default output: " << babylon::audio::test::joined(sink));
        sink.clear();
        return false;
    }
}

TEST_CASE("An audio device reports its backend and output devices")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        const DeviceInfo& info = device.info();
        CHECK_FALSE(info.backend.empty());
        for ( const OutputDevice& output : device.listOutputDevices() )
        {
            CHECK_FALSE(output.id.empty());
            CHECK_FALSE(output.name.empty());
        }
    });
}

TEST_CASE("Open and close control the output and keep the device usable")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        if ( !openDefault(device, sink) )
        {
            return;
        }
        CHECK(device.isOpen());
        device.close();
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isOpen());
        device.close();
        babylon::audio::test::noWarning(sink);
        CHECK(device.open());
        babylon::audio::test::noWarning(sink);
        CHECK(device.isOpen());
    });
}

TEST_CASE("Opening an unknown output is refused and leaves no output open")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        if ( !openDefault(device, sink) )
        {
            return;
        }
        CHECK_FALSE(device.open("no such output"));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.isOpen());
    });
}

TEST_CASE("Sounds are immutable copies and stale handles are refused")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(64,
            SampleFormat::Signed16, 2);
        const SoundDesc desc = babylon::audio::test::descOf(data, 44100, SampleFormat::Signed16, 2);
        const SoundHandle sound = device.createSound(desc);
        REQUIRE_FALSE(sound.isNull());
        babylon::audio::test::noWarning(sink);
        const std::optional<SoundInfo> info = device.soundInfo(sound);
        REQUIRE(info.has_value());
        CHECK(*info == SoundInfo{ SampleFormat::Signed16, 44100, 2, 64 });
        babylon::audio::test::noWarning(sink);

        const SoundHandle other = device.createSound(desc);
        REQUIRE_FALSE(other.isNull());
        CHECK(other != sound);
        babylon::audio::test::noWarning(sink);

        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
        device.destroySound(sound);
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.soundInfo(sound).has_value());
        babylon::audio::test::oneWarning(sink);

        device.destroySound(SoundHandle{});
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.soundInfo(SoundHandle{}).has_value());
        babylon::audio::test::oneWarning(sink);

        const SoundHandle third = device.createSound(desc);
        CHECK_FALSE(third.isNull());
        CHECK(third != sound);
        CHECK(device.soundInfo(third).has_value());
        babylon::audio::test::noWarning(sink);
        device.destroySound(other);
        device.destroySound(third);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Invalid sound descs are refused with one warning each")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(16,
            SampleFormat::Signed16, 1);
        std::vector<std::byte> odd = data;
        odd.push_back(std::byte{ 0 });
        const SoundDesc invalid[] = {
            { static_cast<SampleFormat>(99), 48000, 1, std::span<const std::byte>{ data } },
            { SampleFormat::Signed16, 0, 1, std::span<const std::byte>{ data } },
            { SampleFormat::Signed16, kMaxSampleRate + 1, 1,
                std::span<const std::byte>{ data } },
            { SampleFormat::Signed16, 48000, 0, std::span<const std::byte>{ data } },
            { SampleFormat::Signed16, 48000, 3, std::span<const std::byte>{ data } },
            { SampleFormat::Signed16, 48000, 1, std::span<const std::byte>{} },
            { SampleFormat::Signed16, 48000, 1, std::span<const std::byte>{ odd } },
            { SampleFormat::Unsigned8, 48000, 2, std::span<const std::byte>{ odd } },
        };
        for ( const SoundDesc& desc : invalid )
        {
            CHECK(device.createSound(desc).isNull());
            babylon::audio::test::oneWarning(sink);
        }

        // The bounds themselves are accepted.
        const std::vector<std::byte> boundary = babylon::audio::test::silence(1,
            SampleFormat::Unsigned8, 2);
        const SoundHandle lowest = device.createSound(SoundDesc{ SampleFormat::Unsigned8, 1, 2,
            std::span<const std::byte>{ boundary } });
        const SoundHandle highest = device.createSound(SoundDesc{ SampleFormat::Signed16,
            kMaxSampleRate, 1, std::span<const std::byte>{ data } });
        CHECK_FALSE(lowest.isNull());
        CHECK_FALSE(highest.isNull());
        babylon::audio::test::noWarning(sink);
        device.destroySound(lowest);
        device.destroySound(highest);
    });
}

TEST_CASE("Voices are handles with a defined lifetime and stale handles are refused")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(100,
            SampleFormat::Signed16, 1);
        const SoundDesc desc = babylon::audio::test::descOf(data, 48000);
        const SoundHandle sound = device.createSound(desc);
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == 0);
        babylon::audio::test::noWarning(sink);

        device.destroyVoice(voice);
        babylon::audio::test::noWarning(sink);
        device.destroyVoice(voice);
        babylon::audio::test::oneWarning(sink);

        CHECK_FALSE(device.play(voice));
        babylon::audio::test::oneWarning(sink);
        device.stop(voice);
        babylon::audio::test::oneWarning(sink);
        device.setPaused(voice, true);
        babylon::audio::test::oneWarning(sink);
        device.enableVoiceSpatial(voice, true);
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.isPlaying(voice));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.voicePosition(voice).has_value());
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceGain(voice, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePan(voice, 0.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePitch(voice, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePosition(voice, Vector3{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceVelocity(voice, Vector3{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceDistanceRange(voice, 1.0f, 2.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceRolloff(voice, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.pushStream(voice, data));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.streamState(voice).has_value());
        babylon::audio::test::oneWarning(sink);

        // The null handle: destroying, stopping, pausing and toggling 3D are
        // silent no-ops; queries and the rest are refused.
        device.destroyVoice(VoiceHandle{});
        babylon::audio::test::noWarning(sink);
        device.stop(VoiceHandle{});
        babylon::audio::test::noWarning(sink);
        device.setPaused(VoiceHandle{}, true);
        babylon::audio::test::noWarning(sink);
        device.enableVoiceSpatial(VoiceHandle{}, true);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.play(VoiceHandle{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.isPlaying(VoiceHandle{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.voicePosition(VoiceHandle{}).has_value());
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceGain(VoiceHandle{}, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePan(VoiceHandle{}, 0.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePitch(VoiceHandle{}, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePosition(VoiceHandle{}, Vector3{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceVelocity(VoiceHandle{}, Vector3{}));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceDistanceRange(VoiceHandle{}, 1.0f, 2.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceRolloff(VoiceHandle{}, 1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.pushStream(VoiceHandle{}, data));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.streamState(VoiceHandle{}).has_value());
        babylon::audio::test::oneWarning(sink);

        // A voice needs a live sound.
        const SoundHandle deadSound = device.createSound(desc);
        REQUIRE_FALSE(deadSound.isNull());
        device.destroySound(deadSound);
        babylon::audio::test::noWarning(sink);
        CHECK(device.createVoice(deadSound).isNull());
        babylon::audio::test::oneWarning(sink);
        CHECK(device.createVoice(SoundHandle{}).isNull());
        babylon::audio::test::oneWarning(sink);

        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("A voice keeps playing after its sound is destroyed")
{
    forEachDevice([](Device& device, const DeviceUnderTest& harness)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(100,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(data, 48000));
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());
        CHECK(device.play(voice));
        babylon::audio::test::noWarning(sink);

        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.soundInfo(sound).has_value());
        babylon::audio::test::oneWarning(sink);
        CHECK(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == 0);
        babylon::audio::test::noWarning(sink);

        if ( harness.advance && openDefault(device, sink) )
        {
            harness.advance(10);
            CHECK(babylon::audio::test::positionOf(device, voice) == 10);
            babylon::audio::test::noWarning(sink);
            harness.advance(1000);
            CHECK_FALSE(device.isPlaying(voice));
            CHECK(babylon::audio::test::positionOf(device, voice) == 100);
            babylon::audio::test::noWarning(sink);
        }
        device.destroyVoice(voice);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Play, pause and stop follow the contract")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(100,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(data, 48000));
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());

        CHECK(device.play(voice));
        babylon::audio::test::noWarning(sink);
        CHECK(device.isPlaying(voice));

        device.setPaused(voice, true);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isPlaying(voice));
        device.setPaused(voice, true);
        babylon::audio::test::noWarning(sink);
        device.setPaused(voice, false);
        babylon::audio::test::noWarning(sink);
        CHECK(device.isPlaying(voice));

        device.stop(voice);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == 0);
        device.stop(voice);
        babylon::audio::test::noWarning(sink);
        device.setPaused(voice, true);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isPlaying(voice));
        device.setPaused(voice, false);
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isPlaying(voice));

        // Playing again restarts from the first frame and clears pause.
        device.setPaused(voice, true);
        babylon::audio::test::noWarning(sink);
        CHECK(device.play(voice, true));
        babylon::audio::test::noWarning(sink);
        CHECK(device.isPlaying(voice));

        // A stream refuses the loop flag and stays unchanged.
        const VoiceHandle stream = device.createStreamVoice(StreamFormat{ SampleFormat::Signed16,
            48000, 1 });
        REQUIRE_FALSE(stream.isNull());
        CHECK_FALSE(device.play(stream, true));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.isPlaying(stream));
        babylon::audio::test::noWarning(sink);
        CHECK(device.play(stream, false));
        babylon::audio::test::noWarning(sink);
        CHECK(device.isPlaying(stream));
        babylon::audio::test::noWarning(sink);

        device.destroyVoice(stream);
        device.destroyVoice(voice);
        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Invalid stream formats are refused with one warning each")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const StreamFormat invalid[] = {
            { static_cast<SampleFormat>(99), 48000, 1 },
            { SampleFormat::Signed16, 0, 1 },
            { SampleFormat::Signed16, kMaxSampleRate + 1, 1 },
            { SampleFormat::Signed16, 48000, 0 },
            { SampleFormat::Signed16, 48000, 3 },
        };
        for ( const StreamFormat& format : invalid )
        {
            CHECK(device.createStreamVoice(format).isNull());
            babylon::audio::test::oneWarning(sink);
        }

        const VoiceHandle lowest = device.createStreamVoice(StreamFormat{ SampleFormat::Unsigned8,
            1, 1 });
        const VoiceHandle highest = device.createStreamVoice(StreamFormat{ SampleFormat::Signed16,
            kMaxSampleRate, 2 });
        CHECK_FALSE(lowest.isNull());
        CHECK_FALSE(highest.isNull());
        babylon::audio::test::noWarning(sink);
        device.destroyVoice(lowest);
        device.destroyVoice(highest);
    });
}

TEST_CASE("Stream pushes, the queue cap and the reported state follow the contract")
{
    forEachDevice([](Device& device, const DeviceUnderTest& harness)
    {
        CaptureSink sink;
        const VoiceHandle stream = device.createStreamVoice(StreamFormat{ SampleFormat::Signed16,
            48000, 1 });
        REQUIRE_FALSE(stream.isNull());
        babylon::audio::test::noWarning(sink);

        CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 0, 0 });
        babylon::audio::test::noWarning(sink);

        const std::vector<std::byte> block = babylon::audio::test::silence(100,
            SampleFormat::Signed16, 1);
        CHECK(device.pushStream(stream, block));
        babylon::audio::test::noWarning(sink);
        CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 100, 0 });
        babylon::audio::test::noWarning(sink);

        CHECK(device.pushStream(stream, {}));
        babylon::audio::test::noWarning(sink);
        CHECK(babylon::audio::test::streamStateOf(device, stream).queuedFrames == 100);

        std::vector<std::byte> odd = block;
        odd.pop_back();
        CHECK_FALSE(device.pushStream(stream, odd));
        babylon::audio::test::oneWarning(sink);
        CHECK(babylon::audio::test::streamStateOf(device, stream).queuedFrames == 100);

        // A sound voice is not a stream.
        const std::vector<std::byte> soundData = babylon::audio::test::silence(10,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(soundData, 48000));
        const VoiceHandle soundVoice = device.createVoice(sound);
        REQUIRE_FALSE(soundVoice.isNull());
        CHECK_FALSE(device.pushStream(soundVoice, block));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.streamState(soundVoice).has_value());
        babylon::audio::test::oneWarning(sink);

        // Fill to the cap and refuse one frame more, without changing it.
        const std::vector<std::byte> fill = babylon::audio::test::silence(
            static_cast<std::size_t>(kMaxQueuedStreamFrames) - 100, SampleFormat::Signed16, 1);
        CHECK(device.pushStream(stream, fill));
        babylon::audio::test::noWarning(sink);
        CHECK(babylon::audio::test::streamStateOf(device, stream).queuedFrames
            == kMaxQueuedStreamFrames);
        const std::vector<std::byte> one = babylon::audio::test::silence(1,
            SampleFormat::Signed16, 1);
        CHECK_FALSE(device.pushStream(stream, one));
        babylon::audio::test::oneWarning(sink);
        CHECK(babylon::audio::test::streamStateOf(device, stream).queuedFrames
            == kMaxQueuedStreamFrames);

        // Time-dependent part: play resets the queue, then push and consume.
        if ( harness.advance && openDefault(device, sink) )
        {
            CHECK(device.play(stream));
            babylon::audio::test::noWarning(sink);
            CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 0, 0 });
            babylon::audio::test::noWarning(sink);

            CHECK(device.pushStream(stream, block));
            babylon::audio::test::noWarning(sink);
            harness.advance(60);
            CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 40, 60 });
            CHECK(babylon::audio::test::positionOf(device, stream) == 60);
            babylon::audio::test::noWarning(sink);

            // Advancing past the queue is an underrun: silence, no position
            // movement, still playing.
            harness.advance(100);
            CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 0, 100 });
            CHECK(device.isPlaying(stream));
            babylon::audio::test::noWarning(sink);
            harness.advance(100);
            CHECK(babylon::audio::test::streamStateOf(device, stream).playedFrames == 100);
            babylon::audio::test::noWarning(sink);

            // New data resumes consumption.
            CHECK(device.pushStream(stream, block));
            babylon::audio::test::noWarning(sink);
            harness.advance(10);
            CHECK(babylon::audio::test::streamStateOf(device, stream) == StreamState{ 90, 110 });
            babylon::audio::test::noWarning(sink);
        }

        device.destroyVoice(soundVoice);
        device.destroyVoice(stream);
        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Pitch and loop arithmetic follow the output clock")
{
    forEachDevice([](Device& device, const DeviceUnderTest& harness)
    {
        if ( !harness.advance )
        {
            MESSAGE("skipped: this backend has no deterministic output clock");
            return;
        }
        CaptureSink sink;
        if ( !openDefault(device, sink) )
        {
            return;
        }

        const std::vector<std::byte> data = babylon::audio::test::silence(1000,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(data, 48000));
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());
        CHECK(device.play(voice, false));
        babylon::audio::test::noWarning(sink);

        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, voice) == 100);
        babylon::audio::test::noWarning(sink);

        CHECK(device.setVoicePitch(voice, 2.0f));
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, voice) == 300);
        babylon::audio::test::noWarning(sink);

        CHECK(device.setVoicePitch(voice, 0.5f));
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, voice) == 350);
        babylon::audio::test::noWarning(sink);

        // A rate below the output rate consumes proportionally fewer frames.
        const std::vector<std::byte> halfData = babylon::audio::test::silence(1000,
            SampleFormat::Signed16, 1);
        const SoundHandle half = device.createSound(babylon::audio::test::descOf(halfData, 24000));
        const VoiceHandle halfVoice = device.createVoice(half);
        REQUIRE_FALSE(halfVoice.isNull());
        CHECK(device.play(halfVoice));
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, halfVoice) == 50);
        babylon::audio::test::noWarning(sink);

        // A rate that doesn't divide the output rate carries the fraction.
        const std::vector<std::byte> cdData = babylon::audio::test::silence(1000,
            SampleFormat::Signed16, 1);
        const SoundHandle cd = device.createSound(babylon::audio::test::descOf(cdData, 44100));
        const VoiceHandle cdVoice = device.createVoice(cd);
        REQUIRE_FALSE(cdVoice.isNull());
        CHECK(device.play(cdVoice));
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, cdVoice) == 91);
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, cdVoice) == 183);
        babylon::audio::test::noWarning(sink);

        // Paused voices do not advance.
        CHECK(device.setVoicePitch(voice, 1.0f));
        babylon::audio::test::noWarning(sink);
        device.setPaused(voice, true);
        babylon::audio::test::noWarning(sink);
        const std::uint64_t before = babylon::audio::test::positionOf(device, voice);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, voice) == before);
        device.setPaused(voice, false);
        babylon::audio::test::noWarning(sink);

        // A non-looping sound stops at its last frame.
        CHECK(device.play(voice, false));
        babylon::audio::test::noWarning(sink);
        harness.advance(100000);
        CHECK_FALSE(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == 1000);
        babylon::audio::test::noWarning(sink);
        harness.advance(100);
        CHECK(babylon::audio::test::positionOf(device, voice) == 1000);
        babylon::audio::test::noWarning(sink);

        // A looping sound wraps the playback but keeps counting.
        CHECK(device.play(voice, true));
        babylon::audio::test::noWarning(sink);
        harness.advance(1500);
        CHECK(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == 1500);
        babylon::audio::test::noWarning(sink);
        harness.advance(600);
        CHECK(babylon::audio::test::positionOf(device, voice) == 2100);
        babylon::audio::test::noWarning(sink);

        device.destroyVoice(voice);
        device.destroyVoice(halfVoice);
        device.destroyVoice(cdVoice);
        device.destroySound(sound);
        device.destroySound(half);
        device.destroySound(cd);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Invalid voice settings are refused with one warning each")
{
    forEachDevice([](Device& device, const DeviceUnderTest&)
    {
        CaptureSink sink;
        const std::vector<std::byte> data = babylon::audio::test::silence(10,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(data, 48000));
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());
        babylon::audio::test::noWarning(sink);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        CHECK_FALSE(device.setMasterGain(-1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setMasterGain(nan));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setMasterGain(0.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setMasterGain(2.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.masterGain() == 2.0f);

        CHECK_FALSE(device.setVoiceGain(voice, -1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceGain(voice, inf));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoiceGain(voice, 2.0f));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setVoicePan(voice, -1.5f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePan(voice, 1.5f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePan(voice, nan));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoicePan(voice, -1.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setVoicePan(voice, 1.0f));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setVoicePitch(voice, 0.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePitch(voice, kMinPitch / 2.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePitch(voice, kMaxPitch * 2.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoicePitch(voice, nan));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoicePitch(voice, kMinPitch));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setVoicePitch(voice, kMaxPitch));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setVoicePosition(voice, Vector3{ nan, 0.0f, 0.0f }));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceVelocity(voice, Vector3{ 0.0f, inf, 0.0f }));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoicePosition(voice, Vector3{ 1.0f, 2.0f, 3.0f }));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setVoiceVelocity(voice, Vector3{}));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setVoiceDistanceRange(voice, -1.0f, 10.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceDistanceRange(voice, 10.0f, 10.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceDistanceRange(voice, 10.0f, 5.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceDistanceRange(voice, nan, 10.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoiceDistanceRange(voice, 0.0f, 1.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setVoiceDistanceRange(voice, 1.0f, 10000.0f));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setVoiceRolloff(voice, -0.1f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setVoiceRolloff(voice, inf));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setVoiceRolloff(voice, 0.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setVoiceRolloff(voice, 3.0f));
        babylon::audio::test::noWarning(sink);

        CHECK_FALSE(device.setDistanceFactor(-1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setDistanceFactor(nan));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setDistanceFactor(0.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setDistanceFactor(2.0f));
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.setDopplerFactor(-1.0f));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.setDopplerFactor(nan));
        babylon::audio::test::oneWarning(sink);
        CHECK(device.setDopplerFactor(0.0f));
        babylon::audio::test::noWarning(sink);
        CHECK(device.setDopplerFactor(2.0f));
        babylon::audio::test::noWarning(sink);

        Listener listener;
        CHECK(device.setListener(listener));
        babylon::audio::test::noWarning(sink);
        Listener nanListener = listener;
        nanListener.position.x = nan;
        CHECK_FALSE(device.setListener(nanListener));
        babylon::audio::test::oneWarning(sink);
        Listener zeroForward = listener;
        zeroForward.forward = Vector3{};
        CHECK_FALSE(device.setListener(zeroForward));
        babylon::audio::test::oneWarning(sink);
        Listener zeroUp = listener;
        zeroUp.up = Vector3{};
        CHECK_FALSE(device.setListener(zeroUp));
        babylon::audio::test::oneWarning(sink);
        Listener parallel = listener;
        parallel.forward = Vector3{ 0.0f, 1.0f, 0.0f };
        parallel.up = Vector3{ 0.0f, 1.0f, 0.0f };
        CHECK_FALSE(device.setListener(parallel));
        babylon::audio::test::oneWarning(sink);
        Listener infUp = listener;
        infUp.up.x = inf;
        CHECK_FALSE(device.setListener(infUp));
        babylon::audio::test::oneWarning(sink);

        device.destroyVoice(voice);
        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
    });
}

TEST_CASE("Closing and reopening keeps every handle and setting")
{
    forEachDevice([](Device& device, const DeviceUnderTest& harness)
    {
        CaptureSink sink;
        if ( !openDefault(device, sink) )
        {
            return;
        }

        const std::vector<std::byte> data = babylon::audio::test::silence(1000,
            SampleFormat::Signed16, 1);
        const SoundHandle sound = device.createSound(babylon::audio::test::descOf(data, 48000));
        REQUIRE_FALSE(sound.isNull());
        const VoiceHandle voice = device.createVoice(sound);
        REQUIRE_FALSE(voice.isNull());
        CHECK(device.setMasterGain(0.25f));
        CHECK(device.setVoiceGain(voice, 0.5f));
        CHECK(device.setVoicePan(voice, -0.5f));
        CHECK(device.setVoicePitch(voice, 1.0f));
        CHECK(device.play(voice, true));
        babylon::audio::test::noWarning(sink);
        if ( harness.advance )
        {
            harness.advance(100);
        }
        const std::uint64_t position = babylon::audio::test::positionOf(device, voice);
        if ( harness.advance )
        {
            CHECK(position == 100);
        }

        device.close();
        babylon::audio::test::noWarning(sink);
        CHECK_FALSE(device.isOpen());
        CHECK(device.masterGain() == 0.25f);
        CHECK(device.soundInfo(sound).has_value());
        CHECK(device.isPlaying(voice));
        CHECK(babylon::audio::test::positionOf(device, voice) == position);
        babylon::audio::test::noWarning(sink);

        if ( harness.advance )
        {
            harness.advance(1000);
            CHECK(babylon::audio::test::positionOf(device, voice) == position);
            babylon::audio::test::noWarning(sink);
        }

        CHECK(device.open());
        babylon::audio::test::noWarning(sink);
        CHECK(device.isOpen());
        if ( harness.advance )
        {
            harness.advance(10);
            CHECK(babylon::audio::test::positionOf(device, voice) == position + 10);
            babylon::audio::test::noWarning(sink);
        }

        // A failed open of another id closes the output and keeps handles.
        CHECK_FALSE(device.open("nope"));
        babylon::audio::test::oneWarning(sink);
        CHECK_FALSE(device.isOpen());
        CHECK(device.soundInfo(sound).has_value());
        babylon::audio::test::noWarning(sink);
        CHECK(device.isPlaying(voice));
        babylon::audio::test::noWarning(sink);
        CHECK(device.open());
        babylon::audio::test::noWarning(sink);

        device.destroyVoice(voice);
        device.destroySound(sound);
        babylon::audio::test::noWarning(sink);
    });
}
