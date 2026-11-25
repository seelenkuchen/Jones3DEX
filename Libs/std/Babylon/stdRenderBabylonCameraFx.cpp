// The vignette and film-grain stage of the Babylon render adapter.
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

namespace
{
    constexpr float stdRenderBabylon_kVignetteStrength = 0.35f;
    constexpr float stdRenderBabylon_kVignetteInner    = 0.45f;
    constexpr float stdRenderBabylon_kVignetteOuter    = 1.00f;

    constexpr float stdRenderBabylon_kFilmGrainAmplitude = 0.035f;

}

namespace stdRenderBabylonInternal
{
    // Added: frees the M9.18 camera-effect source target.
    void stdRenderBabylon_FreeFxTarget()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.fxSourceTarget);
        stdRenderBabylon_state.fxSourceTarget = {};
        stdRenderBabylon_state.fxWidth        = 0;
        stdRenderBabylon_state.fxHeight       = 0;
    }

    // Added: the presented-size Bgra8 image the vignette and grain pass reads, made when its size
    // changes. False, logging why, when it can't be created.
    bool stdRenderBabylon_EnsureFxTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.fxSourceTarget.isNull()
            && stdRenderBabylon_state.fxWidth == width
            && stdRenderBabylon_state.fxHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeFxTarget();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Bgra8;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u camera-effect source.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.fxSourceTarget = target;
        stdRenderBabylon_state.fxWidth        = width;
        stdRenderBabylon_state.fxHeight       = height;
        return true;
    }

    // Added: the tone-map pass into the Bgra8 camera-effect source, made on first use.
    bool stdRenderBabylon_EnsureFxToneMapPipeline()
    {
        if ( !stdRenderBabylon_state.fxToneMapPipeline.isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, {} } };

        stdRenderBabylon_state.fxToneMapPipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !stdRenderBabylon_state.fxToneMapPipeline.isNull();
    }

    // Added: the vignette and grain pipeline for target (the swapchain or a Bgra8 capture), made on
    // first use.
    bool stdRenderBabylon_EnsureFxPipeline(bool bSwapchain, render::TextureFormat format)
    {
        render::PipelineHandle* pPipeline = bSwapchain
            ? &stdRenderBabylon_state.fxPipeline : &stdRenderBabylon_state.fxCapturePipeline;
        if ( !pPipeline->isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::cameraFxFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ format, {} } };

        *pPipeline = stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !pPipeline->isNull();
    }

    // Added: the M9.18 vignette and film-grain pass from the presented-size source into target (the
    // swapchain for a null handle). Each effect is skipped in the shader at strength zero; the
    // caller only records the pass when one is on. False, having logged nothing, when a resource
    // can't be made or a pass can't be started or ended.
    bool stdRenderBabylon_RecordCameraFx(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height)
    {
        if ( source.isNull() || width == 0 || height == 0
            || !stdRenderBabylon_EnsureFxPipeline(target.isNull(), format)
            || !stdRenderBabylon_EnsurePresentSampler() )
        {
            return false;
        }

        const float aSize[4] = { (float)width, (float)height, 0.0f, 0.0f };
        const float aVignette[4] = { stdRenderBabylon_state.bVignette
                ? stdRenderBabylon_kVignetteStrength : 0.0f,
            stdRenderBabylon_kVignetteInner, stdRenderBabylon_kVignetteOuter, 0.0f };
        const float aGrain[4] = { stdRenderBabylon_state.bFilmGrain
                ? stdRenderBabylon_kFilmGrainAmplitude : 0.0f,
            (float)stdRenderBabylon_state.grainFrame, 0.0f, 0.0f };

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(target.isNull() ? stdRenderBabylon_state.fxPipeline
                                         : stdRenderBabylon_state.fxCapturePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, source,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aSize, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aVignette, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 2,
            std::as_bytes(std::span<const float>(aGrain, 4u)));
        list.draw(3);
        return list.endPass();
    }

}

bool stdRenderBabylon_SetVignette(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetVignette: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bVignette     = bEnabled;
    stdRenderBabylon_state.bFxSetupFailed = false;
    return true;
}

bool stdRenderBabylon_GetVignette()
{
    return stdRenderBabylon_state.bVignette;
}

bool stdRenderBabylon_SetFilmGrain(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetFilmGrain: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bFilmGrain    = bEnabled;
    stdRenderBabylon_state.bFxSetupFailed = false;
    return true;
}

bool stdRenderBabylon_GetFilmGrain()
{
    return stdRenderBabylon_state.bFilmGrain;
}
