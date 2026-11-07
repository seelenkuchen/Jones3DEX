// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/audio/NullDevice.h>

#include <babylon/log/Log.h>

#include "HandlePool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace babylon::audio
{
namespace
{

    struct NullSound
    {
        SoundInfo info;
        std::vector<std::byte> pcm;
    };

    struct NullVoice
    {
        bool isStream = false;
        std::shared_ptr<const NullSound> sound;
        StreamFormat stream;

        bool started = false;
        bool paused = false;
        bool loop = false;
        bool finished = false;
        bool spatial = false;

        // Source frames consumed since the current play() began, and the
        // fractional source frames carried into the next advance. For a
        // stream, position is the playedFrames the interface reports.
        std::uint64_t position = 0;
        double frameCarry = 0.0;

        float gain = 1.0f;
        float pan = 0.0f;
        float pitch = 1.0f;

        Vector3 position3d;
        Vector3 velocity;
        float minDistance = 1.0f;
        float maxDistance = 10000.0f;
        float rolloff = 1.0f;

        // Stream state: frames pushed and not consumed, and the PCM pushed
        // for them, so the queue really owns a copy of the caller's bytes.
        std::uint64_t queuedFrames = 0;
        std::vector<std::byte> queuedBytes;
        std::size_t queueOffset = 0;
    };

    template <class Tag>
    std::string describe(Handle<Tag> handle)
    {
        if ( handle.isNull() )
        {
            return "the null handle";
        }
        return std::format("stale handle {}:{}", handle.index, handle.generation);
    }

    float dot(const Vector3& a, const Vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vector3 cross(const Vector3& a, const Vector3& b)
    {
        return Vector3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x };
    }

    float length(const Vector3& value)
    {
        return std::sqrt(dot(value, value));
    }

    Vector3 normalized(const Vector3& value)
    {
        const float size = length(value);
        if ( size <= 0.0f )
        {
            return Vector3{};
        }
        return Vector3{ value.x / size, value.y / size, value.z / size };
    }

    // The listener's right, as Spatial.h fixes it: cross(up, forward),
    // normalized. With the default forward +z and up +y this is +x.
    Vector3 listenerRight(const Listener& listener)
    {
        return normalized(cross(normalized(listener.up), normalized(listener.forward)));
    }

    std::uint32_t voiceSampleRate(const NullVoice& voice)
    {
        return voice.isStream ? voice.stream.sampleRate : voice.sound->info.sampleRate;
    }

    std::uint32_t voiceFrameBytes(const NullVoice& voice)
    {
        if ( voice.isStream )
        {
            return bytesPerFrame(voice.stream.format, voice.stream.channels);
        }
        return bytesPerFrame(voice.sound->info.format, voice.sound->info.channels);
    }

    // Drops frames bytes from the front of a stream's queued PCM, keeping the
    // vector from growing with bytes that were already played.
    void consumeQueuedBytes(NullVoice& voice, std::uint64_t frames)
    {
        const std::uint64_t bytes = frames * voiceFrameBytes(voice);
        voice.queueOffset += static_cast<std::size_t>(bytes);
        if ( voice.queueOffset >= voice.queuedBytes.size() )
        {
            voice.queuedBytes.clear();
            voice.queueOffset = 0;
        }
        else if ( voice.queueOffset >= 4096 )
        {
            voice.queuedBytes.erase(voice.queuedBytes.begin(),
                voice.queuedBytes.begin() + static_cast<std::ptrdiff_t>(voice.queueOffset));
            voice.queueOffset = 0;
        }
    }

}

    struct NullDevice::State
    {
        DeviceInfo info{ "null" };
        bool open = false;
        float masterGain = 1.0f;
        Listener listener;
        float distanceFactor = 1.0f;
        float dopplerFactor = 1.0f;
        std::deque<DeviceEvent> events;
        HandlePool<SoundTag, std::shared_ptr<NullSound>> sounds;
        HandlePool<VoiceTag, NullVoice> voices;
    };

    NullDevice::NullDevice() : m_state(std::make_unique<State>()) {}

    NullDevice::~NullDevice() = default;

    const DeviceInfo& NullDevice::info() const
    {
        return m_state->info;
    }

    std::vector<OutputDevice> NullDevice::listOutputDevices() const
    {
        return { OutputDevice{ "null", "Null output" } };
    }

    bool NullDevice::open(std::string_view deviceId)
    {
        if ( !deviceId.empty() && deviceId != "null" )
        {
            m_state->open = false;
            log::warning("audio: open: unknown output device \"{}\"", deviceId);
            return false;
        }
        m_state->open = true;
        return true;
    }

    void NullDevice::close()
    {
        m_state->open = false;
    }

    bool NullDevice::isOpen() const
    {
        return m_state->open;
    }

    std::optional<DeviceEvent> NullDevice::pollEvent()
    {
        if ( m_state->events.empty() )
        {
            return std::nullopt;
        }
        const DeviceEvent event = m_state->events.front();
        m_state->events.pop_front();
        return event;
    }

    float NullDevice::masterGain() const
    {
        return m_state->masterGain;
    }

    void NullDevice::advanceFrames(std::uint64_t frames)
    {
        if ( frames == 0 || !m_state->open )
        {
            return;
        }
        const double outputRate = double(kNullOutputSampleRate);
        m_state->voices.forEach([frames, outputRate](NullVoice& voice)
        {
            if ( !voice.started || voice.paused )
            {
                return;
            }
            if ( !voice.isStream && voice.finished )
            {
                return;
            }
            const double delta = double(frames) * double(voiceSampleRate(voice))
                * double(voice.pitch) / outputRate;
            if ( !(delta > 0.0) )
            {
                return;
            }
            const double total = voice.frameCarry + delta;
            const double floored = std::floor(total);
            const std::uint64_t wanted =
                floored >= double(std::numeric_limits<std::uint64_t>::max())
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(floored);
            voice.frameCarry = total - double(wanted);
            if ( voice.isStream )
            {
                const std::uint64_t consumed = std::min(wanted, voice.queuedFrames);
                voice.position += consumed;
                voice.queuedFrames -= consumed;
                consumeQueuedBytes(voice, consumed);
                if ( consumed < wanted )
                {
                    // The queue ran dry: output silence and start the next
                    // advance without a backlog of fractional frames.
                    voice.frameCarry = 0.0;
                }
            }
            else if ( voice.loop )
            {
                voice.position += wanted;
            }
            else
            {
                const std::uint64_t frameCount = voice.sound->info.frameCount;
                const std::uint64_t remaining = frameCount - voice.position;
                if ( wanted >= remaining )
                {
                    voice.position = frameCount;
                    voice.finished = true;
                    voice.frameCarry = 0.0;
                }
                else
                {
                    voice.position += wanted;
                }
            }
        });
    }

    void NullDevice::loseDevice()
    {
        if ( !m_state->open )
        {
            return;
        }
        m_state->open = false;
        m_state->events.push_back(DeviceEvent::DeviceLost);
    }

    std::optional<VoiceMix> NullDevice::voiceMix(VoiceHandle voice) const
    {
        const NullVoice* const found = m_state->voices.find(voice);
        if ( found == nullptr )
        {
            log::warning("audio: voiceMix: {}", describe(voice));
            return std::nullopt;
        }
        const NullVoice& value = *found;
        float gain = value.gain;
        float pan = value.pan;
        if ( value.spatial )
        {
            const Vector3 relative{ value.position3d.x - m_state->listener.position.x,
                value.position3d.y - m_state->listener.position.y,
                value.position3d.z - m_state->listener.position.z };
            const float distance = length(relative) * m_state->distanceFactor;
            float attenuation = 1.0f;
            if ( distance > value.minDistance )
            {
                const float range = value.maxDistance - value.minDistance;
                const float amount = (distance - value.minDistance) / range;
                attenuation = std::isfinite(amount)
                    ? std::clamp(1.0f - value.rolloff * amount, 0.0f, 1.0f)
                    : (value.rolloff == 0.0f ? 1.0f : 0.0f);
            }
            gain *= attenuation;
            const float size = length(relative);
            pan = size > 0.0f
                ? std::clamp(dot(relative, listenerRight(m_state->listener)) / size, -1.0f, 1.0f)
                : 0.0f;
        }
        gain *= m_state->masterGain;
        return VoiceMix{ gain, pan };
    }

    const SoundInfo* NullDevice::findSound(SoundHandle sound) const
    {
        const std::shared_ptr<NullSound>* const found = m_state->sounds.find(sound);
        return found != nullptr ? &(*found)->info : nullptr;
    }

    bool NullDevice::hasVoice(VoiceHandle voice) const
    {
        return m_state->voices.find(voice) != nullptr;
    }

    std::optional<StreamFormat> NullDevice::findStreamFormat(VoiceHandle voice) const
    {
        const NullVoice* const found = m_state->voices.find(voice);
        if ( found == nullptr || !found->isStream )
        {
            return std::nullopt;
        }
        return found->stream;
    }

    std::uint64_t NullDevice::queuedFrames(VoiceHandle voice) const
    {
        const NullVoice* const found = m_state->voices.find(voice);
        return found != nullptr ? found->queuedFrames : 0;
    }

    void NullDevice::doSetMasterGain(float gain)
    {
        m_state->masterGain = gain;
    }

    SoundHandle NullDevice::doCreateSound(const SoundDesc& desc)
    {
        auto sound = std::make_shared<NullSound>();
        sound->info = SoundInfo{ desc.format, desc.sampleRate, desc.channels,
            desc.data.size() / bytesPerFrame(desc.format, desc.channels) };
        sound->pcm.assign(desc.data.begin(), desc.data.end());
        const std::optional<SoundHandle> handle = m_state->sounds.insert(std::move(sound));
        if ( !handle.has_value() )
        {
            log::warning("audio: createSound: the device has no free slot");
            return {};
        }
        return *handle;
    }

    void NullDevice::doDestroySound(SoundHandle sound)
    {
        m_state->sounds.erase(sound);
    }

    std::optional<SoundInfo> NullDevice::doSoundInfo(SoundHandle sound) const
    {
        const std::shared_ptr<NullSound>* const found = m_state->sounds.find(sound);
        if ( found == nullptr )
        {
            return std::nullopt;
        }
        return (*found)->info;
    }

    VoiceHandle NullDevice::doCreateVoice(SoundHandle sound)
    {
        const std::shared_ptr<NullSound>* const found = m_state->sounds.find(sound);
        if ( found == nullptr )
        {
            log::warning("audio: createVoice: the sound is gone");
            return {};
        }
        NullVoice voice;
        voice.sound = *found;
        const std::optional<VoiceHandle> handle = m_state->voices.insert(std::move(voice));
        if ( !handle.has_value() )
        {
            log::warning("audio: createVoice: the device has no free slot");
            return {};
        }
        return *handle;
    }

    VoiceHandle NullDevice::doCreateStreamVoice(const StreamFormat& format)
    {
        NullVoice voice;
        voice.isStream = true;
        voice.stream = format;
        const std::optional<VoiceHandle> handle = m_state->voices.insert(std::move(voice));
        if ( !handle.has_value() )
        {
            log::warning("audio: createStreamVoice: the device has no free slot");
            return {};
        }
        return *handle;
    }

    void NullDevice::doDestroyVoice(VoiceHandle voice)
    {
        m_state->voices.erase(voice);
    }

    void NullDevice::doPlay(VoiceHandle voice, bool loop)
    {
        NullVoice& value = *m_state->voices.find(voice);
        value.started = true;
        value.paused = false;
        value.loop = loop;
        value.finished = false;
        value.position = 0;
        value.frameCarry = 0.0;
        if ( value.isStream )
        {
            value.queuedFrames = 0;
            value.queuedBytes.clear();
            value.queueOffset = 0;
        }
    }

    void NullDevice::doStop(VoiceHandle voice)
    {
        NullVoice& value = *m_state->voices.find(voice);
        value.started = false;
        value.paused = false;
        value.loop = false;
        value.finished = false;
        value.position = 0;
        value.frameCarry = 0.0;
        if ( value.isStream )
        {
            value.queuedFrames = 0;
            value.queuedBytes.clear();
            value.queueOffset = 0;
        }
    }

    void NullDevice::doSetPaused(VoiceHandle voice, bool paused)
    {
        NullVoice& value = *m_state->voices.find(voice);
        if ( !value.started )
        {
            return;
        }
        value.paused = paused;
    }

    bool NullDevice::doIsPlaying(VoiceHandle voice) const
    {
        const NullVoice& value = *m_state->voices.find(voice);
        if ( !value.started || value.paused )
        {
            return false;
        }
        if ( value.isStream || value.loop )
        {
            return true;
        }
        return !value.finished && value.position < value.sound->info.frameCount;
    }

    std::uint64_t NullDevice::doVoicePosition(VoiceHandle voice) const
    {
        return m_state->voices.find(voice)->position;
    }

    void NullDevice::doSetVoiceGain(VoiceHandle voice, float gain)
    {
        m_state->voices.find(voice)->gain = gain;
    }

    void NullDevice::doSetVoicePan(VoiceHandle voice, float pan)
    {
        m_state->voices.find(voice)->pan = pan;
    }

    void NullDevice::doSetVoicePitch(VoiceHandle voice, float ratio)
    {
        m_state->voices.find(voice)->pitch = ratio;
    }

    void NullDevice::doEnableVoiceSpatial(VoiceHandle voice, bool enabled)
    {
        m_state->voices.find(voice)->spatial = enabled;
    }

    void NullDevice::doSetVoicePosition(VoiceHandle voice, const Vector3& position)
    {
        m_state->voices.find(voice)->position3d = position;
    }

    void NullDevice::doSetVoiceVelocity(VoiceHandle voice, const Vector3& velocity)
    {
        m_state->voices.find(voice)->velocity = velocity;
    }

    void NullDevice::doSetVoiceDistanceRange(VoiceHandle voice, float minDistance,
        float maxDistance)
    {
        NullVoice& value = *m_state->voices.find(voice);
        value.minDistance = minDistance;
        value.maxDistance = maxDistance;
    }

    void NullDevice::doSetVoiceRolloff(VoiceHandle voice, float rolloff)
    {
        m_state->voices.find(voice)->rolloff = rolloff;
    }

    void NullDevice::doSetListener(const Listener& listener)
    {
        m_state->listener = listener;
    }

    void NullDevice::doSetDistanceFactor(float factor)
    {
        m_state->distanceFactor = factor;
    }

    void NullDevice::doSetDopplerFactor(float factor)
    {
        m_state->dopplerFactor = factor;
    }

    void NullDevice::doPushStream(VoiceHandle voice, std::span<const std::byte> data)
    {
        NullVoice& value = *m_state->voices.find(voice);
        value.queuedBytes.insert(value.queuedBytes.end(), data.begin(), data.end());
        value.queuedFrames += data.size() / voiceFrameBytes(value);
    }

    StreamState NullDevice::doStreamState(VoiceHandle voice) const
    {
        const NullVoice& value = *m_state->voices.find(voice);
        return StreamState{ value.queuedFrames, value.position };
    }

    std::unique_ptr<NullDevice> createNullDevice()
    {
        return std::make_unique<NullDevice>();
    }

}
