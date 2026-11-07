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

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace
{
    using namespace babylon::audio;
    using babylon::log::CaptureSink;
}

TEST_CASE("The Null device names itself, lists one output and opens it")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CaptureSink sink;
    CHECK(device->info().backend == "null");
    const std::vector<OutputDevice> outputs = device->listOutputDevices();
    REQUIRE(outputs.size() == 1);
    CHECK(outputs[0].id == "null");
    CHECK_FALSE(outputs[0].name.empty());
    CHECK_FALSE(device->isOpen());
    CHECK(device->open("null"));
    babylon::audio::test::noWarning(sink);
    CHECK(device->isOpen());
    device->close();
    babylon::audio::test::noWarning(sink);
    CHECK_FALSE(device->isOpen());
    device->loseDevice();
    CHECK_FALSE(device->pollEvent().has_value());
    CHECK(device->open());
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("A lost Null output reports once and reopens with handles intact")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CaptureSink sink;
    REQUIRE(device->open());
    const std::vector<std::byte> data = babylon::audio::test::silence(100,
        SampleFormat::Signed16, 1);
    const SoundHandle sound = device->createSound(babylon::audio::test::descOf(data, 48000));
    REQUIRE_FALSE(sound.isNull());
    const VoiceHandle voice = device->createVoice(sound);
    REQUIRE_FALSE(voice.isNull());
    CHECK(device->play(voice, true));
    babylon::audio::test::noWarning(sink);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 10);
    babylon::audio::test::noWarning(sink);

    device->loseDevice();
    babylon::audio::test::noWarning(sink);
    CHECK_FALSE(device->isOpen());
    CHECK(device->pollEvent() == DeviceEvent::DeviceLost);
    CHECK_FALSE(device->pollEvent().has_value());
    CHECK(device->soundInfo(sound).has_value());
    CHECK(device->isPlaying(voice));
    CHECK(babylon::audio::test::positionOf(*device, voice) == 10);
    babylon::audio::test::noWarning(sink);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 10);
    babylon::audio::test::noWarning(sink);

    device->loseDevice();
    CHECK_FALSE(device->pollEvent().has_value());

    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);
    device->advanceFrames(5);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 15);
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(voice);
    device->destroySound(sound);
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("The Null device advances voices only while open and playing")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CaptureSink sink;
    const std::vector<std::byte> data = babylon::audio::test::silence(1000,
        SampleFormat::Signed16, 1);
    const SoundHandle sound = device->createSound(babylon::audio::test::descOf(data, 48000));
    REQUIRE_FALSE(sound.isNull());
    const VoiceHandle voice = device->createVoice(sound);
    REQUIRE_FALSE(voice.isNull());

    // Not open: nothing moves.
    CHECK(device->play(voice));
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 0);
    babylon::audio::test::noWarning(sink);

    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);
    device->advanceFrames(0);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 0);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 10);
    babylon::audio::test::noWarning(sink);

    // Paused: nothing moves.
    device->setPaused(voice, true);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 10);
    device->setPaused(voice, false);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 20);
    babylon::audio::test::noWarning(sink);

    // Stopped: nothing moves, and play restarts.
    device->stop(voice);
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 0);
    CHECK(device->play(voice));
    device->advanceFrames(5);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 5);
    babylon::audio::test::noWarning(sink);

    // Closing freezes; reopening resumes.
    device->close();
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 5);
    REQUIRE(device->open());
    device->advanceFrames(10);
    CHECK(babylon::audio::test::positionOf(*device, voice) == 15);
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(voice);
    device->destroySound(sound);
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("The Null device advances each voice by its own rate and pitch")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CaptureSink sink;
    REQUIRE(device->open());
    babylon::audio::test::noWarning(sink);

    const std::vector<std::byte> fastData = babylon::audio::test::silence(1000,
        SampleFormat::Signed16, 1);
    const std::vector<std::byte> slowData = babylon::audio::test::silence(1000,
        SampleFormat::Signed16, 1);
    const SoundHandle fast = device->createSound(babylon::audio::test::descOf(fastData, 48000));
    const SoundHandle slow = device->createSound(babylon::audio::test::descOf(slowData, 24000));
    REQUIRE_FALSE(fast.isNull());
    REQUIRE_FALSE(slow.isNull());
    const VoiceHandle fastVoice = device->createVoice(fast);
    const VoiceHandle slowVoice = device->createVoice(slow);
    REQUIRE_FALSE(fastVoice.isNull());
    REQUIRE_FALSE(slowVoice.isNull());
    CHECK(device->play(fastVoice));
    CHECK(device->play(slowVoice));
    CHECK(device->setVoicePitch(slowVoice, 2.0f));
    babylon::audio::test::noWarning(sink);

    device->advanceFrames(100);
    CHECK(babylon::audio::test::positionOf(*device, fastVoice) == 100);
    CHECK(babylon::audio::test::positionOf(*device, slowVoice) == 100);
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(fastVoice);
    device->destroyVoice(slowVoice);
    device->destroySound(fast);
    device->destroySound(slow);
    babylon::audio::test::noWarning(sink);
}

TEST_CASE("The Null device reports the effective gain and pan after 3D")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CaptureSink sink;
    const std::vector<std::byte> data = babylon::audio::test::silence(100,
        SampleFormat::Signed16, 1);
    const SoundHandle sound = device->createSound(babylon::audio::test::descOf(data, 48000));
    REQUIRE_FALSE(sound.isNull());
    const VoiceHandle voice = device->createVoice(sound);
    REQUIRE_FALSE(voice.isNull());
    babylon::audio::test::noWarning(sink);

    // 2D defaults: unity gain, center pan.
    std::optional<VoiceMix> mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(1.0f));
    CHECK(mix->pan == doctest::Approx(0.0f));

    // 2D settings and the master gain.
    CHECK(device->setVoiceGain(voice, 0.8f));
    CHECK(device->setVoicePan(voice, 0.5f));
    CHECK(device->setMasterGain(0.5f));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(0.4f));
    CHECK(mix->pan == doctest::Approx(0.5f));
    babylon::audio::test::noWarning(sink);

    // 3D with the default listener: right is +x, so +x pans right.
    CHECK(device->setMasterGain(1.0f));
    CHECK(device->setVoiceGain(voice, 1.0f));
    CHECK(device->setVoicePan(voice, -1.0f));
    device->enableVoiceSpatial(voice, true);
    CHECK(device->setVoicePosition(voice, Vector3{ 10.0f, 0.0f, 0.0f }));
    CHECK(device->setVoiceDistanceRange(voice, 1.0f, 100.0f));
    CHECK(device->setVoiceRolloff(voice, 1.0f));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(1.0f));
    CHECK(mix->gain == doctest::Approx(0.9090909f).epsilon(0.0001));
    babylon::audio::test::noWarning(sink);

    // The opposite side pans left.
    CHECK(device->setVoicePosition(voice, Vector3{ -10.0f, 0.0f, 0.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(-1.0f));

    // Directly in front is centered.
    CHECK(device->setVoicePosition(voice, Vector3{ 0.0f, 0.0f, 10.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(0.0f));

    // Within minDistance there is no attenuation.
    CHECK(device->setVoicePosition(voice, Vector3{ 0.5f, 0.0f, 0.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(1.0f));
    CHECK(mix->pan == doctest::Approx(1.0f));

    // At maxDistance the voice is silent, unless rolloff is 0.
    CHECK(device->setVoicePosition(voice, Vector3{ 100.0f, 0.0f, 0.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(0.0f));
    CHECK(device->setVoiceRolloff(voice, 0.0f));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(1.0f));
    babylon::audio::test::noWarning(sink);

    // Voice gain and master gain multiply the attenuation.
    CHECK(device->setVoiceRolloff(voice, 1.0f));
    CHECK(device->setVoiceGain(voice, 0.5f));
    CHECK(device->setMasterGain(0.5f));
    CHECK(device->setVoicePosition(voice, Vector3{ 10.0f, 0.0f, 0.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(0.2272727f).epsilon(0.0001));
    babylon::audio::test::noWarning(sink);

    // The distance factor scales the distance used for attenuation.
    CHECK(device->setVoiceGain(voice, 1.0f));
    CHECK(device->setMasterGain(1.0f));
    CHECK(device->setDistanceFactor(2.0f));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->gain == doctest::Approx(0.8080808f).epsilon(0.0001));
    CHECK(device->setDistanceFactor(1.0f));
    babylon::audio::test::noWarning(sink);

    // A rotated listener fixes the handedness: facing +x with +y up makes
    // right -z, so +z is left and -z is right.
    Listener rotated;
    rotated.forward = Vector3{ 1.0f, 0.0f, 0.0f };
    rotated.up = Vector3{ 0.0f, 1.0f, 0.0f };
    CHECK(device->setListener(rotated));
    CHECK(device->setVoicePosition(voice, Vector3{ 0.0f, 0.0f, 10.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(-1.0f));
    CHECK(device->setVoicePosition(voice, Vector3{ 0.0f, 0.0f, -10.0f }));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(1.0f));
    babylon::audio::test::noWarning(sink);

    // A voice at the listener has no direction: center, no attenuation.
    CHECK(device->setListener(Listener{}));
    CHECK(device->setVoicePosition(voice, Vector3{}));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(0.0f));
    CHECK(mix->gain == doctest::Approx(1.0f));

    // Turning 3D off returns to the 2D pan and gain.
    device->enableVoiceSpatial(voice, false);
    CHECK(device->setVoicePan(voice, -0.25f));
    mix = device->voiceMix(voice);
    REQUIRE(mix.has_value());
    CHECK(mix->pan == doctest::Approx(-0.25f));
    CHECK(mix->gain == doctest::Approx(1.0f));
    babylon::audio::test::noWarning(sink);

    device->destroyVoice(voice);
    CHECK_FALSE(device->voiceMix(voice).has_value());
    babylon::audio::test::oneWarning(sink);
    CHECK_FALSE(device->voiceMix(VoiceHandle{}).has_value());
    babylon::audio::test::oneWarning(sink);
    device->destroySound(sound);
    babylon::audio::test::noWarning(sink);
}
