// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/DirectionalShadowMap.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_shadow.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace babylon::shadow
{
namespace
{

    constexpr std::uint32_t kMinMapSize = 256;
    constexpr std::uint32_t kMaxMapSize = 8192;

    // The vertex buffer starts with room for 1024 caster vertices and grows by
    // doubling.
    constexpr std::uint64_t kInitialVertexBytes = 1024ull * 3ull * sizeof(float);

    constexpr render::TextureUsage kDepthUsage =
        render::TextureUsage::DepthTarget | render::TextureUsage::Sampled;

}

    DirectionalShadowMap::DirectionalShadowMap(render::Device& device)
        : m_device(device)
    {
    }

    DirectionalShadowMap::~DirectionalShadowMap()
    {
        close();
    }

    bool DirectionalShadowMap::open(const ShadowMapDesc& desc)
    {
        if ( m_open )
        {
            close();
        }

        if ( desc.size < kMinMapSize || desc.size > kMaxMapSize )
        {
            log::warning("shadow: open: the size {} is outside {}..{}", desc.size, kMinMapSize,
                kMaxMapSize);
            return false;
        }

        const render::DeviceInfo& info = m_device.info();
        if ( desc.size > info.limits.maxTextureDimension )
        {
            log::warning("shadow: open: the size {} is above the device maximum {}", desc.size,
                info.limits.maxTextureDimension);
            return false;
        }

        render::TextureFormat depthFormat = render::TextureFormat::D32F;
        if ( !m_device.supportsFormat(depthFormat, kDepthUsage) )
        {
            depthFormat = render::TextureFormat::D24S8;
            if ( !m_device.supportsFormat(depthFormat, kDepthUsage) )
            {
                log::warning("shadow: open: the device supports no sampled depth format");
                return false;
            }
        }

        m_open = true;

        m_texture = m_device.createTexture(render::TextureDesc{ desc.size, desc.size, 1,
            depthFormat, kDepthUsage });
        if ( m_texture.isNull() )
        {
            close();
            return false;
        }

        render::PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader   = shaders::shadowCasterVert();
        pipelineDesc.fragmentShader = shaders::shadowCasterFrag();
        pipelineDesc.vertexLayout.bufferStrides = { 3 * sizeof(float) };
        pipelineDesc.vertexLayout.attributes = {
            render::VertexAttribute{ 0, 0, render::VertexFormat::Float3, 0 },
        };
        pipelineDesc.primitive   = render::PrimitiveType::TriangleList;
        pipelineDesc.depthFormat = depthFormat;
        pipelineDesc.depth.testEnabled        = true;
        pipelineDesc.depth.writeEnabled       = true;
        pipelineDesc.depth.compare            = render::CompareOp::LessOrEqual;
        pipelineDesc.depth.depthBiasConstant  = desc.depthBiasConstant;
        pipelineDesc.depth.depthBiasSlope     = desc.depthBiasSlope;
        pipelineDesc.cull                     = render::CullMode::None;
        m_pipeline = m_device.createPipeline(pipelineDesc);
        if ( m_pipeline.isNull() )
        {
            close();
            return false;
        }

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Linear;
        samplerDesc.magFilter = render::Filter::Linear;
        samplerDesc.mipFilter = render::Filter::Nearest;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;
        samplerDesc.compare   = render::CompareOp::LessOrEqual;
        m_sampler = m_device.createSampler(samplerDesc);
        if ( m_sampler.isNull() )
        {
            close();
            return false;
        }

        m_vertexCapacityBytes = std::min(kInitialVertexBytes,
            static_cast<std::uint64_t>(info.limits.maxBufferSize));
        if ( m_vertexCapacityBytes < 3 * sizeof(float) )
        {
            log::warning("shadow: open: the device's buffer limit is too small for a caster");
            close();
            return false;
        }
        m_vertexBuffer = m_device.createBuffer(render::BufferDesc{
            static_cast<std::uint32_t>(m_vertexCapacityBytes), render::BufferUsage::Vertex });
        if ( m_vertexBuffer.isNull() )
        {
            close();
            return false;
        }

        m_size = desc.size;
        m_valid = false;
        m_failureWarned = false;
        return true;
    }

    void DirectionalShadowMap::close()
    {
        if ( !m_open )
        {
            return;
        }

        if ( !m_vertexBuffer.isNull() )
        {
            m_device.destroyBuffer(m_vertexBuffer);
            m_vertexBuffer = {};
        }
        if ( !m_sampler.isNull() )
        {
            m_device.destroySampler(m_sampler);
            m_sampler = {};
        }
        if ( !m_pipeline.isNull() )
        {
            m_device.destroyPipeline(m_pipeline);
            m_pipeline = {};
        }
        if ( !m_texture.isNull() )
        {
            m_device.destroyTexture(m_texture);
            m_texture = {};
        }

        m_vertexCapacityBytes = 0;
        m_viewToShadowClip    = {};
        m_viewToShadowTexture = {};
        m_size                = 0;
        m_open                = false;
        m_valid               = false;
        m_failureWarned       = false;
    }

    bool DirectionalShadowMap::isOpen() const
    {
        return m_open;
    }

    bool DirectionalShadowMap::record(render::CommandList& list, const float towardLight[3],
        std::span<const CasterBatch> casters, float margin)
    {
        if ( !m_open )
        {
            return failRecord("the shadow map is not open");
        }
        if ( list.inPass() )
        {
            return failRecord("the command list is inside a pass");
        }
        if ( towardLight == nullptr )
        {
            return failRecord("the light direction is missing");
        }

        std::uint64_t totalVertices = 0;
        for ( const CasterBatch& batch : casters )
        {
            if ( batch.numVertices == 0 )
            {
                continue;
            }
            if ( batch.pPositions == nullptr )
            {
                return failRecord("a caster batch has no positions");
            }
            if ( batch.numVertices > std::numeric_limits<std::uint64_t>::max() - totalVertices )
            {
                return failRecord("the caster vertex count overflows");
            }
            totalVertices += batch.numVertices;
        }
        if ( totalVertices == 0 )
        {
            return failRecord("there are no caster vertices");
        }
        if ( totalVertices > std::numeric_limits<std::uint32_t>::max() / (3 * sizeof(float)) )
        {
            return failRecord("the casters do not fit in the vertex buffer");
        }
        const std::uint64_t vertexBytes = totalVertices * 3 * sizeof(float);
        if ( vertexBytes > m_device.info().limits.maxBufferSize )
        {
            return failRecord("the casters do not fit in the vertex buffer");
        }

        // One contiguous upload: copy every batch and take the union of their
        // view-space bounds. A non-finite position can't define a light fit.
        std::vector<float> vertices;
        vertices.reserve(static_cast<std::size_t>(totalVertices) * 3);
        Bounds bounds = {
            { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity() },
            { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() },
        };
        for ( const CasterBatch& batch : casters )
        {
            if ( batch.numVertices == 0 )
            {
                continue;
            }
            for ( std::size_t index = 0; index < batch.numVertices * 3; ++index )
            {
                const float value = batch.pPositions[index];
                if ( !std::isfinite(value) )
                {
                    return failRecord("a caster position is not finite");
                }
                vertices.push_back(value);
                const int axis = static_cast<int>(index % 3);
                bounds.min[axis] = value < bounds.min[axis] ? value : bounds.min[axis];
                bounds.max[axis] = value > bounds.max[axis] ? value : bounds.max[axis];
            }
        }

        Matrix4 viewToShadowClip{};
        if ( !fitDirectional(towardLight, bounds, margin, m_size, &viewToShadowClip) )
        {
            return failRecord("the light or caster bounds are invalid");
        }
        if ( !ensureVertexCapacity(vertexBytes) )
        {
            return failRecord("the vertex buffer could not be grown");
        }

        list.updateBuffer(m_vertexBuffer, 0,
            std::as_bytes(std::span(vertices.data(), vertices.size())));

        render::PassDesc pass;
        render::DepthAttachment depth;
        depth.target       = m_texture;
        depth.load         = render::LoadOp::Clear;
        depth.store        = render::StoreOp::Store;
        depth.clearDepth   = 1.0f;
        depth.clearStencil = 0;
        pass.depthTarget   = depth;
        if ( !list.beginPass(pass) )
        {
            return failRecord("the shadow pass could not begin");
        }

        list.setPipeline(m_pipeline);
        list.bindVertexBuffer(0, m_vertexBuffer);
        list.pushUniforms(render::ShaderStage::Vertex, 0,
            std::as_bytes(std::span(&viewToShadowClip, 1)));
        list.draw(static_cast<std::uint32_t>(totalVertices));
        if ( !list.endPass() )
        {
            return failRecord("the shadow pass could not end");
        }

        m_viewToShadowClip    = viewToShadowClip;
        m_viewToShadowTexture = multiply(shadowClipToTexture(), viewToShadowClip);
        m_valid               = true;
        m_failureWarned       = false;
        return true;
    }

    bool DirectionalShadowMap::isValid() const
    {
        return m_valid;
    }

    render::TextureHandle DirectionalShadowMap::texture() const
    {
        return m_texture;
    }

    render::SamplerHandle DirectionalShadowMap::compareSampler() const
    {
        return m_sampler;
    }

    Matrix4 DirectionalShadowMap::viewToShadowTexture() const
    {
        return m_viewToShadowTexture;
    }

    std::uint32_t DirectionalShadowMap::size() const
    {
        return m_size;
    }

    bool DirectionalShadowMap::ensureVertexCapacity(std::uint64_t bytes)
    {
        if ( !m_vertexBuffer.isNull() && m_vertexCapacityBytes >= bytes )
        {
            return true;
        }

        const std::uint64_t maxBytes = m_device.info().limits.maxBufferSize;
        if ( bytes > maxBytes || bytes > std::numeric_limits<std::uint32_t>::max() )
        {
            return false;
        }

        std::uint64_t capacity = m_vertexCapacityBytes;
        if ( capacity < kInitialVertexBytes )
        {
            capacity = std::min(kInitialVertexBytes, maxBytes);
        }
        while ( capacity < bytes )
        {
            capacity = capacity > maxBytes / 2 ? bytes : capacity * 2;
        }
        if ( capacity > maxBytes )
        {
            capacity = bytes;
        }

        if ( !m_vertexBuffer.isNull() )
        {
            m_device.destroyBuffer(m_vertexBuffer);
            m_vertexBuffer = {};
            m_vertexCapacityBytes = 0;
        }

        m_vertexBuffer = m_device.createBuffer(render::BufferDesc{
            static_cast<std::uint32_t>(capacity), render::BufferUsage::Vertex });
        if ( m_vertexBuffer.isNull() )
        {
            return false;
        }
        m_vertexCapacityBytes = capacity;
        return true;
    }

    bool DirectionalShadowMap::failRecord(const char* reason)
    {
        if ( !m_failureWarned )
        {
            log::warning("shadow: record: {}", reason);
            m_failureWarned = true;
        }
        m_valid = false;
        return false;
    }

}
