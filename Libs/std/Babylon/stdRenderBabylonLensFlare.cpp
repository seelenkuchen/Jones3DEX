// The lens-flare stage of the Babylon render adapter.
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
    struct StdRenderBabylonFlareGhost
    {
        float t;         // fraction along the light-to-centre line, 1 at the centre
        float radius;    // screen-height units
        float intensity;
        float aTint[3];
    };
    constexpr StdRenderBabylonFlareGhost stdRenderBabylon_aFlareGhosts[] = {
        { 0.75f, 0.045f, 0.45f, { 1.00f, 0.72f, 0.38f } },
        { 1.40f, 0.025f, 0.35f, { 0.40f, 0.78f, 1.00f } },
        { 1.95f, 0.035f, 0.30f, { 0.55f, 1.00f, 0.65f } },
        { 2.60f, 0.055f, 0.22f, { 1.00f, 0.50f, 0.90f } },
    };
    constexpr int stdRenderBabylon_kNumFlareGhosts =
        (int)(sizeof(stdRenderBabylon_aFlareGhosts) / sizeof(stdRenderBabylon_aFlareGhosts[0]));
    constexpr float stdRenderBabylon_kFlareGain      = 0.65f;
    constexpr float stdRenderBabylon_kFlareHaloRadius = 0.25f;
    constexpr float stdRenderBabylon_kFlareHaloIntensity = 0.30f;

    // Added: the lens-flare stage's scene-sized HDR target, made when its size changes. False,
    // logging why, when it can't be created.
    bool stdRenderBabylon_EnsureFlareTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.flareColorTarget.isNull()
            && stdRenderBabylon_state.flareWidth == width
            && stdRenderBabylon_state.flareHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeFlareTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u lens-flare target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.flareColorTarget = target;
        stdRenderBabylon_state.flareWidth       = width;
        stdRenderBabylon_state.flareHeight      = height;
        return true;
    }

    // Added: the lens-flare pipeline, made on first use. It is one full-screen pass over the scene
    // in the scene format.
    bool stdRenderBabylon_EnsureFlareResources()
    {
        if ( stdRenderBabylon_state.bFlareSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::lensFlareFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

        stdRenderBabylon_state.flarePipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
        if ( stdRenderBabylon_state.flarePipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the lens-flare pass.\n");
            stdRenderBabylon_state.bFlareSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the lens-flare stage's target. Recreated by the next present with the stage on
    // after a scene size or render scale change.
    void stdRenderBabylon_FreeFlareTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.flareColorTarget);
        stdRenderBabylon_state.flareColorTarget = {};
        stdRenderBabylon_state.flareWidth       = 0;
        stdRenderBabylon_state.flareHeight      = 0;
        stdRenderBabylon_state.bFlareRan        = false;
    }

    // Added: the lens-flare stage over the scene source, after light shafts and before bloom. It
    // reads the 1 x 1 centroid and smoothed strength the shaft stage left, draws tinted ghosts along
    // the light-to-centre line and a halo, and writes the scene-sized HDR target that bloom then
    // reads. The shader itself adds nothing when no sky is visible or the centroid is off screen.
    // False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordLensFlare(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bLensFlare
            || stdRenderBabylon_state.shaftCentroidTarget.isNull()
            || stdRenderBabylon_state.shaftStrengthTarget.isNull()
            || stdRenderBabylon_state.colorTarget.isNull() )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureFlareTargets(width, height)
            || !stdRenderBabylon_EnsureFlareResources() )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.bFlareLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: lens flare on, gain %g, %d ghosts, target %ux%u.\n",
                (double)stdRenderBabylon_kFlareGain, stdRenderBabylon_kNumFlareGhosts, width, height);
            stdRenderBabylon_state.bFlareLogged = true;
        }

        float aParams[4] = { stdRenderBabylon_kFlareGain,
            (float)width / (float)height, stdRenderBabylon_kFlareHaloRadius,
            stdRenderBabylon_kFlareHaloIntensity };
        float aUniforms[4 * (1 + 2 * stdRenderBabylon_kNumFlareGhosts)] = { 0.0f };
        std::memcpy(&aUniforms[0], aParams, sizeof(aParams));
        for ( int i = 0; i < stdRenderBabylon_kNumFlareGhosts; ++i )
        {
            const StdRenderBabylonFlareGhost& ghost = stdRenderBabylon_aFlareGhosts[i];
            const size_t ghostAt = 4u + (size_t)i * 4u;
            aUniforms[ghostAt + 0u] = ghost.t;
            aUniforms[ghostAt + 1u] = ghost.radius;
            aUniforms[ghostAt + 2u] = ghost.intensity;
            aUniforms[ghostAt + 3u] = 0.0f;
            const size_t tintAt = 4u + (size_t)stdRenderBabylon_kNumFlareGhosts * 4u
                + (size_t)i * 4u;
            aUniforms[tintAt + 0u] = ghost.aTint[0];
            aUniforms[tintAt + 1u] = ghost.aTint[1];
            aUniforms[tintAt + 2u] = ghost.aTint[2];
            aUniforms[tintAt + 3u] = 0.0f;
        }

        const render::TextureHandle scene = stdRenderBabylon_SceneSource();
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.flareColorTarget,
            0, render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(stdRenderBabylon_state.flarePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1,
            stdRenderBabylon_state.shaftCentroidTarget, stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2,
            stdRenderBabylon_state.shaftStrengthTarget, stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aUniforms,
                (size_t)4 * (1u + (size_t)2 * (size_t)stdRenderBabylon_kNumFlareGhosts))));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bFlareRan = true;
        return true;
    }

}

#ifdef J3D_TEST
bool stdRenderBabylon_GetLensFlareStateForTest(float* pU, float* pV, float* pStrength)
{
    if ( pU ) { *pU = 0.0f; }
    if ( pV ) { *pV = 0.0f; }
    if ( pStrength ) { *pStrength = 0.0f; }

    if ( !stdRenderBabylon_state.pDevice || !stdRenderBabylon_state.bFlareRan
        || stdRenderBabylon_state.shaftCentroidTarget.isNull()
        || stdRenderBabylon_state.shaftStrengthTarget.isNull() )
    {
        return false;
    }

    const std::vector<std::byte> centroid = stdRenderBabylon_state.pDevice->readTexture(
        stdRenderBabylon_state.shaftCentroidTarget, 0);
    const std::vector<std::byte> strength = stdRenderBabylon_state.pDevice->readTexture(
        stdRenderBabylon_state.shaftStrengthTarget, 0);
    const size_t texelBytes = stdRenderBabylon_state.shaftFormat == render::TextureFormat::Rgba16F
        ? 8u : 4u;
    if ( centroid.size() < texelBytes || strength.size() < texelBytes )
    {
        return false;
    }

    const float weight = stdRenderBabylon_ReadShaftTexel(centroid,
        stdRenderBabylon_state.shaftFormat, 0);
    if ( pStrength )
    {
        *pStrength = stdRenderBabylon_ReadShaftTexel(strength,
            stdRenderBabylon_state.shaftFormat, 0);
    }
    if ( weight > 1.0e-6f )
    {
        if ( pU ) { *pU = stdRenderBabylon_ReadShaftTexel(centroid,
            stdRenderBabylon_state.shaftFormat, 0, 1u) / weight; }
        if ( pV ) { *pV = stdRenderBabylon_ReadShaftTexel(centroid,
            stdRenderBabylon_state.shaftFormat, 0, 2u) / weight; }
    }
    return true;
}

#endif

bool stdRenderBabylon_SetLensFlare(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetLensFlare: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bLensFlare        = bEnabled;
    stdRenderBabylon_state.bFlareSetupFailed = false;
    stdRenderBabylon_state.bFlareRan         = false;
    stdRenderBabylon_state.bFlareLogged      = false;
    return true;
}

bool stdRenderBabylon_GetLensFlare()
{
    return stdRenderBabylon_state.bLensFlare;
}
