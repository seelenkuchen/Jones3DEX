// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/FilmGrainEffect.h>

#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cstdint>
#include <span>

namespace babylon::postfx
{
    namespace
    {
        // Matches the cbuffer in shaders/film_grain.frag.hlsl.
        struct alignas(16) FilmGrainUniforms
        {
            std::uint32_t frameIndexLow;
            float amplitude;
            float padding[2];
        };
        static_assert(sizeof(FilmGrainUniforms) == 16);
    }

    void FilmGrainEffect::setSettings(const FilmGrainSettings& settings)
    {
        m_settings.amplitude = std::clamp(settings.amplitude, 0.0f, 0.25f);
    }

    const FilmGrainSettings& FilmGrainEffect::settings() const
    {
        return m_settings;
    }

    std::string_view FilmGrainEffect::name() const
    {
        return "film grain";
    }

    Stage FilmGrainEffect::stage() const
    {
        return Stage::Ldr;
    }

    bool FilmGrainEffect::active() const
    {
        return m_settings.amplitude > 0.0f;
    }

    void FilmGrainEffect::releaseResources()
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

    bool FilmGrainEffect::doRecord(PassContext& ctx)
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
            desc.fragmentShader = shaders::filmGrainFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ ctx.outputFormat,
                render::BlendState{} } };

            m_device = &ctx.resources.device();
            pipeline = m_device->createPipeline(desc);
            if ( pipeline.isNull() )
            {
                log::warning("postfx: film grain: the pipeline could not be created");
                return false;
            }
            m_pipelines.emplace_back(ctx.outputFormat, pipeline);
        }

        const FilmGrainUniforms uniforms{
            static_cast<std::uint32_t>(ctx.frame.frameIndex & 0xffffffffu),
            m_settings.amplitude, { 0.0f, 0.0f } };
        const render::TextureHandle textures[1] = { ctx.input };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, pipeline,
            std::span<const render::TextureHandle>(textures, 1), ctx.resources.nearestClamp(),
            std::as_bytes(std::span(&uniforms, 1)));
    }

}
