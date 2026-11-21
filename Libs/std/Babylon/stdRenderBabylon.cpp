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
// normal and depth targets. The final tone-map pass then samples the last output instead of the
// back buffer and writes the frame (the swapchain, or the target of RenderPresentToTexture). Its
// shader and present.vert are the plain present pass plus exposure and the operator; at the defaults
// (exposure 1, operator 0) it clamps to 0..1 and presents the same image as the pre-M9.3 present.
// The back buffer itself is never written by the chain or the final pass.
//
// The scene color target is Rgba16F on a device that can render and sample it, else Bgra8, so
// shaders may write values outside 0..1 for a later tone map. ReadBackBuffer and WriteBackBuffer
// translate between that target and the Bgra8 bytes the game and stdDisplay use.
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

#include "stdBabylon.h"
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
        render::TextureFormat colorFormat  = render::TextureFormat::Bgra8;
        render::TextureFormat depthFormat  = render::TextureFormat::D24S8;
        render::TextureFormat normalFormat = render::TextureFormat::R10G10B10A2;
        bool bDepthSampled = false; // the depth target is also Sampled, so post passes can read it
        uint32_t width  = 0;
        uint32_t height = 0;

        // The final pass's tone map, and whether it runs the tone-map shader (false only when a test
        // forced the pre-M9.3 path). At the defaults it is an identity clamp.
        StdRenderBabylonToneMap toneMap;
        bool bToneMap = true;

        // Changes whenever the back buffer's contents may change (stdRenderBabylon_GetBackBufferVersion).
        uint64_t backBufferVersion = 0;

        render::PipelineHandle presentPipeline;
        render::PipelineHandle capturePipeline; // Bgra8, for RenderPresentToTexture
        render::SamplerHandle presentSampler; // also the post passes' sampler

        // The pass a caller records on top of the presented frame, and the
        // offscreen target of stdRenderBabylon_RenderPresentToTexture.
        StdRenderBabylonPresentOverlay presentOverlay = nullptr;
        void* presentOverlayUser = nullptr;

        // Added: the gameplay HUD overlay, drawn before the present overlay so a menu can cover it.
        StdRenderBabylonPresentOverlay hudOverlay = nullptr;
        void* hudOverlayUser = nullptr;

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

#ifdef J3D_TEST
    bool stdRenderBabylon_bLegacyPresentationForTest = false;
#endif

    // An IEEE 754 half's bits as a float.
    float stdRenderBabylon_HalfToFloat(uint16_t half)
    {
        const uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
        uint32_t exponent  = (half >> 10) & 0x1Fu;
        uint32_t mantissa  = half & 0x3FFu;
        uint32_t bits;
        if ( exponent == 0 )
        {
            if ( mantissa == 0 )
            {
                bits = sign;
            }
            else
            {
                // Subnormal: shift the mantissa up into the float's normal range.
                exponent = 127 - 15 + 1;
                while ( (mantissa & 0x400u) == 0 )
                {
                    mantissa <<= 1;
                    --exponent;
                }
                bits = sign | (exponent << 23) | ((mantissa & 0x3FFu) << 13);
            }
        }
        else if ( exponent == 0x1Fu )
        {
            bits = sign | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
        }

        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    // A float as an IEEE 754 half's bits, rounding to nearest even.
    uint16_t stdRenderBabylon_FloatToHalf(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint16_t sign      = (uint16_t)((bits >> 16) & 0x8000u);
        const uint32_t exponent  = (bits >> 23) & 0xFFu;
        const uint32_t mantissa  = bits & 0x7FFFFFu;

        if ( exponent == 0xFFu )
        {
            return (uint16_t)(sign | 0x7C00u | (mantissa != 0 ? 0x0200u : 0u));
        }

        const int halfExponent = (int)exponent - 127 + 15;
        if ( halfExponent >= 0x1F )
        {
            return (uint16_t)(sign | 0x7C00u);
        }
        if ( halfExponent <= 0 )
        {
            if ( halfExponent < -10 )
            {
                return sign;
            }
            const uint32_t shift     = (uint32_t)(14 - halfExponent);
            const uint32_t shifted   = (mantissa | 0x800000u) >> shift;
            const uint32_t remainder = (mantissa | 0x800000u) & ((1u << shift) - 1u);
            const uint32_t halfway   = 1u << (shift - 1u);
            uint32_t result          = shifted;
            if ( remainder > halfway || (remainder == halfway && (result & 1u) != 0) )
            {
                ++result;
            }
            return (uint16_t)(sign | result);
        }

        uint32_t result          = ((uint32_t)halfExponent << 10) | (mantissa >> 13);
        const uint32_t remainder = mantissa & 0x1FFFu;
        if ( remainder > 0x1000u || (remainder == 0x1000u && (result & 1u) != 0) )
        {
            ++result;
        }
        return (uint16_t)(sign | result);
    }

    // A float clamped to 0..1 as an 8-bit unorm, rounded to nearest.
    uint8_t stdRenderBabylon_ToUnorm8(float value)
    {
        const float clamped = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
        return (uint8_t)std::lround(clamped * 255.0f);
    }

    // The scene color target format for this device: Rgba16F where it is a sampled color target,
    // else today's Bgra8.
    render::TextureFormat stdRenderBabylon_ChooseColorFormat(render::Device* pDevice, bool bLegacy)
    {
        const render::TextureUsage usage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        if ( !bLegacy && pDevice->supportsFormat(render::TextureFormat::Rgba16F, usage) )
        {
            return render::TextureFormat::Rgba16F;
        }
        if ( !bLegacy )
        {
            STDLOG_STATUS("stdRenderBabylon: the device can't render and sample Rgba16F; the scene color target stays Bgra8.\n");
        }
        return render::TextureFormat::Bgra8;
    }

    // The final pass's fragment shader: the tone map, or the plain present when a test forces the
    // pre-M9.3 path.
    const render::ShaderDesc& stdRenderBabylon_FinalFrag()
    {
        return stdRenderBabylon_state.bToneMap ? stdBabylon::shaders::tonemapFrag()
                                               : stdBabylon::shaders::presentFrag();
    }

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

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(pipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_PresentSource(),
            stdRenderBabylon_state.presentSampler);
        if ( stdRenderBabylon_state.bToneMap )
        {
            const float aParams[4] = { stdRenderBabylon_state.toneMap.exposure,
                (float)stdRenderBabylon_state.toneMap.oper, 0.0f, 0.0f };
            list.pushUniforms(render::ShaderStage::Fragment, 0,
                std::as_bytes(std::span<const float>(aParams)));
        }
        list.draw(3);
        return list.endPass();
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
#ifdef J3D_TEST
    stdRenderBabylon_state.bToneMap = !stdRenderBabylon_bLegacyPresentationForTest;
    stdRenderBabylon_bLegacyPresentationForTest = false;
#endif
    stdRenderBabylon_state.colorFormat = stdRenderBabylon_ChooseColorFormat(pDevice,
        !stdRenderBabylon_state.bToneMap);
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
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ info.swapchainFormat, {} } };

        stdRenderBabylon_state.presentPipeline = pDevice->createPipeline(desc);
        if ( stdRenderBabylon_state.presentPipeline.isNull() || !stdRenderBabylon_EnsurePresentSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon_Open: could not create the final pass pipeline.\n");
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
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.capturePipeline);
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
    colorDesc.format    = stdRenderBabylon_state.colorFormat;
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
    const uint64_t numPixels = (uint64_t)width * (uint64_t)height;
    const uint64_t size      = numPixels * 4u;
    const render::TextureFormat format = stdRenderBabylon_state.colorFormat;
    const uint64_t readSize   = numPixels * render::bytesPerTexel(format);
    if ( size > (uint64_t)SIZE_MAX || readSize > (uint64_t)SIZE_MAX )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the %ux%u back buffer is too large.\n", width, height);
        return false;
    }

    if ( !stdRenderBabylon_FlushFrame() )
    {
        return false;
    }

    const std::vector<std::byte> pixels = stdRenderBabylon_state.pDevice->readTexture(stdRenderBabylon_state.colorTarget, 0);
    if ( pixels.size() != (size_t)readSize )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the device returned %zu bytes for a %ux%u buffer in its scene format.\n",
            pixels.size(), width, height);
        return false;
    }

    if ( size > 0u )
    {
        uint8_t* pDest = static_cast<uint8_t*>(pOut);
        if ( format == render::TextureFormat::Bgra8 )
        {
            std::memcpy(pDest, pixels.data(), (size_t)size);
        }
        else
        {
            // Rgba16F: half floats R, G, B, A to Bgra8, clamping to the displayable range.
            for ( uint64_t i = 0; i < numPixels; ++i )
            {
                const std::byte* pTexel = pixels.data() + i * 8u;
                uint16_t aHalf[4];
                for ( size_t channel = 0; channel < 4u; ++channel )
                {
                    aHalf[channel] = (uint16_t)((uint8_t)pTexel[channel * 2u]
                        | ((uint16_t)(uint8_t)pTexel[channel * 2u + 1u] << 8));
                }
                const float aChannel[4] = { stdRenderBabylon_HalfToFloat(aHalf[0]),
                    stdRenderBabylon_HalfToFloat(aHalf[1]),
                    stdRenderBabylon_HalfToFloat(aHalf[2]),
                    stdRenderBabylon_HalfToFloat(aHalf[3]) };
                pDest[i * 4u + 0u] = stdRenderBabylon_ToUnorm8(aChannel[2]);
                pDest[i * 4u + 1u] = stdRenderBabylon_ToUnorm8(aChannel[1]);
                pDest[i * 4u + 2u] = stdRenderBabylon_ToUnorm8(aChannel[0]);
                pDest[i * 4u + 3u] = stdRenderBabylon_ToUnorm8(aChannel[3]);
            }
        }
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

render::TextureFormat stdRenderBabylon_GetColorFormat()
{
    return stdRenderBabylon_state.colorFormat;
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

                if ( stdRenderBabylon_state.pDevice->info().hasSwapchain )
                {
                    if ( !stdRenderBabylon_RecordFinalPass(list,
                        stdRenderBabylon_state.presentPipeline, render::TextureHandle{}) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                    }
                }

                if ( stdRenderBabylon_state.hudOverlay || stdRenderBabylon_state.presentOverlay )
                {
                    StdRenderBabylonOverlayTarget target;
                    target.texture = {};
                    target.format  = stdRenderBabylon_state.pDevice->info().hasSwapchain
                        ? stdRenderBabylon_state.pDevice->info().swapchainFormat
                        : render::TextureFormat::Bgra8;
                    target.width  = stdRenderBabylon_state.width;
                    target.height = stdRenderBabylon_state.height;

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

void stdRenderBabylon_SetHudOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser)
{
    stdRenderBabylon_state.hudOverlay     = pfn;
    stdRenderBabylon_state.hudOverlayUser = pfn ? pUser : nullptr;
}

bool stdRenderBabylon_HasHudOverlay()
{
    return stdRenderBabylon_state.hudOverlay != nullptr;
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
        || !stdRenderBabylon_EnsurePresentSampler() )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: no final pass pipeline.\n");
        return false;
    }

    render::CommandList list = stdRenderBabylon_state.pDevice->beginFrame();

    if ( !stdRenderBabylon_RecordFinalPass(list, stdRenderBabylon_state.capturePipeline, target) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the target refused the final pass.\n");
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

    const size_t numPixels = (size_t)stdRenderBabylon_state.width * stdRenderBabylon_state.height;
    const render::TextureFormat format = stdRenderBabylon_state.colorFormat;
    std::vector<std::byte> converted;
    std::span<const std::byte> data(static_cast<const std::byte*>(pData), numPixels * 4u);
    if ( format == render::TextureFormat::Rgba16F )
    {
        // Bgra8 to Rgba16F: half floats R, G, B, A in the 0..1 the bytes hold.
        converted.resize(numPixels * 8u);
        const uint8_t* pSource = static_cast<const uint8_t*>(pData);
        for ( size_t i = 0; i < numPixels; ++i )
        {
            const float aChannel[4] = { pSource[i * 4u + 2u] / 255.0f,
                pSource[i * 4u + 1u] / 255.0f, pSource[i * 4u + 0u] / 255.0f,
                pSource[i * 4u + 3u] / 255.0f };
            std::byte* pTexel = converted.data() + i * 8u;
            for ( size_t channel = 0; channel < 4u; ++channel )
            {
                const uint16_t half = stdRenderBabylon_FloatToHalf(aChannel[channel]);
                pTexel[channel * 2u + 0u] = (std::byte)(half & 0xFFu);
                pTexel[channel * 2u + 1u] = (std::byte)(half >> 8);
            }
        }
        data = converted;
    }

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

void stdRenderBabylon_SetToneMap(const StdRenderBabylonToneMap& params)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: not open.\n");
        return;
    }
    if ( !std::isfinite(params.exposure) || params.exposure < 0.0f )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: exposure %g is not a finite value at or above 0.\n",
            (double)params.exposure);
        return;
    }
    if ( params.oper != 0 )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: tone-map operator %u is not supported.\n",
            params.oper);
        return;
    }
    stdRenderBabylon_state.toneMap = params;
}

StdRenderBabylonToneMap stdRenderBabylon_GetToneMap()
{
    return stdRenderBabylon_state.toneMap;
}

#ifdef J3D_TEST
void stdRenderBabylon_ForceLegacyPresentationForTest(bool bLegacy)
{
    stdRenderBabylon_bLegacyPresentationForTest = bLegacy;
}
#endif

render::TextureHandle stdRenderBabylon_GetPresentSource()
{
    return stdRenderBabylon_PresentSource();
}
