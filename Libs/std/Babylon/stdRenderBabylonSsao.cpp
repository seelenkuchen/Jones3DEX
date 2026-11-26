// The screen-space ambient-occlusion stage of the Babylon render adapter.
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
    // Added: rdCamera's fov is the horizontal fov at the 4:3 reference aspect (RD_REF_APECTRATIO);
    // rdCamera_BuildFOV makes the picture Hor+ by scaling the focal length for the real aspect, so
    // the SSAO reconstruction derives the vertical tangent first and the horizontal one from it.
    constexpr float stdRenderBabylon_kSsaoReferenceAspect = 640.0f / 480.0f;

    // Added: the half-res raw and blurred occlusion targets and the scene-sized composite, made when
    // their size changes. False, logging why, when one can't be created.
    bool stdRenderBabylon_EnsureSsaoTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.ssaoRawTarget.isNull()
            && stdRenderBabylon_state.ssaoWidth == width
            && stdRenderBabylon_state.ssaoHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeSsaoTargets();

        const render::TextureUsage usage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        stdRenderBabylon_state.ssaoFormat = stdRenderBabylon_state.pDevice->supportsFormat(
            render::TextureFormat::Rgba16F, usage)
            ? render::TextureFormat::Rgba16F
            : stdRenderBabylon_state.colorFormat;

        render::TextureDesc desc;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.ssaoFormat;
        desc.usage     = usage;

        desc.width  = std::max(1u, (width + 1u) / 2u);
        desc.height = std::max(1u, (height + 1u) / 2u);
        const render::TextureHandle raw  = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle blur = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( raw.isNull() || blur.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u ambient-occlusion targets.\n",
                desc.width, desc.height);
            stdRenderBabylon_state.pDevice->destroyTexture(raw);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            return false;
        }

        render::TextureDesc colorDesc = desc;
        colorDesc.width  = width;
        colorDesc.height = height;
        colorDesc.format = stdRenderBabylon_state.colorFormat;
        const render::TextureHandle composite = stdRenderBabylon_state.pDevice->createTexture(colorDesc);
        if ( composite.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u ambient-occlusion composite.\n",
                width, height);
            stdRenderBabylon_state.pDevice->destroyTexture(raw);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            return false;
        }

        stdRenderBabylon_state.ssaoRawTarget   = raw;
        stdRenderBabylon_state.ssaoBlurTarget  = blur;
        stdRenderBabylon_state.ssaoColorTarget = composite;
        stdRenderBabylon_state.ssaoWidth       = width;
        stdRenderBabylon_state.ssaoHeight      = height;
        return true;
    }

    // Added: the ambient-occlusion stage's pipelines, made on first use. The occlusion and blur
    // passes write the stage's half-float format where supported; the composite writes the scene
    // format so depth of field, the light shafts, bloom and the tone map keep their HDR input.
    bool stdRenderBabylon_EnsureSsaoResources()
    {
        if ( stdRenderBabylon_state.bSsaoSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_state.ssaoFormat, {} } };

        if ( stdRenderBabylon_state.ssaoPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::ssaoFrag();
            stdRenderBabylon_state.ssaoPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.ssaoBlurPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::ssaoBlurFrag();
            stdRenderBabylon_state.ssaoBlurPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.ssaoCompositePipeline.isNull() )
        {
            desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };
            desc.fragmentShader = stdBabylon::shaders::ssaoCompositeFrag();
            stdRenderBabylon_state.ssaoCompositePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.ssaoPipeline.isNull()
            || stdRenderBabylon_state.ssaoBlurPipeline.isNull()
            || stdRenderBabylon_state.ssaoCompositePipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the ambient-occlusion passes.\n");
            stdRenderBabylon_state.bSsaoSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the ambient-occlusion stage's targets. Recreated by the next present with the
    // stage on after a scene size or render scale change.
    void stdRenderBabylon_FreeSsaoTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.ssaoRawTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.ssaoBlurTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.ssaoColorTarget);
        stdRenderBabylon_state.ssaoRawTarget   = {};
        stdRenderBabylon_state.ssaoBlurTarget  = {};
        stdRenderBabylon_state.ssaoColorTarget = {};
        stdRenderBabylon_state.ssaoWidth       = 0;
        stdRenderBabylon_state.ssaoHeight      = 0;
        stdRenderBabylon_state.bSsaoRan        = false;
    }

    // Added: the SSAO reconstruction's tangent scales for a scene of width x height. rdCamera's fov
    // is the horizontal fov at the 4:3 reference aspect, so the vertical tangent is derived from it
    // and the horizontal one follows the real aspect (Hor+). Both are 1 at 90 degrees, 4:3.
    void stdRenderBabylon_SsaoProjection(uint32_t width, uint32_t height, float* pTanHalfX,
        float* pTanHalfY)
    {
        const float fov = std::clamp(stdRenderBabylon_state.projectionFovDegrees, 1.0f, 179.0f);
        const float tanHalfReference = std::tan(fov * 0.5f * 3.14159265358979323846f / 180.0f);
        const float tanHalfY = tanHalfReference / stdRenderBabylon_kSsaoReferenceAspect;
        const float tanHalfX = tanHalfY * ((width > 0 && height > 0)
            ? (float)width / (float)height : stdRenderBabylon_kSsaoReferenceAspect);
        if ( pTanHalfX )
        {
            *pTanHalfX = tanHalfX;
        }
        if ( pTanHalfY )
        {
            *pTanHalfY = tanHalfY;
        }
    }

    // Added: the ambient-occlusion stage over the scene source (the contact-shadow output, the
    // water-ripple output or the post chain's). It reconstructs view-space positions and normals
    // from the depth target, gathers a half-res normal-oriented hemisphere kernel in a fixed 4 x 4
    // rotation pattern, blurs it
    // depth-aware and multiplies the HDR scene by the result. Sky, water and empty pixels stay
    // unchanged. bSsaoRan is set afterwards so depth of field and the later stages read the
    // composite. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordSsao(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.ssao <= 0
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.depthTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull()
            || !stdRenderBabylon_state.bDepthSampled )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureSsaoTargets(width, height)
            || !stdRenderBabylon_EnsureSsaoResources() )
        {
            return false;
        }

        const StdRenderBabylonSsaoLevel& level = stdRenderBabylon_aSsaoLevels[stdRenderBabylon_state.ssao];
        if ( !stdRenderBabylon_state.bSsaoLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: ambient occlusion on, level %d, %d samples, radius %g; targets %ux%u, composite %ux%u.\n",
                stdRenderBabylon_state.ssao, level.samples, (double)level.radius,
                std::max(1u, (width + 1u) / 2u), std::max(1u, (height + 1u) / 2u), width, height);
            stdRenderBabylon_state.bSsaoLogged = true;
        }

        const render::TextureHandle scene = stdRenderBabylon_state.bContactShadowsRan
            ? stdRenderBabylon_state.contactShadowColorTarget
            : (stdRenderBabylon_state.bWaterRan
                ? stdRenderBabylon_state.waterColorTarget : stdRenderBabylon_ChainSource());
        const uint32_t halfWidth  = std::max(1u, (width + 1u) / 2u);
        const uint32_t halfHeight = std::max(1u, (height + 1u) / 2u);

        // The rdCamera's fov is horizontal at the 4:3 reference aspect; the helper gives the
        // vertical tangent first and the horizontal one for this scene's real aspect.
        float tanHalfX = 0.0f;
        float tanHalfY = 0.0f;
        stdRenderBabylon_SsaoProjection(width, height, &tanHalfX, &tanHalfY);
        const float nearPlane = std::max(stdRenderBabylon_state.projectionNearPlane, 1.0e-5f);
        const float farPlane  = std::max(stdRenderBabylon_state.projectionFarPlane,
            nearPlane + 1.0e-3f);

        const float aSsao[4] = { nearPlane, farPlane, level.radius, level.bias };
        const float aParams[4] = { level.intensity, (float)level.samples,
            1.0f / (float)halfWidth, 1.0f / (float)halfHeight };
        const float aProjection[4] = { tanHalfX, tanHalfY, (float)halfWidth, (float)halfHeight };

        render::PassDesc aoPass;
        aoPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.ssaoRawTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(aoPass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.ssaoPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.depthTarget,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.normalTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aSsao, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aParams, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 2,
            std::as_bytes(std::span<const float>(aProjection, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        const float aBlur[4] = { 1.0f / (float)halfWidth, 1.0f / (float)halfHeight,
            nearPlane, farPlane };
        // The blur's depth weight follows the level's radius, so its reach stays the same fraction of
        // the occlusion kernel and it does not bleed across a nearer silhouette.
        const float aBlurDepth[4] = { 1.0f / std::max(level.radius * 4.0f, 1.0e-4f), 0.0f, 0.0f, 0.0f };
        render::PassDesc blurPass;
        blurPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.ssaoBlurTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(blurPass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.ssaoBlurPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.ssaoRawTarget,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.depthTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aBlur, 4u)));
        list.pushUniforms(render::ShaderStage::Fragment, 1,
            std::as_bytes(std::span<const float>(aBlurDepth, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        const float aComposite[4] = { stdRenderBabylon_kSurfaceTags.ordinary, 0.0f, 0.0f, 0.0f };
        render::PassDesc compositePass;
        compositePass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.ssaoColorTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(compositePass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.ssaoCompositePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.ssaoBlurTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, stdRenderBabylon_state.normalTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aComposite, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bSsaoRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetSsao(int level)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetSsao: not open.\n");
        return false;
    }

    const int clamped = std::clamp(level, 0, stdRenderBabylon_kSsaoMaxLevel);
    if ( clamped != level )
    {
        STDLOG_STATUS("stdRenderBabylon: ambient-occlusion level %d is outside 0..%d; using %d.\n",
            level, stdRenderBabylon_kSsaoMaxLevel, clamped);
    }

    stdRenderBabylon_state.ssao             = clamped;
    stdRenderBabylon_state.bSsaoSetupFailed = false;
    stdRenderBabylon_state.bSsaoRan         = false;
    stdRenderBabylon_state.bSsaoLogged      = false;
    return true;
}

int stdRenderBabylon_GetSsao()
{
    return stdRenderBabylon_state.ssao;
}

#ifdef J3D_TEST
bool stdRenderBabylon_GetSsaoProjectionForTest(float* pTanHalfX, float* pTanHalfY)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return false;
    }

    stdRenderBabylon_SsaoProjection(stdRenderBabylon_state.width,
        stdRenderBabylon_state.height, pTanHalfX, pTanHalfY);
    return true;
}
#endif
