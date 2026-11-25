// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/VignetteEffect.h>

#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <span>

namespace babylon::postfx
{
    namespace
    {
        // Matches the cbuffer in shaders/vignette.frag.hlsl.
        struct alignas(16) VignetteUniforms
        {
            float strength;
            float inner;
            float outer;
            float aspect;
        };
        static_assert(sizeof(VignetteUniforms) == 16);
    }

    void VignetteEffect::setSettings(const VignetteSettings& settings)
    {
        m_settings.strength = std::clamp(settings.strength, 0.0f, 1.0f);
        m_settings.inner    = settings.inner;
        m_settings.outer    = std::max(settings.outer, m_settings.inner + 0.001f);
    }

    const VignetteSettings& VignetteEffect::settings() const
    {
        return m_settings;
    }

    std::string_view VignetteEffect::name() const
    {
        return "vignette";
    }

    Stage VignetteEffect::stage() const
    {
        return Stage::Ldr;
    }

    bool VignetteEffect::active() const
    {
        return m_settings.strength > 0.0f;
    }

    void VignetteEffect::releaseResources()
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

    bool VignetteEffect::doRecord(PassContext& ctx)
    {
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
            desc.fragmentShader = shaders::vignetteFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ ctx.outputFormat,
                render::BlendState{} } };

            m_device = &ctx.resources.device();
            pipeline = m_device->createPipeline(desc);
            if ( pipeline.isNull() )
            {
                log::warning("postfx: vignette: the pipeline could not be created");
                return false;
            }
            m_pipelines.emplace_back(ctx.outputFormat, pipeline);
        }

        const VignetteUniforms uniforms{ m_settings.strength, m_settings.inner, m_settings.outer,
            static_cast<float>(ctx.outputWidth) / static_cast<float>(ctx.outputHeight) };
        const render::TextureHandle textures[1] = { ctx.input };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, pipeline,
            std::span<const render::TextureHandle>(textures, 1), ctx.resources.linearClamp(),
            std::as_bytes(std::span(&uniforms, 1)));
    }

}
