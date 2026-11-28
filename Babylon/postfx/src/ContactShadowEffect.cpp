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
            std::uint32_t hasIgnoreMask;
            float padding[2];
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

        // Matches the cbuffer in shaders/contact_shadow_temporal.frag.hlsl.
        struct alignas(16) ContactShadowTemporalUniforms
        {
            float currentToPrevious[12];
            float nearPlane;
            float farPlane;
            float depthNear;
            float depthFar;
            float tanHalfFovX;
            float tanHalfFovY;
            float inputWidth;
            float inputHeight;
            float blend;
            std::uint32_t hasHistory;
            float padding[2];
        };
        static_assert(sizeof(ContactShadowTemporalUniforms) == 96);

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
        m_settings.historyHalfLife = std::clamp(settings.historyHalfLife, 0.0f, 1.0f);
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

    void ContactShadowEffect::setIgnoreMask(render::TextureHandle mask)
    {
        m_ignoreMask = mask;
    }

    void ContactShadowEffect::setCameraMotion(const CameraMotion& motion)
    {
        m_cameraMotion = motion;
    }

    void ContactShadowEffect::resetHistory()
    {
        m_historyValid = false;
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

    bool ContactShadowEffect::ensureHistoryTextures(render::Device& device, std::uint32_t width,
        std::uint32_t height)
    {
        m_device = &device;
        if ( !m_historyTextures[0].isNull() && !m_historyTextures[1].isNull()
            && m_historyWidth == width && m_historyHeight == height )
        {
            return true;
        }

        // A size change starts a fresh history.
        m_historyValid = false;
        if ( !m_historyTextures[0].isNull() )
        {
            device.destroyTexture(m_historyTextures[0]);
        }
        if ( !m_historyTextures[1].isNull() )
        {
            device.destroyTexture(m_historyTextures[1]);
        }
        m_historyTextures[0] = {};
        m_historyTextures[1] = {};
        m_historyWidth       = 0;
        m_historyHeight      = 0;
        m_historyReadIndex   = 0;

        const render::TextureDesc desc{ width, height, 1, render::TextureFormat::Rgba16F,
            render::TextureUsage::ColorTarget | render::TextureUsage::Sampled };
        m_historyTextures[0] = device.createTexture(desc);
        m_historyTextures[1] = device.createTexture(desc);
        if ( m_historyTextures[0].isNull() || m_historyTextures[1].isNull() )
        {
            log::warning("postfx: contact shadows: the history targets could not be created");
            if ( !m_historyTextures[0].isNull() )
            {
                device.destroyTexture(m_historyTextures[0]);
            }
            if ( !m_historyTextures[1].isNull() )
            {
                device.destroyTexture(m_historyTextures[1]);
            }
            m_historyTextures[0] = {};
            m_historyTextures[1] = {};
            return false;
        }

        m_historyWidth  = width;
        m_historyHeight = height;
        return true;
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
            for ( const auto& entry : m_temporalPipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
            for ( const auto& entry : m_compositePipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
            for ( render::TextureHandle& texture : m_historyTextures )
            {
                if ( !texture.isNull() )
                {
                    m_device->destroyTexture(texture);
                }
            }
        }
        m_maskPipelines.clear();
        m_blurPipelines.clear();
        m_temporalPipelines.clear();
        m_compositePipelines.clear();
        m_historyTextures[0] = {};
        m_historyTextures[1] = {};
        m_historyWidth       = 0;
        m_historyHeight      = 0;
        m_historyReadIndex   = 0;
        m_historyValid       = false;
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

        const bool useHistory = m_settings.historyHalfLife > 0.0f;
        render::PipelineHandle temporalPipeline;
        if ( useHistory )
        {
            temporalPipeline = getPipeline(m_temporalPipelines, render::TextureFormat::Rgba16F,
                shaders::contactShadowTemporalFrag());
            if ( temporalPipeline.isNull() )
            {
                log::warning("postfx: contact shadows: the temporal pipeline could not be created");
                return false;
            }
        }

        const render::PipelineHandle compositePipeline = getPipeline(m_compositePipelines,
            ctx.outputFormat, shaders::contactShadowCompositeFrag());
        if ( compositePipeline.isNull() )
        {
            log::warning("postfx: contact shadows: the composite pipeline could not be created");
            return false;
        }

        const bool hasIgnoreMask = !m_ignoreMask.isNull();

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
            hasIgnoreMask ? 1u : 0u,
            { 0.0f, 0.0f } };

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

        // Mask: depth, normals and the ignore mask (or depth again when there is
        // none, so the layout never changes) in, the shadow amount out.
        const render::TextureHandle maskTextures[3] = { ctx.frame.depth,
            needsNormals ? ctx.frame.normals : ctx.frame.depth,
            hasIgnoreMask ? m_ignoreMask : ctx.frame.depth };
        if ( !Resources::recordFullscreenPass(ctx.list, maskTarget, maskPipeline,
            std::span<const render::TextureHandle>(maskTextures, 3),
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

        render::TextureHandle compositeMask = blurTarget;
        if ( useHistory )
        {
            if ( !ensureHistoryTextures(ctx.resources.device(), ctx.inputWidth, ctx.inputHeight) )
            {
                return false;
            }

            const bool hasHistory = m_historyValid && m_cameraMotion.valid
                && ctx.frame.deltaSeconds > 0.0f;
            const std::uint32_t writeIndex = m_historyReadIndex == 0u ? 1u : 0u;
            const float blend = hasHistory
                ? 1.0f - std::exp2(-ctx.frame.deltaSeconds / m_settings.historyHalfLife)
                : 0.0f;

            ContactShadowTemporalUniforms temporalUniforms{};
            for ( std::size_t i = 0; i < 12; ++i )
            {
                temporalUniforms.currentToPrevious[i] = m_cameraMotion.currentToPrevious[i];
            }
            temporalUniforms.nearPlane   = projection.nearPlane;
            temporalUniforms.farPlane    = projection.farPlane;
            temporalUniforms.depthNear   = projection.depthNear;
            temporalUniforms.depthFar    = projection.depthFar;
            temporalUniforms.tanHalfFovX = projection.tanHalfFovX;
            temporalUniforms.tanHalfFovY = projection.tanHalfFovY;
            temporalUniforms.inputWidth  = static_cast<float>(ctx.inputWidth);
            temporalUniforms.inputHeight = static_cast<float>(ctx.inputHeight);
            temporalUniforms.blend       = blend;
            temporalUniforms.hasHistory  = hasHistory ? 1u : 0u;
            temporalUniforms.padding[0]  = 0.0f;
            temporalUniforms.padding[1]  = 0.0f;

            // Temporal: the blurred mask, the depth and the previous mask, reprojected
            // through the camera motion, out to the history target this frame writes.
            const render::TextureHandle temporalTextures[3] = { blurTarget, ctx.frame.depth,
                m_historyTextures[m_historyReadIndex] };
            if ( !Resources::recordFullscreenPass(ctx.list, m_historyTextures[writeIndex],
                temporalPipeline, std::span<const render::TextureHandle>(temporalTextures, 3),
                ctx.resources.linearClamp(), std::as_bytes(std::span(&temporalUniforms, 1))) )
            {
                return false;
            }

            m_historyReadIndex = writeIndex;
            m_historyValid     = true;
            compositeMask      = m_historyTextures[writeIndex];
        }

        // Composite: depth and normals in the mask pass's slots, then the input colour
        // and the mask. It repeats the receiver test so non-receivers stay exact.
        const render::TextureHandle compositeTextures[4] = { ctx.frame.depth,
            needsNormals ? ctx.frame.normals : ctx.frame.depth, ctx.input, compositeMask };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, compositePipeline,
            std::span<const render::TextureHandle>(compositeTextures, 4),
            ctx.resources.nearestClamp(), std::as_bytes(std::span(&compositeUniforms, 1)));
    }

}
