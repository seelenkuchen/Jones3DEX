// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/SdlGpuDevice.h>

#include "../../src/HandlePool.h"

#include <babylon/log/Log.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
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

    // The fixed limits this backend reports on every driver: 8192 texels is
    // within what every Direct3D 12, Metal and desktop Vulkan device gives,
    // and 128 MiB is the smallest storage buffer range Vulkan allows.
    constexpr std::uint32_t kMaxTextureDimension = 8192;
    constexpr std::uint32_t kMaxBufferSize = 128u * 1024u * 1024u;

    // SDL's format with the same bit layout as the one in Texture.h. SDL
    // names the 16-bit formats from the lowest bits up, as Texture.h does:
    // its B5G6R5 is Vulkan's R5G6B5_UNORM_PACK16 and DXGI's B5G6R5_UNORM,
    // blue in bits 0-4 in both. R10G10B10A2 is Vulkan's
    // A2B10G10R10_UNORM_PACK32, DXGI's R10G10B10A2_UNORM and Metal's
    // RGB10A2Unorm, red in bits 0-9 in all three.
    SDL_GPUTextureFormat toSdl(TextureFormat format)
    {
        switch ( format )
        {
            case TextureFormat::Rgba8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            case TextureFormat::Bgra8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            case TextureFormat::B5G6R5: return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
            case TextureFormat::B5G5R5A1: return SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM;
            case TextureFormat::B4G4R4A4: return SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM;
            case TextureFormat::D24S8: return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
            case TextureFormat::D32F: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            case TextureFormat::R10G10B10A2: return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
        }
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }

    SDL_GPUTextureUsageFlags toSdl(TextureUsage usage)
    {
        SDL_GPUTextureUsageFlags flags = 0;
        if ( hasUsage(usage, TextureUsage::Sampled) )
        {
            flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
        }
        if ( hasUsage(usage, TextureUsage::ColorTarget) )
        {
            flags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        }
        if ( hasUsage(usage, TextureUsage::DepthTarget) )
        {
            flags |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        }
        return flags;
    }

    SDL_GPUBufferUsageFlags toSdl(BufferUsage usage)
    {
        SDL_GPUBufferUsageFlags flags = 0;
        if ( hasUsage(usage, BufferUsage::Vertex) )
        {
            flags |= SDL_GPU_BUFFERUSAGE_VERTEX;
        }
        if ( hasUsage(usage, BufferUsage::Index) )
        {
            flags |= SDL_GPU_BUFFERUSAGE_INDEX;
        }
        if ( hasUsage(usage, BufferUsage::Storage) )
        {
            flags |= SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        }
        return flags;
    }

    SDL_GPUShaderFormat toSdl(ShaderFormat format)
    {
        switch ( format )
        {
            case ShaderFormat::SpirV: return SDL_GPU_SHADERFORMAT_SPIRV;
            case ShaderFormat::Dxil: return SDL_GPU_SHADERFORMAT_DXIL;
            case ShaderFormat::Msl: return SDL_GPU_SHADERFORMAT_MSL;
            default: return SDL_GPU_SHADERFORMAT_INVALID;
        }
    }

    SDL_GPUVertexElementFormat toSdl(VertexFormat format)
    {
        switch ( format )
        {
            case VertexFormat::Float1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
            case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            case VertexFormat::UByte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        }
        return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    }

    SDL_GPUPrimitiveType toSdl(PrimitiveType primitive)
    {
        switch ( primitive )
        {
            case PrimitiveType::TriangleList: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            case PrimitiveType::LineList: return SDL_GPU_PRIMITIVETYPE_LINELIST;
            case PrimitiveType::LineStrip: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
            case PrimitiveType::PointList: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
        }
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }

    SDL_GPUBlendFactor toSdl(BlendFactor factor)
    {
        switch ( factor )
        {
            case BlendFactor::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
            case BlendFactor::One: return SDL_GPU_BLENDFACTOR_ONE;
            case BlendFactor::SrcColor: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor: return SDL_GPU_BLENDFACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        }
        return SDL_GPU_BLENDFACTOR_ONE;
    }

    SDL_GPUBlendOp toSdl(BlendOp op)
    {
        switch ( op )
        {
            case BlendOp::Add: return SDL_GPU_BLENDOP_ADD;
            case BlendOp::Subtract: return SDL_GPU_BLENDOP_SUBTRACT;
            case BlendOp::ReverseSubtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
            case BlendOp::Min: return SDL_GPU_BLENDOP_MIN;
            case BlendOp::Max: return SDL_GPU_BLENDOP_MAX;
        }
        return SDL_GPU_BLENDOP_ADD;
    }

    SDL_GPUCompareOp toSdl(CompareOp op)
    {
        switch ( op )
        {
            case CompareOp::Never: return SDL_GPU_COMPAREOP_NEVER;
            case CompareOp::Less: return SDL_GPU_COMPAREOP_LESS;
            case CompareOp::Equal: return SDL_GPU_COMPAREOP_EQUAL;
            case CompareOp::LessOrEqual: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            case CompareOp::Greater: return SDL_GPU_COMPAREOP_GREATER;
            case CompareOp::NotEqual: return SDL_GPU_COMPAREOP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
            case CompareOp::Always: return SDL_GPU_COMPAREOP_ALWAYS;
        }
        return SDL_GPU_COMPAREOP_ALWAYS;
    }

    SDL_GPUCullMode toSdl(CullMode cull)
    {
        switch ( cull )
        {
            case CullMode::None: return SDL_GPU_CULLMODE_NONE;
            case CullMode::Front: return SDL_GPU_CULLMODE_FRONT;
            case CullMode::Back: return SDL_GPU_CULLMODE_BACK;
        }
        return SDL_GPU_CULLMODE_NONE;
    }

    SDL_GPUFrontFace toSdl(FrontFace face)
    {
        return face == FrontFace::Clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE
            : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    }

    SDL_GPUFilter toSdl(Filter filter)
    {
        return filter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    }

    SDL_GPUSamplerAddressMode toSdl(AddressMode mode)
    {
        switch ( mode )
        {
            case AddressMode::Repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            case AddressMode::ClampToEdge: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            case AddressMode::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        }
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }

    SDL_GPULoadOp toSdl(LoadOp op)
    {
        switch ( op )
        {
            case LoadOp::Load: return SDL_GPU_LOADOP_LOAD;
            case LoadOp::Clear: return SDL_GPU_LOADOP_CLEAR;
            case LoadOp::DontCare: return SDL_GPU_LOADOP_DONT_CARE;
        }
        return SDL_GPU_LOADOP_LOAD;
    }

    SDL_GPUStoreOp toSdl(StoreOp op)
    {
        return op == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    }

    // The swapchain formats this interface can name; nullopt for the rest
    // (HDR and sRGB swapchains, which this backend never asks for).
    std::optional<TextureFormat> fromSdlSwapchain(SDL_GPUTextureFormat format)
    {
        switch ( format )
        {
            case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return TextureFormat::Bgra8;
            case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return TextureFormat::Rgba8;
            default: return std::nullopt;
        }
    }

    ShaderFormat fromSdl(SDL_GPUShaderFormat formats)
    {
        ShaderFormat result = ShaderFormat::None;
        if ( (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 )
        {
            result = result | ShaderFormat::SpirV;
        }
        if ( (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0 )
        {
            result = result | ShaderFormat::Dxil;
        }
        if ( (formats & SDL_GPU_SHADERFORMAT_MSL) != 0 )
        {
            result = result | ShaderFormat::Msl;
        }
        return result;
    }

    void warnExhausted(const char* call)
    {
        log::warning("render: {}: the device has no free slot left", call);
    }

    struct SdlTexture
    {
        TextureDesc desc;
        SDL_GPUTexture* texture;
    };

    struct SdlBuffer
    {
        BufferDesc desc;
        SDL_GPUBuffer* buffer;
    };

    struct SdlSampler
    {
        SDL_GPUSampler* sampler;
    };

    struct SdlPipeline
    {
        PipelineDesc desc;
        SDL_GPUGraphicsPipeline* pipeline;
    };

    // Owns the device and everything made from it. SDL defers the release
    // of a resource until the command buffers that use it are done, so a
    // handle may be destroyed right after the submit that uses it.
    class SdlGpuDevice final : public Device
    {
    public:
        SdlGpuDevice(SDL_GPUDevice* device, SDL_Window* window, DeviceInfo info)
            : m_device(device), m_window(window), m_info(std::move(info))
        {
        }

        ~SdlGpuDevice() override
        {
            m_textures.forEach([this](SdlTexture& t) { SDL_ReleaseGPUTexture(m_device, t.texture); });
            m_buffers.forEach([this](SdlBuffer& b) { SDL_ReleaseGPUBuffer(m_device, b.buffer); });
            m_samplers.forEach([this](SdlSampler& s) { SDL_ReleaseGPUSampler(m_device, s.sampler); });
            m_pipelines.forEach([this](SdlPipeline& p)
            {
                SDL_ReleaseGPUGraphicsPipeline(m_device, p.pipeline);
            });
            if ( m_window != nullptr )
            {
                SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
            }
            SDL_DestroyGPUDevice(m_device);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }

        SdlGpuDevice(const SdlGpuDevice&) = delete;
        SdlGpuDevice& operator=(const SdlGpuDevice&) = delete;

        const DeviceInfo& info() const override
        {
            return m_info;
        }

        bool supportsFormat(TextureFormat format, TextureUsage usage) const override
        {
            if ( isDepthFormat(format) ? hasUsage(usage, TextureUsage::ColorTarget)
                : hasUsage(usage, TextureUsage::DepthTarget) )
            {
                return false;
            }
            // Vulkan has B4G4R4A4 only with blue in the top bits; SDL takes
            // VK_FORMAT_B4G4R4A4_UNORM_PACK16 and swizzles when sampling, so
            // uploads, copies and readback keep the bytes of Texture.h, but
            // clears and draws would write the components to the wrong bits.
            if ( format == TextureFormat::B4G4R4A4 && hasUsage(usage, TextureUsage::ColorTarget) )
            {
                return false;
            }
            return SDL_GPUTextureSupportsFormat(m_device, toSdl(format), SDL_GPU_TEXTURETYPE_2D,
                toSdl(usage));
        }

    private:
        const TextureDesc* findTexture(TextureHandle texture) const override
        {
            const SdlTexture* const found = m_textures.find(texture);
            return found != nullptr ? &found->desc : nullptr;
        }

        const BufferDesc* findBuffer(BufferHandle buffer) const override
        {
            const SdlBuffer* const found = m_buffers.find(buffer);
            return found != nullptr ? &found->desc : nullptr;
        }

        bool hasSampler(SamplerHandle sampler) const override
        {
            return m_samplers.find(sampler) != nullptr;
        }

        const PipelineDesc* findPipeline(PipelineHandle pipeline) const override
        {
            const SdlPipeline* const found = m_pipelines.find(pipeline);
            return found != nullptr ? &found->desc : nullptr;
        }

        TextureHandle doCreateTexture(const TextureDesc& desc) override
        {
            SDL_GPUTextureCreateInfo create{};
            create.type = SDL_GPU_TEXTURETYPE_2D;
            create.format = toSdl(desc.format);
            create.usage = toSdl(desc.usage);
            create.width = desc.width;
            create.height = desc.height;
            create.layer_count_or_depth = 1;
            create.num_levels = desc.mipLevels;
            create.sample_count = SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUTexture* const texture = SDL_CreateGPUTexture(m_device, &create);
            if ( texture == nullptr )
            {
                log::warning("render: createTexture: SDL_CreateGPUTexture failed: {}",
                    SDL_GetError());
                return {};
            }
            const std::optional<TextureHandle> handle = m_textures.insert(SdlTexture{ desc, texture });
            if ( !handle.has_value() )
            {
                SDL_ReleaseGPUTexture(m_device, texture);
                warnExhausted("createTexture");
                return {};
            }
            return *handle;
        }

        void doDestroyTexture(TextureHandle texture) override
        {
            SDL_ReleaseGPUTexture(m_device, m_textures.find(texture)->texture);
            m_textures.erase(texture);
        }

        bool doUploadTexture(TextureHandle texture, const TextureRegion& region,
            std::span<const std::byte> data) override
        {
            SDL_GPUTexture* const target = m_textures.find(texture)->texture;
            return upload("uploadTexture", data, [&](SDL_GPUCopyPass* pass,
                SDL_GPUTransferBuffer* transfer)
            {
                SDL_GPUTextureTransferInfo source{};
                source.transfer_buffer = transfer;
                source.pixels_per_row = region.width;
                source.rows_per_layer = region.height;
                SDL_GPUTextureRegion destination{};
                destination.texture = target;
                destination.mip_level = region.mip;
                destination.x = region.x;
                destination.y = region.y;
                destination.w = region.width;
                destination.h = region.height;
                destination.d = 1;
                SDL_UploadToGPUTexture(pass, &source, &destination, false);
            });
        }

        std::vector<std::byte> doReadTexture(TextureHandle texture, std::uint32_t mip) override
        {
            const SdlTexture& source = *m_textures.find(texture);
            const std::uint32_t width = mipDimension(source.desc.width, mip);
            const std::uint32_t height = mipDimension(source.desc.height, mip);
            const std::size_t size = std::size_t{ width } * height * bytesPerTexel(source.desc.format);
            return download("readTexture", size, [&](SDL_GPUCopyPass* pass,
                SDL_GPUTransferBuffer* transfer)
            {
                SDL_GPUTextureRegion region{};
                region.texture = source.texture;
                region.mip_level = mip;
                region.w = width;
                region.h = height;
                region.d = 1;
                SDL_GPUTextureTransferInfo destination{};
                destination.transfer_buffer = transfer;
                destination.pixels_per_row = width;
                destination.rows_per_layer = height;
                SDL_DownloadFromGPUTexture(pass, &region, &destination);
            });
        }

        BufferHandle doCreateBuffer(const BufferDesc& desc) override
        {
            SDL_GPUBufferCreateInfo create{};
            create.usage = toSdl(desc.usage);
            create.size = desc.size;
            SDL_GPUBuffer* const buffer = SDL_CreateGPUBuffer(m_device, &create);
            if ( buffer == nullptr )
            {
                log::warning("render: createBuffer: SDL_CreateGPUBuffer failed: {}",
                    SDL_GetError());
                return {};
            }
            const std::optional<BufferHandle> handle = m_buffers.insert(SdlBuffer{ desc, buffer });
            if ( !handle.has_value() )
            {
                SDL_ReleaseGPUBuffer(m_device, buffer);
                warnExhausted("createBuffer");
                return {};
            }
            return *handle;
        }

        void doDestroyBuffer(BufferHandle buffer) override
        {
            SDL_ReleaseGPUBuffer(m_device, m_buffers.find(buffer)->buffer);
            m_buffers.erase(buffer);
        }

        bool doUploadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::span<const std::byte> data) override
        {
            SDL_GPUBuffer* const target = m_buffers.find(buffer)->buffer;
            return upload("uploadBuffer", data, [&](SDL_GPUCopyPass* pass,
                SDL_GPUTransferBuffer* transfer)
            {
                const SDL_GPUTransferBufferLocation source{ transfer, 0 };
                const SDL_GPUBufferRegion destination{ target, offset,
                    static_cast<Uint32>(data.size()) };
                SDL_UploadToGPUBuffer(pass, &source, &destination, false);
            });
        }

        std::vector<std::byte> doReadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::uint32_t size) override
        {
            SDL_GPUBuffer* const source = m_buffers.find(buffer)->buffer;
            return download("readBuffer", size, [&](SDL_GPUCopyPass* pass,
                SDL_GPUTransferBuffer* transfer)
            {
                const SDL_GPUBufferRegion region{ source, offset, size };
                const SDL_GPUTransferBufferLocation destination{ transfer, 0 };
                SDL_DownloadFromGPUBuffer(pass, &region, &destination);
            });
        }

        SamplerHandle doCreateSampler(const SamplerDesc& desc) override
        {
            SDL_GPUSamplerCreateInfo create{};
            create.min_filter = toSdl(desc.minFilter);
            create.mag_filter = toSdl(desc.magFilter);
            create.mipmap_mode = desc.mipFilter == Filter::Linear
                ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            create.address_mode_u = toSdl(desc.addressU);
            create.address_mode_v = toSdl(desc.addressV);
            create.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            create.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            create.min_lod = 0.0f;
            create.max_lod = 1000.0f;   // every mip the texture has
            SDL_GPUSampler* const sampler = SDL_CreateGPUSampler(m_device, &create);
            if ( sampler == nullptr )
            {
                log::warning("render: createSampler: SDL_CreateGPUSampler failed: {}",
                    SDL_GetError());
                return {};
            }
            const std::optional<SamplerHandle> handle = m_samplers.insert(SdlSampler{ sampler });
            if ( !handle.has_value() )
            {
                SDL_ReleaseGPUSampler(m_device, sampler);
                warnExhausted("createSampler");
                return {};
            }
            return *handle;
        }

        void doDestroySampler(SamplerHandle sampler) override
        {
            SDL_ReleaseGPUSampler(m_device, m_samplers.find(sampler)->sampler);
            m_samplers.erase(sampler);
        }

        // Compiles one stage from the first blob in a format the device
        // takes; the shared checks made sure there is one. nullptr after one
        // warning when the driver refuses the code.
        SDL_GPUShader* createShader(const ShaderDesc& shader, SDL_GPUShaderStage stage)
        {
            const auto blob = std::find_if(shader.blobs.begin(), shader.blobs.end(),
                [this](const ShaderBlob& b) { return hasFormat(m_info.shaderFormats, b.format); });
            SDL_GPUShaderCreateInfo create{};
            create.code_size = blob->code.size();
            create.code = reinterpret_cast<const Uint8*>(blob->code.data());
            create.entrypoint = blob->entryPoint.c_str();
            create.format = toSdl(blob->format);
            create.stage = stage;
            create.num_samplers = shader.textureCount;
            create.num_uniform_buffers = shader.uniformCount;
            create.num_storage_buffers = shader.storageBufferCount;
            SDL_GPUShader* const result = SDL_CreateGPUShader(m_device, &create);
            if ( result == nullptr )
            {
                log::warning("render: createPipeline: the {} shader was refused: {}",
                    stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertex" : "fragment", SDL_GetError());
            }
            return result;
        }

        PipelineHandle doCreatePipeline(const PipelineDesc& desc) override
        {
            SDL_GPUShader* const vertex = createShader(desc.vertexShader, SDL_GPU_SHADERSTAGE_VERTEX);
            if ( vertex == nullptr )
            {
                return {};
            }
            SDL_GPUShader* const fragment =
                createShader(desc.fragmentShader, SDL_GPU_SHADERSTAGE_FRAGMENT);
            if ( fragment == nullptr )
            {
                SDL_ReleaseGPUShader(m_device, vertex);
                return {};
            }

            std::vector<SDL_GPUVertexBufferDescription> buffers;
            for ( std::uint32_t slot = 0; slot < desc.vertexLayout.bufferStrides.size(); ++slot )
            {
                SDL_GPUVertexBufferDescription buffer{};
                buffer.slot = slot;
                buffer.pitch = desc.vertexLayout.bufferStrides[slot];
                buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
                buffers.push_back(buffer);
            }
            std::vector<SDL_GPUVertexAttribute> attributes;
            for ( const VertexAttribute& attribute : desc.vertexLayout.attributes )
            {
                attributes.push_back(SDL_GPUVertexAttribute{ attribute.location,
                    attribute.bufferSlot, toSdl(attribute.format), attribute.offset });
            }
            std::vector<SDL_GPUColorTargetDescription> targets;
            for ( const ColorTargetDesc& target : desc.colorTargets )
            {
                SDL_GPUColorTargetDescription color{};
                color.format = toSdl(target.format);
                color.blend_state.enable_blend = target.blend.enabled;
                color.blend_state.src_color_blendfactor = toSdl(target.blend.srcColor);
                color.blend_state.dst_color_blendfactor = toSdl(target.blend.dstColor);
                color.blend_state.color_blend_op = toSdl(target.blend.colorOp);
                color.blend_state.src_alpha_blendfactor = toSdl(target.blend.srcAlpha);
                color.blend_state.dst_alpha_blendfactor = toSdl(target.blend.dstAlpha);
                color.blend_state.alpha_blend_op = toSdl(target.blend.alphaOp);
                targets.push_back(color);
            }

            SDL_GPUGraphicsPipelineCreateInfo create{};
            create.vertex_shader = vertex;
            create.fragment_shader = fragment;
            create.vertex_input_state.vertex_buffer_descriptions = buffers.data();
            create.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(buffers.size());
            create.vertex_input_state.vertex_attributes = attributes.data();
            create.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
            create.primitive_type = toSdl(desc.primitive);
            create.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            create.rasterizer_state.cull_mode = toSdl(desc.cull);
            create.rasterizer_state.front_face = toSdl(desc.frontFace);
            create.rasterizer_state.enable_depth_clip = true;
            create.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            // Stencil is not part of the interface: always pass, keep.
            const SDL_GPUStencilOpState keep{ SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                SDL_GPU_STENCILOP_KEEP, SDL_GPU_COMPAREOP_ALWAYS };
            create.depth_stencil_state.compare_op = toSdl(desc.depth.compare);
            create.depth_stencil_state.back_stencil_state = keep;
            create.depth_stencil_state.front_stencil_state = keep;
            create.depth_stencil_state.enable_depth_test = desc.depth.testEnabled;
            create.depth_stencil_state.enable_depth_write = desc.depth.writeEnabled;
            create.target_info.color_target_descriptions = targets.data();
            create.target_info.num_color_targets = static_cast<Uint32>(targets.size());
            create.target_info.has_depth_stencil_target = desc.depthFormat.has_value();
            create.target_info.depth_stencil_format = desc.depthFormat.has_value()
                ? toSdl(*desc.depthFormat) : SDL_GPU_TEXTUREFORMAT_INVALID;

            SDL_GPUGraphicsPipeline* const pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &create);
            // The pipeline keeps what it needs of the shaders.
            SDL_ReleaseGPUShader(m_device, vertex);
            SDL_ReleaseGPUShader(m_device, fragment);
            if ( pipeline == nullptr )
            {
                log::warning("render: createPipeline: SDL_CreateGPUGraphicsPipeline failed: {}",
                    SDL_GetError());
                return {};
            }
            const std::optional<PipelineHandle> handle =
                m_pipelines.insert(SdlPipeline{ desc, pipeline });
            if ( !handle.has_value() )
            {
                SDL_ReleaseGPUGraphicsPipeline(m_device, pipeline);
                warnExhausted("createPipeline");
                return {};
            }
            return *handle;
        }

        void doDestroyPipeline(PipelineHandle pipeline) override
        {
            SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelines.find(pipeline)->pipeline);
            m_pipelines.erase(pipeline);
        }

        std::uint64_t doBeginFrame() override
        {
            return ++m_frame;
        }

        std::uint64_t currentFrame() const override
        {
            return m_frame;
        }

        void doSubmit(std::vector<Command> commands, bool drawsToSwapchain) override;

        using CopyRecorder = std::function<void(SDL_GPUCopyPass*, SDL_GPUTransferBuffer*)>;

        // A transfer buffer of size bytes, or nullptr after one warning.
        SDL_GPUTransferBuffer* createTransferBuffer(const char* call, std::size_t size,
            SDL_GPUTransferBufferUsage usage)
        {
            if ( size > std::numeric_limits<Uint32>::max() )
            {
                log::warning("render: {}: {} bytes are more than one transfer can carry",
                    call, size);
                return nullptr;
            }
            SDL_GPUTransferBufferCreateInfo create{};
            create.usage = usage;
            create.size = static_cast<Uint32>(size);
            SDL_GPUTransferBuffer* const transfer = SDL_CreateGPUTransferBuffer(m_device, &create);
            if ( transfer == nullptr )
            {
                log::warning("render: {}: SDL_CreateGPUTransferBuffer failed: {}", call,
                    SDL_GetError());
            }
            return transfer;
        }

        // Copies data into a transfer buffer and submits one copy pass that
        // record fills from it. Ordered with submits by the GPU queue.
        bool upload(const char* call, std::span<const std::byte> data, const CopyRecorder& record)
        {
            SDL_GPUTransferBuffer* const transfer =
                createTransferBuffer(call, data.size(), SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            if ( transfer == nullptr )
            {
                return false;
            }
            void* const mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
            if ( mapped == nullptr )
            {
                log::warning("render: {}: SDL_MapGPUTransferBuffer failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return false;
            }
            std::memcpy(mapped, data.data(), data.size());
            SDL_UnmapGPUTransferBuffer(m_device, transfer);

            SDL_GPUCommandBuffer* const commands = SDL_AcquireGPUCommandBuffer(m_device);
            if ( commands == nullptr )
            {
                log::warning("render: {}: SDL_AcquireGPUCommandBuffer failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return false;
            }
            SDL_GPUCopyPass* const pass = SDL_BeginGPUCopyPass(commands);
            record(pass, transfer);
            SDL_EndGPUCopyPass(pass);
            const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
            if ( !submitted )
            {
                log::warning("render: {}: SDL_SubmitGPUCommandBuffer failed: {}", call,
                    SDL_GetError());
            }
            // Released once the GPU is done with it.
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
            return submitted;
        }

        // Submits one copy pass that record fills into a transfer buffer of
        // size bytes, waits for it and returns the bytes; empty after one
        // warning on failure.
        std::vector<std::byte> download(const char* call, std::size_t size,
            const CopyRecorder& record)
        {
            SDL_GPUTransferBuffer* const transfer =
                createTransferBuffer(call, size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
            if ( transfer == nullptr )
            {
                return {};
            }
            SDL_GPUCommandBuffer* const commands = SDL_AcquireGPUCommandBuffer(m_device);
            if ( commands == nullptr )
            {
                log::warning("render: {}: SDL_AcquireGPUCommandBuffer failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return {};
            }
            SDL_GPUCopyPass* const pass = SDL_BeginGPUCopyPass(commands);
            record(pass, transfer);
            SDL_EndGPUCopyPass(pass);
            SDL_GPUFence* const fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            if ( fence == nullptr )
            {
                log::warning("render: {}: submitting the download failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return {};
            }
            const bool waited = SDL_WaitForGPUFences(m_device, true, &fence, 1);
            SDL_ReleaseGPUFence(m_device, fence);
            if ( !waited )
            {
                log::warning("render: {}: waiting for the download failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return {};
            }
            const void* const mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
            if ( mapped == nullptr )
            {
                log::warning("render: {}: SDL_MapGPUTransferBuffer failed: {}", call,
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                return {};
            }
            const auto* const first = static_cast<const std::byte*>(mapped);
            std::vector<std::byte> bytes(first, first + size);
            SDL_UnmapGPUTransferBuffer(m_device, transfer);
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
            return bytes;
        }

        SDL_GPUDevice* m_device;
        SDL_Window* m_window;   // claimed for the swapchain, or nullptr
        DeviceInfo m_info;
        HandlePool<TextureTag, SdlTexture> m_textures;
        HandlePool<BufferTag, SdlBuffer> m_buffers;
        HandlePool<SamplerTag, SdlSampler> m_samplers;
        HandlePool<PipelineTag, SdlPipeline> m_pipelines;
        std::uint64_t m_frame = 0;
    };

    // Translates one checked frame into one SDL command buffer. Copies and
    // buffer updates between passes share a copy pass, which ends before the
    // next render pass begins. A pass to the swapchain of a headless device,
    // or of a window that has no image (minimized), is skipped whole.
    void SdlGpuDevice::doSubmit(std::vector<Command> commands, bool drawsToSwapchain)
    {
        (void)drawsToSwapchain;   // presenting is part of submitting the acquired image
        SDL_GPUCommandBuffer* const buffer = SDL_AcquireGPUCommandBuffer(m_device);
        if ( buffer == nullptr )
        {
            log::warning("render: submit: SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
            return;
        }

        // Every updateBuffer's bytes go into one transfer buffer, at offsets
        // in list order, before anything is recorded.
        std::size_t staged = 0;
        std::vector<std::size_t> stagedOffsets;
        for ( const Command& command : commands )
        {
            if ( const auto* const update = std::get_if<UpdateBuffer>(&command) )
            {
                stagedOffsets.push_back(staged);
                staged += update->data.size();
            }
        }
        SDL_GPUTransferBuffer* transfer = nullptr;
        if ( staged > 0 )
        {
            transfer = createTransferBuffer("submit", staged, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            auto* const mapped = transfer != nullptr
                ? static_cast<std::byte*>(SDL_MapGPUTransferBuffer(m_device, transfer, false))
                : nullptr;
            if ( transfer != nullptr && mapped == nullptr )
            {
                log::warning("render: submit: SDL_MapGPUTransferBuffer failed: {}",
                    SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(m_device, transfer);
                transfer = nullptr;
            }
            if ( mapped != nullptr )
            {
                std::size_t next = 0;
                for ( const Command& command : commands )
                {
                    if ( const auto* const update = std::get_if<UpdateBuffer>(&command) )
                    {
                        std::memcpy(mapped + stagedOffsets[next++], update->data.data(),
                            update->data.size());
                    }
                }
                SDL_UnmapGPUTransferBuffer(m_device, transfer);
            }
            // Without a transfer buffer the frame runs without its updates;
            // the one warning above said why.
        }

        SDL_GPUCopyPass* copyPass = nullptr;
        SDL_GPURenderPass* renderPass = nullptr;
        bool skipping = false;   // inside a pass that draws nothing
        bool acquired = false;
        SDL_GPUTexture* swapchain = nullptr;
        std::size_t nextUpdate = 0;

        const auto openCopyPass = [&]
        {
            if ( copyPass == nullptr )
            {
                copyPass = SDL_BeginGPUCopyPass(buffer);
            }
            return copyPass;
        };
        const auto closeCopyPass = [&]
        {
            if ( copyPass != nullptr )
            {
                SDL_EndGPUCopyPass(copyPass);
                copyPass = nullptr;
            }
        };

        for ( const Command& command : commands )
        {
            if ( const auto* const begin = std::get_if<BeginPass>(&command) )
            {
                closeCopyPass();
                const bool toSwapchain = !begin->desc.colorTargets.empty()
                    && begin->desc.colorTargets[0].target.isNull();
                if ( toSwapchain && m_window != nullptr && !acquired )
                {
                    acquired = true;
                    if ( !SDL_WaitAndAcquireGPUSwapchainTexture(buffer, m_window, &swapchain,
                        nullptr, nullptr) )
                    {
                        log::warning("render: submit: acquiring the swapchain image failed: {}",
                            SDL_GetError());
                        swapchain = nullptr;
                    }
                }
                if ( toSwapchain && swapchain == nullptr )
                {
                    skipping = true;
                    continue;
                }
                std::vector<SDL_GPUColorTargetInfo> colors;
                for ( const ColorAttachment& attachment : begin->desc.colorTargets )
                {
                    SDL_GPUColorTargetInfo color{};
                    color.texture = attachment.target.isNull() ? swapchain
                        : m_textures.find(attachment.target)->texture;
                    color.mip_level = attachment.mip;
                    color.clear_color = SDL_FColor{ attachment.clear.r, attachment.clear.g,
                        attachment.clear.b, attachment.clear.a };
                    color.load_op = toSdl(attachment.load);
                    color.store_op = toSdl(attachment.store);
                    colors.push_back(color);
                }
                SDL_GPUDepthStencilTargetInfo depth{};
                if ( begin->desc.depthTarget.has_value() )
                {
                    const DepthAttachment& attachment = *begin->desc.depthTarget;
                    const SdlTexture& texture = *m_textures.find(attachment.target);
                    depth.texture = texture.texture;
                    depth.clear_depth = attachment.clearDepth;
                    depth.load_op = toSdl(attachment.load);
                    depth.store_op = toSdl(attachment.store);
                    // D32F has no stencil, D24S8's follows the depth.
                    const bool stencil = texture.desc.format == TextureFormat::D24S8;
                    depth.stencil_load_op = stencil ? depth.load_op : SDL_GPU_LOADOP_DONT_CARE;
                    depth.stencil_store_op = stencil ? depth.store_op : SDL_GPU_STOREOP_DONT_CARE;
                    depth.clear_stencil = attachment.clearStencil;
                }
                renderPass = SDL_BeginGPURenderPass(buffer, colors.data(),
                    static_cast<Uint32>(colors.size()),
                    begin->desc.depthTarget.has_value() ? &depth : nullptr);
                if ( renderPass == nullptr )
                {
                    log::warning("render: submit: SDL_BeginGPURenderPass failed: {}",
                        SDL_GetError());
                    skipping = true;
                }
                continue;
            }
            if ( std::holds_alternative<EndPass>(command) )
            {
                if ( renderPass != nullptr )
                {
                    SDL_EndGPURenderPass(renderPass);
                    renderPass = nullptr;
                }
                skipping = false;
                continue;
            }
            if ( const auto* const copy = std::get_if<CopyTexture>(&command) )
            {
                const TextureRegion& region = copy->sourceRegion;
                const SDL_GPUTextureLocation source{ m_textures.find(copy->source)->texture,
                    region.mip, 0, region.x, region.y, 0 };
                const SDL_GPUTextureLocation destination{
                    m_textures.find(copy->destination)->texture, copy->destinationMip, 0,
                    copy->destinationX, copy->destinationY, 0 };
                SDL_CopyGPUTextureToTexture(openCopyPass(), &source, &destination, region.width,
                    region.height, 1, false);
                continue;
            }
            if ( const auto* const update = std::get_if<UpdateBuffer>(&command) )
            {
                const std::size_t at = stagedOffsets[nextUpdate++];
                if ( transfer == nullptr )
                {
                    continue;
                }
                const SDL_GPUTransferBufferLocation source{ transfer, static_cast<Uint32>(at) };
                const SDL_GPUBufferRegion destination{ m_buffers.find(update->buffer)->buffer,
                    update->offset, static_cast<Uint32>(update->data.size()) };
                SDL_UploadToGPUBuffer(openCopyPass(), &source, &destination, false);
                continue;
            }

            // Everything else is a render command inside a pass.
            if ( skipping || renderPass == nullptr )
            {
                continue;
            }
            if ( const auto* const set = std::get_if<SetPipeline>(&command) )
            {
                SDL_BindGPUGraphicsPipeline(renderPass, m_pipelines.find(set->pipeline)->pipeline);
            }
            else if ( const auto* const viewport = std::get_if<SetViewport>(&command) )
            {
                const Viewport& v = viewport->viewport;
                const SDL_GPUViewport sdlViewport{ v.x, v.y, v.width, v.height, v.minDepth,
                    v.maxDepth };
                SDL_SetGPUViewport(renderPass, &sdlViewport);
            }
            else if ( const auto* const scissor = std::get_if<SetScissor>(&command) )
            {
                // SDL_Rect is signed, and x + width must not overflow.
                constexpr std::uint32_t maxInt = std::numeric_limits<int>::max();
                const ScissorRect& r = scissor->rect;
                const std::uint32_t x = std::min(r.x, maxInt);
                const std::uint32_t y = std::min(r.y, maxInt);
                const SDL_Rect rect{ static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(std::min(r.width, maxInt - x)),
                    static_cast<int>(std::min(r.height, maxInt - y)) };
                SDL_SetGPUScissor(renderPass, &rect);
            }
            else if ( const auto* const bind = std::get_if<BindVertexBuffer>(&command) )
            {
                const SDL_GPUBufferBinding binding{ m_buffers.find(bind->buffer)->buffer,
                    bind->offset };
                SDL_BindGPUVertexBuffers(renderPass, bind->slot, &binding, 1);
            }
            else if ( const auto* const bindIndex = std::get_if<BindIndexBuffer>(&command) )
            {
                const SDL_GPUBufferBinding binding{ m_buffers.find(bindIndex->buffer)->buffer,
                    bindIndex->offset };
                SDL_BindGPUIndexBuffer(renderPass, &binding,
                    bindIndex->type == IndexType::Uint16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                        : SDL_GPU_INDEXELEMENTSIZE_32BIT);
            }
            else if ( const auto* const bindTexture = std::get_if<BindTexture>(&command) )
            {
                const SDL_GPUTextureSamplerBinding binding{
                    m_textures.find(bindTexture->texture)->texture,
                    m_samplers.find(bindTexture->sampler)->sampler };
                if ( bindTexture->stage == ShaderStage::Vertex )
                {
                    SDL_BindGPUVertexSamplers(renderPass, bindTexture->slot, &binding, 1);
                }
                else
                {
                    SDL_BindGPUFragmentSamplers(renderPass, bindTexture->slot, &binding, 1);
                }
            }
            else if ( const auto* const bindStorage = std::get_if<BindStorageBuffer>(&command) )
            {
                // SDL keeps bindings beyond what the pipeline's shaders
                // declare and passes on only the declared ones.
                SDL_GPUBuffer* const storage = m_buffers.find(bindStorage->buffer)->buffer;
                if ( bindStorage->stage == ShaderStage::Vertex )
                {
                    SDL_BindGPUVertexStorageBuffers(renderPass, bindStorage->slot, &storage, 1);
                }
                else
                {
                    SDL_BindGPUFragmentStorageBuffers(renderPass, bindStorage->slot, &storage, 1);
                }
            }
            else if ( const auto* const push = std::get_if<PushUniforms>(&command) )
            {
                const auto size = static_cast<Uint32>(push->data.size());
                if ( push->stage == ShaderStage::Vertex )
                {
                    SDL_PushGPUVertexUniformData(buffer, push->slot, push->data.data(), size);
                }
                else
                {
                    SDL_PushGPUFragmentUniformData(buffer, push->slot, push->data.data(), size);
                }
            }
            else if ( const auto* const draw = std::get_if<Draw>(&command) )
            {
                SDL_DrawGPUPrimitives(renderPass, draw->vertexCount, 1, draw->firstVertex, 0);
            }
            else if ( const auto* const drawIndexed = std::get_if<DrawIndexed>(&command) )
            {
                SDL_DrawGPUIndexedPrimitives(renderPass, drawIndexed->indexCount, 1,
                    drawIndexed->firstIndex, drawIndexed->vertexOffset, 0);
            }
        }
        closeCopyPass();

        // Submitting the buffer that acquired the swapchain image presents it.
        if ( !SDL_SubmitGPUCommandBuffer(buffer) )
        {
            log::warning("render: submit: SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
        }
        if ( transfer != nullptr )
        {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        }
    }

    std::string lowercase(const char* text)
    {
        std::string result = text != nullptr ? text : "unknown";
        for ( char& c : result )
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    }

}

    std::unique_ptr<Device> createSdlGpuDevice(platform::Window* window)
    {
        SDL_Window* sdlWindow = nullptr;
        if ( window != nullptr )
        {
            sdlWindow = platform::sdlWindowOf(*window);
            if ( sdlWindow == nullptr )
            {
                log::warning("render: createSdlGpuDevice: the window is not one of the SDL "
                    "platform's");
                return nullptr;
            }
        }
        // SDL GPU picks its driver through the video subsystem, even
        // without a window; the count keeps it up while the device lives.
        if ( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
        {
            log::warning("render: createSdlGpuDevice: initializing SDL video failed: {}",
                SDL_GetError());
            return nullptr;
        }
        SDL_GPUDevice* const device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
            false, nullptr);
        if ( device == nullptr )
        {
            log::warning("render: createSdlGpuDevice: no GPU device: {}", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return nullptr;
        }

        DeviceInfo info;
        info.backend = "sdl-gpu " + lowercase(SDL_GetGPUDeviceDriver(device));
        info.shaderFormats = fromSdl(SDL_GetGPUShaderFormats(device));
        info.hasSwapchain = sdlWindow != nullptr;
        info.swapchainFormat = TextureFormat::Bgra8;
        info.limits = DeviceLimits{ kMaxTextureDimension, kMaxBufferSize };
        if ( sdlWindow != nullptr )
        {
            if ( !SDL_ClaimWindowForGPUDevice(device, sdlWindow) )
            {
                log::warning("render: createSdlGpuDevice: claiming the window failed: {}",
                    SDL_GetError());
                SDL_DestroyGPUDevice(device);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return nullptr;
            }
            const SDL_GPUTextureFormat sdlFormat = SDL_GetGPUSwapchainTextureFormat(device, sdlWindow);
            const std::optional<TextureFormat> format = fromSdlSwapchain(sdlFormat);
            if ( !format.has_value() )
            {
                log::warning("render: createSdlGpuDevice: the swapchain has SDL format {}, "
                    "neither BGRA8 nor RGBA8", static_cast<int>(sdlFormat));
                SDL_ReleaseWindowFromGPUDevice(device, sdlWindow);
                SDL_DestroyGPUDevice(device);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return nullptr;
            }
            info.swapchainFormat = *format;
        }
        return std::make_unique<SdlGpuDevice>(device, sdlWindow, std::move(info));
    }

}
