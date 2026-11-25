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
// The bloom stage of the Babylon render adapter.
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
    constexpr float stdRenderBabylon_kBloomThreshold = 0.6f;
    constexpr float stdRenderBabylon_kBloomKnee      = 0.3f;

    // Added: a bloom target dimension at level 1 (half) to 5 (1/32), rounded to nearest and at least
    // one texel so a tiny scene still has every level.
    uint32_t stdRenderBabylon_BloomDimension(uint32_t size, int level)
    {
        return std::max(1u, (size + (1u << (level - 1))) >> level);
    }

    // Added: the bloom stage's half, quarter, eighth, sixteenth, 1/32 and scene-size composite
    // targets, made when their size changes. False, logging why, when one can't be created.
    bool stdRenderBabylon_EnsureBloomTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.aBloomTargets[0].isNull()
            && stdRenderBabylon_state.bloomWidth == width
            && stdRenderBabylon_state.bloomHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeBloomTargets();

        render::TextureDesc desc;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        for ( int level = 1; level <= stdRenderBabylon_kBloomLevels; ++level )
        {
            desc.width  = stdRenderBabylon_BloomDimension(width, level);
            desc.height = stdRenderBabylon_BloomDimension(height, level);
            const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
            if ( target.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u bloom target.\n",
                    desc.width, desc.height);
                stdRenderBabylon_FreeBloomTargets();
                return false;
            }
            stdRenderBabylon_state.aBloomTargets[level - 1] = target;
        }

        desc.width  = width;
        desc.height = height;
        const render::TextureHandle composite = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( composite.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u bloom composite.\n", width, height);
            stdRenderBabylon_FreeBloomTargets();
            return false;
        }

        stdRenderBabylon_state.bloomCompositeTarget = composite;
        stdRenderBabylon_state.bloomWidth           = width;
        stdRenderBabylon_state.bloomHeight          = height;
        return true;
    }

    // Added: the bloom stage's pipelines, made on first use. The upsample pipeline blends additively
    // into the next larger target, which still holds its downsampled image.
    bool stdRenderBabylon_EnsureBloomResources()
    {
        if ( stdRenderBabylon_state.bBloomSetupFailed )
        {
            return false;
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

        if ( stdRenderBabylon_state.bloomPrefilterPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::bloomPrefilterFrag();
            stdRenderBabylon_state.bloomPrefilterPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.bloomDownsamplePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::bloomDownsampleFrag();
            stdRenderBabylon_state.bloomDownsamplePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.bloomUpsamplePipeline.isNull() )
        {
            render::BlendState blend;
            blend.enabled  = true;
            blend.srcColor = render::BlendFactor::One;
            blend.dstColor = render::BlendFactor::One;
            blend.colorOp  = render::BlendOp::Add;
            blend.srcAlpha = render::BlendFactor::One;
            blend.dstAlpha = render::BlendFactor::One;
            blend.alphaOp  = render::BlendOp::Add;
            desc.fragmentShader = stdBabylon::shaders::bloomUpsampleFrag();
            desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, blend } };
            stdRenderBabylon_state.bloomUpsamplePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
            desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };
        }
        if ( stdRenderBabylon_state.bloomCompositePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::bloomCompositeFrag();
            stdRenderBabylon_state.bloomCompositePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.bloomPrefilterPipeline.isNull()
            || stdRenderBabylon_state.bloomDownsamplePipeline.isNull()
            || stdRenderBabylon_state.bloomUpsamplePipeline.isNull()
            || stdRenderBabylon_state.bloomCompositePipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the bloom passes.\n");
            stdRenderBabylon_state.bBloomSetupFailed = true;
            return false;
        }

        return true;
    }

    // Added: records one bloom pass that samples input in fragment slot 0 and writes output. False,
    // having logged nothing, when the pass can't be started or ended; the caller abandons bloom.
    bool stdRenderBabylon_RecordBloomPass(render::CommandList& list, render::PipelineHandle pipeline,
        render::TextureHandle input, render::SamplerHandle sampler, render::TextureHandle output,
        render::LoadOp load, const float* pParams)
    {
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ output, 0, load,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(pipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, input, sampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(pParams, 4u)));
        list.draw(3);
        return list.endPass();
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the bloom stage's targets. Recreated by the next present with bloom on after a
    // scene size or render scale change.
    void stdRenderBabylon_FreeBloomTargets()
    {
        for ( render::TextureHandle& target : stdRenderBabylon_state.aBloomTargets )
        {
            stdRenderBabylon_DestroyTexture(target);
            target = {};
        }

        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.bloomCompositeTarget);
        stdRenderBabylon_state.bloomCompositeTarget = {};
        stdRenderBabylon_state.bloomWidth           = 0;
        stdRenderBabylon_state.bloomHeight          = 0;
        stdRenderBabylon_state.bBloomRan            = false;
    }

    // Added: the bloom stage over the scene source (the post chain's output, or the back buffer).
    // It prefilters at half size, downsamples to 1/32, upsamples additively back to 1/2 and adds the
    // result times the strength to the scene in bloomCompositeTarget. bBloomRan is set afterwards so
    // the tone map reads the composite. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordBloom(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.bloom <= 0.0f || stdRenderBabylon_state.colorTarget.isNull() )
        {
            return false;
        }

        if ( !stdRenderBabylon_EnsureBloomResources()
            || !stdRenderBabylon_EnsureBloomTargets(stdRenderBabylon_state.width,
                stdRenderBabylon_state.height) )
        {
            return false;
        }

        // Added: one line per open (or strength change) so a windowed run's log shows that the
        // stage runs and at what sizes, not only that the option was read.
        if ( !stdRenderBabylon_state.bBloomLogged )
        {
            const uint32_t width  = stdRenderBabylon_state.width;
            const uint32_t height = stdRenderBabylon_state.height;
            STDLOG_STATUS("stdRenderBabylon: bloom on, strength %g; targets %ux%u, %ux%u, %ux%u, %ux%u, %ux%u, composite %ux%u.\n",
                (double)stdRenderBabylon_state.bloom,
                stdRenderBabylon_BloomDimension(width, 1), stdRenderBabylon_BloomDimension(height, 1),
                stdRenderBabylon_BloomDimension(width, 2), stdRenderBabylon_BloomDimension(height, 2),
                stdRenderBabylon_BloomDimension(width, 3), stdRenderBabylon_BloomDimension(height, 3),
                stdRenderBabylon_BloomDimension(width, 4), stdRenderBabylon_BloomDimension(height, 4),
                stdRenderBabylon_BloomDimension(width, 5), stdRenderBabylon_BloomDimension(height, 5),
                width, height);
            stdRenderBabylon_state.bBloomLogged = true;
        }

        const render::TextureHandle scene = stdRenderBabylon_SceneSource();
        const float aPrefilter[4] = { stdRenderBabylon_kBloomThreshold,
            stdRenderBabylon_kBloomKnee, 1.0f / (float)stdRenderBabylon_state.width,
            1.0f / (float)stdRenderBabylon_state.height };
        if ( !stdRenderBabylon_RecordBloomPass(list, stdRenderBabylon_state.bloomPrefilterPipeline,
            scene, stdRenderBabylon_state.finalSampler, stdRenderBabylon_state.aBloomTargets[0],
            render::LoadOp::DontCare, aPrefilter) )
        {
            return false;
        }

        for ( int level = 2; level <= stdRenderBabylon_kBloomLevels; ++level )
        {
            const uint32_t sourceWidth = stdRenderBabylon_BloomDimension(
                stdRenderBabylon_state.width, level - 1);
            const uint32_t sourceHeight = stdRenderBabylon_BloomDimension(
                stdRenderBabylon_state.height, level - 1);
            const float aParams[4] = { 1.0f / (float)sourceWidth, 1.0f / (float)sourceHeight, 0.0f, 0.0f };
            if ( !stdRenderBabylon_RecordBloomPass(list, stdRenderBabylon_state.bloomDownsamplePipeline,
                stdRenderBabylon_state.aBloomTargets[level - 2], stdRenderBabylon_state.finalSampler,
                stdRenderBabylon_state.aBloomTargets[level - 1], render::LoadOp::DontCare, aParams) )
            {
                return false;
            }
        }

        for ( int level = stdRenderBabylon_kBloomLevels - 1; level >= 1; --level )
        {
            const uint32_t sourceWidth = stdRenderBabylon_BloomDimension(
                stdRenderBabylon_state.width, level + 1);
            const uint32_t sourceHeight = stdRenderBabylon_BloomDimension(
                stdRenderBabylon_state.height, level + 1);
            const float aParams[4] = { 1.0f / (float)sourceWidth, 1.0f / (float)sourceHeight, 0.0f, 0.0f };
            if ( !stdRenderBabylon_RecordBloomPass(list, stdRenderBabylon_state.bloomUpsamplePipeline,
                stdRenderBabylon_state.aBloomTargets[level], stdRenderBabylon_state.finalSampler,
                stdRenderBabylon_state.aBloomTargets[level - 1], render::LoadOp::Load, aParams) )
            {
                return false;
            }
        }

        const float aComposite[4] = { stdRenderBabylon_state.bloom, 0.0f, 0.0f, 0.0f };
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.bloomCompositeTarget, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.bloomCompositePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, scene,
            stdRenderBabylon_state.presentSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.aBloomTargets[0],
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0,
            std::as_bytes(std::span<const float>(aComposite)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bBloomRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetBloom(float strength)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBloom: not open.\n");
        return false;
    }

    if ( !std::isfinite(strength) )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBloom: bloom %g is not finite.\n", (double)strength);
        return false;
    }

    const float maxStrength = stdRenderBabylon_aBloomStrength[stdRenderBabylon_kBloomMaxLevel];
    const float clamped     = std::clamp(strength, 0.0f, maxStrength);
    if ( clamped != strength )
    {
        STDLOG_STATUS("stdRenderBabylon: bloom %g is outside 0..%g; using %g.\n",
            (double)strength, (double)maxStrength, (double)clamped);
    }

    stdRenderBabylon_state.bloom             = clamped;
    stdRenderBabylon_state.bBloomSetupFailed = false;
    stdRenderBabylon_state.bBloomRan         = false;
    stdRenderBabylon_state.bBloomLogged      = false;
    return true;
}

float stdRenderBabylon_GetBloom()
{
    return stdRenderBabylon_state.bloom;
}
