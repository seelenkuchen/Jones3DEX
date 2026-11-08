// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/audio/MiniaudioDevice.h>

#include <babylon/log/Log.h>

#include "../../src/HandlePool.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::audio
{
namespace
{

    // The output format this backend requests from miniaudio: the mixer
    // produces float32 stereo at exactly this rate and lets the device
    // adapt to the hardware.
    constexpr std::uint32_t kOutputChannels = 2;
    constexpr std::uint32_t kOutputSampleRate = 48000;

    // Speed of sound in caller units per second used by the approximate
    // doppler model, with distances treated as meters through the global
    // distance factor (see MiniaudioDevice.h).
    constexpr float kSpeedOfSound = 343.0f;

    constexpr float kPi = 3.14159265358979f;

    struct MiniaSound
    {
        SoundInfo info;
        // Decoded to float once at create time: one sample per channel,
        // interleaved frame by frame.
        std::vector<float> pcm;
    };

    struct MiniaVoice
    {
        bool isStream = false;
        std::shared_ptr<MiniaSound> sound;
        StreamFormat stream;

        bool started = false;
        bool paused = false;
        bool loop = false;
        bool finished = false;
        bool spatial = false;

        // Source frames consumed since the current play() began, and the
        // read cursor: for a sound the absolute source frames since play(),
        // for a stream the frames since the front of the still queued
        // bytes. position reports the whole frames consumed; the fraction
        // carries into the next block, except when a sound ends or a stream
        // underruns, where it is dropped.
        std::uint64_t position = 0;
        double cur = 0.0;

        float gain = 1.0f;
        float pan = 0.0f;
        float pitch = 1.0f;

        Vector3 position3d;
        Vector3 velocity;
        float minDistance = 1.0f;
        float maxDistance = 10000.0f;
        float rolloff = 1.0f;

        std::uint64_t queuedFrames = 0;
        std::vector<std::byte> queuedBytes;
        std::size_t readOffset = 0;
    };

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
    // normalized, so with forward +z and up +y it is +x.
    Vector3 listenerRight(const Listener& listener)
    {
        return normalized(cross(normalized(listener.up), normalized(listener.forward)));
    }

    float sampleFromBytes(const std::byte* p, SampleFormat format)
    {
        if ( format == SampleFormat::Unsigned8 )
        {
            return (static_cast<float>(static_cast<unsigned char>(*p)) - 128.0f) / 128.0f;
        }
        const unsigned lo = static_cast<unsigned char>(p[0]);
        const unsigned hi = static_cast<unsigned char>(p[1]);
        int value = static_cast<int>(lo | (hi << 8U)); // little-endian Signed16
        if ( value >= 0x8000 )
        {
            value -= 0x10000;
        }
        return static_cast<float>(value) / 32768.0f;
    }

    std::string hexEncode(const std::uint8_t* data, std::size_t size)
    {
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string text;
        text.resize(size * 2);
        for ( std::size_t i = 0; i < size; ++i )
        {
            text[2 * i] = kDigits[data[i] >> 4];
            text[2 * i + 1] = kDigits[data[i] & 0x0FU];
        }
        return text;
    }

    bool hexDecode(std::string_view hex, std::uint8_t* data, std::size_t size)
    {
        if ( hex.size() != size * 2 )
        {
            return false;
        }
        for ( std::size_t i = 0; i < size; ++i )
        {
            int value = 0;
            for ( int k = 0; k < 2; ++k )
            {
                const char c = hex[2 * i + static_cast<std::size_t>(k)];
                int digit;
                if ( c >= '0' && c <= '9' )
                {
                    digit = c - '0';
                }
                else if ( c >= 'a' && c <= 'f' )
                {
                    digit = c - 'a' + 10;
                }
                else if ( c >= 'A' && c <= 'F' )
                {
                    digit = c - 'A' + 10;
                }
                else
                {
                    return false;
                }
                value = value * 16 + digit;
            }
            data[i] = static_cast<std::uint8_t>(value);
        }
        return true;
    }

    // One output frame of linearly interpolated sound samples. The caller
    // clamps the neighbor index so reads stay inside the data.
    void readLerp(const MiniaSound& sound, std::uint64_t frame, std::uint64_t next,
        float fraction, float& left, float& right)
    {
        const std::uint32_t channels = sound.info.channels;
        const float* p0 = sound.pcm.data() + frame * channels;
        const float* p1 = sound.pcm.data() + next * channels;
        left = p0[0] + fraction * (p1[0] - p0[0]);
        right = channels == 2 ? p0[1] + fraction * (p1[1] - p0[1]) : left;
    }

    void readLerpBytes(const std::byte* base, std::uint32_t frameBytes,
        std::uint32_t sampleBytes, std::uint32_t channels, SampleFormat format,
        std::uint64_t frame, std::uint64_t next, float fraction, float& left, float& right)
    {
        const std::byte* p0 = base + static_cast<std::size_t>(frame) * frameBytes;
        const std::byte* p1 = base + static_cast<std::size_t>(next) * frameBytes;
        const float first0 = sampleFromBytes(p0, format);
        const float first1 = sampleFromBytes(p1, format);
        left = first0 + fraction * (first1 - first0);
        if ( channels == 2 )
        {
            const float second0 = sampleFromBytes(p0 + sampleBytes, format);
            const float second1 = sampleFromBytes(p1 + sampleBytes, format);
            right = second0 + fraction * (second1 - second0);
        }
        else
        {
            right = left;
        }
    }

}

    struct MiniaudioDevice::Impl
    {
        explicit Impl(const MiniaudioOptions& options) :
            nullBackend(options.useNullBackend)
        {
            if ( nullBackend )
            {
                const ma_backend backends[] = { ma_backend_null };
                contextConfig = ma_context_config_init();
                contextOk = ma_context_init(backends, 1, &contextConfig, &context) == MA_SUCCESS;
            }
            else
            {
                contextOk = ma_context_init(nullptr, 0, nullptr, &context) == MA_SUCCESS;
            }
        }

        ~Impl()
        {
            if ( deviceUp )
            {
                ma_device_uninit(&device);
                deviceUp = false;
            }
            if ( contextOk )
            {
                ma_context_uninit(&context);
                contextOk = false;
            }
        }

        DeviceInfo info{ "miniaudio" };

        mutable std::mutex mutex;

        ma_context context{};
        ma_context_config contextConfig{};
        bool contextOk = false;
        bool nullBackend = false;

        ma_device device{};
        bool deviceUp = false; // only the caller's thread touches this
        bool open = false;     // the Device contract's state
        bool mixing = false;   // the data callback may consume frames
        bool closing = false;  // a teardown of our own; ignore notifications
        bool lossPending = false;

        std::uint32_t outputRate = kOutputSampleRate;

        std::deque<DeviceEvent> events;
        float masterGain = 1.0f;
        Listener listener;
        float distanceFactor = 1.0f;
        float dopplerFactor = 1.0f;
        HandlePool<SoundTag, std::shared_ptr<MiniaSound>> sounds;
        HandlePool<VoiceTag, MiniaVoice> voices;

        // miniaudio playback enumeration without logging, in contract order.
        // Returns false when the backend cannot enumerate.
        bool enumerate(std::vector<OutputDevice>& entries, std::vector<ma_device_id>& ids)
        {
            std::lock_guard lock{ mutex };
            ma_device_info* pDevices = nullptr;
            ma_uint32 count = 0;
            if ( ma_context_get_devices(&context, &pDevices, &count, nullptr, nullptr)
                != MA_SUCCESS )
            {
                return false;
            }
            entries.clear();
            ids.clear();
            for ( ma_uint32 i = 0; i < count; ++i )
            {
                const ma_device_info& deviceInfo = pDevices[i];
                std::string name = deviceInfo.name;
                if ( name.empty() )
                {
                    name = "Output device";
                }
                entries.push_back(OutputDevice{
                    hexEncode(reinterpret_cast<const std::uint8_t*>(&deviceInfo.id),
                        sizeof(deviceInfo.id)),
                    std::move(name) });
                ids.push_back(deviceInfo.id);
            }
            return true;
        }

        // The data callback's whole job: take the lock, mix, account for the
        // consumed frames, release. Never logs.
        void mix(float* output, std::uint32_t frames)
        {
            std::fill_n(output, static_cast<std::size_t>(frames) * kOutputChannels, 0.0f);
            std::lock_guard lock{ mutex };
            if ( !mixing )
            {
                return;
            }
            voices.forEach([this, output, frames](MiniaVoice& voice)
            {
                if ( voice.started && !voice.paused && !voice.finished )
                {
                    mixVoice(voice, output, frames);
                }
            });
        }

        static void onData(ma_device* pDevice, void* pOutput, const void* /*pInput*/,
            ma_uint32 frameCount)
        {
            static_cast<Impl*>(pDevice->pUserData)
                ->mix(static_cast<float*>(pOutput), frameCount);
        }

        // Runs on a miniaudio thread. Queue the loss for the caller's
        // pollEvent(); the device is never torn down from here.
        static void onNotification(const ma_device_notification* pNotify)
        {
            if ( pNotify->type != ma_device_notification_type_stopped
                && pNotify->type != ma_device_notification_type_rerouted )
            {
                return;
            }
            Impl& impl = *static_cast<Impl*>(pNotify->pDevice->pUserData);
            std::lock_guard lock{ impl.mutex };
            if ( !impl.open || impl.closing )
            {
                return;
            }
            impl.lossPending = true;
            impl.events.push_back(DeviceEvent::DeviceLost);
        }

        void mixVoice(MiniaVoice& voice, float* output, std::uint32_t frames)
        {
            float gain = voice.gain;
            float pan = voice.pan;
            if ( voice.spatial )
            {
                const Vector3 relative{ voice.position3d.x - listener.position.x,
                    voice.position3d.y - listener.position.y,
                    voice.position3d.z - listener.position.z };
                const float size = length(relative);
                const float distance = size * distanceFactor;
                float attenuation = 1.0f;
                if ( distance > voice.minDistance )
                {
                    const float range = voice.maxDistance - voice.minDistance;
                    const float amount = (distance - voice.minDistance) / range;
                    attenuation = std::isfinite(amount)
                        ? std::clamp(1.0f - voice.rolloff * amount, 0.0f, 1.0f)
                        : (voice.rolloff == 0.0f ? 1.0f : 0.0f);
                }
                gain *= attenuation;
                pan = size > 0.0f
                    ? std::clamp(dot(relative, listenerRight(listener)) / size, -1.0f, 1.0f)
                    : 0.0f;
            }
            gain *= masterGain;
            if ( !(gain > 0.0f) || !std::isfinite(gain) )
            {
                return;
            }

            // Equal-power pan for mono sources, balance law for stereo ones.
            const bool monoSource = voice.isStream ? voice.stream.channels == 1
                                                   : voice.sound->info.channels == 1;
            float weightLeft = gain;
            float weightRight = gain;
            if ( monoSource )
            {
                const float t = (pan + 1.0f) * 0.5f;
                weightLeft = gain * std::cos(t * 0.5f * kPi);
                weightRight = gain * std::sin(t * 0.5f * kPi);
            }
            else if ( pan > 0.0f )
            {
                weightLeft = pan >= 1.0f ? 0.0f : gain * std::cos(pan * 0.5f * kPi);
            }
            else if ( pan < 0.0f )
            {
                weightRight = pan <= -1.0f ? 0.0f : gain * std::cos(-pan * 0.5f * kPi);
            }

            const std::uint32_t sampleRate = voice.isStream ? voice.stream.sampleRate
                                                            : voice.sound->info.sampleRate;
            float pitchMul = voice.pitch;
            if ( voice.spatial && dopplerFactor > 0.0f )
            {
                pitchMul = dopplerPitch(voice, pitchMul);
            }
            if ( !std::isfinite(pitchMul) || pitchMul < kMinPitch || pitchMul > kMaxPitch )
            {
                return;
            }
            const double step = double(sampleRate) * double(pitchMul) / double(outputRate);
            if ( !(step > 0.0) || !std::isfinite(step) )
            {
                return;
            }
            if ( !std::isfinite(voice.cur) )
            {
                voice.cur = 0.0;
            }

            if ( voice.isStream )
            {
                mixStream(voice, output, frames, step, weightLeft, weightRight);
            }
            else
            {
                mixSound(voice, output, frames, step, weightLeft, weightRight);
            }
        }

        float dopplerPitch(const MiniaVoice& voice, float pitchMul) const
        {
            const Vector3 relative{ voice.position3d.x - listener.position.x,
                voice.position3d.y - listener.position.y,
                voice.position3d.z - listener.position.z };
            const float size = length(relative);
            if ( size <= 0.0f )
            {
                return pitchMul;
            }
            const Vector3 dir{ relative.x / size, relative.y / size, relative.z / size };
            const Vector3 lv{ listener.velocity.x * distanceFactor,
                listener.velocity.y * distanceFactor, listener.velocity.z * distanceFactor };
            const Vector3 sv{ voice.velocity.x * distanceFactor,
                voice.velocity.y * distanceFactor, voice.velocity.z * distanceFactor };
            if ( dot(lv, lv) <= 0.0f && dot(sv, sv) <= 0.0f )
            {
                return pitchMul;
            }
            // The listener moving toward the voice (num > c) and the voice
            // moving toward the listener (den < c) both raise the pitch:
            // f' = f * (c + vL) / (c - vS). dir points from the listener to
            // the voice, so the voice approaching is dot(sv, dir) < 0.
            const float num = kSpeedOfSound + dot(lv, dir);
            const float den = kSpeedOfSound + dot(sv, dir);
            if ( !std::isfinite(num) || !std::isfinite(den) || num <= 0.0f || den <= 0.0f )
            {
                return pitchMul;
            }
            const float shift = 1.0f + (num / den - 1.0f) * dopplerFactor;
            return std::clamp(pitchMul * shift, kMinPitch, kMaxPitch);
        }

        void mixSound(MiniaVoice& voice, float* output, std::uint32_t frames, double step,
            float weightLeft, float weightRight) const
        {
            const MiniaSound& sound = *voice.sound;
            const std::uint64_t count = sound.info.frameCount;
            std::uint32_t i = 0;
            double& cur = voice.cur; // absolute source frames since play()
            if ( voice.loop )
            {
                // cur keeps growing across blocks; position is the total
                // consumed frames and the sound index is cur modulo count.
                while ( i < frames )
                {
                    const std::uint64_t whole = static_cast<std::uint64_t>(cur);
                    const float fraction = float(cur - double(whole));
                    float left = 0.0f;
                    float right = 0.0f;
                    readLerp(sound, whole % count, (whole + 1) % count, fraction, left, right);
                    output[2 * i] += left * weightLeft;
                    output[2 * i + 1] += right * weightRight;
                    cur += step;
                    ++i;
                }
                voice.position = static_cast<std::uint64_t>(cur);
                return;
            }
            while ( i < frames && cur < double(count) )
            {
                const std::uint64_t whole = static_cast<std::uint64_t>(cur);
                const float fraction = float(cur - double(whole));
                float left = 0.0f;
                float right = 0.0f;
                readLerp(sound, whole, std::min(whole + 1, count - 1), fraction, left, right);
                output[2 * i] += left * weightLeft;
                output[2 * i + 1] += right * weightRight;
                cur += step;
                ++i;
            }
            std::uint64_t whole = static_cast<std::uint64_t>(cur);
            if ( whole > count )
            {
                whole = count;
            }
            voice.position = whole;
            if ( whole >= count )
            {
                voice.finished = true;
                cur = 0.0;
            }
        }

        void mixStream(MiniaVoice& voice, float* output, std::uint32_t frames, double step,
            float weightLeft, float weightRight) const
        {
            const std::uint32_t frameBytes = bytesPerFrame(voice.stream.format,
                voice.stream.channels);
            const std::uint32_t sampleBytes = bytesPerSample(voice.stream.format);
            const std::uint64_t avail = voice.queuedFrames;
            const std::byte* const base
                = avail > 0 ? voice.queuedBytes.data() + voice.readOffset : nullptr;
            std::uint32_t i = 0;
            double& cur = voice.cur; // frames since the front of the queue
            while ( i < frames && cur < double(avail) )
            {
                const std::uint64_t whole = static_cast<std::uint64_t>(cur);
                const float fraction = float(cur - double(whole));
                const std::uint64_t f1 = std::min(whole + 1, avail - 1);
                float left = 0.0f;
                float right = 0.0f;
                readLerpBytes(base, frameBytes, sampleBytes, voice.stream.channels,
                    voice.stream.format, whole, f1, fraction, left, right);
                output[2 * i] += left * weightLeft;
                output[2 * i + 1] += right * weightRight;
                cur += step;
                ++i;
            }
            const std::uint64_t consumed = std::min(static_cast<std::uint64_t>(cur), avail);
            voice.position += consumed;
            voice.queuedFrames -= consumed;
            voice.readOffset += static_cast<std::size_t>(consumed) * frameBytes;
            cur -= double(consumed);
            if ( cur >= double(voice.queuedFrames) )
            {
                // Underrun: the fraction cannot be played without data, so
                // drop it and start the next block cleanly.
                cur = 0.0;
            }
            if ( voice.readOffset >= voice.queuedBytes.size() )
            {
                voice.queuedBytes.clear();
                voice.readOffset = 0;
            }
            else if ( voice.readOffset >= 4096U )
            {
                voice.queuedBytes.erase(voice.queuedBytes.begin(),
                    voice.queuedBytes.begin() + static_cast<std::ptrdiff_t>(voice.readOffset));
                voice.readOffset = 0;
            }
        }
    };

    MiniaudioDevice::MiniaudioDevice(const MiniaudioOptions& options) :
        m_impl(std::make_unique<Impl>(options))
    {
    }

    MiniaudioDevice::~MiniaudioDevice() = default;

    const DeviceInfo& MiniaudioDevice::info() const
    {
        return m_impl->info;
    }

    std::vector<OutputDevice> MiniaudioDevice::listOutputDevices() const
    {
        if ( !m_impl->contextOk )
        {
            log::warning("audio: listOutputDevices: the miniaudio context is not available");
            return {};
        }
        std::vector<OutputDevice> entries;
        std::vector<ma_device_id> ids;
        if ( !m_impl->enumerate(entries, ids) )
        {
            log::warning("audio: listOutputDevices: the backend cannot enumerate outputs");
            return {};
        }
        return entries;
    }

    bool MiniaudioDevice::open(std::string_view deviceId)
    {
        close(); // an open output is closed first; a failed open leaves none

        if ( !m_impl->contextOk )
        {
            log::warning("audio: open: the miniaudio context is not available");
            return false;
        }

        ma_device_id target{};
        const ma_device_id* pTarget = nullptr;
        if ( !deviceId.empty() )
        {
            std::vector<OutputDevice> entries;
            std::vector<ma_device_id> ids;
            if ( !m_impl->enumerate(entries, ids) )
            {
                log::warning("audio: open: the backend cannot enumerate outputs");
                return false;
            }
            bool found = false;
            for ( std::size_t i = 0; i < entries.size(); ++i )
            {
                if ( entries[i].id == deviceId )
                {
                    target = ids[i];
                    found = true;
                    break;
                }
            }
            if ( !found
                && !hexDecode(deviceId, reinterpret_cast<std::uint8_t*>(&target), sizeof(target)) )
            {
                log::warning("audio: open: unknown output device \"{}\"", deviceId);
                return false;
            }
            pTarget = &target;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = kOutputChannels;
        config.sampleRate = kOutputSampleRate;
        config.playback.pDeviceID = pTarget;
        config.dataCallback = &Impl::onData;
        config.notificationCallback = &Impl::onNotification;
        config.pUserData = m_impl.get();

        if ( ma_device_init(&m_impl->context, &config, &m_impl->device) != MA_SUCCESS )
        {
            log::warning("audio: open: cannot open output device \"{}\"",
                deviceId.empty() ? "default" : deviceId);
            return false;
        }
        m_impl->outputRate = m_impl->device.sampleRate;
        {
            std::lock_guard lock{ m_impl->mutex };
            m_impl->mixing = true;
            m_impl->open = true;
        }
        if ( ma_device_start(&m_impl->device) != MA_SUCCESS )
        {
            {
                std::lock_guard lock{ m_impl->mutex };
                m_impl->mixing = false;
                m_impl->open = false;
            }
            ma_device_uninit(&m_impl->device);
            log::warning("audio: open: cannot start output device \"{}\"",
                deviceId.empty() ? "default" : deviceId);
            return false;
        }
        m_impl->deviceUp = true;
        return true;
    }

    void MiniaudioDevice::close()
    {
        {
            std::lock_guard lock{ m_impl->mutex };
            m_impl->open = false;
            m_impl->mixing = false;
            if ( !m_impl->deviceUp )
            {
                return;
            }
            m_impl->closing = true;
        }
        ma_device_uninit(&m_impl->device); // fires a stopped notification, guarded
        m_impl->deviceUp = false;
        std::lock_guard lock{ m_impl->mutex };
        m_impl->closing = false;
    }

    bool MiniaudioDevice::isOpen() const
    {
        return m_impl->open;
    }

    std::optional<DeviceEvent> MiniaudioDevice::pollEvent()
    {
        std::optional<DeviceEvent> event;
        bool uninit = false;
        {
            std::lock_guard lock{ m_impl->mutex };
            if ( m_impl->lossPending && m_impl->open && !m_impl->closing )
            {
                m_impl->open = false;
                m_impl->mixing = false;
                m_impl->lossPending = false;
                m_impl->closing = true;
                uninit = true;
            }
            if ( !m_impl->events.empty() )
            {
                event = m_impl->events.front();
                m_impl->events.pop_front();
            }
        }
        if ( uninit )
        {
            // The lost output is gone; miniaudio's uninit joins its threads.
            ma_device_uninit(&m_impl->device);
            m_impl->deviceUp = false;
            std::lock_guard lock{ m_impl->mutex };
            m_impl->closing = false;
        }
        return event;
    }

    float MiniaudioDevice::masterGain() const
    {
        std::lock_guard lock{ m_impl->mutex };
        return m_impl->masterGain;
    }

    const SoundInfo* MiniaudioDevice::findSound(SoundHandle sound) const
    {
        const std::shared_ptr<MiniaSound>* const found = m_impl->sounds.find(sound);
        return found != nullptr ? &(*found)->info : nullptr;
    }

    bool MiniaudioDevice::hasVoice(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        return m_impl->voices.find(voice) != nullptr;
    }

    std::optional<StreamFormat> MiniaudioDevice::findStreamFormat(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        const MiniaVoice* const found = m_impl->voices.find(voice);
        if ( found == nullptr || !found->isStream )
        {
            return std::nullopt;
        }
        return found->stream;
    }

    std::uint64_t MiniaudioDevice::queuedFrames(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        const MiniaVoice* const found = m_impl->voices.find(voice);
        return found != nullptr ? found->queuedFrames : 0;
    }

    void MiniaudioDevice::doSetMasterGain(float gain)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->masterGain = gain;
    }

    SoundHandle MiniaudioDevice::doCreateSound(const SoundDesc& desc)
    {
        const std::uint32_t frameBytes = bytesPerFrame(desc.format, desc.channels);
        const std::uint32_t sampleBytes = bytesPerSample(desc.format);
        auto sound = std::make_shared<MiniaSound>();
        sound->info = SoundInfo{ desc.format, desc.sampleRate, desc.channels,
            desc.data.size() / frameBytes };
        sound->pcm.resize(desc.data.size() / sampleBytes);
        for ( std::size_t i = 0; i < sound->pcm.size(); ++i )
        {
            sound->pcm[i] = sampleFromBytes(desc.data.data() + i * sampleBytes, desc.format);
        }
        std::lock_guard lock{ m_impl->mutex };
        const std::optional<SoundHandle> handle = m_impl->sounds.insert(std::move(sound));
        if ( !handle.has_value() )
        {
            log::warning("audio: createSound: the device has no free slot");
            return {};
        }
        return *handle;
    }

    void MiniaudioDevice::doDestroySound(SoundHandle sound)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->sounds.erase(sound);
    }

    std::optional<SoundInfo> MiniaudioDevice::doSoundInfo(SoundHandle sound) const
    {
        std::lock_guard lock{ m_impl->mutex };
        const std::shared_ptr<MiniaSound>* const found = m_impl->sounds.find(sound);
        if ( found == nullptr )
        {
            return std::nullopt;
        }
        return (*found)->info;
    }

    VoiceHandle MiniaudioDevice::doCreateVoice(SoundHandle sound)
    {
        std::lock_guard lock{ m_impl->mutex };
        const std::shared_ptr<MiniaSound>* const found = m_impl->sounds.find(sound);
        if ( found == nullptr )
        {
            log::warning("audio: createVoice: the sound is gone");
            return {};
        }
        MiniaVoice voice;
        voice.sound = *found;
        const std::optional<VoiceHandle> handle = m_impl->voices.insert(std::move(voice));
        if ( !handle.has_value() )
        {
            log::warning("audio: createVoice: the device has no free slot");
            return {};
        }
        return *handle;
    }

    VoiceHandle MiniaudioDevice::doCreateStreamVoice(const StreamFormat& format)
    {
        MiniaVoice voice;
        voice.isStream = true;
        voice.stream = format;
        std::lock_guard lock{ m_impl->mutex };
        const std::optional<VoiceHandle> handle = m_impl->voices.insert(std::move(voice));
        if ( !handle.has_value() )
        {
            log::warning("audio: createStreamVoice: the device has no free slot");
            return {};
        }
        return *handle;
    }

    void MiniaudioDevice::doDestroyVoice(VoiceHandle voice)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.erase(voice);
    }

    void MiniaudioDevice::doPlay(VoiceHandle voice, bool loop)
    {
        std::lock_guard lock{ m_impl->mutex };
        MiniaVoice& value = *m_impl->voices.find(voice);
        value.started = true;
        value.paused = false;
        value.loop = loop;
        value.finished = false;
        value.position = 0;
        value.cur = 0.0;
        if ( value.isStream )
        {
            value.queuedFrames = 0;
            value.queuedBytes.clear();
            value.readOffset = 0;
        }
    }

    void MiniaudioDevice::doStop(VoiceHandle voice)
    {
        std::lock_guard lock{ m_impl->mutex };
        MiniaVoice& value = *m_impl->voices.find(voice);
        value.started = false;
        value.paused = false;
        value.loop = false;
        value.finished = false;
        value.position = 0;
        value.cur = 0.0;
        if ( value.isStream )
        {
            value.queuedFrames = 0;
            value.queuedBytes.clear();
            value.readOffset = 0;
        }
    }

    void MiniaudioDevice::doSetPaused(VoiceHandle voice, bool paused)
    {
        std::lock_guard lock{ m_impl->mutex };
        MiniaVoice& value = *m_impl->voices.find(voice);
        if ( !value.started )
        {
            return;
        }
        value.paused = paused;
    }

    bool MiniaudioDevice::doIsPlaying(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        const MiniaVoice& value = *m_impl->voices.find(voice);
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

    std::uint64_t MiniaudioDevice::doVoicePosition(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        return m_impl->voices.find(voice)->position;
    }

    void MiniaudioDevice::doSetVoiceGain(VoiceHandle voice, float gain)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->gain = gain;
    }

    void MiniaudioDevice::doSetVoicePan(VoiceHandle voice, float pan)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->pan = pan;
    }

    void MiniaudioDevice::doSetVoicePitch(VoiceHandle voice, float ratio)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->pitch = ratio;
    }

    void MiniaudioDevice::doEnableVoiceSpatial(VoiceHandle voice, bool enabled)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->spatial = enabled;
    }

    void MiniaudioDevice::doSetVoicePosition(VoiceHandle voice, const Vector3& position)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->position3d = position;
    }

    void MiniaudioDevice::doSetVoiceVelocity(VoiceHandle voice, const Vector3& velocity)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->velocity = velocity;
    }

    void MiniaudioDevice::doSetVoiceDistanceRange(VoiceHandle voice, float minDistance,
        float maxDistance)
    {
        std::lock_guard lock{ m_impl->mutex };
        MiniaVoice& value = *m_impl->voices.find(voice);
        value.minDistance = minDistance;
        value.maxDistance = maxDistance;
    }

    void MiniaudioDevice::doSetVoiceRolloff(VoiceHandle voice, float rolloff)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->voices.find(voice)->rolloff = rolloff;
    }

    void MiniaudioDevice::doSetListener(const Listener& listener)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->listener = listener;
    }

    void MiniaudioDevice::doSetDistanceFactor(float factor)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->distanceFactor = factor;
    }

    void MiniaudioDevice::doSetDopplerFactor(float factor)
    {
        std::lock_guard lock{ m_impl->mutex };
        m_impl->dopplerFactor = factor;
    }

    void MiniaudioDevice::doPushStream(VoiceHandle voice, std::span<const std::byte> data)
    {
        std::lock_guard lock{ m_impl->mutex };
        MiniaVoice& value = *m_impl->voices.find(voice);
        const std::uint32_t frameBytes = bytesPerFrame(value.stream.format, value.stream.channels);
        value.queuedBytes.insert(value.queuedBytes.end(), data.begin(), data.end());
        value.queuedFrames += data.size() / frameBytes;
    }

    StreamState MiniaudioDevice::doStreamState(VoiceHandle voice) const
    {
        std::lock_guard lock{ m_impl->mutex };
        const MiniaVoice& value = *m_impl->voices.find(voice);
        return StreamState{ value.queuedFrames, value.position };
    }

    std::unique_ptr<MiniaudioDevice> createMiniaudioDevice(const MiniaudioOptions& options)
    {
        return std::make_unique<MiniaudioDevice>(options);
    }

}
