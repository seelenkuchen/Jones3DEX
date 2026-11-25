// The depth-of-field stage and its focus tracking in the Babylon render adapter.
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
    constexpr float stdRenderBabylon_kDofFocusSmoothingTime = 0.2f; // seconds to reach a new focus
    constexpr float stdRenderBabylon_kDofMaxRadius          = 6.0f; // half-res texels at strength 1
    constexpr float stdRenderBabylon_kDofGameplayStartScale = 6.0f;
    constexpr float stdRenderBabylon_kDofGameplayMinStart    = 1.5f;
    constexpr float stdRenderBabylon_kDofGameplaySpanScale  = 24.0f;
    constexpr float stdRenderBabylon_kDofGameplayMinSpan     = 6.0f;
    constexpr float stdRenderBabylon_kDofCutsceneDeadScale  = 0.25f;
    constexpr float stdRenderBabylon_kDofCutsceneSpanScale  = 1.2f;
    constexpr float stdRenderBabylon_kDofCutsceneMinDead     = 0.1f;
    constexpr float stdRenderBabylon_kDofCutsceneMinSpan     = 1.0f;

    // Added: the half-res gather target and the scene-sized composite target, made when their size
    // changes. False, logging why, when one can't be created.
    bool stdRenderBabylon_EnsureDoFTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.dofBlurTarget.isNull()
            && stdRenderBabylon_state.dofWidth == width
            && stdRenderBabylon_state.dofHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeDoFTargets();

        render::TextureDesc desc;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        desc.width  = std::max(1u, (width + 1u) / 2u);
        desc.height = std::max(1u, (height + 1u) / 2u);
        const render::TextureHandle blur = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( blur.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u depth-of-field blur target.\n",
                desc.width, desc.height);
            stdRenderBabylon_FreeDoFTargets();
            return false;
        }

        desc.width  = width;
        desc.height = height;
        const render::TextureHandle composite = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( composite.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u depth-of-field composite.\n",
                width, height);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            stdRenderBabylon_FreeDoFTargets();
            return false;
        }

        stdRenderBabylon_state.dofBlurTarget  = blur;
        stdRenderBabylon_state.dofColorTarget = composite;
        stdRenderBabylon_state.dofWidth       = width;
        stdRenderBabylon_state.dofHeight      = height;
        return true;
    }

    // Added: the depth-of-field pipelines, made on first use.
    bool stdRenderBabylon_EnsureDoFResources()
    {
        if ( stdRenderBabylon_state.bDofSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

        if ( stdRenderBabylon_state.dofGatherPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::dofGatherFrag();
            stdRenderBabylon_state.dofGatherPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.dofCompositePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::dofCompositeFrag();
            stdRenderBabylon_state.dofCompositePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.dofGatherPipeline.isNull()
            || stdRenderBabylon_state.dofCompositePipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the depth-of-field passes.\n");
            stdRenderBabylon_state.bDofSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the depth-of-field stage's targets. Recreated by the next present with the stage
    // active after a scene size or render scale change.
    void stdRenderBabylon_FreeDoFTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.dofBlurTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.dofColorTarget);
        stdRenderBabylon_state.dofBlurTarget  = {};
        stdRenderBabylon_state.dofColorTarget = {};
        stdRenderBabylon_state.dofWidth       = 0;
        stdRenderBabylon_state.dofHeight      = 0;
        stdRenderBabylon_state.bDofRan        = false;
    }

    // Added: the depth-of-field stage over the post chain's output. It gathers a circle-of-confusion
    // weighted blur into a half-res target and composites it back over the scene, so the tone map
    // and bloom read the result. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordDoF(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bHasFocus || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.depthTarget.isNull() || !stdRenderBabylon_state.bDepthSampled )
        {
            return false;
        }

        const bool bCutscene = stdRenderBabylon_state.bFocusCutscene;
        if ( bCutscene ? !stdRenderBabylon_state.bDofCutscenes : !stdRenderBabylon_state.bDofGameplay )
        {
            return false;
        }

        if ( !stdRenderBabylon_EnsureDoFResources()
            || !stdRenderBabylon_EnsureDoFTargets(stdRenderBabylon_state.width,
                stdRenderBabylon_state.height) )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.bDofLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: depth of field on, %s, strength %g, focus %g.\n",
                bCutscene ? "cutscenes" : "gameplay", (double)stdRenderBabylon_state.dofStrength,
                (double)stdRenderBabylon_state.focusDistance);
            stdRenderBabylon_state.bDofLogged = true;
        }

        const render::TextureHandle scene = stdRenderBabylon_state.bMotionBlurRan
            ? stdRenderBabylon_state.motionBlurColorTarget
            : (stdRenderBabylon_state.bSsaoRan
                ? stdRenderBabylon_state.ssaoColorTarget
                : (stdRenderBabylon_state.bWaterRan
                    ? stdRenderBabylon_state.waterColorTarget : stdRenderBabylon_ChainSource()));
        const render::TextureHandle blur  = stdRenderBabylon_state.dofBlurTarget;
        const uint32_t blurWidth  = std::max(1u, (stdRenderBabylon_state.width + 1u) / 2u);
        const uint32_t blurHeight = std::max(1u, (stdRenderBabylon_state.height + 1u) / 2u);
        const float aDof[4] = { stdRenderBabylon_state.focusDistance,
            stdRenderBabylon_state.focusNearPlane, stdRenderBabylon_state.focusFarPlane,
            bCutscene ? 1.0f : 0.0f };
        const float aParams[4] = { stdRenderBabylon_state.dofStrength,
            stdRenderBabylon_kDofMaxRadius, 1.0f / (float)blurWidth, 1.0f / (float)blurHeight };

        // Added: the circle of confusion's ramp. In gameplay x is where the blur starts and y its
        // span; in cutscene mode x is the dead zone around the focus and y its span. Keeping the
        // tuning here means the shaders carry no magic numbers of their own.
        const float focus = stdRenderBabylon_state.focusDistance;
        const float ramp  = bCutscene
            ? std::max(focus * stdRenderBabylon_kDofCutsceneDeadScale, stdRenderBabylon_kDofCutsceneMinDead)
            : std::max(focus * stdRenderBabylon_kDofGameplayStartScale, stdRenderBabylon_kDofGameplayMinStart);
        const float span  = bCutscene
            ? std::max(focus * stdRenderBabylon_kDofCutsceneSpanScale, stdRenderBabylon_kDofCutsceneMinSpan)
            : std::max(focus * stdRenderBabylon_kDofGameplaySpanScale, stdRenderBabylon_kDofGameplayMinSpan);
        const float aCoC[4] = { ramp, span, 0.0f, 0.0f };

        render::PassDesc gatherPass;
        gatherPass.colorTargets.push_back(render::ColorAttachment{ blur, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(gatherPass) )
        {
            return false;
        }

        list.setPipeline(stdRenderBabylon_state.dofGatherPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.depthTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aDof, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aParams, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 2,
            std::as_bytes(std::span<const float>(aCoC, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        render::PassDesc compositePass;
        compositePass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.dofColorTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(compositePass) )
        {
            return false;
        }

        list.setPipeline(stdRenderBabylon_state.dofCompositePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, blur,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, stdRenderBabylon_state.depthTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aDof, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aCoC, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bDofRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetDepthOfField(bool bCutscenes, bool bGameplay, float strength)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetDepthOfField: not open.\n");
        return false;
    }

    if ( !std::isfinite(strength) )
    {
        STDLOG_ERROR("stdRenderBabylon_SetDepthOfField: strength %g is not finite.\n",
            (double)strength);
        return false;
    }

    const float clamped = std::clamp(strength, stdRenderBabylon_kMinDofStrength,
        stdRenderBabylon_kMaxDofStrength);
    if ( clamped != strength )
    {
        STDLOG_STATUS("stdRenderBabylon: depth of field strength %g is outside %g..%g; using %g.\n",
            (double)strength, (double)stdRenderBabylon_kMinDofStrength,
            (double)stdRenderBabylon_kMaxDofStrength, (double)clamped);
    }

    stdRenderBabylon_state.bDofCutscenes  = bCutscenes;
    stdRenderBabylon_state.bDofGameplay   = bGameplay;
    stdRenderBabylon_state.dofStrength    = clamped;
    stdRenderBabylon_state.bDofSetupFailed = false;
    stdRenderBabylon_state.bDofRan         = false;
    stdRenderBabylon_state.bDofLogged      = false;
    return true;
}

bool stdRenderBabylon_GetDepthOfFieldCutscenes()
{
    return stdRenderBabylon_state.bDofCutscenes;
}

bool stdRenderBabylon_GetDepthOfFieldGameplay()
{
    return stdRenderBabylon_state.bDofGameplay;
}

float stdRenderBabylon_GetDepthOfFieldStrength()
{
    return stdRenderBabylon_state.dofStrength;
}

void stdRenderBabylon_SetFocus(float distance, bool bCutscene, float nearPlane, float farPlane,
    float deltaTime)
{
    if ( !stdRenderBabylon_state.pDevice || !std::isfinite(distance) )
    {
        return;
    }

    const float clamped = std::max(distance, 0.0f);
    stdRenderBabylon_state.bFocusCutscene = bCutscene;
    stdRenderBabylon_state.focusNearPlane = std::isfinite(nearPlane) ? nearPlane : 0.1f;
    stdRenderBabylon_state.focusFarPlane  = std::isfinite(farPlane) ? farPlane : 1000.0f;
    stdRenderBabylon_state.focusTarget    = clamped;

    if ( !stdRenderBabylon_state.bHasFocus )
    {
        stdRenderBabylon_state.focusDistance = clamped;
        stdRenderBabylon_state.bHasFocus     = true;
        return;
    }

    // Added: an exponential approach whose remaining gap falls to about 2% over the first 0.2 s of
    // game time, so the transition is frame-rate independent and settles quickly. A non-positive
    // step snaps instead (a paused or reset clock).
    const float alpha = deltaTime > 0.0f
        ? 1.0f - std::exp(-4.0f * deltaTime / stdRenderBabylon_kDofFocusSmoothingTime)
        : 1.0f;
    stdRenderBabylon_state.focusDistance +=
        (stdRenderBabylon_state.focusTarget - stdRenderBabylon_state.focusDistance) * alpha;
}

float stdRenderBabylon_GetFocusDistance()
{
    return stdRenderBabylon_state.focusDistance;
}
