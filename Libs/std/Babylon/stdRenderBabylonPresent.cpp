// The frame submit, final pass and presentation of the Babylon render adapter.
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

#ifdef J3D_TEST
namespace stdRenderBabylonInternal
{
    bool stdRenderBabylon_bLegacyPresentationForTest = false;
}
#endif

namespace
{
    // Added: the size of the presented frame in pixels: the window's drawable size, or the game's
    // display size without a window. The anti-aliasing targets match it.
    void stdRenderBabylon_GetPresentedSize(uint32_t* pWidth, uint32_t* pHeight)
    {
        uint32_t width  = stdRenderBabylon_state.modeWidth;
        uint32_t height = stdRenderBabylon_state.modeHeight;

        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( pContext && pContext->pWindow )
        {
            const babylon::platform::Size size = pContext->pWindow->size();
            if ( size.width > 0 && size.height > 0 )
            {
                width  = static_cast<std::uint32_t>(size.width);
                height = static_cast<std::uint32_t>(size.height);
            }
        }

        *pWidth  = width;
        *pHeight = height;
    }

    // The submit of SubmitFrame and FlushFrame; bPresent records the present pass first.
    bool stdRenderBabylon_Submit(bool bPresent)
    {
        if ( !stdRenderBabylon_state.frame )
        {
            return true;
        }

        render::CommandList& list = *stdRenderBabylon_state.frame;
        bool bSubmitted = false;
        if ( list.inPass() )
        {
            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: a pass is still open, the frame is dropped.\n");
        }
        else
        {
            // Added: remember whether this frame drew the HUD layer, for the composite below and for
            // a later RenderPresentToTexture; a frame without a HUD pass leaves no HUD behind.
            stdRenderBabylon_state.bHudLayerDrawn = stdRenderBabylon_DrawsHudLayer(list);

            if ( bPresent )
            {
                stdRenderBabylon_RecordPostChain(list);

                // Added: the water-ripple stage after the post chain and before depth of field. Off,
                // or a failure, skips it so the image is byte-identical to the path without it.
                stdRenderBabylon_state.bWaterRan = false;
                if ( stdRenderBabylon_state.bWaterRipples
                    && !stdRenderBabylon_state.bWaterSetupFailed
                    && !stdRenderBabylon_RecordWaterRipples(list) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the water-ripple stage; presenting without it.\n");
                    stdRenderBabylon_state.bWaterSetupFailed = true;
                }

                // Added: the screen-space contact-shadow stage after the water ripples and before
                // ambient occlusion. Off, without a camera transform, or a failure, skips the pass
                // so the image is byte-identical to the path without it.
                stdRenderBabylon_state.bContactShadowsRan = false;
                if ( stdRenderBabylon_state.bContactShadows
                    && !stdRenderBabylon_state.bContactShadowsSetupFailed
                    && stdRenderBabylon_state.bHasCameraTransform
                    && !stdRenderBabylon_RecordContactShadows(list) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the contact-shadow stage; presenting without it.\n");
                    stdRenderBabylon_state.bContactShadowsSetupFailed = true;
                }

                // Added: the screen-space ambient-occlusion stage after the contact shadows and
                // before depth of field. Off, or a failure, skips every pass so the image is
                // byte-identical to the path without it.
                stdRenderBabylon_state.bSsaoRan = false;
                if ( stdRenderBabylon_state.ssao > 0
                    && !stdRenderBabylon_state.bSsaoSetupFailed
                    && !stdRenderBabylon_RecordSsao(list) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the ambient-occlusion stage; presenting without it.\n");
                    stdRenderBabylon_state.bSsaoSetupFailed = true;
                }

                // Added: the camera motion-blur stage after ambient occlusion and before depth of
                // field. Off, inactive (a cut, a paused frame, a teleport or a snap) or a failure
                // skips every pass so the image is byte-identical to the path without it.
                stdRenderBabylon_state.bMotionBlurRan = false;
                if ( stdRenderBabylon_state.motionBlur > 0
                    && stdRenderBabylon_state.bMotionBlurActive
                    && stdRenderBabylon_state.bHasCameraTransform
                    && !stdRenderBabylon_state.bMotionBlurSetupFailed
                    && !stdRenderBabylon_RecordMotionBlur(list) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the motion-blur stage; presenting without it.\n");
                    stdRenderBabylon_state.bMotionBlurSetupFailed = true;
                }

                // Added: the depth-of-field stage on the HDR scene, after the post chain and before
                // bloom. Both Options rows off, or no focus set this run, skips it entirely so the
                // image is byte-identical. A failure is logged once and the scene is presented
                // without it.
                stdRenderBabylon_state.bDofRan = false;
                if ( !stdRenderBabylon_state.bDofSetupFailed
                    && !stdRenderBabylon_RecordDoF(list)
                    && stdRenderBabylon_state.bHasFocus
                    && (stdRenderBabylon_state.bFocusCutscene
                        ? stdRenderBabylon_state.bDofCutscenes
                        : stdRenderBabylon_state.bDofGameplay) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the depth-of-field stage; presenting without it.\n");
                    stdRenderBabylon_state.bDofSetupFailed = true;
                }

                // Added: the built-in light-shaft stage after depth of field and before bloom, and
                // the lens-flare stage after it. The flare reuses the shaft stage's mask, centroid
                // and smoothed strength, so those run when either option needs them; only the
                // shaft blur and composite are skipped when light shafts are off. Off, or a
                // failure, skips the rest so the image is byte-identical to the path without it.
                stdRenderBabylon_state.bShaftsRan = false;
                stdRenderBabylon_state.bFlareRan  = false;
                const bool bShaftsNeeded = stdRenderBabylon_state.lightShaftStrength > 0.0f
                    && !stdRenderBabylon_state.bShaftsSetupFailed;
                const bool bFlareNeeded = stdRenderBabylon_state.bLensFlare
                    && !stdRenderBabylon_state.bFlareSetupFailed;
                if ( bShaftsNeeded || bFlareNeeded )
                {
                    const bool bShaftLight = !stdRenderBabylon_state.bShaftsSetupFailed
                        && stdRenderBabylon_RecordShaftMaskCentroid(list);
                    if ( !bShaftLight )
                    {
                        if ( bShaftsNeeded )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the light-shaft stage; presenting without it.\n");
                            stdRenderBabylon_state.bShaftsSetupFailed = true;
                        }
                        if ( bFlareNeeded )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the lens-flare stage; presenting without it.\n");
                            stdRenderBabylon_state.bFlareSetupFailed = true;
                        }
                    }
                    else
                    {
                        if ( bShaftsNeeded
                            && !stdRenderBabylon_RecordShaftComposite(list) )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the light-shaft stage; presenting without it.\n");
                            stdRenderBabylon_state.bShaftsSetupFailed = true;
                        }
                        if ( bFlareNeeded
                            && !stdRenderBabylon_RecordLensFlare(list) )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the lens-flare stage; presenting without it.\n");
                            stdRenderBabylon_state.bFlareSetupFailed = true;
                        }
                    }
                }

                // Added: the built-in bloom stage before the tone map. When its resources or passes
                // can't be made it is skipped for this and later presents, so the scene is shown
                // without it. Clearing the flag here also keeps a frame without bloom from reading
                // an earlier composite.
                stdRenderBabylon_state.bBloomRan = false;
                if ( stdRenderBabylon_state.bloom > 0.0f
                    && !stdRenderBabylon_state.bBloomSetupFailed
                    && !stdRenderBabylon_RecordBloom(list) )
                {
                    STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the bloom stage; presenting without it.\n");
                    stdRenderBabylon_state.bBloomSetupFailed = true;
                }

                if ( stdRenderBabylon_state.pDevice->info().hasSwapchain )
                {
                    const render::TextureFormat swapchainFormat =
                        stdRenderBabylon_state.pDevice->info().swapchainFormat;
                    uint32_t width  = 0;
                    uint32_t height = 0;
                    stdRenderBabylon_GetPresentedSize(&width, &height);

                    // Added: sharpening writes its own presented-size source, after SMAA or the
                    // tone map. When its resources can't be made, present without it.
                    bool bSharpen = stdRenderBabylon_state.sharpening > 0.0f
                        && !stdRenderBabylon_state.bCasSetupFailed;
                    if ( bSharpen
                        && ( !stdRenderBabylon_EnsureCasTarget(width, height)
                            || !stdRenderBabylon_EnsureCasToneMapPipeline() ) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not set up sharpening; presenting without it.\n");
                        stdRenderBabylon_state.bCasSetupFailed = true;
                        bSharpen = false;
                    }

                    // Added: the vignette and film grain write after SMAA and sharpening, into their
                    // own Bgra8 source, and the Babylon postfx effects then write the swapchain. Both
                    // off skip the target and passes, so the existing path is byte-identical.
                    bool bFx = ( stdRenderBabylon_state.bVignette
                            || stdRenderBabylon_state.bFilmGrain )
                        && !stdRenderBabylon_state.bFxSetupFailed;
                    if ( bFx
                        && ( !stdRenderBabylon_EnsureFxTarget(width, height)
                            || !stdRenderBabylon_EnsureFxToneMapPipeline()
                            || !stdRenderBabylon_EnsurePresentSampler() ) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not set up the camera effects; presenting without them.\n");
                        stdRenderBabylon_state.bFxSetupFailed = true;
                        bFx = false;
                    }

                    const render::TextureHandle chainTarget =
                        bFx ? stdRenderBabylon_state.fxSourceTarget : render::TextureHandle{};
                    const render::TextureFormat chainFormat =
                        bFx ? render::TextureFormat::Bgra8 : swapchainFormat;

                    bool bPresented = false;
                    if ( stdRenderBabylon_state.bAntiAliasing )
                    {
                        bPresented = stdRenderBabylon_RecordSmaa(list,
                            bSharpen ? stdRenderBabylon_state.casSourceTarget : chainTarget,
                            bSharpen ? render::TextureFormat::Rgba8 : chainFormat,
                            width, height, bSharpen);
                        if ( !bPresented )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the anti-aliasing passes; presenting without it.\n");
                        }
                    }

                    bool bChainWritten = bPresented;
                    if ( !bPresented )
                    {
                        if ( bSharpen )
                        {
                            bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                                stdRenderBabylon_state.casToneMapPipeline,
                                stdRenderBabylon_state.casSourceTarget)
                                && stdRenderBabylon_RecordSharpening(list,
                                    stdRenderBabylon_state.casSourceTarget, chainTarget,
                                    chainFormat, width, height);
                            if ( !bChainWritten )
                            {
                                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the sharpening pass; presenting without it.\n");
                            }
                        }

                        if ( !bChainWritten && bSharpen )
                        {
                            // Sharpening failed, so present the scene without it.
                            bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                                bFx ? stdRenderBabylon_state.fxToneMapPipeline
                                    : stdRenderBabylon_state.presentPipeline,
                                chainTarget);
                            if ( !bChainWritten )
                            {
                                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                            }
                        }
                        else if ( !bChainWritten && !bSharpen )
                        {
                            bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                                bFx ? stdRenderBabylon_state.fxToneMapPipeline
                                    : stdRenderBabylon_state.presentPipeline,
                                chainTarget);
                            if ( !bChainWritten )
                            {
                                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                            }
                        }
                    }
                    else if ( bSharpen
                        && !stdRenderBabylon_RecordSharpening(list,
                            stdRenderBabylon_state.casSourceTarget, chainTarget,
                            chainFormat, width, height) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the sharpening pass; presenting without it.\n");
                        bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                            bFx ? stdRenderBabylon_state.fxToneMapPipeline
                                : stdRenderBabylon_state.presentPipeline,
                            chainTarget);
                        if ( !bChainWritten )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                        }
                    }

                    if ( bFx && bChainWritten
                        && !stdRenderBabylon_RecordCameraFx(list,
                            stdRenderBabylon_state.fxSourceTarget, render::TextureHandle{},
                            swapchainFormat, width, height) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the camera effects.\n");
                    }

                    // Added: composite the HUD layer over the tone-mapped scene, after the scene's
                    // post chain, so no scene effect touches the HUD. It runs before the prompt and
                    // menu overlays.
                    if ( stdRenderBabylon_state.bHudLayerDrawn
                        && !stdRenderBabylon_RecordHudComposite(list, render::TextureHandle{},
                            stdRenderBabylon_state.pDevice->info().swapchainFormat) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the HUD composite.\n");
                    }
                }

                if ( stdRenderBabylon_state.hudOverlay || stdRenderBabylon_state.presentOverlay )
                {
                    StdRenderBabylonOverlayTarget target;
                    target.texture = {};
                    target.format  = stdRenderBabylon_state.pDevice->info().hasSwapchain
                        ? stdRenderBabylon_state.pDevice->info().swapchainFormat
                        : render::TextureFormat::Bgra8;
                    // Without a window the overlay's native size is the game's display size, not
                    // the scaled scene target.
                    target.width  = stdRenderBabylon_state.modeWidth;
                    target.height = stdRenderBabylon_state.modeHeight;

                    StdBabylonContext* pContext = stdBabylon_GetContext();
                    if ( pContext && pContext->pWindow )
                    {
                        const babylon::platform::Size size = pContext->pWindow->size();
                        target.width  = size.width > 0 ? static_cast<std::uint32_t>(size.width) : 0;
                        target.height = size.height > 0 ? static_cast<std::uint32_t>(size.height) : 0;
                    }

                    if ( target.width > 0 && target.height > 0 )
                    {
                        if ( stdRenderBabylon_state.hudOverlay )
                        {
                            static_cast<void>(stdRenderBabylon_state.hudOverlay(
                                stdRenderBabylon_state.hudOverlayUser, list, target));
                        }

                        if ( stdRenderBabylon_state.presentOverlay )
                        {
                            static_cast<void>(stdRenderBabylon_state.presentOverlay(
                                stdRenderBabylon_state.presentOverlayUser, list, target));
                        }
                    }
                }
            }

            const bool bDrawsBackBuffer = stdRenderBabylon_DrawsBackBuffer(list);
            bSubmitted = stdRenderBabylon_state.pDevice->submit(list);
            if ( !bSubmitted )
            {
                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: the device refused the frame.\n");
            }
            else if ( bDrawsBackBuffer )
            {
                ++stdRenderBabylon_state.backBufferVersion;
            }
        }

        stdRenderBabylon_state.frame.reset();
        stdRenderBabylon_DestroyDeferred();
        return bSubmitted;
    }

}

namespace stdRenderBabylonInternal
{
    // The final pass: the presented source drawn over the whole target (the null handle, for the
    // swapchain), with the tone map when it runs. The vertex shader makes the triangle itself, so
    // nothing is bound but the result and the input. Returns false, having logged nothing, when the
    // pass can't be started or ended; the caller decides what that means.
    bool stdRenderBabylon_RecordFinalPass(render::CommandList& list, render::PipelineHandle pipeline,
        render::TextureHandle target)
    {
        if ( pipeline.isNull() || stdRenderBabylon_state.colorTarget.isNull() )
        {
            return false;
        }
        if ( stdRenderBabylon_state.bToneMap && !stdRenderBabylon_EnsureColorGradingLut() )
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

        // At render scale 1 the present is the pre-M9.4 1:1 nearest sample; at any other scale the
        // final pass resolves the scaled scene into the presented size with linear filtering.
        const render::SamplerHandle sampler = stdRenderBabylon_state.renderScale == 1.0f
            ? stdRenderBabylon_state.presentSampler
            : stdRenderBabylon_state.finalSampler;
        list.setPipeline(pipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_PresentSource(), sampler);
        if ( stdRenderBabylon_state.bToneMap )
        {
            const float aParams[4] = { stdRenderBabylon_state.toneMap.exposure,
                (float)stdRenderBabylon_state.toneMap.oper,
                stdRenderBabylon_state.toneMap.brightness,
                (float)stdRenderBabylon_state.toneMap.colorGrading };
            list.pushUniforms(render::ShaderStage::Fragment, 0,
                std::as_bytes(std::span<const float>(aParams)));
            // The grading lookup is a linear-sampled strip; preset 0 skips it in the shader.
            list.bindTexture(render::ShaderStage::Fragment, 1,
                stdRenderBabylon_state.colorGradingLut, stdRenderBabylon_state.finalSampler);
        }
        list.draw(3);
        return list.endPass();
    }

}


#ifdef J3D_TEST
void stdRenderBabylon_ForceLegacyPresentationForTest(bool bLegacy)
{
    stdRenderBabylon_bLegacyPresentationForTest = bLegacy;
}

#endif

bool stdRenderBabylon_SubmitFrame()
{
    return stdRenderBabylon_Submit(true);
}

bool stdRenderBabylon_FlushFrame()
{
    return stdRenderBabylon_Submit(false);
}

bool stdRenderBabylon_PresentFrame()
{
    (void)stdRenderBabylon_GetFrame();
    return stdRenderBabylon_Submit(true);
}

void stdRenderBabylon_SetPresentOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser)
{
    stdRenderBabylon_state.presentOverlay     = pfn;
    stdRenderBabylon_state.presentOverlayUser = pfn ? pUser : nullptr;
}

bool stdRenderBabylon_HasPresentOverlay()
{
    return stdRenderBabylon_state.presentOverlay != nullptr;
}

bool stdRenderBabylon_RenderPresentToTexture(render::TextureHandle target,
    std::uint32_t width, std::uint32_t height)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: not open.\n");
        return false;
    }
    if ( stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: no back buffer.\n");
        return false;
    }
    if ( stdRenderBabylon_state.frame )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: a frame is being recorded.\n");
        return false;
    }
    if ( target.isNull() || width == 0 || height == 0 )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: bad target or size %ux%u.\n",
            width, height);
        return false;
    }
    if ( stdRenderBabylon_state.capturePipeline.isNull() )
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, {} } };
        stdRenderBabylon_state.capturePipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
    }
    if ( stdRenderBabylon_state.capturePipeline.isNull()
        || !stdRenderBabylon_EnsurePresentSampler() || !stdRenderBabylon_EnsureFinalSampler() )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: no final pass pipeline.\n");
        return false;
    }

    render::CommandList list = stdRenderBabylon_state.pDevice->beginFrame();

    // Added: sharpening into the capture target through its own presented-size source, after SMAA
    // or the tone map. When its resources can't be made, capture without it.
    bool bSharpen = stdRenderBabylon_state.sharpening > 0.0f
        && !stdRenderBabylon_state.bCasSetupFailed;
    if ( bSharpen
        && ( !stdRenderBabylon_EnsureCasTarget(width, height)
            || !stdRenderBabylon_EnsureCasToneMapPipeline() ) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not set up sharpening; capturing without it.\n");
        stdRenderBabylon_state.bCasSetupFailed = true;
        bSharpen = false;
    }

    // Added: the vignette and film grain write after SMAA and sharpening, into their own Bgra8
    // source, and the Babylon postfx effects then write the capture target. Both off skip the
    // target and passes, so the existing capture path is byte-identical.
    bool bFx = ( stdRenderBabylon_state.bVignette || stdRenderBabylon_state.bFilmGrain )
        && !stdRenderBabylon_state.bFxSetupFailed;
    if ( bFx
        && ( !stdRenderBabylon_EnsureFxTarget(width, height)
            || !stdRenderBabylon_EnsureFxToneMapPipeline()
            || !stdRenderBabylon_EnsurePresentSampler() ) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not set up the camera effects; capturing without them.\n");
        stdRenderBabylon_state.bFxSetupFailed = true;
        bFx = false;
    }

    const render::TextureHandle chainTarget =
        bFx ? stdRenderBabylon_state.fxSourceTarget : target;

    bool bPresented = false;
    if ( stdRenderBabylon_state.bAntiAliasing )
    {
        bPresented = stdRenderBabylon_RecordSmaa(list,
            bSharpen ? stdRenderBabylon_state.casSourceTarget : chainTarget,
            bSharpen ? render::TextureFormat::Rgba8 : render::TextureFormat::Bgra8,
            width, height, bSharpen);
        if ( !bPresented )
        {
            STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the anti-aliasing passes; presenting without it.\n");
        }
    }

    bool bChainWritten = bPresented;
    if ( !bPresented )
    {
        if ( bSharpen )
        {
            bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                stdRenderBabylon_state.casToneMapPipeline,
                stdRenderBabylon_state.casSourceTarget)
                && stdRenderBabylon_RecordSharpening(list,
                    stdRenderBabylon_state.casSourceTarget, chainTarget,
                    render::TextureFormat::Bgra8, width, height);
            if ( !bChainWritten )
            {
                STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the sharpening pass; capturing without it.\n");
            }
        }

        if ( !bChainWritten )
        {
            bChainWritten = stdRenderBabylon_RecordFinalPass(list,
                bFx ? stdRenderBabylon_state.fxToneMapPipeline
                    : stdRenderBabylon_state.capturePipeline,
                chainTarget);
            if ( !bChainWritten )
            {
                STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the target refused the final pass.\n");
                return false;
            }
        }
    }
    else if ( bSharpen
        && !stdRenderBabylon_RecordSharpening(list, stdRenderBabylon_state.casSourceTarget,
            chainTarget, render::TextureFormat::Bgra8, width, height) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the sharpening pass; capturing without it.\n");
        if ( !stdRenderBabylon_RecordFinalPass(list,
            bFx ? stdRenderBabylon_state.fxToneMapPipeline
                : stdRenderBabylon_state.capturePipeline, chainTarget) )
        {
            STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the target refused the final pass.\n");
            return false;
        }
    }

    if ( bFx && !stdRenderBabylon_RecordCameraFx(list,
            stdRenderBabylon_state.fxSourceTarget, target,
            render::TextureFormat::Bgra8, width, height) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the camera effects.\n");
        return false;
    }

    // Added: the captured presented frame includes the HUD layer, before the prompt and menu
    // overlays, like the window.
    if ( !stdRenderBabylon_RecordHudComposite(list, target, render::TextureFormat::Bgra8) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the HUD composite.\n");
        return false;
    }

    const StdRenderBabylonOverlayTarget overlayTarget{ target,
        render::TextureFormat::Bgra8, width, height };
    if ( stdRenderBabylon_state.hudOverlay )
    {
        static_cast<void>(stdRenderBabylon_state.hudOverlay(
            stdRenderBabylon_state.hudOverlayUser, list, overlayTarget));
    }

    if ( stdRenderBabylon_state.presentOverlay )
    {
        static_cast<void>(stdRenderBabylon_state.presentOverlay(
            stdRenderBabylon_state.presentOverlayUser, list, overlayTarget));
    }

    if ( !stdRenderBabylon_state.pDevice->submit(list) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the device refused the frame.\n");
        return false;
    }

    return true;
}

render::TextureHandle stdRenderBabylon_GetPresentSource()
{
    return stdRenderBabylon_PresentSource();
}
