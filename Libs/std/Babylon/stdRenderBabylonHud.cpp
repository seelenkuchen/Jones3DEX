// The HUD layer composite of the Babylon render adapter.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>
#include <babylon/platform/Window.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <std/General/std.h>
#include <std/General/stdConfig.h>

#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
using namespace stdRenderBabylonInternal;

namespace stdRenderBabylonInternal
{
    // Added: the pipeline that composites the HUD layer over the presented frame. It draws the
    // present pass's triangle sampling the HUD color target and blends it with ONE / ONE_MINUS_SRC_ALPHA,
    // because the HUD pass accumulates premultiplied color and coverage alpha. The target format is
    // the swapchain's or Bgra8 for a capture.
    render::PipelineHandle stdRenderBabylon_CreateHudPipeline(render::TextureFormat format)
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::presentFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;

        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::One;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        blend.srcAlpha = render::BlendFactor::One;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        desc.colorTargets = { render::ColorTargetDesc{ format, blend } };

        return stdRenderBabylon_state.pDevice->createPipeline(desc);
    }

    // Added: true when list has a pass that drew into the HUD layer's color target.
    bool stdRenderBabylon_DrawsHudLayer(const render::CommandList& list)
    {
        const render::TextureHandle hudTarget = stdRenderBabylon_state.hudColorTarget;
        if ( hudTarget.isNull() )
        {
            return false;
        }

        for ( const render::Command& command : list.commands() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                for ( const render::ColorAttachment& attachment : pBegin->desc.colorTargets )
                {
                    if ( attachment.target == hudTarget )
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    // Added: records the HUD layer over the presented frame and returns false, logging why, when the
    // pipeline or the pass can't be made. Nothing is recorded when no HUD was drawn or there is no
    // layer. The display-sized layer is sampled like the final pass samples the scene.
    bool stdRenderBabylon_RecordHudComposite(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format)
    {
        if ( !stdRenderBabylon_state.bHudLayerDrawn || stdRenderBabylon_state.hudColorTarget.isNull() )
        {
            return true;
        }

        render::PipelineHandle* pPipeline = target.isNull()
            ? &stdRenderBabylon_state.hudPipeline : &stdRenderBabylon_state.hudCapturePipeline;
        if ( pPipeline->isNull() )
        {
            *pPipeline = stdRenderBabylon_CreateHudPipeline(format);
        }
        if ( pPipeline->isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the HUD composite pipeline.\n");
            return false;
        }

        // Mirror the final pass: the pre-M9.4 1:1 nearest sample at render scale 1, linear otherwise.
        // At render scale 1 this keeps the HUD's pixels exactly where the old single pass put them,
        // also when the window scales the display-sized frame.
        render::SamplerHandle sampler;
        if ( stdRenderBabylon_state.renderScale == 1.0f )
        {
            if ( !stdRenderBabylon_EnsurePresentSampler() )
            {
                return false;
            }
            sampler = stdRenderBabylon_state.presentSampler;
        }
        else
        {
            if ( !stdRenderBabylon_EnsureFinalSampler() )
            {
                return false;
            }
            sampler = stdRenderBabylon_state.finalSampler;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::Load, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(*pPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.hudColorTarget, sampler);
        list.draw(3);
        return list.endPass();
    }

}

void stdRenderBabylon_SetHudOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser)
{
    stdRenderBabylon_state.hudOverlay     = pfn;
    stdRenderBabylon_state.hudOverlayUser = pfn ? pUser : nullptr;
}

bool stdRenderBabylon_HasHudOverlay()
{
    return stdRenderBabylon_state.hudOverlay != nullptr;
}
