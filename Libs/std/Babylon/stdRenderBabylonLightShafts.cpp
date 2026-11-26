// The light-shaft stage of the Babylon render adapter.
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
    constexpr float stdRenderBabylon_kLightShaftLumaThreshold = 0.4f;
    constexpr float stdRenderBabylon_kLightShaftLumaKnee      = 0.2f;
    constexpr float stdRenderBabylon_kLightShaftMinCoverage   = 0.0002f;
    constexpr float stdRenderBabylon_kLightShaftFullCoverage  = 0.002f;
    constexpr float stdRenderBabylon_kLightShaftFadeTime      = 0.3f; // seconds to reach a new strength
    constexpr float stdRenderBabylon_kLightShaftDecay         = 0.96f;
    constexpr float stdRenderBabylon_kLightShaftRayLength     = 0.95f;
    constexpr float stdRenderBabylon_kLightShaftSkyScale      = 0.15f; // keep the clouds themselves crisp

    // Added: one dimension of a light-shaft reduce level, halved and at least one texel.
    uint32_t stdRenderBabylon_ShaftHalf(uint32_t size)
    {
        return std::max(1u, (size + 1u) / 2u);
    }

    // Added: the light-shaft stage's half-size mask and blur, its reduce chain from half size down
    // to 1 x 1, its two 1 x 1 strength targets and its scene-sized composite, made when their size
    // changes. False, logging why, when one can't be created.
    bool stdRenderBabylon_EnsureShaftTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.shaftMaskTarget.isNull()
            && stdRenderBabylon_state.shaftWidth == width
            && stdRenderBabylon_state.shaftHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeShaftTargets();

        const render::TextureUsage usage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        stdRenderBabylon_state.shaftFormat = stdRenderBabylon_state.pDevice->supportsFormat(
            render::TextureFormat::Rgba16F, usage)
            ? render::TextureFormat::Rgba16F
            : stdRenderBabylon_state.colorFormat;

        render::TextureDesc desc;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.shaftFormat;
        desc.usage     = usage;

        desc.width  = stdRenderBabylon_ShaftHalf(width);
        desc.height = stdRenderBabylon_ShaftHalf(height);
        const render::TextureHandle mask = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle blur = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( mask.isNull() || blur.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u light-shaft targets.\n",
                desc.width, desc.height);
            stdRenderBabylon_state.pDevice->destroyTexture(mask);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            return false;
        }

        std::vector<render::TextureHandle> aReduceTargets;
        uint32_t reduceWidth  = desc.width;
        uint32_t reduceHeight = desc.height;
        while ( reduceWidth > 1u || reduceHeight > 1u )
        {
            reduceWidth  = stdRenderBabylon_ShaftHalf(reduceWidth);
            reduceHeight = stdRenderBabylon_ShaftHalf(reduceHeight);
            render::TextureDesc reduceDesc = desc;
            reduceDesc.width  = reduceWidth;
            reduceDesc.height = reduceHeight;
            const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(reduceDesc);
            if ( target.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u light-shaft reduce target.\n",
                    reduceWidth, reduceHeight);
                stdRenderBabylon_state.pDevice->destroyTexture(mask);
                stdRenderBabylon_state.pDevice->destroyTexture(blur);
                for ( const render::TextureHandle created : aReduceTargets )
                {
                    stdRenderBabylon_state.pDevice->destroyTexture(created);
                }
                return false;
            }
            aReduceTargets.push_back(target);
        }

        render::TextureDesc oneDesc = desc;
        oneDesc.width  = 1;
        oneDesc.height = 1;
        const render::TextureHandle strength0 = stdRenderBabylon_state.pDevice->createTexture(oneDesc);
        const render::TextureHandle strength1 = stdRenderBabylon_state.pDevice->createTexture(oneDesc);
        if ( strength0.isNull() || strength1.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the light-shaft strength targets.\n");
            stdRenderBabylon_state.pDevice->destroyTexture(mask);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            stdRenderBabylon_state.pDevice->destroyTexture(strength0);
            stdRenderBabylon_state.pDevice->destroyTexture(strength1);
            for ( const render::TextureHandle created : aReduceTargets )
            {
                stdRenderBabylon_state.pDevice->destroyTexture(created);
            }
            return false;
        }

        render::TextureDesc colorDesc = desc;
        colorDesc.width  = width;
        colorDesc.height = height;
        colorDesc.format = stdRenderBabylon_state.colorFormat;
        const render::TextureHandle composite = stdRenderBabylon_state.pDevice->createTexture(colorDesc);
        if ( composite.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u light-shaft composite.\n",
                width, height);
            stdRenderBabylon_state.pDevice->destroyTexture(mask);
            stdRenderBabylon_state.pDevice->destroyTexture(blur);
            stdRenderBabylon_state.pDevice->destroyTexture(strength0);
            stdRenderBabylon_state.pDevice->destroyTexture(strength1);
            for ( const render::TextureHandle created : aReduceTargets )
            {
                stdRenderBabylon_state.pDevice->destroyTexture(created);
            }
            return false;
        }

        stdRenderBabylon_state.shaftMaskTarget      = mask;
        stdRenderBabylon_state.shaftBlurTarget      = blur;
        stdRenderBabylon_state.shaftColorTarget     = composite;
        stdRenderBabylon_state.aShaftReduceTargets  = std::move(aReduceTargets);
        stdRenderBabylon_state.aShaftStrengthTargets[0] = strength0;
        stdRenderBabylon_state.aShaftStrengthTargets[1] = strength1;
        stdRenderBabylon_state.shaftWidth           = width;
        stdRenderBabylon_state.shaftHeight          = height;
        stdRenderBabylon_state.bShaftStrengthValid  = false;
        stdRenderBabylon_state.shaftStrengthIndex   = 0;
        return true;
    }

    // Added: the light-shaft stage's pipelines, made on first use. The mask, reduce and strength
    // passes write the stage's own (half-float where supported) format; the composite writes the
    // scene format so bloom and the tone map keep their HDR input.
    bool stdRenderBabylon_EnsureShaftResources()
    {
        if ( stdRenderBabylon_state.bShaftsSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_state.shaftFormat, {} } };

        if ( stdRenderBabylon_state.shaftMaskPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::lightshaftMaskFrag();
            stdRenderBabylon_state.shaftMaskPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.shaftReducePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::lightshaftReduceFrag();
            stdRenderBabylon_state.shaftReducePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.shaftStrengthPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::lightshaftStrengthFrag();
            stdRenderBabylon_state.shaftStrengthPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.shaftBlurPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::lightshaftBlurFrag();
            stdRenderBabylon_state.shaftBlurPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.shaftCompositePipeline.isNull() )
        {
            desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };
            desc.fragmentShader = stdBabylon::shaders::lightshaftCompositeFrag();
            stdRenderBabylon_state.shaftCompositePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.shaftMaskPipeline.isNull()
            || stdRenderBabylon_state.shaftReducePipeline.isNull()
            || stdRenderBabylon_state.shaftStrengthPipeline.isNull()
            || stdRenderBabylon_state.shaftBlurPipeline.isNull()
            || stdRenderBabylon_state.shaftCompositePipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the light-shaft passes.\n");
            stdRenderBabylon_state.bShaftsSetupFailed = true;
            return false;
        }

        return true;
    }

    // Added: the light-shaft stage over the scene source: the mask and centroid reduction, then the
    // radial blur and composite. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordLightShafts(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.lightShaftStrength <= 0.0f
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull() )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_RecordShaftMaskCentroid(list) )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.bShaftsLogged )
        {
            STDLOG_STATUS("stdRenderBabylon: light shafts on, level %d, strength %g; mask %ux%u, %zu reduce levels, composite %ux%u.\n",
                stdRenderBabylon_state.lightShafts, (double)stdRenderBabylon_state.lightShaftStrength,
                stdRenderBabylon_ShaftHalf(width), stdRenderBabylon_ShaftHalf(height),
                stdRenderBabylon_state.aShaftReduceTargets.size(), width, height);
            stdRenderBabylon_state.bShaftsLogged = true;
        }

        return stdRenderBabylon_RecordShaftComposite(list);
    }
}

namespace stdRenderBabylonInternal
{
    // Added: frees the light-shaft stage's targets. Recreated by the next present with the stage on
    // after a scene size or render scale change.
    void stdRenderBabylon_FreeShaftTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shaftMaskTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shaftBlurTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shaftColorTarget);
        for ( render::TextureHandle& target : stdRenderBabylon_state.aShaftReduceTargets )
        {
            stdRenderBabylon_DestroyTexture(target);
        }
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.aShaftStrengthTargets[0]);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.aShaftStrengthTargets[1]);
        stdRenderBabylon_state.shaftMaskTarget            = {};
        stdRenderBabylon_state.shaftBlurTarget            = {};
        stdRenderBabylon_state.shaftColorTarget           = {};
        stdRenderBabylon_state.shaftCentroidTarget        = {};
        stdRenderBabylon_state.shaftStrengthTarget        = {};
        stdRenderBabylon_state.aShaftReduceTargets.clear();
        stdRenderBabylon_state.aShaftStrengthTargets[0]   = {};
        stdRenderBabylon_state.aShaftStrengthTargets[1]   = {};
        stdRenderBabylon_state.shaftWidth                 = 0;
        stdRenderBabylon_state.shaftHeight                = 0;
        stdRenderBabylon_state.bShaftsRan                 = false;
        stdRenderBabylon_state.bShaftStrengthValid        = false;
        stdRenderBabylon_state.shaftStrengthIndex         = 0;
    }

    // Added: the mask, centroid reduction and strength part of the light-shaft stage. It masks the
    // sky at half size, reduces the mask's mean weighted position to 1 x 1 and smooths a coverage
    // strength against the previous frame. The 1 x 1 centroid and strength are kept in the state,
    // so the lens-flare stage can reuse them without a readback, including when light shafts are
    // off. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordShaftMaskCentroid(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull() )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureShaftTargets(width, height)
            || !stdRenderBabylon_EnsureShaftResources() )
        {
            return false;
        }

        const render::TextureHandle scene = stdRenderBabylon_SceneSource();

        const float aMask[4] = { stdRenderBabylon_kSurfaceTags.sky,
            stdRenderBabylon_kLightShaftLumaThreshold, stdRenderBabylon_kLightShaftLumaKnee, 0.0f };
        render::PassDesc maskPass;
        maskPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.shaftMaskTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(maskPass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.shaftMaskPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.normalTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aMask, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        render::TextureHandle centroid = stdRenderBabylon_state.shaftMaskTarget;
        uint32_t reduceWidth  = stdRenderBabylon_ShaftHalf(width);
        uint32_t reduceHeight = stdRenderBabylon_ShaftHalf(height);
        for ( const render::TextureHandle target : stdRenderBabylon_state.aShaftReduceTargets )
        {
            render::PassDesc pass;
            pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
                render::LoadOp::DontCare, render::StoreOp::Store, {} });
            if ( !list.beginPass(pass) )
            {
                return false;
            }
            const float aReduce[4] = { 1.0f / (float)reduceWidth, 1.0f / (float)reduceHeight, 0.0f, 0.0f };
            list.setPipeline(stdRenderBabylon_state.shaftReducePipeline);
            list.bindTexture(render::ShaderStage::Fragment, 0, centroid,
                stdRenderBabylon_state.finalSampler);
            list.pushUniforms(render::ShaderStage::Fragment, 0,
                std::as_bytes(std::span<const float>(aReduce, 4u)));
            list.draw(3);
            if ( !list.endPass() )
            {
                return false;
            }

            centroid     = target;
            reduceWidth  = stdRenderBabylon_ShaftHalf(reduceWidth);
            reduceHeight = stdRenderBabylon_ShaftHalf(reduceHeight);
        }

        // Added: smooth the coverage strength against the previous frame over the fade time. A first
        // frame, or a non-positive game-time step, snaps instead of reading an uninitialised target.
        const int previousIndex = stdRenderBabylon_state.shaftStrengthIndex;
        const int nextIndex     = 1 - previousIndex;
        const float blend = stdRenderBabylon_state.bShaftStrengthValid
            && stdRenderBabylon_state.shaftDeltaTime > 0.0f
            ? 1.0f - std::exp(-stdRenderBabylon_state.shaftDeltaTime / stdRenderBabylon_kLightShaftFadeTime)
            : 1.0f;
        const float aStrength[4] = { blend, stdRenderBabylon_kLightShaftMinCoverage,
            stdRenderBabylon_kLightShaftFullCoverage, 0.0f };
        render::PassDesc strengthPass;
        strengthPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.aShaftStrengthTargets[nextIndex], 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(strengthPass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.shaftStrengthPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, centroid,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1,
            stdRenderBabylon_state.aShaftStrengthTargets[previousIndex],
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aStrength, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }
        stdRenderBabylon_state.shaftStrengthIndex = nextIndex;
        stdRenderBabylon_state.bShaftStrengthValid = true;
        stdRenderBabylon_state.shaftCentroidTarget = centroid;
        stdRenderBabylon_state.shaftStrengthTarget =
            stdRenderBabylon_state.aShaftStrengthTargets[nextIndex];
        return true;
    }

    // Added: the radial blur and composite part of the light-shaft stage over the scene source (the
    // depth-of-field output or the post chain's). It adds the shafts to the HDR scene into the
    // scene-sized target that bloom then reads. bShaftsRan is set afterwards so bloom and the tone
    // map read the composite. False, having logged nothing, when a pass fails.
    bool stdRenderBabylon_RecordShaftComposite(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.lightShaftStrength <= 0.0f
            || stdRenderBabylon_state.shaftCentroidTarget.isNull()
            || stdRenderBabylon_state.shaftStrengthTarget.isNull()
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull() )
        {
            return false;
        }

        const render::TextureHandle scene = stdRenderBabylon_SceneSource();
        const render::TextureHandle centroid = stdRenderBabylon_state.shaftCentroidTarget;

        // Added: the radial blur of the mask toward the centroid, scaled by the smoothed strength.
        const float aBlur[4] = { stdRenderBabylon_kLightShaftDecay,
            stdRenderBabylon_kLightShaftRayLength, 0.0f, 0.0f };
        render::PassDesc blurPass;
        blurPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.shaftBlurTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(blurPass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.shaftBlurPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.shaftMaskTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, centroid,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2,
            stdRenderBabylon_state.shaftStrengthTarget,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aBlur, 4u)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        // Added: add the shafts to the HDR scene into the scene-sized target that bloom reads. On a
        // sky-tagged pixel the normal target's alpha scales them down, so the clouds stay crisp.
        const float aComposite[4] = { stdRenderBabylon_state.lightShaftStrength,
            stdRenderBabylon_kLightShaftSkyScale, stdRenderBabylon_kSurfaceTags.sky, 0.0f };
        render::PassDesc compositePass;
        compositePass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.shaftColorTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(compositePass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.shaftCompositePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.shaftBlurTarget,
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

        stdRenderBabylon_state.bShaftsRan = true;
        return true;
    }

}

#ifdef J3D_TEST
namespace stdRenderBabylonInternal
{
    // One channel of a light-shaft target texel at index i, decoded from its format.
    float stdRenderBabylon_ReadShaftTexel(const std::vector<std::byte>& texels,
        render::TextureFormat format, size_t index, size_t channel)
    {
        const uint8_t* pBytes = reinterpret_cast<const uint8_t*>(texels.data());
        if ( format == render::TextureFormat::Rgba16F )
        {
            const uint16_t half = (uint16_t)(pBytes[index * 8u + channel * 2u]
                | ((uint16_t)pBytes[index * 8u + channel * 2u + 1u] << 8));
            return stdRenderBabylon_HalfToFloat(half);
        }
        return (float)pBytes[index * 4u + channel] / 255.0f;
    }
}

bool stdRenderBabylon_GetLightShaftValueForTest(uint32_t x, uint32_t y, float* pValue)
{
    if ( pValue )
    {
        *pValue = 0.0f;
    }

    if ( !stdRenderBabylon_state.pDevice || !stdRenderBabylon_state.bShaftsRan
        || stdRenderBabylon_state.shaftBlurTarget.isNull() )
    {
        return false;
    }

    const uint32_t width  = stdRenderBabylon_state.width;
    const uint32_t height = stdRenderBabylon_state.height;
    const uint32_t blurWidth  = stdRenderBabylon_ShaftHalf(width);
    const uint32_t blurHeight = stdRenderBabylon_ShaftHalf(height);
    if ( x >= width || y >= height )
    {
        return false;
    }

    const std::vector<std::byte> texels =
        stdRenderBabylon_state.pDevice->readTexture(stdRenderBabylon_state.shaftBlurTarget, 0);
    const size_t expected = (size_t)blurWidth * blurHeight
        * (stdRenderBabylon_state.shaftFormat == render::TextureFormat::Rgba16F ? 8u : 4u);
    if ( texels.size() != expected )
    {
        return false;
    }

    // The composite samples the half-res blur with a linear sampler at the full-res pixel centre.
    float fx = ((float)x + 0.5f) / (float)width * (float)blurWidth - 0.5f;
    float fy = ((float)y + 0.5f) / (float)height * (float)blurHeight - 0.5f;
    fx = std::clamp(fx, 0.0f, (float)(blurWidth - 1u));
    fy = std::clamp(fy, 0.0f, (float)(blurHeight - 1u));
    const uint32_t x0 = (uint32_t)fx;
    const uint32_t y0 = (uint32_t)fy;
    const uint32_t x1 = std::min(x0 + 1u, blurWidth - 1u);
    const uint32_t y1 = std::min(y0 + 1u, blurHeight - 1u);
    const float tx = fx - (float)x0;
    const float ty = fy - (float)y0;
    const float a = stdRenderBabylon_ReadShaftTexel(texels, stdRenderBabylon_state.shaftFormat,
        (size_t)y0 * blurWidth + x0);
    const float b = stdRenderBabylon_ReadShaftTexel(texels, stdRenderBabylon_state.shaftFormat,
        (size_t)y0 * blurWidth + x1);
    const float c = stdRenderBabylon_ReadShaftTexel(texels, stdRenderBabylon_state.shaftFormat,
        (size_t)y1 * blurWidth + x0);
    const float d = stdRenderBabylon_ReadShaftTexel(texels, stdRenderBabylon_state.shaftFormat,
        (size_t)y1 * blurWidth + x1);
    if ( pValue )
    {
        *pValue = (a * (1.0f - tx) + b * tx) * (1.0f - ty)
            + (c * (1.0f - tx) + d * tx) * ty;
    }
    return true;
}

bool stdRenderBabylon_GetLightShaftCentroidForTest(float* pU, float* pV, float* pWeight)
{
    if ( pU ) { *pU = 0.0f; }
    if ( pV ) { *pV = 0.0f; }
    if ( pWeight ) { *pWeight = 0.0f; }

    if ( !stdRenderBabylon_state.pDevice
        || ( !stdRenderBabylon_state.bShaftsRan && !stdRenderBabylon_state.bFlareRan )
        || stdRenderBabylon_state.shaftMaskTarget.isNull() )
    {
        return false;
    }

    const render::TextureHandle centroid = stdRenderBabylon_state.aShaftReduceTargets.empty()
        ? stdRenderBabylon_state.shaftMaskTarget
        : stdRenderBabylon_state.aShaftReduceTargets.back();
    const std::vector<std::byte> texel =
        stdRenderBabylon_state.pDevice->readTexture(centroid, 0);
    if ( texel.size() < 4u )
    {
        return false;
    }

    float channels[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if ( stdRenderBabylon_state.shaftFormat == render::TextureFormat::Rgba16F )
    {
        if ( texel.size() < 8u )
        {
            return false;
        }
        for ( size_t i = 0; i < 4u; ++i )
        {
            const uint16_t half = (uint16_t)((uint8_t)texel[i * 2u]
                | ((uint16_t)(uint8_t)texel[i * 2u + 1u] << 8));
            channels[i] = stdRenderBabylon_HalfToFloat(half);
        }
    }
    else
    {
        for ( size_t i = 0; i < 4u; ++i )
        {
            channels[i] = (float)(uint8_t)texel[i] / 255.0f;
        }
    }

    if ( pWeight ) { *pWeight = channels[0]; }
    if ( channels[0] > 1.0e-6f )
    {
        if ( pU ) { *pU = channels[1] / channels[0]; }
        if ( pV ) { *pV = channels[2] / channels[0]; }
    }
    return true;
}

#endif

bool stdRenderBabylon_SetLightShafts(int level)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetLightShafts: not open.\n");
        return false;
    }

    const int clamped = std::clamp(level, 0, stdRenderBabylon_kLightShaftsMaxLevel);
    if ( clamped != level )
    {
        STDLOG_STATUS("stdRenderBabylon: light shafts level %d is outside 0..%d; using %d.\n",
            level, stdRenderBabylon_kLightShaftsMaxLevel, clamped);
    }

    stdRenderBabylon_state.lightShafts        = clamped;
    stdRenderBabylon_state.lightShaftStrength = stdRenderBabylon_aLightShaftStrength[clamped];
    stdRenderBabylon_state.bShaftsSetupFailed = false;
    stdRenderBabylon_state.bShaftsRan         = false;
    stdRenderBabylon_state.bShaftsLogged      = false;
    return true;
}

int stdRenderBabylon_GetLightShafts()
{
    return stdRenderBabylon_state.lightShafts;
}
