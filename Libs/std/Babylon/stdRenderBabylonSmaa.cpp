// The anti-aliasing (SMAA 1x) stage of the Babylon render adapter.
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
#include "SmaaTextures.h"

namespace render = babylon::render;
using namespace stdRenderBabylonInternal;

namespace
{
    // Added: the three presented-size targets of the anti-aliasing stage, made when their size
    // changes. False, logging why, when one can't be created; there are none then.
    bool stdRenderBabylon_EnsureSmaaTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.smaaColorTarget.isNull()
            && stdRenderBabylon_state.smaaWidth == width
            && stdRenderBabylon_state.smaaHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeSmaaTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle color   = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle edges   = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle weights = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( color.isNull() || edges.isNull() || weights.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u anti-aliasing targets.\n",
                width, height);
            stdRenderBabylon_state.pDevice->destroyTexture(color);
            stdRenderBabylon_state.pDevice->destroyTexture(edges);
            stdRenderBabylon_state.pDevice->destroyTexture(weights);
            return false;
        }

        stdRenderBabylon_state.smaaColorTarget   = color;
        stdRenderBabylon_state.smaaEdgesTarget   = edges;
        stdRenderBabylon_state.smaaWeightsTarget = weights;
        stdRenderBabylon_state.smaaWidth         = width;
        stdRenderBabylon_state.smaaHeight        = height;
        return true;
    }

    // Added: an Rgba8, linearly sampled texture from SMAA's RG8 or R8 precomputed data. Babylon has
    // no two- or one-channel format, so the extra channels are padded. Null, logging why, on
    // failure.
    render::TextureHandle stdRenderBabylon_CreateSmaaDataTexture(uint32_t width, uint32_t height,
        const unsigned char* pSrc, uint32_t numChannels)
    {
        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::Sampled;

        const render::TextureHandle texture = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( texture.isNull() )
        {
            return {};
        }

        std::vector<std::byte> data((size_t)width * height * 4u);
        for ( size_t i = 0; i < (size_t)width * height; ++i )
        {
            data[i * 4u + 0u] = (std::byte)pSrc[i * numChannels + 0u];
            data[i * 4u + 1u] = numChannels > 1u ? (std::byte)pSrc[i * numChannels + 1u]
                                                 : (std::byte)0;
            data[i * 4u + 2u] = (std::byte)0;
            data[i * 4u + 3u] = (std::byte)0xFF;
        }

        if ( !stdRenderBabylon_state.pDevice->uploadTexture(texture, 0,
            std::span<const std::byte>(data)) )
        {
            stdRenderBabylon_state.pDevice->destroyTexture(texture);
            return {};
        }

        return texture;
    }

    // Added: the pipelines and precomputed textures of the anti-aliasing stage, made on first use.
    bool stdRenderBabylon_EnsureSmaaResources()
    {
        if ( stdRenderBabylon_state.bSmaaSetupFailed )
        {
            return false;
        }

        if ( stdRenderBabylon_state.smaaAreaTexture.isNull() )
        {
            stdRenderBabylon_state.smaaAreaTexture = stdRenderBabylon_CreateSmaaDataTexture(
                stdSmaaTextures::kAreaTexWidth, stdSmaaTextures::kAreaTexHeight,
                stdSmaaTextures::kAreaTexBytes, 2u);
            if ( stdRenderBabylon_state.smaaAreaTexture.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create SMAA's area texture.\n");
                stdRenderBabylon_state.bSmaaSetupFailed = true;
                return false;
            }
        }

        if ( stdRenderBabylon_state.smaaSearchTexture.isNull() )
        {
            stdRenderBabylon_state.smaaSearchTexture = stdRenderBabylon_CreateSmaaDataTexture(
                stdSmaaTextures::kSearchTexWidth, stdSmaaTextures::kSearchTexHeight,
                stdSmaaTextures::kSearchTexBytes, 1u);
            if ( stdRenderBabylon_state.smaaSearchTexture.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create SMAA's search texture.\n");
                stdRenderBabylon_state.bSmaaSetupFailed = true;
                return false;
            }
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ render::TextureFormat::Rgba8, {} } };

        if ( stdRenderBabylon_state.smaaToneMapPipeline.isNull() )
        {
            desc.fragmentShader = stdRenderBabylon_FinalFrag();
            stdRenderBabylon_state.smaaToneMapPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.smaaEdgePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::smaaEdgeFrag();
            stdRenderBabylon_state.smaaEdgePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.smaaWeightsPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::smaaWeightsFrag();
            stdRenderBabylon_state.smaaWeightsPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.smaaToneMapPipeline.isNull()
            || stdRenderBabylon_state.smaaEdgePipeline.isNull()
            || stdRenderBabylon_state.smaaWeightsPipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the anti-aliasing passes.\n");
            stdRenderBabylon_state.bSmaaSetupFailed = true;
            return false;
        }

        return true;
    }

    // Added: the last anti-aliasing pass's pipeline for its target: the swapchain, a Bgra8 capture
    // or the Rgba8 sharpening source. Made on first use; false when it can't be.
    bool stdRenderBabylon_EnsureSmaaBlendPipeline(bool bSwapchain, bool bCasSource,
        render::TextureFormat format)
    {
        render::PipelineHandle* pPipeline = bSwapchain
            ? &stdRenderBabylon_state.smaaBlendPipeline
            : (bCasSource ? &stdRenderBabylon_state.smaaBlendCasPipeline
                          : &stdRenderBabylon_state.smaaBlendCapturePipeline);
        if ( !pPipeline->isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::smaaBlendFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ format, {} } };

        *pPipeline = stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !pPipeline->isNull();
    }

}

namespace stdRenderBabylonInternal
{
    // Added: frees the anti-aliasing stage's presented-size targets. Recreated by the next present
    // that anti-aliases after a display or window size change.
    void stdRenderBabylon_FreeSmaaTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaColorTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaEdgesTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaWeightsTarget);
        stdRenderBabylon_state.smaaColorTarget   = {};
        stdRenderBabylon_state.smaaEdgesTarget   = {};
        stdRenderBabylon_state.smaaWeightsTarget = {};
        stdRenderBabylon_state.smaaWidth         = 0;
        stdRenderBabylon_state.smaaHeight        = 0;
    }

    // Added: the SMAA 1x stage over the tone-mapped image, before the HUD composite: the tone map
    // into an LDR target (presented size), luma edges, blending weights with SMAA's area and search
    // textures, then neighbourhood blending into target (the swapchain for a null handle). False,
    // having logged nothing, when a resource can't be made or a pass can't be started or ended; the
    // caller then presents without anti-aliasing.
    bool stdRenderBabylon_RecordSmaa(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format, uint32_t width, uint32_t height, bool bCasSource)
    {
        if ( width == 0 || height == 0 || !stdRenderBabylon_EnsureSmaaResources()
            || !stdRenderBabylon_EnsureSmaaTargets(width, height)
            || !stdRenderBabylon_EnsureSmaaBlendPipeline(target.isNull(), bCasSource, format) )
        {
            return false;
        }

        // The final pass's tone map into the LDR image, with the same source and sampling rules.
        if ( !stdRenderBabylon_RecordFinalPass(list, stdRenderBabylon_state.smaaToneMapPipeline,
            stdRenderBabylon_state.smaaColorTarget) )
        {
            return false;
        }

        const float aParams[4] = { 1.0f / (float)width, 1.0f / (float)height,
            (float)width, (float)height };

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.smaaEdgesTarget,
            0, render::LoadOp::Clear, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.smaaEdgePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaColorTarget,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        pass.colorTargets[0] = render::ColorAttachment{ stdRenderBabylon_state.smaaWeightsTarget,
            0, render::LoadOp::DontCare, render::StoreOp::Store, {} };
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.smaaWeightsPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaEdgesTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.smaaAreaTexture,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, stdRenderBabylon_state.smaaSearchTexture,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        const render::PipelineHandle blend = target.isNull()
            ? stdRenderBabylon_state.smaaBlendPipeline
            : (bCasSource ? stdRenderBabylon_state.smaaBlendCasPipeline
                          : stdRenderBabylon_state.smaaBlendCapturePipeline);
        pass.colorTargets[0] = render::ColorAttachment{ target, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} };
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(blend);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaColorTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.smaaWeightsTarget,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        return list.endPass();
    }

}

bool stdRenderBabylon_SetAntiAliasing(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetAntiAliasing: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bAntiAliasing    = bEnabled;
    stdRenderBabylon_state.bSmaaSetupFailed = false;
    return true;
}

bool stdRenderBabylon_GetAntiAliasing()
{
    return stdRenderBabylon_state.bAntiAliasing;
}
