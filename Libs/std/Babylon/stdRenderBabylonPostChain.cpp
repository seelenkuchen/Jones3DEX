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
// The post-processing chain and its scene sources in the Babylon render adapter.
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
    // Makes the two post targets at the back buffer's size when they are missing. Their format is
    // the scene's, so an Rgba16F scene keeps values outside 0..1 through the passes before the tone
    // map. False, logging why, when one can't be created; there are none then.
    bool stdRenderBabylon_EnsurePostTargets()
    {
        if ( !stdRenderBabylon_state.aPostTargets[0].isNull() )
        {
            return true;
        }

        render::TextureDesc desc;
        desc.width     = stdRenderBabylon_state.width;
        desc.height    = stdRenderBabylon_state.height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        for ( render::TextureHandle& target : stdRenderBabylon_state.aPostTargets )
        {
            target = stdRenderBabylon_state.pDevice->createTexture(desc);
            if ( target.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u post-processing targets.\n", desc.width, desc.height);
                stdRenderBabylon_FreePostTargets();
                return false;
            }
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    void stdRenderBabylon_FreePostTargets()
    {
        for ( render::TextureHandle& target : stdRenderBabylon_state.aPostTargets )
        {
            stdRenderBabylon_DestroyTexture(target);
            target = {};
        }
    }

    // The texture the post chain leaves for the next stage: the last post pass's output, or the back
    // buffer color.
    render::TextureHandle stdRenderBabylon_ChainSource()
    {
        const size_t numPasses = stdRenderBabylon_state.aPostPasses.size();
        if ( numPasses == 0 || stdRenderBabylon_state.aPostTargets[0].isNull() )
        {
            return stdRenderBabylon_state.colorTarget;
        }

        return stdRenderBabylon_state.aPostTargets[(numPasses - 1) % 2];
    }

    // Added: the scene after the post chain, the water-ripple stage, the ambient-occlusion stage,
    // the motion-blur stage, the depth-of-field stage and the light-shaft stage, which bloom then
    // reads.
    render::TextureHandle stdRenderBabylon_SceneSource()
    {
        if ( stdRenderBabylon_state.bFlareRan
            && !stdRenderBabylon_state.flareColorTarget.isNull() )
        {
            return stdRenderBabylon_state.flareColorTarget;
        }

        if ( stdRenderBabylon_state.bShaftsRan
            && !stdRenderBabylon_state.shaftColorTarget.isNull() )
        {
            return stdRenderBabylon_state.shaftColorTarget;
        }

        if ( stdRenderBabylon_state.bDofRan
            && !stdRenderBabylon_state.dofColorTarget.isNull() )
        {
            return stdRenderBabylon_state.dofColorTarget;
        }

        if ( stdRenderBabylon_state.bMotionBlurRan
            && !stdRenderBabylon_state.motionBlurColorTarget.isNull() )
        {
            return stdRenderBabylon_state.motionBlurColorTarget;
        }

        if ( stdRenderBabylon_state.bSsaoRan
            && !stdRenderBabylon_state.ssaoColorTarget.isNull() )
        {
            return stdRenderBabylon_state.ssaoColorTarget;
        }

        if ( stdRenderBabylon_state.bWaterRan
            && !stdRenderBabylon_state.waterColorTarget.isNull() )
        {
            return stdRenderBabylon_state.waterColorTarget;
        }

        return stdRenderBabylon_ChainSource();
    }

    // The texture the final tone map samples: the bloom composite after a presenting submit ran the
    // bloom stage, else the depth-of-field output, the post chain's output or the back buffer.
    render::TextureHandle stdRenderBabylon_PresentSource()
    {
        if ( stdRenderBabylon_state.bBloomRan
            && !stdRenderBabylon_state.bloomCompositeTarget.isNull() )
        {
            return stdRenderBabylon_state.bloomCompositeTarget;
        }

        return stdRenderBabylon_SceneSource();
    }

    // The post-processing chain into the frame: pass i reads the output of pass i - 1 (the back buffer
    // color for pass 0) in fragment slot 0, the normal target in slot 1 and the depth target in slot 2
    // as far as its shader reads, and writes post target i % 2. Nothing without passes or a back buffer.
    void stdRenderBabylon_RecordPostChain(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.aPostPasses.empty() || stdRenderBabylon_state.colorTarget.isNull()
            || !stdRenderBabylon_EnsurePostTargets() )
        {
            return;
        }

        render::TextureHandle input = stdRenderBabylon_state.colorTarget;
        for ( size_t i = 0; i < stdRenderBabylon_state.aPostPasses.size(); ++i )
        {
            const StdRenderBabylonState::PostPass& postPass = stdRenderBabylon_state.aPostPasses[i];
            const render::TextureHandle output = stdRenderBabylon_state.aPostTargets[i % 2];

            render::PassDesc pass;
            pass.colorTargets.push_back(render::ColorAttachment{ output, 0, render::LoadOp::DontCare,
                render::StoreOp::Store, {} });
            if ( !list.beginPass(pass) )
            {
                return;
            }

            const render::TextureHandle aInputs[3] = { input, stdRenderBabylon_state.normalTarget, stdRenderBabylon_state.depthTarget };
            list.setPipeline(postPass.pipeline);
            for ( uint32_t slot = 0; slot < postPass.numInputs; ++slot )
            {
                list.bindTexture(render::ShaderStage::Fragment, slot, aInputs[slot], stdRenderBabylon_state.presentSampler);
            }

            list.draw(3);
            (void)list.endPass();
            input = output;
        }
    }

}

bool stdRenderBabylon_AddPostPass(const render::ShaderDesc& fragmentShader)
{
    render::Device* pDevice = stdRenderBabylon_state.pDevice;
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: not open.\n");
        return false;
    }

    if ( fragmentShader.textureCount < 1 || fragmentShader.textureCount > 3 || fragmentShader.uniformCount != 0 )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: the shader reads %u textures and %u uniform blocks; a post pass reads 1 to 3 textures and no uniforms.\n",
            fragmentShader.textureCount, fragmentShader.uniformCount);
        return false;
    }

    if ( fragmentShader.textureCount == 3 && !stdRenderBabylon_state.bDepthSampled )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: the shader reads depth, which this device can't sample.\n");
        return false;
    }

    render::PipelineDesc desc;
    desc.vertexShader   = stdBabylon::shaders::presentVert();
    desc.fragmentShader = fragmentShader;
    desc.primitive      = render::PrimitiveType::TriangleList;
    // The chain's targets have the scene's format, so HDR values survive to the tone map.
    desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

    const render::PipelineHandle pipeline = pDevice->createPipeline(desc);
    if ( pipeline.isNull() || !stdRenderBabylon_EnsurePresentSampler() )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: could not create the pass's pipeline.\n");
        pDevice->destroyPipeline(pipeline);
        return false;
    }

    stdRenderBabylon_state.aPostPasses.push_back(StdRenderBabylonState::PostPass{ pipeline, fragmentShader.textureCount });
    return true;
}

void stdRenderBabylon_ClearPostPasses()
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    for ( const StdRenderBabylonState::PostPass& postPass : stdRenderBabylon_state.aPostPasses )
    {
        stdRenderBabylon_state.pDevice->destroyPipeline(postPass.pipeline);
    }

    stdRenderBabylon_state.aPostPasses.clear();
    stdRenderBabylon_FreePostTargets();
}

size_t stdRenderBabylon_GetNumPostPasses()
{
    return stdRenderBabylon_state.aPostPasses.size();
}
