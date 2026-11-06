// Added: the Babylon column's render frame owner (stdRenderBabylon.h). Adapter code: no Fixed/Added
// markers inside.
//
// The back buffer reaches the window through a pass to the swapchain that draws one full-screen
// triangle sampling the color target (Shaders/present.*.hlsl). Babylon's copyTexture can't name the
// swapchain (its null handle means the swapchain only as a pass attachment), so a pass is the only
// way the render interface offers to put an offscreen target on the screen.
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
        render::TextureFormat depthFormat = render::TextureFormat::D24S8;
        uint32_t width  = 0;
        uint32_t height = 0;

        render::PipelineHandle presentPipeline;
        render::SamplerHandle presentSampler;

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

    void stdRenderBabylon_FreeBackBuffer()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.colorTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.depthTarget);
        stdRenderBabylon_state.colorTarget = {};
        stdRenderBabylon_state.depthTarget = {};
        stdRenderBabylon_state.width       = 0;
        stdRenderBabylon_state.height      = 0;
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
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.colorTarget,
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

    const render::DeviceInfo& info = pDevice->info();
    if ( info.hasSwapchain )
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::presentFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ info.swapchainFormat, {} } };

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Nearest;
        samplerDesc.magFilter = render::Filter::Nearest;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;

        stdRenderBabylon_state.presentPipeline = pDevice->createPipeline(desc);
        stdRenderBabylon_state.presentSampler  = pDevice->createSampler(samplerDesc);
        if ( stdRenderBabylon_state.presentPipeline.isNull() || stdRenderBabylon_state.presentSampler.isNull() )
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
    depthDesc.usage     = render::TextureUsage::DepthTarget;

    const render::TextureHandle colorTarget = pDevice->createTexture(colorDesc);
    const render::TextureHandle depthTarget = pDevice->createTexture(depthDesc);
    if ( colorTarget.isNull() || depthTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: could not create the %ux%u back buffer.\n", width, height);
        pDevice->destroyTexture(colorTarget);
        pDevice->destroyTexture(depthTarget);
        return false;
    }

    stdRenderBabylon_state.colorTarget = colorTarget;
    stdRenderBabylon_state.depthTarget = depthTarget;
    stdRenderBabylon_state.width       = width;
    stdRenderBabylon_state.height      = height;
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

bool stdRenderBabylon_SubmitFrame()
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
        if ( stdRenderBabylon_state.pDevice->info().hasSwapchain )
        {
            stdRenderBabylon_RecordPresent(list);
        }

        bSubmitted = stdRenderBabylon_state.pDevice->submit(list);
        if ( !bSubmitted )
        {
            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: the device refused the frame.\n");
        }
    }

    stdRenderBabylon_state.frame.reset();
    stdRenderBabylon_DestroyDeferred();
    return bSubmitted;
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
