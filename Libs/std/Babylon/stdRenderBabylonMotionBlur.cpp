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
// The camera motion-blur stage of the Babylon render adapter.
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
    // Added: the motion-blur stage's scene-sized target, made when its size changes. False, logging
    // why, when it can't be created.
    bool stdRenderBabylon_EnsureMotionBlurTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.motionBlurColorTarget.isNull()
            && stdRenderBabylon_state.motionBlurWidth == width
            && stdRenderBabylon_state.motionBlurHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeMotionBlurTarget();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u motion-blur target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.motionBlurColorTarget = target;
        stdRenderBabylon_state.motionBlurWidth       = width;
        stdRenderBabylon_state.motionBlurHeight      = height;
        return true;
    }

    // Added: the motion-blur pipeline, made on first use.
    bool stdRenderBabylon_EnsureMotionBlurResources()
    {
        if ( stdRenderBabylon_state.bMotionBlurSetupFailed )
        {
            return false;
        }

        if ( stdRenderBabylon_state.motionBlurPipeline.isNull() )
        {
            render::PipelineDesc desc;
            desc.vertexShader   = stdBabylon::shaders::presentVert();
            desc.fragmentShader = stdBabylon::shaders::motionBlurFrag();
            desc.primitive      = render::PrimitiveType::TriangleList;
            desc.colorTargets   = { render::ColorTargetDesc{
                stdRenderBabylon_state.colorFormat, {} } };
            stdRenderBabylon_state.motionBlurPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.motionBlurPipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the motion-blur pass.\n");
            stdRenderBabylon_state.bMotionBlurSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the motion-blur stage's target. Recreated by the next present with the stage on
    // after a scene size or render scale change.
    void stdRenderBabylon_FreeMotionBlurTarget()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.motionBlurColorTarget);
        stdRenderBabylon_state.motionBlurColorTarget = {};
        stdRenderBabylon_state.motionBlurWidth       = 0;
        stdRenderBabylon_state.motionBlurHeight      = 0;
        stdRenderBabylon_state.bMotionBlurRan        = false;
    }

    // Added: the camera motion-blur stage over the scene source (the ambient-occlusion output, or
    // the water-ripple or post-chain output when those stages did not run). It reconstructs each
    // pixel's view position, reprojects it through the previous camera and gathers the scene along
    // the screen-space velocity. bMotionBlurRan is set afterwards so depth of field and the later
    // stages read the blurred image. False, having logged nothing, when the stage is off, inactive
    // or a resource fails.
    bool stdRenderBabylon_RecordMotionBlur(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.motionBlur <= 0
            || !stdRenderBabylon_state.bMotionBlurActive
            || !stdRenderBabylon_state.bHasCameraTransform
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.depthTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull()
            || !stdRenderBabylon_state.bDepthSampled )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureMotionBlurTarget(width, height)
            || !stdRenderBabylon_EnsureMotionBlurResources() )
        {
            return false;
        }

        const StdRenderBabylonMotionBlurLevel& level =
            stdRenderBabylon_aMotionBlurLevels[stdRenderBabylon_state.motionBlur];
        if ( level.taps <= 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.bMotionBlurLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: motion blur on, level %d, %d taps, shutter %g, up to %g px.\n",
                stdRenderBabylon_state.motionBlur, level.taps, (double)level.shutter,
                (double)(level.maxLength * (float)height));
            stdRenderBabylon_state.bMotionBlurLogged = true;
        }

        const render::TextureHandle scene = stdRenderBabylon_state.bSsaoRan
            ? stdRenderBabylon_state.ssaoColorTarget
            : (stdRenderBabylon_state.bWaterRan
                ? stdRenderBabylon_state.waterColorTarget : stdRenderBabylon_ChainSource());

        float tanHalfX = 0.0f;
        stdRenderBabylon_SsaoProjection(width, height, &tanHalfX, nullptr);

        const float nearPlane = std::max(stdRenderBabylon_state.projectionNearPlane, 1.0e-5f);
        const float farPlane  = std::max(stdRenderBabylon_state.projectionFarPlane,
            nearPlane + 1.0e-3f);

        float aCameras[24];
        std::memcpy(aCameras, stdRenderBabylon_state.aMotionBlurCurrent,
            sizeof(stdRenderBabylon_state.aMotionBlurCurrent));
        std::memcpy(aCameras + 12, stdRenderBabylon_state.aMotionBlurPrevious,
            sizeof(stdRenderBabylon_state.aMotionBlurPrevious));

        const float aParams0[4] = { nearPlane, farPlane, level.shutter,
            level.maxLength * (float)height };
        const float aParams1[4] = { (float)width, (float)height, tanHalfX, (float)level.taps };
        const float aParams2[4] = { level.depthFalloff, 0.0f, 0.0f, 0.0f };

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.motionBlurColorTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(stdRenderBabylon_state.motionBlurPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.depthTarget,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, stdRenderBabylon_state.normalTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aCameras, 24u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aParams0, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 2,
            std::as_bytes(std::span<const float>(aParams1, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 3,
            std::as_bytes(std::span<const float>(aParams2, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bMotionBlurRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetMotionBlur(int level)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetMotionBlur: not open.\n");
        return false;
    }

    const int clamped = std::clamp(level, 0, stdRenderBabylon_kMotionBlurMaxLevel);
    if ( clamped != level )
    {
        STDLOG_STATUS("stdRenderBabylon: motion-blur level %d is outside 0..%d; using %d.\n",
            level, stdRenderBabylon_kMotionBlurMaxLevel, clamped);
    }

    stdRenderBabylon_state.motionBlur             = clamped;
    stdRenderBabylon_state.bMotionBlurSetupFailed = false;
    stdRenderBabylon_state.bMotionBlurRan         = false;
    stdRenderBabylon_state.bMotionBlurLogged      = false;
    // A new level starts a fresh history, so the first frame after the change has no blur.
    stdRenderBabylon_state.bMotionBlurActive      = false;
    stdRenderBabylon_state.bHasCameraTransform    = false;
    return true;
}

int stdRenderBabylon_GetMotionBlur()
{
    return stdRenderBabylon_state.motionBlur;
}
