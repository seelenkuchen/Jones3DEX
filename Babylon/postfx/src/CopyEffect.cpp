// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/CopyEffect.h>

#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <span>

namespace babylon::postfx
{

    std::string_view CopyEffect::name() const
    {
        return "copy";
    }

    Stage CopyEffect::stage() const
    {
        return Stage::Resolve;
    }

    bool CopyEffect::active() const
    {
        return true;
    }

    void CopyEffect::releaseResources()
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

    bool CopyEffect::doRecord(PassContext& ctx)
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
            desc.fragmentShader = shaders::copyFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{ ctx.outputFormat,
                render::BlendState{} } };

            m_device = &ctx.resources.device();
            pipeline = m_device->createPipeline(desc);
            if ( pipeline.isNull() )
            {
                log::warning("postfx: copy: the pipeline could not be created");
                return false;
            }
            m_pipelines.emplace_back(ctx.outputFormat, pipeline);
        }

        const render::TextureHandle textures[1] = { ctx.input };
        return Resources::recordFullscreenPass(ctx.list, ctx.output, pipeline,
            std::span<const render::TextureHandle>(textures, 1),
            ctx.resources.linearClamp());
    }

}
