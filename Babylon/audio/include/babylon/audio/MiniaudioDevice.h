// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/audio/Device.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace babylon::audio
{

    // Backend options for createMiniaudioDevice().
    struct MiniaudioOptions
    {
        // Run on miniaudio's null output device (ma_backend_null) instead of
        // the host's real sound system. The null device plays in real time
        // like any other output but needs no sound card, so tests and CI can
        // exercise this backend everywhere.
        bool useNullBackend = false;
    };

    // The real sound output on miniaudio 0.11 (ADR-0010), with backend name
    // "miniaudio". It honors the whole Device contract; the rules shared with
    // the Null device (validation, handles, warnings, state across close and
    // open) live at Device. Design choices:
    //
    // - A low-level ma_device with a data callback that mixes the voices
    //   itself, not ma_engine: the contract's exact distance attenuation and
    //   pan rules, the source-frame counters of voicePosition() and
    //   streamState(), playback frozen while no output is open and the
    //   caller-fed stream queue all need frame-exact control that the engine
    //   does not expose.
    // - Threading: every public call runs on the caller's thread. One mutex
    //   guards the voice table; the audio callback locks it, mixes, and
    //   updates positions and stream counters, then unlocks. miniaudio's own
    //   device teardown only happens from caller-thread calls, never while
    //   the mutex is held, so the callback can never block it. Nothing calls
    //   back into the caller; device loss arrives through pollEvent().
    // - Output format: stereo, 32-bit float, 48000 frames per second
    //   requested; miniaudio adapts to the hardware. Sources are linearly
    //   resampled to that rate; pitch scales the resampling step. The last
    //   frame of a stream block is held (not interpolated past) until more
    //   data arrives.
    // - 3D: the same rules the Null backend documents at VoiceMix: distance
    //   attenuation is 1 within minDistance and clamp(1 - rolloff *
    //   (d - minDistance) / (maxDistance - minDistance), 0, 1) beyond, with d
    //   scaled by the global distance factor; pan is the direction to the
    //   voice along right = normalize(cross(up, forward)), clamped to -1..1.
    //   (gain, pan) becomes a stereo pair with an equal-power law for mono
    //   sources (left = gain * cos((pan + 1) * pi / 4), right = gain *
    //   sin((pan + 1) * pi / 4)) and a balance law for stereo sources (the
    //   side away from the pan is rolled off with cos(|pan| * pi / 2)).
    // - Doppler is approximate: once per callback period (~10 ms) the ratio
    //   (c + vL) / (c + vS) is computed from the listener and voice velocity
    //   components along the line between them, treating caller units as
    //   meters via the distance factor and c as 343 m/s; the shift is
    //   (ratio - 1) * dopplerFactor around 1 and clamped so the final pitch
    //   stays within kMinPitch..kMaxPitch. Velocities are assumed constant
    //   within the period.
    // - listOutputDevices() enumerates miniaudio's playback devices; the id
    //   is the hex encoding of the ma_device_id bytes, stable across
    //   enumerations on the same machine.
    // - Device loss: miniaudio's stopped and rerouted notifications queue
    //   one DeviceLost event, mark the output closed and freeze playback;
    //   pollEvent() reports the event and tears the device down on the
    //   caller's thread. open() starts it again; sounds and voices survive
    //   unchanged.
    class MiniaudioDevice final : public Device
    {
    public:
        explicit MiniaudioDevice(const MiniaudioOptions& options);
        ~MiniaudioDevice() override;
        MiniaudioDevice(const MiniaudioDevice&) = delete;
        MiniaudioDevice& operator=(const MiniaudioDevice&) = delete;

        const DeviceInfo& info() const override;
        std::vector<OutputDevice> listOutputDevices() const override;
        bool open(std::string_view deviceId = {}) override;
        void close() override;
        bool isOpen() const override;
        std::optional<DeviceEvent> pollEvent() override;
        float masterGain() const override;

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

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // Creates a miniaudio device. This does not touch an output: when the
    // miniaudio context cannot be initialized the device still exists and
    // open() and listOutputDevices() report one warning each. The caller owns
    // the device.
    std::unique_ptr<MiniaudioDevice> createMiniaudioDevice(
        const MiniaudioOptions& options = {});

}
