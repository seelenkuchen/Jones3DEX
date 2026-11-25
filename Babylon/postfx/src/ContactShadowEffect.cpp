// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/ContactShadowEffect.h>

#include <babylon/postfx/Resources.h>

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
        // Matches the cbuffer in shaders/contact_shadow.frag.hlsl.
        struct alignas(16) ContactShadowUniforms
        {
            float lightDirection[3];
            float length;
            float thickness;
            float strength;
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
            float padding[2];
        };
        static_assert(sizeof(ContactShadowUniforms) == 80);

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
            for ( const auto& entry : m_pipelines )
            {
                m_device->destroyPipeline(entry.second);
            }
        }
        m_pipelines.clear();
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

        render::PipelineHandle pipeline;
        for ( const auto& entry : m_pipelines )
        {
            if ( entry.first == ctx.outputFormat )
            {
                pipeline = entry.second;
                break;
            }
        }

        if ( pipeline.isNull() )
        {
            render::PipelineDesc desc;
            desc.vertexShader   = Resources::fullscreenVertexShader();
            desc.fragmentShader = shaders::contactShadowFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ ctx.outputFormat,
                render::BlendState{} } };

            m_device = &ctx.resources.device();
            pipeline = m_device->createPipeline(desc);
            if ( pipeline.isNull() )
            {
                log::warning("postfx: contact shadows: the pipeline could not be created");
                return false;
            }
            m_pipelines.emplace_back(ctx.outputFormat, pipeline);
        }

        const Projection& projection = ctx.frame.projection;
        const ContactShadowUniforms uniforms{
            { m_lightDirection[0], m_lightDirection[1], m_lightDirection[2] },
            m_settings.length,
            m_settings.thickness,
            m_settings.strength,
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
            { 0.0f, 0.0f } };

        const render::TextureHandle textures[3] = { ctx.input, ctx.frame.depth,
            needsNormals ? ctx.frame.normals : ctx.frame.depth };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, pipeline,
            std::span<const render::TextureHandle>(textures, 3), ctx.resources.nearestClamp(),
            std::as_bytes(std::span(&uniforms, 1)));
    }

}
