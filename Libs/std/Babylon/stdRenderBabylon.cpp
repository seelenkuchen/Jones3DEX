// Added: the Babylon column's render frame owner (stdRenderBabylon.h). Adapter code: no Fixed/Added
// markers inside.
//
// The back buffer reaches the window through a pass to the swapchain that draws one full-screen
// triangle sampling the color target (Shaders/present.*.hlsl). Babylon's copyTexture can't name the
// swapchain (its null handle means the swapchain only as a pass attachment), so a pass is the only
// way the render interface offers to put an offscreen target on the screen.
//
// A presenting submit runs the post-processing chain first: each pass draws the same full-screen
// triangle with its own fragment shader into one of two Bgra8 targets the chain owns, reading the
// output of the pass before (the back buffer color for the first) and, as its shader declares, the
// normal and depth targets. The present pass then samples the last output instead of the back
// buffer. The back buffer itself is never written by the chain.
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <std/General/std.h>

#include "stdRenderBabylon.h"

namespace
{
    namespace render = babylon::render;

    struct StdRenderBabylonState
    {
        render::Device* pDevice = nullptr;
        std::optional<render::CommandList> frame;

        render::TextureHandle colorTarget;
        render::TextureHandle depthTarget;
        render::TextureHandle normalTarget;
        render::TextureFormat depthFormat  = render::TextureFormat::D24S8;
        render::TextureFormat normalFormat = render::TextureFormat::R10G10B10A2;
        bool bDepthSampled = false; // the depth target is also Sampled, so post passes can read it
        uint32_t width  = 0;
        uint32_t height = 0;

        // Changes whenever the back buffer's contents may change (stdRenderBabylon_GetBackBufferVersion).
        uint64_t backBufferVersion = 0;

        render::PipelineHandle presentPipeline;
        render::SamplerHandle presentSampler; // also the post passes' sampler

        // The post-processing chain: one pipeline per pass, in order, with the number of inputs its
        // shader reads, and the two targets the passes write in turn (made at the first presenting
        // submit that needs them, freed with the back buffer).
        struct PostPass
        {
            render::PipelineHandle pipeline;
            uint32_t numInputs = 0;
        };
        std::vector<PostPass> aPostPasses;
        render::TextureHandle aPostTargets[2];

        std::vector<render::TextureHandle> aDeferredTextures;
        std::vector<render::BufferHandle> aDeferredBuffers;
    };

    StdRenderBabylonState stdRenderBabylon_state;

    void stdRenderBabylon_DestroyDeferred()
    {
        for ( const render::TextureHandle texture : stdRenderBabylon_state.aDeferredTextures )
        {
            stdRenderBabylon_state.pDevice->destroyTexture(texture);
        }

        stdRenderBabylon_state.aDeferredTextures.clear();

        for ( const render::BufferHandle buffer : stdRenderBabylon_state.aDeferredBuffers )
        {
            stdRenderBabylon_state.pDevice->destroyBuffer(buffer);
        }

        stdRenderBabylon_state.aDeferredBuffers.clear();
    }

    // The nearest-filtering, clamping sampler of the present and post passes, made on first use.
    bool stdRenderBabylon_EnsurePresentSampler()
    {
        if ( !stdRenderBabylon_state.presentSampler.isNull() )
        {
            return true;
        }

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Nearest;
        samplerDesc.magFilter = render::Filter::Nearest;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;
        stdRenderBabylon_state.presentSampler = stdRenderBabylon_state.pDevice->createSampler(samplerDesc);
        return !stdRenderBabylon_state.presentSampler.isNull();
    }

    void stdRenderBabylon_FreePostTargets()
    {
        for ( render::TextureHandle& target : stdRenderBabylon_state.aPostTargets )
        {
            stdRenderBabylon_DestroyTexture(target);
            target = {};
        }
    }

    // Makes the two post targets at the back buffer's size when they are missing. False, logging why,
    // when one can't be created; there are none then.
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
        desc.format    = render::TextureFormat::Bgra8;
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

    // The texture the present pass samples: the last post pass's output, or the back buffer color.
    render::TextureHandle stdRenderBabylon_PresentSource()
    {
        const size_t numPasses = stdRenderBabylon_state.aPostPasses.size();
        if ( numPasses == 0 || stdRenderBabylon_state.aPostTargets[0].isNull() )
        {
            return stdRenderBabylon_state.colorTarget;
        }

        return stdRenderBabylon_state.aPostTargets[(numPasses - 1) % 2];
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

    // True when list has a pass with the back buffer's color target as an attachment or a copy into it.
    bool stdRenderBabylon_DrawsBackBuffer(const render::CommandList& list)
    {
        const render::TextureHandle colorTarget = stdRenderBabylon_state.colorTarget;
        if ( colorTarget.isNull() )
        {
            return false;
        }

        for ( const render::Command& command : list.commands() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                for ( const render::ColorAttachment& attachment : pBegin->desc.colorTargets )
                {
                    if ( attachment.target == colorTarget )
                    {
                        return true;
                    }
                }
            }
            else if ( const render::CopyTexture* pCopy = std::get_if<render::CopyTexture>(&command) )
            {
                if ( pCopy->destination == colorTarget )
                {
                    return true;
                }
            }
        }

        return false;
    }

    // The present pass: the back buffer drawn over the whole swapchain image. The vertex shader
    // makes the triangle itself, so nothing is bound but the color target.
    void stdRenderBabylon_RecordPresent(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.presentPipeline.isNull() || stdRenderBabylon_state.colorTarget.isNull() )
        {
            return;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ render::TextureHandle{}, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return;
        }

        list.setPipeline(stdRenderBabylon_state.presentPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_PresentSource(),
            stdRenderBabylon_state.presentSampler);
        list.draw(3);
        (void)list.endPass();
    }
}

bool stdRenderBabylon_TakeRenderOption(int* pArgc, char** argv, StdRenderBabylonDeviceType* pType)
{
    if ( !pArgc || !argv || !pType )
    {
        return false;
    }

    // Validate every pair first, so a bad one changes nothing.
    StdRenderBabylonDeviceType type = *pType;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--render") != 0 )
        {
            continue;
        }

        if ( i + 1 >= *pArgc )
        {
            return false;
        }

        const char* pValue = argv[++i];
        if ( std::strcmp(pValue, "null") == 0 )
        {
            type = StdRenderBabylonDeviceType::Null;
        }
        else if ( std::strcmp(pValue, "gpu") == 0 )
        {
            type = StdRenderBabylonDeviceType::Gpu;
        }
        else
        {
            return false;
        }
    }

    int numKept = 1;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--render") == 0 )
        {
            ++i;
            continue;
        }

        argv[numKept++] = argv[i];
    }

    argv[numKept] = nullptr;
    *pArgc        = numKept;
    *pType        = type;
    return true;
}

bool stdRenderBabylon_Open(render::Device* pDevice)
{
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_Open: no render device.\n");
        return false;
    }

    if ( stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_Open: already open.\n");
        return false;
    }

    stdRenderBabylon_state             = StdRenderBabylonState{};
    stdRenderBabylon_state.pDevice     = pDevice;
    stdRenderBabylon_state.depthFormat = pDevice->supportsFormat(render::TextureFormat::D24S8, render::TextureUsage::DepthTarget)
        ? render::TextureFormat::D24S8
        : render::TextureFormat::D32F;

    // The depth format stays the one the scene always used; it is only also sampled where it can be.
    stdRenderBabylon_state.bDepthSampled = pDevice->supportsFormat(stdRenderBabylon_state.depthFormat,
        render::TextureUsage::DepthTarget | render::TextureUsage::Sampled);

    // R10G10B10A2 holds a normal's components with 10 bits each in 4 bytes; Rgba8 where the device
    // can't render to it.
    const render::TextureUsage normalUsage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
    stdRenderBabylon_state.normalFormat = pDevice->supportsFormat(render::TextureFormat::R10G10B10A2, normalUsage)
        ? render::TextureFormat::R10G10B10A2
        : render::TextureFormat::Rgba8;

    const render::DeviceInfo& info = pDevice->info();
    if ( info.hasSwapchain )
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::presentFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ info.swapchainFormat, {} } };

        stdRenderBabylon_state.presentPipeline = pDevice->createPipeline(desc);
        if ( stdRenderBabylon_state.presentPipeline.isNull() || !stdRenderBabylon_EnsurePresentSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon_Open: could not create the present pipeline.\n");
            pDevice->destroyPipeline(stdRenderBabylon_state.presentPipeline);
            pDevice->destroySampler(stdRenderBabylon_state.presentSampler);
            stdRenderBabylon_state = StdRenderBabylonState{};
            return false;
        }
    }

    return true;
}

void stdRenderBabylon_Close()
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    stdRenderBabylon_state.frame.reset();
    stdRenderBabylon_ClearPostPasses();
    stdRenderBabylon_FreeBackBuffer();
    stdRenderBabylon_DestroyDeferred();

    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.presentPipeline);
    stdRenderBabylon_state.pDevice->destroySampler(stdRenderBabylon_state.presentSampler);
    stdRenderBabylon_state = StdRenderBabylonState{};
}

bool stdRenderBabylon_IsOpen()
{
    return stdRenderBabylon_state.pDevice != nullptr;
}

render::Device* stdRenderBabylon_GetDevice()
{
    return stdRenderBabylon_state.pDevice;
}

void stdRenderBabylon_FreeBackBuffer()
{
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.colorTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.depthTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.normalTarget);
    stdRenderBabylon_FreePostTargets();
    stdRenderBabylon_state.colorTarget  = {};
    stdRenderBabylon_state.depthTarget  = {};
    stdRenderBabylon_state.normalTarget = {};
    stdRenderBabylon_state.width       = 0;
    stdRenderBabylon_state.height      = 0;
    ++stdRenderBabylon_state.backBufferVersion;
}

bool stdRenderBabylon_SetBackBufferSize(uint32_t width, uint32_t height, bool* pbRecreated)
{
    if ( pbRecreated )
    {
        *pbRecreated = false;
    }

    render::Device* pDevice = stdRenderBabylon_state.pDevice;
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: not open.\n");
        return false;
    }

    if ( !stdRenderBabylon_state.colorTarget.isNull()
        && stdRenderBabylon_state.width == width
        && stdRenderBabylon_state.height == height )
    {
        return true;
    }

    stdRenderBabylon_FreeBackBuffer();

    const uint32_t maxSize = pDevice->info().limits.maxTextureDimension;
    if ( width == 0 || height == 0 || width > maxSize || height > maxSize )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: invalid back buffer size %ux%u (limit %u).\n",
            width, height, maxSize);
        return false;
    }

    render::TextureDesc colorDesc;
    colorDesc.width     = width;
    colorDesc.height    = height;
    colorDesc.mipLevels = 1;
    colorDesc.format    = render::TextureFormat::Bgra8;
    colorDesc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

    render::TextureDesc depthDesc;
    depthDesc.width     = width;
    depthDesc.height    = height;
    depthDesc.mipLevels = 1;
    depthDesc.format    = stdRenderBabylon_state.depthFormat;
    depthDesc.usage     = stdRenderBabylon_state.bDepthSampled ? render::TextureUsage::DepthTarget | render::TextureUsage::Sampled : render::TextureUsage::DepthTarget;

    render::TextureDesc normalDesc = colorDesc;
    normalDesc.format = stdRenderBabylon_state.normalFormat;

    const render::TextureHandle colorTarget  = pDevice->createTexture(colorDesc);
    const render::TextureHandle depthTarget  = pDevice->createTexture(depthDesc);
    const render::TextureHandle normalTarget = pDevice->createTexture(normalDesc);
    if ( colorTarget.isNull() || depthTarget.isNull() || normalTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: could not create the %ux%u back buffer.\n", width, height);
        pDevice->destroyTexture(colorTarget);
        pDevice->destroyTexture(depthTarget);
        pDevice->destroyTexture(normalTarget);
        return false;
    }

    stdRenderBabylon_state.colorTarget  = colorTarget;
    stdRenderBabylon_state.depthTarget  = depthTarget;
    stdRenderBabylon_state.normalTarget = normalTarget;
    stdRenderBabylon_state.width       = width;
    stdRenderBabylon_state.height      = height;
    ++stdRenderBabylon_state.backBufferVersion; // undefined until something clears or uploads it
    if ( pbRecreated )
    {
        *pbRecreated = true;
    }

    return true;
}

bool stdRenderBabylon_ReadBackBuffer(void* pOut, uint32_t* pWidth, uint32_t* pHeight)
{
    if ( pWidth )
    {
        *pWidth = 0;
    }

    if ( pHeight )
    {
        *pHeight = 0;
    }

    if ( !pOut )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: no destination buffer.\n");
        return false;
    }

    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: no back buffer.\n");
        return false;
    }

    const uint32_t width  = stdRenderBabylon_state.width;
    const uint32_t height = stdRenderBabylon_state.height;
    const uint64_t size   = (uint64_t)width * (uint64_t)height * 4u;
    if ( size > (uint64_t)SIZE_MAX )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the %ux%u back buffer is too large.\n", width, height);
        return false;
    }

    if ( !stdRenderBabylon_FlushFrame() )
    {
        return false;
    }

    const std::vector<std::byte> pixels = stdRenderBabylon_state.pDevice->readTexture(stdRenderBabylon_state.colorTarget, 0);
    if ( pixels.size() != (size_t)size )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the device returned %zu bytes for a %ux%u Bgra8 buffer.\n",
            pixels.size(), width, height);
        return false;
    }

    if ( size > 0u )
    {
        std::memcpy(pOut, pixels.data(), (size_t)size);
    }

    if ( pWidth )
    {
        *pWidth = width;
    }

    if ( pHeight )
    {
        *pHeight = height;
    }

    return true;
}

render::TextureHandle stdRenderBabylon_GetColorTarget()
{
    return stdRenderBabylon_state.colorTarget;
}

render::TextureHandle stdRenderBabylon_GetDepthTarget()
{
    return stdRenderBabylon_state.depthTarget;
}

render::TextureFormat stdRenderBabylon_GetDepthFormat()
{
    return stdRenderBabylon_state.depthFormat;
}

render::TextureHandle stdRenderBabylon_GetNormalTarget()
{
    return stdRenderBabylon_state.normalTarget;
}

render::TextureFormat stdRenderBabylon_GetNormalFormat()
{
    return stdRenderBabylon_state.normalFormat;
}

uint32_t stdRenderBabylon_GetBackBufferWidth()
{
    return stdRenderBabylon_state.width;
}

uint32_t stdRenderBabylon_GetBackBufferHeight()
{
    return stdRenderBabylon_state.height;
}

render::CommandList& stdRenderBabylon_GetFrame()
{
    STD_ASSERTREL(stdRenderBabylon_state.pDevice);
    if ( !stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.frame.emplace(stdRenderBabylon_state.pDevice->beginFrame());
    }

    return *stdRenderBabylon_state.frame;
}

bool stdRenderBabylon_HasFrame()
{
    return stdRenderBabylon_state.frame.has_value();
}

namespace
{
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
            if ( bPresent )
            {
                stdRenderBabylon_RecordPostChain(list);
            }

            if ( bPresent && stdRenderBabylon_state.pDevice->info().hasSwapchain )
            {
                stdRenderBabylon_RecordPresent(list);
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

uint64_t stdRenderBabylon_GetBackBufferVersion()
{
    const bool bPendingDraws = stdRenderBabylon_state.frame && stdRenderBabylon_DrawsBackBuffer(*stdRenderBabylon_state.frame);
    return stdRenderBabylon_state.backBufferVersion + (bPendingDraws ? 1u : 0u);
}

bool stdRenderBabylon_WriteBackBuffer(const void* pData)
{
    if ( !pData )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: no source pixels.\n");
        return false;
    }

    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: no back buffer.\n");
        return false;
    }

    if ( !stdRenderBabylon_FlushFrame() )
    {
        return false;
    }

    const size_t size = (size_t)stdRenderBabylon_state.width * stdRenderBabylon_state.height * 4u;
    const std::span<const std::byte> data(static_cast<const std::byte*>(pData), size);
    if ( !stdRenderBabylon_state.pDevice->uploadTexture(stdRenderBabylon_state.colorTarget, 0, data) )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: the device refused the %ux%u upload.\n",
            stdRenderBabylon_state.width, stdRenderBabylon_state.height);
        return false;
    }

    ++stdRenderBabylon_state.backBufferVersion;
    return true;
}

bool stdRenderBabylon_ClearBackBuffer(const render::ClearColor& color)
{
    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_ClearBackBuffer: no back buffer.\n");
        return false;
    }

    render::CommandList& list = stdRenderBabylon_GetFrame();
    render::PassDesc pass;
    pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.colorTarget, 0,
        render::LoadOp::Clear, render::StoreOp::Store, color });
    if ( !list.beginPass(pass) || !list.endPass() )
    {
        STDLOG_ERROR("stdRenderBabylon_ClearBackBuffer: the frame refused the clear pass.\n");
        return false;
    }

    return true;
}

void stdRenderBabylon_DestroyTexture(render::TextureHandle texture)
{
    if ( texture.isNull() || !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    if ( stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.aDeferredTextures.push_back(texture);
        return;
    }

    stdRenderBabylon_state.pDevice->destroyTexture(texture);
}

void stdRenderBabylon_DestroyBuffer(render::BufferHandle buffer)
{
    if ( buffer.isNull() || !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    if ( stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.aDeferredBuffers.push_back(buffer);
        return;
    }

    stdRenderBabylon_state.pDevice->destroyBuffer(buffer);
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
    desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, {} } };

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

render::TextureHandle stdRenderBabylon_GetPresentSource()
{
    return stdRenderBabylon_PresentSource();
}
