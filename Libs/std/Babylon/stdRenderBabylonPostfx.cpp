// The Babylon postfx objects behind the adapter's M9.18 vignette and film grain.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/postfx/Effect.h>
#include <babylon/postfx/FilmGrainEffect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/TargetPool.h>
#include <babylon/postfx/VignetteEffect.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <std/General/std.h>

#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
namespace postfx = babylon::postfx;
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
    // Added: the Babylon postfx objects the M9.18 camera effects draw with. A failure leaves them
    // unavailable, so the frame is presented without the effects, not failed.
    bool stdRenderBabylon_OpenPostfx()
    {
        if ( !stdRenderBabylon_state.pDevice )
        {
            return false;
        }

        std::unique_ptr<postfx::Resources> pResources =
            std::make_unique<postfx::Resources>(*stdRenderBabylon_state.pDevice);
        if ( !pResources->open() )
        {
            STDLOG_ERROR("stdRenderBabylon_OpenPostfx: could not open the postfx resources; presenting without the camera effects.\n");
            stdRenderBabylon_state.bFxSetupFailed = true;
            return false;
        }

        stdRenderBabylon_state.pPostfxResources = std::move(pResources);
        stdRenderBabylon_state.pPostfxTargets =
            std::make_unique<postfx::TargetPool>(*stdRenderBabylon_state.pDevice);
        stdRenderBabylon_state.pPostfxVignette = std::make_unique<postfx::VignetteEffect>();
        stdRenderBabylon_state.pPostfxGrain    = std::make_unique<postfx::FilmGrainEffect>();
        return true;
    }

    // Added: releases the effects and their pooled targets, then the shared resources. Safe when
    // nothing was made.
    void stdRenderBabylon_ClosePostfx()
    {
        if ( stdRenderBabylon_state.pPostfxVignette )
        {
            stdRenderBabylon_state.pPostfxVignette->releaseResources();
        }
        if ( stdRenderBabylon_state.pPostfxGrain )
        {
            stdRenderBabylon_state.pPostfxGrain->releaseResources();
        }
        if ( stdRenderBabylon_state.pPostfxTargets )
        {
            stdRenderBabylon_state.pPostfxTargets->releaseAll();
        }
        if ( stdRenderBabylon_state.pPostfxResources )
        {
            stdRenderBabylon_state.pPostfxResources->close();
        }

        stdRenderBabylon_state.pPostfxVignette.reset();
        stdRenderBabylon_state.pPostfxGrain.reset();
        stdRenderBabylon_state.pPostfxTargets.reset();
        stdRenderBabylon_state.pPostfxResources.reset();
    }

    // Added: records the M9.18 vignette and film grain with the Babylon postfx effects, from the
    // presented-size Bgra8 source into target (the swapchain for a null handle). With both on the
    // vignette writes a pooled scratch target and the grain reads it, so the passes stay one effect
    // each. False when the effects are unavailable or a pass can't be recorded.
    bool stdRenderBabylon_RecordCameraFx(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height)
    {
        if ( source.isNull() || width == 0 || height == 0
            || !stdRenderBabylon_state.pPostfxResources
            || !stdRenderBabylon_state.pPostfxTargets
            || !stdRenderBabylon_state.pPostfxVignette
            || !stdRenderBabylon_state.pPostfxGrain )
        {
            return false;
        }

        postfx::VignetteSettings vignette;
        vignette.strength = stdRenderBabylon_state.bVignette
            ? stdRenderBabylon_kVignetteStrength : 0.0f;
        vignette.inner = stdRenderBabylon_kVignetteInner;
        vignette.outer = stdRenderBabylon_kVignetteOuter;
        stdRenderBabylon_state.pPostfxVignette->setSettings(vignette);

        postfx::FilmGrainSettings grain;
        grain.amplitude = stdRenderBabylon_state.bFilmGrain
            ? stdRenderBabylon_kFilmGrainAmplitude : 0.0f;
        stdRenderBabylon_state.pPostfxGrain->setSettings(grain);

        postfx::FrameInputs frame;
        frame.color       = source;
        frame.colorFormat = render::TextureFormat::Bgra8;
        frame.width       = width;
        frame.height      = height;
        frame.frameIndex  = stdRenderBabylon_state.grainFrame;
        frame.timeSeconds = 0.0;

        const render::TextureFormat outputFormat = target.isNull()
            ? stdRenderBabylon_state.pDevice->info().swapchainFormat : format;

        if ( stdRenderBabylon_state.pPostfxVignette->active()
            && stdRenderBabylon_state.pPostfxGrain->active() )
        {
            const render::TextureHandle scratch = stdRenderBabylon_state.pPostfxTargets->acquire(
                width, height, render::TextureFormat::Rgba8);
            if ( scratch.isNull() )
            {
                stdRenderBabylon_state.pPostfxTargets->endFrame();
                return false;
            }

            postfx::PassContext vignettePass{ list, *stdRenderBabylon_state.pPostfxResources,
                *stdRenderBabylon_state.pPostfxTargets, frame, source,
                render::TextureFormat::Bgra8, width, height, scratch,
                render::TextureFormat::Rgba8, width, height };
            const bool bVignette = stdRenderBabylon_state.pPostfxVignette->record(vignettePass);

            bool bGrain = false;
            if ( bVignette )
            {
                postfx::PassContext grainPass{ list, *stdRenderBabylon_state.pPostfxResources,
                    *stdRenderBabylon_state.pPostfxTargets, frame, scratch,
                    render::TextureFormat::Rgba8, width, height, target, outputFormat,
                    width, height };
                bGrain = stdRenderBabylon_state.pPostfxGrain->record(grainPass);
            }

            stdRenderBabylon_state.pPostfxTargets->endFrame();
            return bVignette && bGrain;
        }

        postfx::Effect* pEffect = nullptr;
        if ( stdRenderBabylon_state.pPostfxVignette->active() )
        {
            pEffect = stdRenderBabylon_state.pPostfxVignette.get();
        }
        else if ( stdRenderBabylon_state.pPostfxGrain->active() )
        {
            pEffect = stdRenderBabylon_state.pPostfxGrain.get();
        }

        bool bRecorded = false;
        if ( pEffect )
        {
            postfx::PassContext pass{ list, *stdRenderBabylon_state.pPostfxResources,
                *stdRenderBabylon_state.pPostfxTargets, frame, source,
                render::TextureFormat::Bgra8, width, height, target, outputFormat, width,
                height };
            bRecorded = pEffect->record(pass);
        }

        // Added: return any scratch target the previous frame held, even on a frame with no or one
        // effect, so a size change or switching effects frees it after one idle frame.
        stdRenderBabylon_state.pPostfxTargets->endFrame();
        return bRecorded;
    }
}

#ifdef J3D_TEST
std::size_t stdRenderBabylon_GetPostfxTargetCountForTest()
{
    if ( !stdRenderBabylon_state.pPostfxTargets )
    {
        return 0;
    }
    return stdRenderBabylon_state.pPostfxTargets->textureCount();
}
#endif
