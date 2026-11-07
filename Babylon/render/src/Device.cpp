// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/Device.h>

#include <babylon/log/Log.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace babylon::render
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

    // The reason a texture desc is refused, or nullopt when it is valid.
    std::optional<std::string> checkTextureDesc(const Device& device, const TextureDesc& desc)
    {
        const std::uint32_t maxDimension = device.info().limits.maxTextureDimension;
        if ( desc.width == 0 || desc.height == 0 )
        {
            return std::format("size {}x{}: both dimensions must be at least 1",
                desc.width, desc.height);
        }
        if ( desc.width > maxDimension || desc.height > maxDimension )
        {
            return std::format("size {}x{}: both dimensions must be at most {}",
                desc.width, desc.height, maxDimension);
        }
        const std::uint32_t maxLevels = maxMipLevels(desc.width, desc.height);
        if ( desc.mipLevels == 0 || desc.mipLevels > maxLevels )
        {
            return std::format("{} mip levels: a {}x{} texture has 1 to {}",
                desc.mipLevels, desc.width, desc.height, maxLevels);
        }
        if ( desc.usage == TextureUsage::None )
        {
            return std::string("the usage names no flag");
        }
        if ( hasUsage(desc.usage, TextureUsage::ColorTarget)
            && hasUsage(desc.usage, TextureUsage::DepthTarget) )
        {
            return std::string("a texture can't be both a color and a depth target");
        }
        if ( !device.supportsFormat(desc.format, desc.usage) )
        {
            return std::format("format {} is not supported for usage {} on this device",
                static_cast<int>(desc.format), static_cast<int>(desc.usage));
        }
        return std::nullopt;
    }

    // The reason a region of a texture is refused, or nullopt.
    std::optional<std::string> checkRegion(const TextureDesc& desc, const TextureRegion& region)
    {
        if ( region.mip >= desc.mipLevels )
        {
            return std::format("mip {} out of range, the texture has {}", region.mip,
                desc.mipLevels);
        }
        const std::uint64_t mipWidth = mipDimension(desc.width, region.mip);
        const std::uint64_t mipHeight = mipDimension(desc.height, region.mip);
        if ( region.width == 0 || region.height == 0
            || std::uint64_t{ region.x } + region.width > mipWidth
            || std::uint64_t{ region.y } + region.height > mipHeight )
        {
            return std::format("region {}x{} at {},{} is empty or outside mip {} ({}x{})",
                region.width, region.height, region.x, region.y, region.mip, mipWidth, mipHeight);
        }
        return std::nullopt;
    }

    std::optional<std::string> checkShader(const Device& device, const ShaderDesc& shader,
        const char* stage)
    {
        bool accepted = false;
        for ( const ShaderBlob& blob : shader.blobs )
        {
            const auto format = static_cast<std::uint8_t>(blob.format);
            const bool oneFlag = format != 0 && (format & (format - 1)) == 0
                && format <= static_cast<std::uint8_t>(ShaderFormat::Msl);
            if ( !oneFlag || blob.code.empty() || blob.entryPoint.empty() )
            {
                return std::format("a {} shader blob needs exactly one format, code and an "
                    "entry point", stage);
            }
            accepted = accepted || hasFormat(device.info().shaderFormats, blob.format);
        }
        if ( !accepted )
        {
            return std::format("the {} shader has no blob in a format this device accepts",
                stage);
        }
        if ( shader.textureCount > kMaxTextureSlots || shader.uniformCount > kMaxUniformSlots )
        {
            return std::format("the {} shader declares {} textures and {} uniform blocks, at "
                "most {} and {}", stage, shader.textureCount, shader.uniformCount,
                kMaxTextureSlots, kMaxUniformSlots);
        }
        if ( shader.storageBufferCount > kMaxStorageBufferSlots )
        {
            return std::format("the {} shader declares {} storage buffers, at most {}", stage,
                shader.storageBufferCount, kMaxStorageBufferSlots);
        }
        return std::nullopt;
    }

    std::optional<std::string> checkVertexLayout(const VertexLayout& layout)
    {
        if ( layout.bufferStrides.size() > kMaxVertexBuffers )
        {
            return std::format("{} vertex buffer slots, at most {}", layout.bufferStrides.size(),
                kMaxVertexBuffers);
        }
        if ( layout.attributes.size() > kMaxVertexAttributes )
        {
            return std::format("{} vertex attributes, at most {}", layout.attributes.size(),
                kMaxVertexAttributes);
        }
        for ( std::size_t i = 0; i < layout.attributes.size(); ++i )
        {
            const VertexAttribute& attribute = layout.attributes[i];
            if ( attribute.location >= kMaxVertexAttributes )
            {
                return std::format("vertex attribute location {}, at most {}",
                    attribute.location, kMaxVertexAttributes - 1);
            }
            if ( attribute.bufferSlot >= layout.bufferStrides.size() )
            {
                return std::format("vertex attribute {} reads slot {}, which has no stride",
                    attribute.location, attribute.bufferSlot);
            }
            const std::uint64_t end = std::uint64_t{ attribute.offset }
                + vertexFormatSize(attribute.format);
            if ( end > layout.bufferStrides[attribute.bufferSlot] )
            {
                return std::format("vertex attribute {} ends at byte {}, past the stride {}",
                    attribute.location, end, layout.bufferStrides[attribute.bufferSlot]);
            }
            for ( std::size_t j = 0; j < i; ++j )
            {
                if ( layout.attributes[j].location == attribute.location )
                {
                    return std::format("vertex attribute location {} is used twice",
                        attribute.location);
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> checkPipelineDesc(const Device& device, const PipelineDesc& desc)
    {
        if ( auto reason = checkShader(device, desc.vertexShader, "vertex") )
        {
            return reason;
        }
        if ( auto reason = checkShader(device, desc.fragmentShader, "fragment") )
        {
            return reason;
        }
        if ( desc.colorTargets.size() > kMaxColorTargets )
        {
            return std::format("{} color targets, at most {}", desc.colorTargets.size(),
                kMaxColorTargets);
        }
        if ( desc.colorTargets.empty() && !desc.depthFormat.has_value() )
        {
            return std::string("no color target and no depth format");
        }
        for ( const ColorTargetDesc& target : desc.colorTargets )
        {
            if ( isDepthFormat(target.format)
                || !device.supportsFormat(target.format, TextureUsage::ColorTarget) )
            {
                return std::format("color target format {} can't be rendered to on this device",
                    static_cast<int>(target.format));
            }
        }
        if ( desc.depthFormat.has_value() && (!isDepthFormat(*desc.depthFormat)
            || !device.supportsFormat(*desc.depthFormat, TextureUsage::DepthTarget)) )
        {
            return std::format("depth format {} is no depth format this device supports",
                static_cast<int>(*desc.depthFormat));
        }
        if ( (desc.depth.testEnabled || desc.depth.writeEnabled) && !desc.depthFormat.has_value() )
        {
            return std::string("depth test or write without a depth format");
        }
        return checkVertexLayout(desc.vertexLayout);
    }

    std::uint32_t indexSize(IndexType type)
    {
        return type == IndexType::Uint16 ? 2 : 4;
    }

    // The state of the pass being checked in Device::submit().
    struct PassCheck
    {
        bool open = false;
        bool dropped = false;    // beginPass was dropped: drop all until endPass
        bool poisoned = false;   // a command was dropped: drop the pass's later draws
        std::vector<TextureFormat> colorFormats;
        std::optional<TextureFormat> depthFormat;
        const PipelineDesc* pipeline = nullptr;
        std::array<const BufferDesc*, kMaxVertexBuffers> vertexBuffers{};
        std::array<std::uint32_t, kMaxVertexBuffers> vertexOffsets{};
        const BufferDesc* indexBuffer = nullptr;
        std::uint32_t indexOffset = 0;
        IndexType indexType = IndexType::Uint16;
        // Per stage (ShaderStage as index), one bit per slot: the texture
        // slots bound, the uniform slots pushed and the storage buffer slots
        // bound since the pass began.
        std::array<std::uint32_t, 2> boundTextures{};
        std::array<std::uint32_t, 2> pushedUniforms{};
        std::array<std::uint32_t, 2> boundStorageBuffers{};
    };

    std::size_t stageIndex(ShaderStage stage)
    {
        return stage == ShaderStage::Vertex ? 0 : 1;
    }

    const char* stageName(ShaderStage stage)
    {
        return stage == ShaderStage::Vertex ? "vertex" : "fragment";
    }

    // The bits of slots 0 to count - 1; count is at most 32 (kMaxTextureSlots
    // and kMaxUniformSlots are far below).
    std::uint32_t slotMask(std::uint32_t count)
    {
        return count >= 32 ? ~std::uint32_t{ 0 } : (std::uint32_t{ 1 } << count) - 1;
    }

    // Why a draw with the pass's pipeline can't run: a texture slot or
    // uniform slot its shaders declare that nothing filled in this pass.
    std::optional<std::string> checkShaderResources(const PassCheck& pass)
    {
        for ( const ShaderStage stage : { ShaderStage::Vertex, ShaderStage::Fragment } )
        {
            const ShaderDesc& shader = stage == ShaderStage::Vertex
                ? pass.pipeline->vertexShader : pass.pipeline->fragmentShader;
            const std::size_t index = stageIndex(stage);
            const std::uint32_t missingTextures =
                slotMask(shader.textureCount) & ~pass.boundTextures[index];
            if ( missingTextures != 0 )
            {
                return std::format("the {} shader reads {} textures, slot {} is not bound",
                    stageName(stage), shader.textureCount, std::countr_zero(missingTextures));
            }
            const std::uint32_t missingUniforms =
                slotMask(shader.uniformCount) & ~pass.pushedUniforms[index];
            if ( missingUniforms != 0 )
            {
                return std::format("the {} shader reads {} uniform blocks, slot {} was not pushed",
                    stageName(stage), shader.uniformCount, std::countr_zero(missingUniforms));
            }
            const std::uint32_t missingStorageBuffers =
                slotMask(shader.storageBufferCount) & ~pass.boundStorageBuffers[index];
            if ( missingStorageBuffers != 0 )
            {
                return std::format("the {} shader reads {} storage buffers, slot {} is not bound",
                    stageName(stage), shader.storageBufferCount,
                    std::countr_zero(missingStorageBuffers));
            }
        }
        return std::nullopt;
    }

}

    TextureHandle Device::createTexture(const TextureDesc& desc)
    {
        if ( const auto reason = checkTextureDesc(*this, desc) )
        {
            log::warning("render: createTexture: {}", *reason);
            return {};
        }
        return doCreateTexture(desc);
    }

    void Device::destroyTexture(TextureHandle texture)
    {
        if ( texture.isNull() )
        {
            return;   // like closing something that isn't open
        }
        if ( findTexture(texture) == nullptr )
        {
            log::warning("render: destroyTexture: {}", describe(texture));
            return;
        }
        doDestroyTexture(texture);
    }

    bool Device::uploadTexture(TextureHandle texture, const TextureRegion& region,
        std::span<const std::byte> data)
    {
        const TextureDesc* const desc = findTexture(texture);
        if ( desc == nullptr )
        {
            log::warning("render: uploadTexture: {}", describe(texture));
            return false;
        }
        if ( isDepthFormat(desc->format) )
        {
            log::warning("render: uploadTexture: depth textures can't be uploaded");
            return false;
        }
        if ( const auto reason = checkRegion(*desc, region) )
        {
            log::warning("render: uploadTexture: {}", *reason);
            return false;
        }
        const std::uint64_t expected = std::uint64_t{ region.width } * region.height
            * bytesPerTexel(desc->format);
        if ( data.size() != expected )
        {
            log::warning("render: uploadTexture: {} bytes for a {}x{} region, expected {}",
                data.size(), region.width, region.height, expected);
            return false;
        }
        return doUploadTexture(texture, region, data);
    }

    bool Device::uploadTexture(TextureHandle texture, std::uint32_t mip,
        std::span<const std::byte> data)
    {
        const TextureDesc* const desc = findTexture(texture);
        if ( desc == nullptr )
        {
            log::warning("render: uploadTexture: {}", describe(texture));
            return false;
        }
        return uploadTexture(texture, wholeMip(*desc, mip), data);
    }

    std::vector<std::byte> Device::readTexture(TextureHandle texture, std::uint32_t mip)
    {
        const TextureDesc* const desc = findTexture(texture);
        if ( desc == nullptr )
        {
            log::warning("render: readTexture: {}", describe(texture));
            return {};
        }
        if ( isDepthFormat(desc->format) )
        {
            log::warning("render: readTexture: depth textures can't be read back");
            return {};
        }
        if ( mip >= desc->mipLevels )
        {
            log::warning("render: readTexture: mip {} out of range, the texture has {}",
                mip, desc->mipLevels);
            return {};
        }
        return doReadTexture(texture, mip);
    }

    BufferHandle Device::createBuffer(const BufferDesc& desc)
    {
        const std::uint32_t maxSize = info().limits.maxBufferSize;
        if ( desc.size == 0 || desc.size > maxSize )
        {
            log::warning("render: createBuffer: size {} must be within 1 and {}",
                desc.size, maxSize);
            return {};
        }
        if ( desc.usage == BufferUsage::None )
        {
            log::warning("render: createBuffer: the usage names no flag");
            return {};
        }
        return doCreateBuffer(desc);
    }

    void Device::destroyBuffer(BufferHandle buffer)
    {
        if ( buffer.isNull() )
        {
            return;
        }
        if ( findBuffer(buffer) == nullptr )
        {
            log::warning("render: destroyBuffer: {}", describe(buffer));
            return;
        }
        doDestroyBuffer(buffer);
    }

    bool Device::uploadBuffer(BufferHandle buffer, std::uint32_t offset,
        std::span<const std::byte> data)
    {
        const BufferDesc* const desc = findBuffer(buffer);
        if ( desc == nullptr )
        {
            log::warning("render: uploadBuffer: {}", describe(buffer));
            return false;
        }
        if ( data.empty() || std::uint64_t{ offset } + data.size() > desc->size )
        {
            log::warning("render: uploadBuffer: {} bytes at {} in a buffer of {}: empty or "
                "past the end", data.size(), offset, desc->size);
            return false;
        }
        return doUploadBuffer(buffer, offset, data);
    }

    std::vector<std::byte> Device::readBuffer(BufferHandle buffer, std::uint32_t offset,
        std::uint32_t size)
    {
        const BufferDesc* const desc = findBuffer(buffer);
        if ( desc == nullptr )
        {
            log::warning("render: readBuffer: {}", describe(buffer));
            return {};
        }
        if ( size == 0 || std::uint64_t{ offset } + size > desc->size )
        {
            log::warning("render: readBuffer: {} bytes at {} in a buffer of {}: empty or past "
                "the end", size, offset, desc->size);
            return {};
        }
        return doReadBuffer(buffer, offset, size);
    }

    SamplerHandle Device::createSampler(const SamplerDesc& desc)
    {
        return doCreateSampler(desc);
    }

    void Device::destroySampler(SamplerHandle sampler)
    {
        if ( sampler.isNull() )
        {
            return;
        }
        if ( !hasSampler(sampler) )
        {
            log::warning("render: destroySampler: {}", describe(sampler));
            return;
        }
        doDestroySampler(sampler);
    }

    PipelineHandle Device::createPipeline(const PipelineDesc& desc)
    {
        if ( const auto reason = checkPipelineDesc(*this, desc) )
        {
            log::warning("render: createPipeline: {}", *reason);
            return {};
        }
        return doCreatePipeline(desc);
    }

    void Device::destroyPipeline(PipelineHandle pipeline)
    {
        if ( pipeline.isNull() )
        {
            return;
        }
        if ( findPipeline(pipeline) == nullptr )
        {
            log::warning("render: destroyPipeline: {}", describe(pipeline));
            return;
        }
        doDestroyPipeline(pipeline);
    }

    CommandList Device::beginFrame()
    {
        return CommandList(this, doBeginFrame());
    }

    bool Device::submit(CommandList& list)
    {
        if ( list.m_device != this )
        {
            log::warning("render: submit: the command list belongs to another device");
            return false;
        }
        if ( list.m_submitted )
        {
            log::warning("render: submit: the command list of frame {} was already submitted",
                list.m_frame);
            return false;
        }
        if ( list.m_frame != currentFrame() )
        {
            log::warning("render: submit: the command list is of frame {}, the current frame "
                "is {}", list.m_frame, currentFrame());
            return false;
        }
        if ( list.m_inPass )
        {
            log::warning("render: submit: a pass is still open");
            return false;
        }

        std::vector<Command> accepted;
        accepted.reserve(list.m_commands.size());
        bool drawsToSwapchain = false;
        PassCheck pass;

        // Each visitor returns the reason to drop its command, or nullopt to
        // keep it.
        const auto checkBeginPass = [&](const BeginPass& command) -> std::optional<std::string>
        {
            pass = PassCheck{};
            pass.open = true;
            bool haveSize = false;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            const auto sameSize = [&](std::uint32_t w, std::uint32_t h)
            {
                if ( !haveSize )
                {
                    haveSize = true;
                    width = w;
                    height = h;
                    return true;
                }
                return w == width && h == height;
            };
            bool swapchain = false;
            for ( const ColorAttachment& attachment : command.desc.colorTargets )
            {
                if ( attachment.target.isNull() )
                {
                    swapchain = true;
                    pass.colorFormats.push_back(info().swapchainFormat);
                    continue;
                }
                const TextureDesc* const desc = findTexture(attachment.target);
                if ( desc == nullptr )
                {
                    return std::format("color attachment: {}", describe(attachment.target));
                }
                if ( !hasUsage(desc->usage, TextureUsage::ColorTarget) )
                {
                    return std::string("color attachment without ColorTarget usage");
                }
                if ( attachment.mip >= desc->mipLevels )
                {
                    return std::format("color attachment mip {} out of range", attachment.mip);
                }
                if ( !sameSize(mipDimension(desc->width, attachment.mip),
                    mipDimension(desc->height, attachment.mip)) )
                {
                    return std::string("attachments differ in size");
                }
                pass.colorFormats.push_back(desc->format);
            }
            if ( command.desc.depthTarget.has_value() )
            {
                const TextureDesc* const desc = findTexture(command.desc.depthTarget->target);
                if ( desc == nullptr )
                {
                    return std::format("depth attachment: {}",
                        describe(command.desc.depthTarget->target));
                }
                if ( !hasUsage(desc->usage, TextureUsage::DepthTarget) )
                {
                    return std::string("depth attachment without DepthTarget usage");
                }
                if ( !sameSize(desc->width, desc->height) )
                {
                    return std::string("attachments differ in size");
                }
                pass.depthFormat = desc->format;
            }
            drawsToSwapchain = drawsToSwapchain || swapchain;
            return std::nullopt;
        };

        const auto checkPipeline = [&](const SetPipeline& command) -> std::optional<std::string>
        {
            const PipelineDesc* const desc = findPipeline(command.pipeline);
            if ( desc == nullptr )
            {
                return describe(command.pipeline);
            }
            bool matches = desc->colorTargets.size() == pass.colorFormats.size()
                && desc->depthFormat == pass.depthFormat;
            for ( std::size_t i = 0; matches && i < desc->colorTargets.size(); ++i )
            {
                matches = desc->colorTargets[i].format == pass.colorFormats[i];
            }
            if ( !matches )
            {
                return std::string("the pipeline's target formats differ from the pass's");
            }
            pass.pipeline = desc;
            return std::nullopt;
        };

        const auto checkVertexBuffers = [&](std::optional<std::uint64_t> vertexEnd)
            -> std::optional<std::string>
        {
            const VertexLayout& layout = pass.pipeline->vertexLayout;
            for ( std::uint32_t slot = 0; slot < layout.bufferStrides.size(); ++slot )
            {
                const BufferDesc* const buffer = pass.vertexBuffers[slot];
                if ( buffer == nullptr )
                {
                    return std::format("vertex buffer slot {} is not bound", slot);
                }
                if ( !vertexEnd.has_value() )
                {
                    continue;
                }
                // The bytes the last vertex reads: its start plus the end of
                // the furthest attribute in this slot.
                std::uint64_t extent = 0;
                for ( const VertexAttribute& attribute : layout.attributes )
                {
                    if ( attribute.bufferSlot == slot )
                    {
                        const std::uint64_t end = std::uint64_t{ attribute.offset }
                            + vertexFormatSize(attribute.format);
                        extent = end > extent ? end : extent;
                    }
                }
                const std::uint64_t needed = pass.vertexOffsets[slot]
                    + (*vertexEnd - 1) * layout.bufferStrides[slot] + extent;
                if ( needed > buffer->size )
                {
                    return std::format("the draw reads {} bytes of vertex buffer slot {}, "
                        "which has {}", needed, slot, buffer->size);
                }
            }
            return std::nullopt;
        };

        const auto checkCopy = [&](const CopyTexture& command) -> std::optional<std::string>
        {
            const TextureDesc* const source = findTexture(command.source);
            if ( source == nullptr )
            {
                return std::format("source: {}", describe(command.source));
            }
            const TextureDesc* const destination = findTexture(command.destination);
            if ( destination == nullptr )
            {
                return std::format("destination: {}", describe(command.destination));
            }
            if ( isDepthFormat(source->format) || source->format != destination->format )
            {
                return std::string("both textures need the same color format");
            }
            if ( command.source == command.destination
                && command.sourceRegion.mip == command.destinationMip )
            {
                return std::string("source and destination are the same mip");
            }
            if ( const auto reason = checkRegion(*source, command.sourceRegion) )
            {
                return std::format("source {}", *reason);
            }
            const TextureRegion target{ command.destinationMip, command.destinationX,
                command.destinationY, command.sourceRegion.width, command.sourceRegion.height };
            if ( const auto reason = checkRegion(*destination, target) )
            {
                return std::format("destination {}", *reason);
            }
            return std::nullopt;
        };

        for ( Command& command : list.m_commands )
        {
            // Inside a dropped pass everything up to its endPass goes too.
            if ( pass.open && pass.dropped )
            {
                if ( std::holds_alternative<EndPass>(command) )
                {
                    pass = PassCheck{};
                }
                continue;
            }

            std::optional<std::string> reason;
            bool silentDrop = false;
            const char* name = "";
            if ( const auto* const begin = std::get_if<BeginPass>(&command) )
            {
                name = "beginPass";
                reason = checkBeginPass(*begin);
                pass.dropped = reason.has_value();
            }
            else if ( std::holds_alternative<EndPass>(command) )
            {
                pass = PassCheck{};
            }
            else if ( const auto* const set = std::get_if<SetPipeline>(&command) )
            {
                name = "setPipeline";
                pass.pipeline = nullptr;
                reason = checkPipeline(*set);
            }
            else if ( const auto* const bind = std::get_if<BindVertexBuffer>(&command) )
            {
                name = "bindVertexBuffer";
                const BufferDesc* const desc = findBuffer(bind->buffer);
                pass.vertexBuffers[bind->slot] = nullptr;
                if ( desc == nullptr )
                {
                    reason = describe(bind->buffer);
                }
                else if ( !hasUsage(desc->usage, BufferUsage::Vertex) )
                {
                    reason = "the buffer has no Vertex usage";
                }
                else if ( bind->offset >= desc->size )
                {
                    reason = std::format("offset {} past the end of {} bytes", bind->offset,
                        desc->size);
                }
                else
                {
                    pass.vertexBuffers[bind->slot] = desc;
                    pass.vertexOffsets[bind->slot] = bind->offset;
                }
            }
            else if ( const auto* const bindIndex = std::get_if<BindIndexBuffer>(&command) )
            {
                name = "bindIndexBuffer";
                const BufferDesc* const desc = findBuffer(bindIndex->buffer);
                pass.indexBuffer = nullptr;
                if ( desc == nullptr )
                {
                    reason = describe(bindIndex->buffer);
                }
                else if ( !hasUsage(desc->usage, BufferUsage::Index) )
                {
                    reason = "the buffer has no Index usage";
                }
                else if ( bindIndex->offset >= desc->size
                    || bindIndex->offset % indexSize(bindIndex->type) != 0 )
                {
                    reason = std::format("offset {} is past the end or not a multiple of the "
                        "index size", bindIndex->offset);
                }
                else
                {
                    pass.indexBuffer = desc;
                    pass.indexOffset = bindIndex->offset;
                    pass.indexType = bindIndex->type;
                }
            }
            else if ( const auto* const bindTexture = std::get_if<BindTexture>(&command) )
            {
                name = "bindTexture";
                const TextureDesc* const desc = findTexture(bindTexture->texture);
                if ( desc == nullptr )
                {
                    reason = describe(bindTexture->texture);
                }
                else if ( !hasUsage(desc->usage, TextureUsage::Sampled) )
                {
                    reason = "the texture has no Sampled usage";
                }
                else if ( !hasSampler(bindTexture->sampler) )
                {
                    reason = std::format("sampler: {}", describe(bindTexture->sampler));
                }
                else
                {
                    pass.boundTextures[stageIndex(bindTexture->stage)] |=
                        std::uint32_t{ 1 } << bindTexture->slot;
                }
            }
            else if ( const auto* const bindStorage = std::get_if<BindStorageBuffer>(&command) )
            {
                name = "bindStorageBuffer";
                const BufferDesc* const desc = findBuffer(bindStorage->buffer);
                if ( desc == nullptr )
                {
                    reason = describe(bindStorage->buffer);
                }
                else if ( !hasUsage(desc->usage, BufferUsage::Storage) )
                {
                    reason = "the buffer has no Storage usage";
                }
                else
                {
                    pass.boundStorageBuffers[stageIndex(bindStorage->stage)] |=
                        std::uint32_t{ 1 } << bindStorage->slot;
                }
            }
            else if ( const auto* const push = std::get_if<PushUniforms>(&command) )
            {
                pass.pushedUniforms[stageIndex(push->stage)] |= std::uint32_t{ 1 } << push->slot;
            }
            else if ( const auto* const drawCommand = std::get_if<Draw>(&command) )
            {
                name = "draw";
                silentDrop = pass.poisoned;
                if ( !silentDrop )
                {
                    reason = checkShaderResources(pass);
                }
                if ( !silentDrop && !reason.has_value() )
                {
                    reason = checkVertexBuffers(std::uint64_t{ drawCommand->firstVertex }
                        + drawCommand->vertexCount);
                }
            }
            else if ( const auto* const drawIndexed = std::get_if<DrawIndexed>(&command) )
            {
                name = "drawIndexed";
                silentDrop = pass.poisoned;
                if ( !silentDrop )
                {
                    reason = checkShaderResources(pass);
                }
                if ( !silentDrop && !reason.has_value() )
                {
                    reason = checkVertexBuffers(std::nullopt);
                }
                if ( !silentDrop && !reason.has_value() )
                {
                    const std::uint64_t needed = pass.indexOffset
                        + (std::uint64_t{ drawIndexed->firstIndex } + drawIndexed->indexCount)
                        * indexSize(pass.indexType);
                    if ( needed > pass.indexBuffer->size )
                    {
                        reason = std::format("the draw reads {} bytes of an index buffer of {}",
                            needed, pass.indexBuffer->size);
                    }
                }
            }
            else if ( const auto* const copy = std::get_if<CopyTexture>(&command) )
            {
                name = "copyTexture";
                reason = checkCopy(*copy);
            }
            else if ( const auto* const update = std::get_if<UpdateBuffer>(&command) )
            {
                name = "updateBuffer";
                const BufferDesc* const desc = findBuffer(update->buffer);
                if ( desc == nullptr )
                {
                    reason = describe(update->buffer);
                }
                else if ( std::uint64_t{ update->offset } + update->data.size() > desc->size )
                {
                    reason = std::format("{} bytes at {} in a buffer of {}: past the end",
                        update->data.size(), update->offset, desc->size);
                }
            }
            // SetViewport and SetScissor need no resources: the command list
            // checked them completely.

            if ( silentDrop )
            {
                continue;
            }
            if ( reason.has_value() )
            {
                log::warning("render: submit: dropped {}: {}", name, *reason);
                pass.poisoned = pass.open;
                continue;
            }
            accepted.push_back(std::move(command));
        }

        list.m_commands.clear();
        list.m_submitted = true;
        doSubmit(std::move(accepted), drawsToSwapchain);
        return true;
    }

}
