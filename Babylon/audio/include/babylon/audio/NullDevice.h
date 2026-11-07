// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/audio/Device.h>
#include <babylon/audio/Handle.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace babylon::audio
{

    // The output rate advanceFrames() uses to turn output frames into source
    // frames, in frames per second. A test that wants exact voice positions
    // picks voice rates that divide it, such as 4800, 16000, 24000 or 48000.
    inline constexpr std::uint32_t kNullOutputSampleRate = 48000;

    // The 2D gain and pan the Null device would mix for a voice, after the
    // voice's own gain, its 3D distance attenuation and pan, and the master
    // gain. gain is linear amplitude; pan is -1 (left) .. 1 (right).
    //
    // For a 3D voice, pan is the signed component of the direction to the
    // voice along the listener's right, divided by the distance and clamped
    // to -1..1, where right = normalize(cross(up, forward)) as Spatial.h
    // fixes it. With the default forward +z and up +y, right is +x, so a
    // voice at +x pans to +1. A voice at the listener has pan 0.
    //
    // Distance attenuation is 1 within minDistance and
    // clamp(1 - rolloff * (d - minDistance) / (maxDistance - minDistance),
    // 0, 1) beyond it, with d scaled by the global distance factor. Thus
    // rolloff 0 means no attenuation at any distance and rolloff 1 reaches
    // silence at maxDistance. The doppler factor is kept but, since the Null
    // device reports no pitch, has no effect here.
    struct VoiceMix
    {
        float gain = 0.0f;
        float pan = 0.0f;

        bool operator==(const VoiceMix&) const = default;
    };

    // Device that opens no output and produces no sound, for tests and
    // headless runs. It honors the whole Device contract: the same checks
    // and warnings, handle lifetimes, gain, pan, pitch, 3D settings and
    // stream queues, all kept in memory. Playback advances only through
    // advanceFrames(), so a test controls how many frames every playing
    // voice consumes; nothing reads a real clock. Its one output device has
    // the id "null" and the name "Null output"; open() with an empty id or
    // "null" succeeds and every other id is refused. isPlaying(),
    // voicePosition() and streamState() report real state, but nothing can
    // hear the result.
    class NullDevice final : public Device
    {
    public:
        NullDevice();
        ~NullDevice() override;
        NullDevice(const NullDevice&) = delete;
        NullDevice& operator=(const NullDevice&) = delete;

        const DeviceInfo& info() const override;
        std::vector<OutputDevice> listOutputDevices() const override;
        bool open(std::string_view deviceId = {}) override;
        void close() override;
        bool isOpen() const override;
        std::optional<DeviceEvent> pollEvent() override;
        float masterGain() const override;

        // Test helper: advances the open output by frames output frames at
        // kNullOutputSampleRate. Every playing voice consumes
        // frames * voiceRate * pitch / kNullOutputSampleRate source frames,
        // and fractional frames carry over to the next call. A non-looping
        // sound stops at its last frame; a looping sound wraps and keeps
        // playing; a stream consumes queued frames and outputs silence
        // without advancing when the queue is empty. Paused, stopped and
        // already finished voices are unchanged. Does nothing while no
        // output is open; zero is a no-op.
        void advanceFrames(std::uint64_t frames);

        // Test helper: the gain and pan the Null device would mix for a
        // voice right now. It includes the voice's own gain, the 3D
        // attenuation and pan (see VoiceMix), and the master gain, and does
        // not depend on whether an output is open or the voice is playing.
        // nullopt and one warning for a null or stale handle.
        [[nodiscard]] std::optional<VoiceMix> voiceMix(VoiceHandle voice) const;

        // Test helper: reports the open output as lost, the way a real
        // device does when it is unplugged. Queues one DeviceLost event and
        // closes the output; sounds, voices and their state stay valid, and
        // open() opens the output again. Does nothing when no output is
        // open.
        void loseDevice();

    private:
        const SoundInfo* findSound(SoundHandle sound) const override;
        bool hasVoice(VoiceHandle voice) const override;
        std::optional<StreamFormat> findStreamFormat(VoiceHandle voice) const override;
        std::uint64_t queuedFrames(VoiceHandle voice) const override;

        void doSetMasterGain(float gain) override;
        SoundHandle doCreateSound(const SoundDesc& desc) override;
        void doDestroySound(SoundHandle sound) override;
        std::optional<SoundInfo> doSoundInfo(SoundHandle sound) const override;
        VoiceHandle doCreateVoice(SoundHandle sound) override;
        VoiceHandle doCreateStreamVoice(const StreamFormat& format) override;
        void doDestroyVoice(VoiceHandle voice) override;
        void doPlay(VoiceHandle voice, bool loop) override;
        void doStop(VoiceHandle voice) override;
        void doSetPaused(VoiceHandle voice, bool paused) override;
        bool doIsPlaying(VoiceHandle voice) const override;
        std::uint64_t doVoicePosition(VoiceHandle voice) const override;
        void doSetVoiceGain(VoiceHandle voice, float gain) override;
        void doSetVoicePan(VoiceHandle voice, float pan) override;
        void doSetVoicePitch(VoiceHandle voice, float ratio) override;
        void doEnableVoiceSpatial(VoiceHandle voice, bool enabled) override;
        void doSetVoicePosition(VoiceHandle voice, const Vector3& position) override;
        void doSetVoiceVelocity(VoiceHandle voice, const Vector3& velocity) override;
        void doSetVoiceDistanceRange(VoiceHandle voice, float minDistance,
            float maxDistance) override;
        void doSetVoiceRolloff(VoiceHandle voice, float rolloff) override;
        void doSetListener(const Listener& listener) override;
        void doSetDistanceFactor(float factor) override;
        void doSetDopplerFactor(float factor) override;
        void doPushStream(VoiceHandle voice, std::span<const std::byte> data) override;
        StreamState doStreamState(VoiceHandle voice) const override;

        // The sounds, voices, output state and event queue, private to
        // NullDevice.cpp.
        struct State;
        std::unique_ptr<State> m_state;
    };

    // Creates a Null device; never fails. The caller owns it.
    std::unique_ptr<NullDevice> createNullDevice();

}
