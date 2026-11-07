// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/NullDevice.h>

#include <babylon/log/Log.h>

#include "HandlePool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace babylon::render
{
namespace
{

    struct NullTexture
    {
        TextureDesc desc;
        std::vector<std::vector<std::byte>> mips;   // tightly packed rows per level
    };

    struct NullBuffer
    {
        BufferDesc desc;
        std::vector<std::byte> bytes;
    };

    // A clear component in 0..1 as an integer of bits bits, rounded to
    // nearest like a GPU's float to unorm conversion.
    std::uint32_t toUnorm(float value, int bits)
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        const float maxValue = static_cast<float>((1u << bits) - 1u);
        return static_cast<std::uint32_t>(std::lround(clamped * maxValue));
    }

    // One texel of color in format, as readTexture() returns it. Only color
    // formats come here.
    std::array<std::byte, 4> encodeTexel(TextureFormat format, const ClearColor& color)
    {
        const auto byte = [](std::uint32_t v) { return static_cast<std::byte>(v & 0xFFu); };
        const auto word = [&byte](std::uint32_t v)
        {
            return std::array<std::byte, 4>{ byte(v), byte(v >> 8), std::byte{ 0 },
                std::byte{ 0 } };
        };
        const auto dword = [&byte](std::uint32_t v)
        {
            return std::array<std::byte, 4>{ byte(v), byte(v >> 8), byte(v >> 16),
                byte(v >> 24) };
        };
        switch ( format )
        {
            case TextureFormat::Rgba8:
                return { byte(toUnorm(color.r, 8)), byte(toUnorm(color.g, 8)),
                    byte(toUnorm(color.b, 8)), byte(toUnorm(color.a, 8)) };
            case TextureFormat::Bgra8:
                return { byte(toUnorm(color.b, 8)), byte(toUnorm(color.g, 8)),
                    byte(toUnorm(color.r, 8)), byte(toUnorm(color.a, 8)) };
            case TextureFormat::B5G6R5:
                return word(toUnorm(color.b, 5) | (toUnorm(color.g, 6) << 5)
                    | (toUnorm(color.r, 5) << 11));
            case TextureFormat::B5G5R5A1:
                return word(toUnorm(color.b, 5) | (toUnorm(color.g, 5) << 5)
                    | (toUnorm(color.r, 5) << 10) | (toUnorm(color.a, 1) << 15));
            case TextureFormat::B4G4R4A4:
                return word(toUnorm(color.b, 4) | (toUnorm(color.g, 4) << 4)
                    | (toUnorm(color.r, 4) << 8) | (toUnorm(color.a, 4) << 12));
            case TextureFormat::R10G10B10A2:
                return dword(toUnorm(color.r, 10) | (toUnorm(color.g, 10) << 10)
                    | (toUnorm(color.b, 10) << 20) | (toUnorm(color.a, 2) << 30));
            default:
                return {};
        }
    }

    std::size_t rowBytes(const TextureDesc& desc, std::uint32_t mip)
    {
        return std::size_t{ mipDimension(desc.width, mip) } * bytesPerTexel(desc.format);
    }

    void warnExhausted(const char* call)
    {
        log::warning("render: {}: the device has no free slot left", call);
    }

}

    struct NullDevice::State
    {
        DeviceInfo info;
        std::vector<TextureFormat> unsupportedFormats;
        HandlePool<TextureTag, NullTexture> textures;
        HandlePool<BufferTag, NullBuffer> buffers;
        HandlePool<SamplerTag, SamplerDesc> samplers;
        HandlePool<PipelineTag, PipelineDesc> pipelines;
        std::uint64_t frame = 0;
        bool record = true;
        std::vector<RecordedCommand> recorded;
    };

    NullDevice::NullDevice(const NullDeviceDesc& desc) : m_state(std::make_unique<State>())
    {
        m_state->info.backend = "null";
        m_state->info.shaderFormats = ShaderFormat::SpirV | ShaderFormat::Dxil | ShaderFormat::Msl;
        m_state->info.hasSwapchain = false;
        m_state->info.swapchainFormat = TextureFormat::Bgra8;
        m_state->info.limits = DeviceLimits{ 8192, 256u * 1024u * 1024u };
        m_state->unsupportedFormats = desc.unsupportedFormats;
        m_state->record = desc.record;
    }

    NullDevice::~NullDevice() = default;

    const DeviceInfo& NullDevice::info() const
    {
        return m_state->info;
    }

    bool NullDevice::supportsFormat(TextureFormat format, TextureUsage usage) const
    {
        const auto& unsupported = m_state->unsupportedFormats;
        if ( std::find(unsupported.begin(), unsupported.end(), format) != unsupported.end() )
        {
            return false;
        }
        if ( isDepthFormat(format) )
        {
            return !hasUsage(usage, TextureUsage::ColorTarget);
        }
        return !hasUsage(usage, TextureUsage::DepthTarget);
    }

    const std::vector<RecordedCommand>& NullDevice::recorded() const
    {
        return m_state->recorded;
    }

    void NullDevice::clearRecorded()
    {
        m_state->recorded.clear();
    }

    const TextureDesc* NullDevice::findTexture(TextureHandle texture) const
    {
        const NullTexture* const found = m_state->textures.find(texture);
        return found != nullptr ? &found->desc : nullptr;
    }

    const BufferDesc* NullDevice::findBuffer(BufferHandle buffer) const
    {
        const NullBuffer* const found = m_state->buffers.find(buffer);
        return found != nullptr ? &found->desc : nullptr;
    }

    bool NullDevice::hasSampler(SamplerHandle sampler) const
    {
        return m_state->samplers.find(sampler) != nullptr;
    }

    const PipelineDesc* NullDevice::findPipeline(PipelineHandle pipeline) const
    {
        return m_state->pipelines.find(pipeline);
    }

    TextureHandle NullDevice::doCreateTexture(const TextureDesc& desc)
    {
        NullTexture texture{ desc, {} };
        for ( std::uint32_t mip = 0; mip < desc.mipLevels; ++mip )
        {
            texture.mips.emplace_back(rowBytes(desc, mip) * mipDimension(desc.height, mip));
        }
        const std::optional<TextureHandle> handle = m_state->textures.insert(std::move(texture));
        if ( !handle.has_value() )
        {
            warnExhausted("createTexture");
            return {};
        }
        return *handle;
    }

    void NullDevice::doDestroyTexture(TextureHandle texture)
    {
        m_state->textures.erase(texture);
    }

    bool NullDevice::doUploadTexture(TextureHandle texture, const TextureRegion& region,
        std::span<const std::byte> data)
    {
        NullTexture& target = *m_state->textures.find(texture);
        const std::size_t stride = rowBytes(target.desc, region.mip);
        const std::size_t texel = bytesPerTexel(target.desc.format);
        const std::size_t length = std::size_t{ region.width } * texel;
        std::vector<std::byte>& bytes = target.mips[region.mip];
        for ( std::uint32_t row = 0; row < region.height; ++row )
        {
            std::memcpy(bytes.data() + (region.y + row) * stride + region.x * texel,
                data.data() + row * length, length);
        }
        return true;
    }

    std::vector<std::byte> NullDevice::doReadTexture(TextureHandle texture, std::uint32_t mip)
    {
        return m_state->textures.find(texture)->mips[mip];
    }

    BufferHandle NullDevice::doCreateBuffer(const BufferDesc& desc)
    {
        const std::optional<BufferHandle> handle =
            m_state->buffers.insert(NullBuffer{ desc, std::vector<std::byte>(desc.size) });
        if ( !handle.has_value() )
        {
            warnExhausted("createBuffer");
            return {};
        }
        return *handle;
    }

    void NullDevice::doDestroyBuffer(BufferHandle buffer)
    {
        m_state->buffers.erase(buffer);
    }

    bool NullDevice::doUploadBuffer(BufferHandle buffer, std::uint32_t offset,
        std::span<const std::byte> data)
    {
        NullBuffer& target = *m_state->buffers.find(buffer);
        std::memcpy(target.bytes.data() + offset, data.data(), data.size());
        return true;
    }

    std::vector<std::byte> NullDevice::doReadBuffer(BufferHandle buffer, std::uint32_t offset,
        std::uint32_t size)
    {
        const NullBuffer& source = *m_state->buffers.find(buffer);
        const auto first = source.bytes.begin() + offset;
        return std::vector<std::byte>(first, first + size);
    }

    SamplerHandle NullDevice::doCreateSampler(const SamplerDesc& desc)
    {
        const std::optional<SamplerHandle> handle = m_state->samplers.insert(desc);
        if ( !handle.has_value() )
        {
            warnExhausted("createSampler");
            return {};
        }
        return *handle;
    }

    void NullDevice::doDestroySampler(SamplerHandle sampler)
    {
        m_state->samplers.erase(sampler);
    }

    PipelineHandle NullDevice::doCreatePipeline(const PipelineDesc& desc)
    {
        const std::optional<PipelineHandle> handle = m_state->pipelines.insert(desc);
        if ( !handle.has_value() )
        {
            warnExhausted("createPipeline");
            return {};
        }
        if ( m_state->record )
        {
            m_state->recorded.emplace_back(PipelineCreated{ *handle, desc });
        }
        return *handle;
    }

    void NullDevice::doDestroyPipeline(PipelineHandle pipeline)
    {
        m_state->pipelines.erase(pipeline);
    }

    std::uint64_t NullDevice::doBeginFrame()
    {
        return ++m_state->frame;
    }

    std::uint64_t NullDevice::currentFrame() const
    {
        return m_state->frame;
    }

    void NullDevice::doSubmit(std::vector<Command> commands, bool drawsToSwapchain)
    {
        for ( Command& command : commands )
        {
            // Clears of texture targets, copies and buffer updates change the
            // contents in memory; the swapchain of a headless device has none,
            // and draws are not rasterized.
            if ( const auto* const begin = std::get_if<BeginPass>(&command) )
            {
                for ( const ColorAttachment& attachment : begin->desc.colorTargets )
                {
                    if ( attachment.target.isNull() || attachment.load != LoadOp::Clear )
                    {
                        continue;
                    }
                    NullTexture& target = *m_state->textures.find(attachment.target);
                    const std::array<std::byte, 4> texel =
                        encodeTexel(target.desc.format, attachment.clear);
                    const std::size_t size = bytesPerTexel(target.desc.format);
                    std::vector<std::byte>& bytes = target.mips[attachment.mip];
                    for ( std::size_t at = 0; at < bytes.size(); at += size )
                    {
                        std::memcpy(bytes.data() + at, texel.data(), size);
                    }
                }
            }
            else if ( const auto* const copy = std::get_if<CopyTexture>(&command) )
            {
                const NullTexture& source = *m_state->textures.find(copy->source);
                NullTexture& destination = *m_state->textures.find(copy->destination);
                const TextureRegion& region = copy->sourceRegion;
                const std::size_t texel = bytesPerTexel(source.desc.format);
                const std::size_t length = std::size_t{ region.width } * texel;
                const std::size_t sourceStride = rowBytes(source.desc, region.mip);
                const std::size_t destinationStride =
                    rowBytes(destination.desc, copy->destinationMip);
                const std::vector<std::byte>& from = source.mips[region.mip];
                std::vector<std::byte>& to = destination.mips[copy->destinationMip];
                for ( std::uint32_t row = 0; row < region.height; ++row )
                {
                    std::memcpy(
                        to.data() + (copy->destinationY + row) * destinationStride
                            + copy->destinationX * texel,
                        from.data() + (region.y + row) * sourceStride + region.x * texel,
                        length);
                }
            }
            else if ( const auto* const update = std::get_if<UpdateBuffer>(&command) )
            {
                NullBuffer& target = *m_state->buffers.find(update->buffer);
                std::memcpy(target.bytes.data() + update->offset, update->data.data(),
                    update->data.size());
            }
            if ( m_state->record )
            {
                std::visit([this](auto& recorded) { m_state->recorded.emplace_back(std::move(recorded)); },
                    command);
            }
        }
        if ( m_state->record )
        {
            m_state->recorded.emplace_back(FrameSubmitted{ m_state->frame,
                drawsToSwapchain && m_state->info.hasSwapchain });
        }
    }

    std::unique_ptr<NullDevice> createNullDevice(const NullDeviceDesc& desc)
    {
        return std::make_unique<NullDevice>(desc);
    }

}
