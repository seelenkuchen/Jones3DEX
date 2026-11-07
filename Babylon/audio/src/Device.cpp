// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/audio/Device.h>

#include <babylon/log/Log.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace babylon::audio
{
namespace
{

    template <class Tag>
    std::string describe(Handle<Tag> handle)
    {
        if ( handle.isNull() )
        {
            return "the null handle";
        }
        return std::format("stale handle {}:{}", handle.index, handle.generation);
    }

    bool isFinite(float value)
    {
        return std::isfinite(value);
    }

    bool isFinite(const Vector3& value)
    {
        return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
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

    // The reason a sound desc is refused, or nullopt when it is valid.
    std::optional<std::string> checkSoundDesc(const SoundDesc& desc)
    {
        if ( desc.format != SampleFormat::Unsigned8 && desc.format != SampleFormat::Signed16 )
        {
            return std::string("the sample format is not Unsigned8 or Signed16");
        }
        if ( desc.sampleRate == 0 || desc.sampleRate > kMaxSampleRate )
        {
            return std::format("sample rate {} must be within 1 and {}", desc.sampleRate,
                kMaxSampleRate);
        }
        if ( desc.channels != 1 && desc.channels != 2 )
        {
            return std::format("{} channels: only 1 or 2 are supported", desc.channels);
        }
        if ( desc.data.empty() )
        {
            return std::string("the PCM data is empty");
        }
        const std::uint32_t frameBytes = bytesPerFrame(desc.format, desc.channels);
        if ( desc.data.size() % frameBytes != 0 )
        {
            return std::format("{} bytes is not a whole number of {}-byte frames",
                desc.data.size(), frameBytes);
        }
        return std::nullopt;
    }

    // The reason a stream format is refused, or nullopt when it is valid.
    std::optional<std::string> checkStreamFormat(const StreamFormat& format)
    {
        if ( format.format != SampleFormat::Unsigned8
            && format.format != SampleFormat::Signed16 )
        {
            return std::string("the sample format is not Unsigned8 or Signed16");
        }
        if ( format.sampleRate == 0 || format.sampleRate > kMaxSampleRate )
        {
            return std::format("sample rate {} must be within 1 and {}", format.sampleRate,
                kMaxSampleRate);
        }
        if ( format.channels != 1 && format.channels != 2 )
        {
            return std::format("{} channels: only 1 or 2 are supported", format.channels);
        }
        return std::nullopt;
    }

}

    bool Device::setMasterGain(float gain)
    {
        if ( !isFinite(gain) || gain < 0.0f )
        {
            log::warning("audio: setMasterGain: gain {} must be finite and at least 0", gain);
            return false;
        }
        doSetMasterGain(gain);
        return true;
    }

    SoundHandle Device::createSound(const SoundDesc& desc)
    {
        if ( const auto reason = checkSoundDesc(desc) )
        {
            log::warning("audio: createSound: {}", *reason);
            return {};
        }
        return doCreateSound(desc);
    }

    void Device::destroySound(SoundHandle sound)
    {
        if ( sound.isNull() )
        {
            return;
        }
        if ( findSound(sound) == nullptr )
        {
            log::warning("audio: destroySound: {}", describe(sound));
            return;
        }
        doDestroySound(sound);
    }

    std::optional<SoundInfo> Device::soundInfo(SoundHandle sound) const
    {
        if ( findSound(sound) == nullptr )
        {
            log::warning("audio: soundInfo: {}", describe(sound));
            return std::nullopt;
        }
        return doSoundInfo(sound);
    }

    VoiceHandle Device::createVoice(SoundHandle sound)
    {
        if ( findSound(sound) == nullptr )
        {
            log::warning("audio: createVoice: the sound is {}", describe(sound));
            return {};
        }
        return doCreateVoice(sound);
    }

    VoiceHandle Device::createStreamVoice(const StreamFormat& format)
    {
        if ( const auto reason = checkStreamFormat(format) )
        {
            log::warning("audio: createStreamVoice: {}", *reason);
            return {};
        }
        return doCreateStreamVoice(format);
    }

    void Device::destroyVoice(VoiceHandle voice)
    {
        if ( voice.isNull() )
        {
            return;
        }
        if ( !hasVoice(voice) )
        {
            log::warning("audio: destroyVoice: {}", describe(voice));
            return;
        }
        doDestroyVoice(voice);
    }

    bool Device::play(VoiceHandle voice, bool loop)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: play: {}", describe(voice));
            return false;
        }
        if ( loop && findStreamFormat(voice).has_value() )
        {
            log::warning("audio: play: a stream voice can't loop");
            return false;
        }
        doPlay(voice, loop);
        return true;
    }

    void Device::stop(VoiceHandle voice)
    {
        if ( voice.isNull() )
        {
            return;
        }
        if ( !hasVoice(voice) )
        {
            log::warning("audio: stop: {}", describe(voice));
            return;
        }
        doStop(voice);
    }

    void Device::setPaused(VoiceHandle voice, bool paused)
    {
        if ( voice.isNull() )
        {
            return;
        }
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setPaused: {}", describe(voice));
            return;
        }
        doSetPaused(voice, paused);
    }

    bool Device::isPlaying(VoiceHandle voice) const
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: isPlaying: {}", describe(voice));
            return false;
        }
        return doIsPlaying(voice);
    }

    std::optional<std::uint64_t> Device::voicePosition(VoiceHandle voice) const
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: voicePosition: {}", describe(voice));
            return std::nullopt;
        }
        return doVoicePosition(voice);
    }

    bool Device::setVoiceGain(VoiceHandle voice, float gain)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoiceGain: {}", describe(voice));
            return false;
        }
        if ( !isFinite(gain) || gain < 0.0f )
        {
            log::warning("audio: setVoiceGain: gain {} must be finite and at least 0", gain);
            return false;
        }
        doSetVoiceGain(voice, gain);
        return true;
    }

    bool Device::setVoicePan(VoiceHandle voice, float pan)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoicePan: {}", describe(voice));
            return false;
        }
        if ( !isFinite(pan) || pan < -1.0f || pan > 1.0f )
        {
            log::warning("audio: setVoicePan: pan {} must be finite and within -1..1", pan);
            return false;
        }
        doSetVoicePan(voice, pan);
        return true;
    }

    bool Device::setVoicePitch(VoiceHandle voice, float ratio)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoicePitch: {}", describe(voice));
            return false;
        }
        if ( !isFinite(ratio) || ratio < kMinPitch || ratio > kMaxPitch )
        {
            log::warning("audio: setVoicePitch: ratio {} must be within {} and {}", ratio,
                kMinPitch, kMaxPitch);
            return false;
        }
        doSetVoicePitch(voice, ratio);
        return true;
    }

    void Device::enableVoiceSpatial(VoiceHandle voice, bool enabled)
    {
        if ( voice.isNull() )
        {
            return;
        }
        if ( !hasVoice(voice) )
        {
            log::warning("audio: enableVoiceSpatial: {}", describe(voice));
            return;
        }
        doEnableVoiceSpatial(voice, enabled);
    }

    bool Device::setVoicePosition(VoiceHandle voice, const Vector3& position)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoicePosition: {}", describe(voice));
            return false;
        }
        if ( !isFinite(position) )
        {
            log::warning("audio: setVoicePosition: every component must be finite");
            return false;
        }
        doSetVoicePosition(voice, position);
        return true;
    }

    bool Device::setVoiceVelocity(VoiceHandle voice, const Vector3& velocity)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoiceVelocity: {}", describe(voice));
            return false;
        }
        if ( !isFinite(velocity) )
        {
            log::warning("audio: setVoiceVelocity: every component must be finite");
            return false;
        }
        doSetVoiceVelocity(voice, velocity);
        return true;
    }

    bool Device::setVoiceDistanceRange(VoiceHandle voice, float minDistance, float maxDistance)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoiceDistanceRange: {}", describe(voice));
            return false;
        }
        if ( !isFinite(minDistance) || !isFinite(maxDistance) || minDistance < 0.0f
            || maxDistance <= minDistance )
        {
            log::warning("audio: setVoiceDistanceRange: {}..{} needs finite values with 0 <= "
                "min < max", minDistance, maxDistance);
            return false;
        }
        doSetVoiceDistanceRange(voice, minDistance, maxDistance);
        return true;
    }

    bool Device::setVoiceRolloff(VoiceHandle voice, float rolloff)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: setVoiceRolloff: {}", describe(voice));
            return false;
        }
        if ( !isFinite(rolloff) || rolloff < 0.0f )
        {
            log::warning("audio: setVoiceRolloff: rolloff {} must be finite and at least 0",
                rolloff);
            return false;
        }
        doSetVoiceRolloff(voice, rolloff);
        return true;
    }

    bool Device::setListener(const Listener& listener)
    {
        if ( !isFinite(listener.position) || !isFinite(listener.velocity)
            || !isFinite(listener.forward) || !isFinite(listener.up) )
        {
            log::warning("audio: setListener: every component must be finite");
            return false;
        }
        if ( dot(listener.forward, listener.forward) <= 0.0f
            || dot(listener.up, listener.up) <= 0.0f )
        {
            log::warning("audio: setListener: forward and up must be non-zero");
            return false;
        }
        const Vector3 right = cross(listener.up, listener.forward);
        if ( !isFinite(right) || dot(right, right) <= 0.0f )
        {
            log::warning("audio: setListener: forward and up must not be parallel");
            return false;
        }
        doSetListener(listener);
        return true;
    }

    bool Device::setDistanceFactor(float factor)
    {
        if ( !isFinite(factor) || factor < 0.0f )
        {
            log::warning("audio: setDistanceFactor: factor {} must be finite and at least 0",
                factor);
            return false;
        }
        doSetDistanceFactor(factor);
        return true;
    }

    bool Device::setDopplerFactor(float factor)
    {
        if ( !isFinite(factor) || factor < 0.0f )
        {
            log::warning("audio: setDopplerFactor: factor {} must be finite and at least 0",
                factor);
            return false;
        }
        doSetDopplerFactor(factor);
        return true;
    }

    bool Device::pushStream(VoiceHandle voice, std::span<const std::byte> data)
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: pushStream: {}", describe(voice));
            return false;
        }
        const std::optional<StreamFormat> format = findStreamFormat(voice);
        if ( !format.has_value() )
        {
            log::warning("audio: pushStream: the voice is not a stream voice");
            return false;
        }
        if ( data.empty() )
        {
            return true;
        }
        const std::uint32_t frameBytes = bytesPerFrame(format->format, format->channels);
        if ( data.size() % frameBytes != 0 )
        {
            log::warning("audio: pushStream: {} bytes is not a whole number of {}-byte frames",
                data.size(), frameBytes);
            return false;
        }
        const std::uint64_t frames = data.size() / frameBytes;
        const std::uint64_t queued = queuedFrames(voice);
        if ( frames > kMaxQueuedStreamFrames || queued > kMaxQueuedStreamFrames - frames )
        {
            log::warning("audio: pushStream: {} frames would leave more than {} queued", frames,
                kMaxQueuedStreamFrames);
            return false;
        }
        doPushStream(voice, data);
        return true;
    }

    std::optional<StreamState> Device::streamState(VoiceHandle voice) const
    {
        if ( !hasVoice(voice) )
        {
            log::warning("audio: streamState: {}", describe(voice));
            return std::nullopt;
        }
        if ( !findStreamFormat(voice).has_value() )
        {
            log::warning("audio: streamState: the voice is not a stream voice");
            return std::nullopt;
        }
        return doStreamState(voice);
    }

}
