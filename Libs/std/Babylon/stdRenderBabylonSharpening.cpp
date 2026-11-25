// Added: the Babylon column's render frame owner (stdRenderBabylon.h). Adapter code: no Fixed/Added
// markers inside.
//
// The back buffer reaches the window through a pass to the swapchain that draws one full-screen
// triangle sampling the color target (Shaders/present.*.hlsl). Babylon's copyTexture can't name the
// swapchain (its null handle means the swapchain only as a pass attachment), so a pass is the only
// way the render interface offers to put an offscreen target on the screen.
//
// A presenting submit runs the post-processing chain first: each pass draws the same full-screen
// triangle with its own fragment shader into one of two targets the chain owns, in the scene color
// format, so an Rgba16F scene keeps HDR values through the passes, reading the output of the pass
// before (the back buffer color for the first) and, as its shader declares, the normal and depth
// targets. The final tone-map pass then samples the last output instead of the back buffer and
// writes the frame (the swapchain, or the target of RenderPresentToTexture). Its shader and
// present.vert are the plain present pass plus exposure and the operator (0 clamp, 1 PBR Neutral,
// 2 ACES fitted); at exposure 1 and operator 0 it clamps to 0..1 and presents the same image as the
// pre-M9.3 present. The back buffer itself is never written by the chain or the final pass.
//
// The scene color target is Rgba16F on a device that can render and sample it, else Bgra8, so
// shaders may write values outside 0..1 for a later tone map. ReadBackBuffer and WriteBackBuffer
// translate between that target and the Bgra8 bytes the game and stdDisplay use.
// The contrast-adaptive sharpening (CAS) stage of the Babylon render adapter.
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
    // Added: the sharpening pass's pipeline for target (the swapchain or a Bgra8 capture), made on
    // first use.
    bool stdRenderBabylon_EnsureCasPipeline(bool bSwapchain, render::TextureFormat format)
    {
        render::PipelineHandle* pPipeline = bSwapchain
            ? &stdRenderBabylon_state.casPipeline
            : &stdRenderBabylon_state.casCapturePipeline;
        if ( !pPipeline->isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::casFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ format, {} } };

        *pPipeline = stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !pPipeline->isNull();
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the sharpening stage's presented-size source image. Recreated by the next
    // present that sharpens after a display or window size change.
    void stdRenderBabylon_FreeCasTarget()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.casSourceTarget);
        stdRenderBabylon_state.casSourceTarget = {};
        stdRenderBabylon_state.casWidth        = 0;
        stdRenderBabylon_state.casHeight       = 0;
    }

    // Added: the presented-size Rgba8 image the sharpening stage reads, made when its size
    // changes. False, logging why, when it can't be created; there is none then.
    bool stdRenderBabylon_EnsureCasTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.casSourceTarget.isNull()
            && stdRenderBabylon_state.casWidth == width
            && stdRenderBabylon_state.casHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeCasTarget();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u sharpening source.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.casSourceTarget = target;
        stdRenderBabylon_state.casWidth        = width;
        stdRenderBabylon_state.casHeight       = height;
        return true;
    }

    // Added: the tone-map pass into the sharpening source (which anti-aliasing off and sharpening
    // on share), made on first use.
    bool stdRenderBabylon_EnsureCasToneMapPipeline()
    {
        if ( !stdRenderBabylon_state.casToneMapPipeline.isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Rgba8, {} } };

        stdRenderBabylon_state.casToneMapPipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !stdRenderBabylon_state.casToneMapPipeline.isNull();
    }

    // Added: the sharpening pass from source (the presented-size LDR image) into target (the
    // swapchain for a null handle). False, having logged nothing, when a resource can't be made or
    // a pass can't be started or ended; the caller then presents without sharpening.
    bool stdRenderBabylon_RecordSharpening(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height)
    {
        if ( source.isNull() || width == 0 || height == 0
            || !stdRenderBabylon_EnsureCasPipeline(target.isNull(), format)
            || !stdRenderBabylon_EnsurePresentSampler() )
        {
            return false;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        const float aParams[4] = { 1.0f / (float)width, 1.0f / (float)height,
            stdRenderBabylon_state.sharpening, 0.0f };
        list.setPipeline(target.isNull() ? stdRenderBabylon_state.casPipeline
                                         : stdRenderBabylon_state.casCapturePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, source,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        return list.endPass();
    }

}

bool stdRenderBabylon_SetSharpening(float strength)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetSharpening: not open.\n");
        return false;
    }

    if ( !std::isfinite(strength) )
    {
        STDLOG_ERROR("stdRenderBabylon_SetSharpening: sharpening %g is not finite.\n",
            (double)strength);
        return false;
    }

    const float clamped = std::clamp(strength, 0.0f, stdRenderBabylon_kMaxSharpening);
    if ( clamped != strength )
    {
        STDLOG_STATUS("stdRenderBabylon: sharpening %g is outside 0..%g; using %g.\n",
            (double)strength, (double)stdRenderBabylon_kMaxSharpening, (double)clamped);
    }

    stdRenderBabylon_state.sharpening      = clamped;
    stdRenderBabylon_state.bCasSetupFailed = false;
    return true;
}

float stdRenderBabylon_GetSharpening()
{
    return stdRenderBabylon_state.sharpening;
}
