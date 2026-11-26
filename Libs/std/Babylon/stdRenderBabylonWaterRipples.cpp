// The water-ripple stage of the Babylon render adapter.
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
    constexpr float stdRenderBabylon_kWaterRippleAmplitude        = 0.0025f; // fraction of output height
    constexpr float stdRenderBabylon_kWaterRippleScale            = 7.0f;    // wave cycles across the screen
    constexpr float stdRenderBabylon_kWaterRippleSpeed1           = 0.9f;    // radians per second
    constexpr float stdRenderBabylon_kWaterRippleSpeed2           = 1.3f;
    constexpr float stdRenderBabylon_kWaterRippleSlopeScale       = 0.35f;   // slope to highlight normal
    constexpr float stdRenderBabylon_kWaterRippleHighlight        = 0.035f;
    constexpr float stdRenderBabylon_kWaterRippleHighlightPower   = 24.0f;

    // Added: the water-ripple stage's scene-sized target, made when its size changes. False,
    // logging why, when it can't be created.
    bool stdRenderBabylon_EnsureWaterTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.waterColorTarget.isNull()
            && stdRenderBabylon_state.waterWidth == width
            && stdRenderBabylon_state.waterHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeWaterTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u water-ripple target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.waterColorTarget = target;
        stdRenderBabylon_state.waterWidth       = width;
        stdRenderBabylon_state.waterHeight      = height;
        return true;
    }

    // Added: the water-ripple pipeline, made on first use. It is one full-screen pass over the scene
    // in the scene format.
    bool stdRenderBabylon_EnsureWaterResources()
    {
        if ( stdRenderBabylon_state.bWaterSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::waterRipplesFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

        stdRenderBabylon_state.waterRipplesPipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
        if ( stdRenderBabylon_state.waterRipplesPipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the water-ripple pass.\n");
            stdRenderBabylon_state.bWaterSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the water-ripple stage's target. Recreated by the next present with the stage on
    // after a scene size or render scale change.
    void stdRenderBabylon_FreeWaterTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.waterColorTarget);
        stdRenderBabylon_state.waterColorTarget = {};
        stdRenderBabylon_state.waterWidth       = 0;
        stdRenderBabylon_state.waterHeight      = 0;
        stdRenderBabylon_state.bWaterRan        = false;
    }

    // Added: the water-ripple stage over the post chain's output, before depth of field. On pixels
    // whose normal-target alpha carries the water tag it samples the scene at a small animated offset
    // and adds a faint highlight; every other pixel is copied unchanged. A sample that lands on a
    // non-water pixel falls back to the unoffset sample, so geometry next to water never smears in.
    // False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordWaterRipples(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bWaterRipples
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull() )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureWaterTargets(width, height)
            || !stdRenderBabylon_EnsureWaterResources() )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.bWaterLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: water ripples on, amplitude %g of the height, scale %g, target %ux%u.\n",
                (double)stdRenderBabylon_kWaterRippleAmplitude,
                (double)stdRenderBabylon_kWaterRippleScale, width, height);
            stdRenderBabylon_state.bWaterLogged = true;
        }

        const render::TextureHandle scene  = stdRenderBabylon_ChainSource();
        const render::TextureHandle normal = stdRenderBabylon_state.normalTarget;
        const float aWater[4] = { stdRenderBabylon_state.waterTime,
            stdRenderBabylon_kWaterRippleAmplitude, stdRenderBabylon_kWaterRippleScale,
            stdRenderBabylon_kSurfaceTags.water };
        const float aShape[4] = { stdRenderBabylon_kWaterRippleHighlight,
            stdRenderBabylon_kWaterRippleHighlightPower,
            stdRenderBabylon_kWaterRippleSpeed1, stdRenderBabylon_kWaterRippleSpeed2 };
        const float aSize[4] = { (float)width, (float)height,
            stdRenderBabylon_kWaterRippleSlopeScale, 0.0f };

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.waterColorTarget,
            0, render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(stdRenderBabylon_state.waterRipplesPipeline);
        // Slot 0 is the pixel's own scene sample (nearest), slot 1 the offset sample (linear), slot 2
        // the normal target that carries the water tag.
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, scene,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, normal,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aWater, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aShape, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 2,
            std::as_bytes(std::span<const float>(aSize, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bWaterRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetWaterRipples(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetWaterRipples: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bWaterRipples     = bEnabled;
    stdRenderBabylon_state.bWaterSetupFailed = false;
    stdRenderBabylon_state.bWaterRan         = false;
    stdRenderBabylon_state.bWaterLogged      = false;
    return true;
}

bool stdRenderBabylon_GetWaterRipples()
{
    return stdRenderBabylon_state.bWaterRipples;
}
