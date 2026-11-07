// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/audio/Handle.h>
#include <babylon/audio/Sound.h>
#include <babylon/audio/Spatial.h>
#include <babylon/audio/Stream.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace babylon::audio
{

    // The lowest and highest playback-rate ratio setVoicePitch() accepts.
    // The range is wide enough for an octave or four in either direction and
    // keeps resampling within what every backend can do.
    inline constexpr float kMinPitch = 0.0625f; // 1/16
    inline constexpr float kMaxPitch = 16.0f;

    // What a backend calls itself. backend is a short lowercase name such as
    // "null"; it is for logs and test output, never for branching.
    struct DeviceInfo
    {
        std::string backend;

        bool operator==(const DeviceInfo&) const = default;
    };

    // One output device the backend can open. id is a stable, opaque string
    // for this device on this machine, never empty; pass it to
    // Device::open(). name is a display name for the user, in the host's
    // language. The list may change between calls as devices are plugged in
    // or removed.
    struct OutputDevice
    {
        std::string id;
        std::string name;

        bool operator==(const OutputDevice&) const = default;
    };

    // What a device reports through Device::pollEvent(). New kinds are added
    // as further values, so callers should handle the ones they know and
    // ignore the rest.
    enum class DeviceEvent
    {
        // The open output device went away, for example because it was
        // unplugged. It is reported once per loss, and isOpen() is false
        // from then on until open() succeeds again. Sounds and voices keep
        // their handles and state as described at Device.
        DeviceLost,
    };

    // The sound output: output devices, immutable sounds, voices, 3D
    // processing and caller-fed streams. Backends are chosen at startup by
    // their factories; callers only see this interface and typed handles,
    // never a backend object.
    //
    // Contract shared by every backend, checked here once:
    // - Every failure logs exactly one babylon::log::warning line and
    //   changes nothing; nothing throws, success paths log nothing.
    // - A create that fails returns the null handle.
    // - A stale handle (its resource was destroyed) is detected: the call
    //   logs one warning and does nothing. Destroying the null handle is a
    //   silent no-op.
    // - Sounds, voices and their settings belong to the device, not to the
    //   open output. close() and open() keep every handle valid, and after
    //   a device loss the caller may open another output and continue with
    //   the same sounds and voices: the backend recreates what the output
    //   needs, and the caller recreates nothing. The master gain, listener
    //   and global distance and doppler factors are device state and are
    //   kept too. While no output is open, a playing voice stays logically
    //   playing but consumes no frames, so positions and stream counters
    //   stop until open() succeeds.
    // - Sounds may be created and voices started while no output is open;
    //   they simply make no sound until one is.
    //
    // Not thread-safe: call every method from one thread. No backend calls
    // back into the caller from another thread; device loss and other
    // events arrive through pollEvent(). A backend that owns a mixing
    // thread does so privately and never exposes it through this interface.
    //
    // Destroy every sound and voice or let the device go: destroying the
    // device frees all of them.
    class Device
    {
    public:
        virtual ~Device() = default;

        virtual const DeviceInfo& info() const = 0;

        // The output devices this backend can open now. Empty and one
        // warning when the backend cannot enumerate them. May be called
        // whether or not an output is open.
        virtual std::vector<OutputDevice> listOutputDevices() const = 0;

        // Opens an output. An empty deviceId opens the backend's default
        // device; otherwise the id must be one of listOutputDevices(). An
        // output that is already open is closed first, so this also
        // switches outputs. Returns false and logs one warning when the id
        // is unknown or the output cannot be opened, and leaves no output
        // open. Sounds, voices and the master gain are kept either way.
        [[nodiscard]] virtual bool open(std::string_view deviceId = {}) = 0;

        // Closes the open output. Sounds, voices and their state are kept,
        // and playing voices freeze where they are until open() succeeds
        // again. Closing when nothing is open is a safe no-op.
        virtual void close() = 0;

        // True while an output is open. False after close() and after a
        // loss was reported.
        virtual bool isOpen() const = 0;

        // Removes and returns the next event, or nullopt when none is
        // pending. Events are returned in the order they happened and stay
        // queued until polled, even if open() succeeds again first. Poll
        // once per frame; there are no callbacks.
        virtual std::optional<DeviceEvent> pollEvent() = 0;

        // Sets the master gain applied to every voice after its own gain.
        // Linear amplitude: 0 is silence, 1 is unity, and above 1 amplifies
        // and may clip. The value must be finite and at least 0; otherwise
        // this returns false, logs one warning and leaves the gain unchanged.
        // Starts at 1 and is kept across close() and open().
        [[nodiscard]] bool setMasterGain(float gain);

        // The master gain set last, or 1 before any call.
        virtual float masterGain() const = 0;

        // Creates an immutable sound by copying desc.data. The caller may
        // free or reuse the memory once this returns. Returns the null
        // handle and logs one warning when desc is invalid (see SoundDesc)
        // or the device has no free slot. The caller owns the sound until
        // destroySound() or the device is destroyed.
        [[nodiscard]] SoundHandle createSound(const SoundDesc& desc);

        // Destroys a sound. Its handle becomes stale at once, but a voice
        // already playing it keeps the PCM data alive until that voice
        // stops or is destroyed; see createVoice(). The null handle is a
        // silent no-op; a stale handle logs one warning.
        void destroySound(SoundHandle sound);

        // The format and length of a sound. nullopt and one warning for a
        // null or stale handle.
        [[nodiscard]] std::optional<SoundInfo> soundInfo(SoundHandle sound) const;

        // Creates a stopped, 2D voice for a sound. The voice keeps a
        // reference to the sound's PCM data, so destroying the sound does
        // not cut off a voice that is already using it. Initial state: gain
        // 1, pan 0, pitch 1, 3D off, position 0. Returns the null handle and
        // logs one warning when sound is null or stale or the device has no
        // free slot. The caller owns the voice until destroyVoice() or the
        // device is destroyed.
        [[nodiscard]] VoiceHandle createVoice(SoundHandle sound);

        // Creates a stopped, 2D stream voice with the given format and no
        // queued data. Initial state is otherwise that of createVoice().
        // The format is refused as StreamFormat describes; returns the null
        // handle and logs one warning then, or when the device has no free
        // slot.
        [[nodiscard]] VoiceHandle createStreamVoice(const StreamFormat& format);

        // Destroys a voice: it stops, its queued stream data is discarded,
        // and any sound data that only this voice still kept alive is freed.
        // The null handle is a silent no-op; a stale handle logs one
        // warning.
        void destroyVoice(VoiceHandle voice);

        // Starts a voice from its first frame and clears the paused state.
        // loop repeats a sound from its beginning at the end; a stream voice
        // refuses loop because its data arrives as it plays. Starting a
        // voice that is already playing restarts it. A stream's queued data
        // and counters are reset, so an empty queue outputs silence until
        // pushStream() adds data. Returns false and logs one warning for a
        // null or stale handle or for loop on a stream voice.
        [[nodiscard]] bool play(VoiceHandle voice, bool loop = false);

        // Stops a voice, resets its position to 0 and clears the paused
        // state. A stream's queued data is discarded and its counters are
        // reset. The voice keeps its gain, pan, pitch and 3D settings.
        // Stopping a stopped voice does nothing. The null handle does
        // nothing; a stale handle logs one warning.
        void stop(VoiceHandle voice);

        // Pauses or resumes a voice. A paused voice outputs silence and
        // consumes no frames; resuming continues where it stopped. Pausing
        // a stopped or already paused voice, and resuming a voice that is
        // not paused, do nothing. The null handle does nothing; a stale
        // handle logs one warning.
        void setPaused(VoiceHandle voice, bool paused);

        // True when a voice was started and has not stopped, been paused or
        // reached the end of a non-looping sound. A stream voice with an
        // empty queue is still playing: it outputs silence (an underrun)
        // until data arrives. False and one warning for a null or stale
        // handle.
        [[nodiscard]] bool isPlaying(VoiceHandle voice) const;

        // The number of source frames consumed since the current play()
        // began: 0 for a voice never played or stopped, the sound's
        // frameCount for a non-looping sound that reached its end, and a
        // growing count for a looping sound (take the remainder by
        // frameCount for the position inside the sound). For a stream it
        // equals StreamState::playedFrames. Pausing a voice or letting a
        // stream underrun does not advance it. This is presentation data
        // only: callers must not feed it into game logic or simulation
        // state; drive timing from the caller's clock instead. nullopt and
        // one warning for a null or stale handle.
        [[nodiscard]] std::optional<std::uint64_t> voicePosition(VoiceHandle voice) const;

        // Sets a voice's own gain, applied before the master gain. Linear
        // amplitude: 0 is silence, 1 is unity, above 1 amplifies and may
        // clip. The value must be finite and at least 0; otherwise this
        // returns false, logs one warning and leaves the gain unchanged.
        // A 3D voice's gain is combined with its distance attenuation.
        // Default: 1.
        [[nodiscard]] bool setVoiceGain(VoiceHandle voice, float gain);

        // Sets a 2D voice's pan: -1 is fully left, 0 is center, 1 is fully
        // right. The value must be finite and within -1..1; otherwise this
        // returns false, logs one warning and leaves the pan unchanged. A
        // 3D voice ignores pan, which comes from its position. Default: 0.
        [[nodiscard]] bool setVoicePan(VoiceHandle voice, float pan);

        // Sets a voice's playback-rate ratio: 1 plays at the sound's or
        // stream's recorded rate, 2 is an octave up and 0.5 an octave down.
        // The value must be finite and within kMinPitch..kMaxPitch;
        // otherwise this returns false, logs one warning and leaves the
        // ratio unchanged. Default: 1.
        [[nodiscard]] bool setVoicePitch(VoiceHandle voice, float ratio);

        // Turns 3D processing on or off for a voice. A 3D voice's gain and
        // pan come from its position, the listener and its distance
        // settings; its own gain still applies and its pan is ignored. A 2D
        // voice uses its pan and gain directly. The 3D settings are kept
        // while it is off, so turning it back on restores them. Default:
        // off. The null handle does nothing; a stale handle logs one
        // warning.
        void enableVoiceSpatial(VoiceHandle voice, bool enabled);

        // Sets a 3D voice's position or velocity, in caller units and caller
        // units per second. Every component must be finite; otherwise this
        // returns false, logs one warning and leaves the value unchanged.
        // Default: (0, 0, 0).
        [[nodiscard]] bool setVoicePosition(VoiceHandle voice, const Vector3& position);
        [[nodiscard]] bool setVoiceVelocity(VoiceHandle voice, const Vector3& velocity);

        // Sets the distance range of a 3D voice, in caller units: there is
        // no attenuation nearer than minDistance, and maxDistance is where
        // the backend's distance model reaches full attenuation. Both must
        // be finite, minDistance at least 0 and maxDistance greater than
        // minDistance; otherwise this returns false, logs one warning and
        // leaves the range unchanged. Defaults: 1 and 10000.
        [[nodiscard]] bool setVoiceDistanceRange(VoiceHandle voice, float minDistance,
            float maxDistance);

        // Sets how strongly a 3D voice fades with distance: 0 turns
        // distance attenuation off and 1 uses the backend's normal curve.
        // The value must be finite and at least 0; otherwise this returns
        // false, logs one warning and leaves the value unchanged. Default:
        // 1.
        [[nodiscard]] bool setVoiceRolloff(VoiceHandle voice, float rolloff);

        // Sets the listener used by every 3D voice. Applied at once; call it
        // once per frame after positions are updated. Refused (false, one
        // warning, unchanged) as Listener describes. Default: at the origin,
        // standing still, looking along +z with +y up.
        [[nodiscard]] bool setListener(const Listener& listener);

        // Scales the distances used by 3D attenuation and doppler for every
        // voice: 1 leaves caller units as they are, 2 treats every distance
        // as twice as far, and 0 collapses every distance to zero. The value
        // must be finite and at least 0; otherwise this returns false, logs
        // one warning and leaves the factor unchanged. Default: 1.
        [[nodiscard]] bool setDistanceFactor(float factor);

        // Scales the doppler shift of every 3D voice: 0 disables it, 1 is
        // the physical shift and above 1 exaggerates it. The value must be
        // finite and at least 0; otherwise this returns false, logs one
        // warning and leaves the factor unchanged. Default: 1.
        [[nodiscard]] bool setDopplerFactor(float factor);

        // Appends data to a stream voice's queue: interleaved frames in the
        // stream's format, as SoundDesc describes, not empty and a whole
        // number of frames. The bytes are copied. Returns false and logs one
        // warning for a null or stale handle, a voice that is not a stream,
        // a size that is not whole frames, or a push that would leave more
        // than kMaxQueuedStreamFrames queued. Empty data does nothing and
        // returns true. A playing stream that runs out of queued frames
        // outputs silence (an underrun) until more is pushed, and keeps
        // isPlaying() true.
        [[nodiscard]] bool pushStream(VoiceHandle voice, std::span<const std::byte> data);

        // The queued and played frames of a stream voice. nullopt and one
        // warning for a null or stale handle or for a voice that is not a
        // stream. Presentation data only.
        [[nodiscard]] std::optional<StreamState> streamState(VoiceHandle voice) const;

    private:
        // Lookups for the shared checks: nullptr or nullopt for null and
        // stale handles. These never log. findStreamFormat() is called only
        // for a live handle and returns nullopt for a sound voice.
        virtual const SoundInfo* findSound(SoundHandle sound) const = 0;
        virtual bool hasVoice(VoiceHandle voice) const = 0;
        virtual std::optional<StreamFormat> findStreamFormat(VoiceHandle voice) const = 0;
        virtual std::uint64_t queuedFrames(VoiceHandle voice) const = 0;

        // Called only with arguments that passed the shared checks and with
        // live handles. A create or open the backend refuses logs its own
        // one warning and returns the null handle, false or nothing.
        virtual void doSetMasterGain(float gain) = 0;
        virtual SoundHandle doCreateSound(const SoundDesc& desc) = 0;
        virtual void doDestroySound(SoundHandle sound) = 0;
        virtual std::optional<SoundInfo> doSoundInfo(SoundHandle sound) const = 0;
        virtual VoiceHandle doCreateVoice(SoundHandle sound) = 0;
        virtual VoiceHandle doCreateStreamVoice(const StreamFormat& format) = 0;
        virtual void doDestroyVoice(VoiceHandle voice) = 0;
        virtual void doPlay(VoiceHandle voice, bool loop) = 0;
        virtual void doStop(VoiceHandle voice) = 0;
        virtual void doSetPaused(VoiceHandle voice, bool paused) = 0;
        virtual bool doIsPlaying(VoiceHandle voice) const = 0;
        virtual std::uint64_t doVoicePosition(VoiceHandle voice) const = 0;
        virtual void doSetVoiceGain(VoiceHandle voice, float gain) = 0;
        virtual void doSetVoicePan(VoiceHandle voice, float pan) = 0;
        virtual void doSetVoicePitch(VoiceHandle voice, float ratio) = 0;
        virtual void doEnableVoiceSpatial(VoiceHandle voice, bool enabled) = 0;
        virtual void doSetVoicePosition(VoiceHandle voice, const Vector3& position) = 0;
        virtual void doSetVoiceVelocity(VoiceHandle voice, const Vector3& velocity) = 0;
        virtual void doSetVoiceDistanceRange(VoiceHandle voice, float minDistance,
            float maxDistance) = 0;
        virtual void doSetVoiceRolloff(VoiceHandle voice, float rolloff) = 0;
        virtual void doSetListener(const Listener& listener) = 0;
        virtual void doSetDistanceFactor(float factor) = 0;
        virtual void doSetDopplerFactor(float factor) = 0;
        virtual void doPushStream(VoiceHandle voice, std::span<const std::byte> data) = 0;
        virtual StreamState doStreamState(VoiceHandle voice) const = 0;
    };

}
