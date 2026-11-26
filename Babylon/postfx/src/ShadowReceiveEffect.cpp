// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace babylon::postfx
{
    namespace
    {
        // Matches the cbuffer in shaders/shadow_receive.frag.hlsl.
        struct alignas(16) ShadowReceiveUniforms
        {
            float matrix[16];
            float towardLight[3];
            float strength;
            float normalOffset;
            float depthBias;
            float filterRadius;
            float mapSize;
            std::uint32_t receiverClasses;
            float fadeStart;
            float fadeEnd;
            float nearPlane;
            float farPlane;
            float depthNear;
            float depthFar;
            float tanHalfFovX;
            float tanHalfFovY;
            std::uint32_t hasExcludeMask;
            float padding[2];
        };
        static_assert(sizeof(ShadowReceiveUniforms) == 144);
    }

    void ShadowReceiveEffect::setSettings(const ShadowReceiveSettings& settings)
    {
        m_settings.strength        = std::clamp(settings.strength, 0.0f, 1.0f);
        m_settings.normalOffset    = std::max(settings.normalOffset, 0.0f);
        m_settings.depthBias       = std::max(settings.depthBias, 0.0f);
        m_settings.filterRadius    = std::clamp(settings.filterRadius, 0.5f, 4.0f);
        m_settings.receiverClasses = settings.receiverClasses;
        m_settings.fadeStart       = settings.fadeStart;
        m_settings.fadeEnd         = std::max(settings.fadeEnd, m_settings.fadeStart);
    }

    const ShadowReceiveSettings& ShadowReceiveEffect::settings() const
    {
        return m_settings;
    }

    void ShadowReceiveEffect::setShadowMap(const ShadowMapInput& shadowMap)
    {
        m_map = shadowMap;
    }

    void ShadowReceiveEffect::clearShadowMap()
    {
        m_map = {};
    }

    std::string_view ShadowReceiveEffect::name() const
    {
        return "shadow receive";
    }

    Stage ShadowReceiveEffect::stage() const
    {
        return Stage::Hdr;
    }

    Needs ShadowReceiveEffect::needs() const
    {
        return Needs{ true, m_settings.receiverClasses != 0xFu };
    }

    bool ShadowReceiveEffect::active() const
    {
        return m_settings.strength > 0.0f && !m_map.texture.isNull()
            && !m_map.compareSampler.isNull() && m_map.mapSize > 0;
    }

    void ShadowReceiveEffect::releaseResources()
    {
        if ( m_device != nullptr )
        {
            for ( const auto& entry : m_pipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
        }
        m_pipelines.clear();
        m_device = nullptr;
    }

    bool ShadowReceiveEffect::doRecord(PassContext& ctx)
    {
        if ( ctx.frame.depth.isNull() )
        {
            log::warning("postfx: shadow receive: the frame has no depth texture");
            return false;
        }

        const bool needsNormals = m_settings.receiverClasses != 0xFu;
        if ( needsNormals && ctx.frame.normals.isNull() )
        {
            log::warning("postfx: shadow receive: the frame has no normals texture");
            return false;
        }

        if ( m_map.texture.isNull() || m_map.compareSampler.isNull() || m_map.mapSize == 0 )
        {
            log::warning("postfx: shadow receive: there is no valid shadow map");
            return false;
        }

        const auto getPipeline = [this, &ctx](render::TextureFormat format)
        {
            for ( const auto& entry : m_pipelines )
            {
                if ( entry.first == format )
                {
                    return entry.second;
                }
            }

            render::PipelineDesc desc;
            desc.vertexShader   = Resources::fullscreenVertexShader();
            desc.fragmentShader = shaders::shadowReceiveFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ format, render::BlendState{} } };

            m_device = &ctx.resources.device();
            const render::PipelineHandle pipeline = m_device->createPipeline(desc);
            if ( !pipeline.isNull() )
            {
                m_pipelines.emplace_back(format, pipeline);
            }
            return pipeline;
        };

        const render::PipelineHandle pipeline = getPipeline(ctx.outputFormat);
        if ( pipeline.isNull() )
        {
            log::warning("postfx: shadow receive: the pipeline could not be created");
            return false;
        }

        const Projection& projection = ctx.frame.projection;
        const ShadowReceiveUniforms uniforms{
            { m_map.matrix[0], m_map.matrix[1], m_map.matrix[2], m_map.matrix[3],
                m_map.matrix[4], m_map.matrix[5], m_map.matrix[6], m_map.matrix[7],
                m_map.matrix[8], m_map.matrix[9], m_map.matrix[10], m_map.matrix[11],
                m_map.matrix[12], m_map.matrix[13], m_map.matrix[14], m_map.matrix[15] },
            { m_map.towardLight[0], m_map.towardLight[1], m_map.towardLight[2] },
            m_settings.strength,
            m_settings.normalOffset,
            m_settings.depthBias,
            m_settings.filterRadius,
            static_cast<float>(m_map.mapSize),
            m_settings.receiverClasses,
            m_settings.fadeStart,
            m_settings.fadeEnd,
            projection.nearPlane,
            projection.farPlane,
            projection.depthNear,
            projection.depthFar,
            projection.tanHalfFovX,
            projection.tanHalfFovY,
            m_map.excludeMask.isNull() ? 0u : 1u,
            { 0.0f, 0.0f } };

        // One pass: colour and depth of the frame, the normals (or depth again),
        // the shadow map with its comparison sampler, and the exclude mask (or
        // depth again when there is none, so the layout never changes).
        render::PassDesc pass;
        render::ColorAttachment attachment;
        attachment.target = ctx.output;
        attachment.load   = render::LoadOp::DontCare;
        attachment.store  = render::StoreOp::Store;
        pass.colorTargets.push_back(attachment);
        if ( !ctx.list.beginPass(pass) )
        {
            return false;
        }

        ctx.list.setPipeline(pipeline);
        ctx.list.bindTexture(render::ShaderStage::Fragment, 0, ctx.input,
            ctx.resources.nearestClamp());
        ctx.list.bindTexture(render::ShaderStage::Fragment, 1, ctx.frame.depth,
            ctx.resources.nearestClamp());
        ctx.list.bindTexture(render::ShaderStage::Fragment, 2,
            needsNormals ? ctx.frame.normals : ctx.frame.depth, ctx.resources.nearestClamp());
        ctx.list.bindTexture(render::ShaderStage::Fragment, 3, m_map.texture,
            m_map.compareSampler);
        ctx.list.bindTexture(render::ShaderStage::Fragment, 4,
            m_map.excludeMask.isNull() ? ctx.frame.depth : m_map.excludeMask,
            ctx.resources.nearestClamp());
        ctx.list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span(&uniforms, 1)));
        ctx.list.draw(3);
        return ctx.list.endPass();
    }

}
