// The vignette and film-grain stage's source target and options of the Babylon render adapter; the
// effects themselves are drawn by the Babylon postfx objects in stdRenderBabylonPostfx.cpp.
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
