// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/ContactShadowEffect.h>

#include <babylon/postfx/Resources.h>
#include <babylon/postfx/TargetPool.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace babylon::postfx
{
    namespace
    {
        // Matches the cbuffer in shaders/contact_shadow_mask.frag.hlsl.
        struct alignas(16) ContactShadowMaskUniforms
        {
            float lightDirection[3];
            float length;
            float thickness;
            std::uint32_t steps;
            std::uint32_t receiverClasses;
            float fadeStart;
            float fadeEnd;
            float nearPlane;
            float farPlane;
            float depthNear;
            float depthFar;
            float tanHalfFovX;
            float tanHalfFovY;
            float inputWidth;
            float inputHeight;
            float padding[3];
        };
        static_assert(sizeof(ContactShadowMaskUniforms) == 80);

        // Matches the cbuffer in shaders/contact_shadow_blur.frag.hlsl.
        struct alignas(16) ContactShadowBlurUniforms
        {
            float nearPlane;
            float farPlane;
            float depthNear;
            float depthFar;
            float inputWidth;
            float inputHeight;
            float padding[2];
        };
        static_assert(sizeof(ContactShadowBlurUniforms) == 32);

        // Matches the cbuffer in shaders/contact_shadow_composite.frag.hlsl.
        struct alignas(16) ContactShadowCompositeUniforms
        {
            float strength;
            std::uint32_t receiverClasses;
            float depthNear;
            float depthFar;
        };
        static_assert(sizeof(ContactShadowCompositeUniforms) == 16);

        constexpr float kSmallestLength = 1.0e-4f;
    }

    void ContactShadowEffect::setSettings(const ContactShadowSettings& settings)
    {
        m_settings.strength        = std::clamp(settings.strength, 0.0f, 1.0f);
        m_settings.length          = std::max(settings.length, kSmallestLength);
        m_settings.thickness       = std::max(settings.thickness, kSmallestLength);
        m_settings.steps           = std::clamp<std::uint32_t>(settings.steps, 4u, 32u);
        m_settings.receiverClasses = settings.receiverClasses;
        m_settings.fadeStart       = settings.fadeStart;
        m_settings.fadeEnd         = std::max(settings.fadeEnd, m_settings.fadeStart);
    }

    const ContactShadowSettings& ContactShadowEffect::settings() const
    {
        return m_settings;
    }

    void ContactShadowEffect::setLightDirection(float x, float y, float z)
    {
        const float length = std::sqrt(x * x + y * y + z * z);
        if ( !(length > 0.0f) )
        {
            log::warning("postfx: contact shadows: a zero light direction is ignored");
            return;
        }
        m_lightDirection[0] = x / length;
        m_lightDirection[1] = y / length;
        m_lightDirection[2] = z / length;
    }

    std::string_view ContactShadowEffect::name() const
    {
        return "contact shadows";
    }

    Stage ContactShadowEffect::stage() const
    {
        return Stage::Hdr;
    }

    Needs ContactShadowEffect::needs() const
    {
        return Needs{ true, m_settings.receiverClasses != 0xFu };
    }

    bool ContactShadowEffect::active() const
    {
        return m_settings.strength > 0.0f;
    }

    void ContactShadowEffect::releaseResources()
    {
        if ( m_device != nullptr )
        {
            for ( const auto& entry : m_maskPipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
            for ( const auto& entry : m_blurPipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
            for ( const auto& entry : m_compositePipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
        }
        m_maskPipelines.clear();
        m_blurPipelines.clear();
        m_compositePipelines.clear();
        m_device = nullptr;
    }

    bool ContactShadowEffect::doRecord(PassContext& ctx)
    {
        if ( ctx.frame.depth.isNull() )
        {
            log::warning("postfx: contact shadows: the frame has no depth texture");
            return false;
        }

        const bool needsNormals = m_settings.receiverClasses != 0xFu;
        if ( needsNormals && ctx.frame.normals.isNull() )
        {
            log::warning("postfx: contact shadows: the frame has no normals texture");
            return false;
        }

        // Two scene-sized scratch targets: the mask and its blur.
        const render::TextureHandle maskTarget = ctx.targets.acquire(ctx.inputWidth,
            ctx.inputHeight, render::TextureFormat::Rgba8);
        if ( maskTarget.isNull() )
        {
            log::warning("postfx: contact shadows: the mask target could not be acquired");
            return false;
        }

        const render::TextureHandle blurTarget = ctx.targets.acquire(ctx.inputWidth,
            ctx.inputHeight, render::TextureFormat::Rgba8);
        if ( blurTarget.isNull() )
        {
            log::warning("postfx: contact shadows: the blur target could not be acquired");
            return false;
        }

        const auto getPipeline = [this, &ctx](
            std::vector<std::pair<render::TextureFormat, render::PipelineHandle>>& pipelines,
            render::TextureFormat format, const render::ShaderDesc& fragmentShader)
        {
            for ( const auto& entry : pipelines )
            {
                if ( entry.first == format )
                {
                    return entry.second;
                }
            }

            render::PipelineDesc desc;
            desc.vertexShader   = Resources::fullscreenVertexShader();
            desc.fragmentShader = fragmentShader;
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ format, render::BlendState{} } };

            m_device = &ctx.resources.device();
            const render::PipelineHandle pipeline = m_device->createPipeline(desc);
            if ( !pipeline.isNull() )
            {
                pipelines.emplace_back(format, pipeline);
            }
            return pipeline;
        };

        const render::PipelineHandle maskPipeline = getPipeline(m_maskPipelines,
            render::TextureFormat::Rgba8, shaders::contactShadowMaskFrag());
        if ( maskPipeline.isNull() )
        {
            log::warning("postfx: contact shadows: the mask pipeline could not be created");
            return false;
        }

        const render::PipelineHandle blurPipeline = getPipeline(m_blurPipelines,
            render::TextureFormat::Rgba8, shaders::contactShadowBlurFrag());
        if ( blurPipeline.isNull() )
        {
            log::warning("postfx: contact shadows: the blur pipeline could not be created");
            return false;
        }

        const render::PipelineHandle compositePipeline = getPipeline(m_compositePipelines,
            ctx.outputFormat, shaders::contactShadowCompositeFrag());
        if ( compositePipeline.isNull() )
        {
            log::warning("postfx: contact shadows: the composite pipeline could not be created");
            return false;
        }

        const Projection& projection = ctx.frame.projection;
        const ContactShadowMaskUniforms maskUniforms{
            { m_lightDirection[0], m_lightDirection[1], m_lightDirection[2] },
            m_settings.length,
            m_settings.thickness,
            m_settings.steps,
            m_settings.receiverClasses,
            m_settings.fadeStart,
            m_settings.fadeEnd,
            projection.nearPlane,
            projection.farPlane,
            projection.depthNear,
            projection.depthFar,
            projection.tanHalfFovX,
            projection.tanHalfFovY,
            static_cast<float>(ctx.inputWidth),
            static_cast<float>(ctx.inputHeight),
            { 0.0f, 0.0f, 0.0f } };

        const ContactShadowBlurUniforms blurUniforms{
            projection.nearPlane,
            projection.farPlane,
            projection.depthNear,
            projection.depthFar,
            static_cast<float>(ctx.inputWidth),
            static_cast<float>(ctx.inputHeight),
            { 0.0f, 0.0f } };

        const ContactShadowCompositeUniforms compositeUniforms{
            m_settings.strength,
            m_settings.receiverClasses,
            projection.depthNear,
            projection.depthFar };

        // Mask: depth and normals in, the shadow amount out.
        const render::TextureHandle maskTextures[2] = { ctx.frame.depth,
            needsNormals ? ctx.frame.normals : ctx.frame.depth };
        if ( !Resources::recordFullscreenPass(ctx.list, maskTarget, maskPipeline,
            std::span<const render::TextureHandle>(maskTextures, 2),
            ctx.resources.nearestClamp(), std::as_bytes(std::span(&maskUniforms, 1))) )
        {
            return false;
        }

        // Blur: the mask and the depth to weight its taps in, a smoothed mask out.
        const render::TextureHandle blurTextures[2] = { maskTarget, ctx.frame.depth };
        if ( !Resources::recordFullscreenPass(ctx.list, blurTarget, blurPipeline,
            std::span<const render::TextureHandle>(blurTextures, 2),
            ctx.resources.nearestClamp(), std::as_bytes(std::span(&blurUniforms, 1))) )
        {
            return false;
        }

        // Composite: depth and normals in the mask pass's slots, then the input colour
        // and the blurred mask. It repeats the receiver test so non-receivers stay exact.
        const render::TextureHandle compositeTextures[4] = { ctx.frame.depth,
            needsNormals ? ctx.frame.normals : ctx.frame.depth, ctx.input, blurTarget };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, compositePipeline,
            std::span<const render::TextureHandle>(compositeTextures, 4),
            ctx.resources.nearestClamp(), std::as_bytes(std::span(&compositeUniforms, 1)));
    }

}
