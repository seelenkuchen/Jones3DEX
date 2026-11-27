// The flight recorder: the last few seconds of presented frames, kept on the GPU and written on
// request with Shift+F12.
// Part of the Babylon column's display adapter; see stdDisplayBabylon.cpp for the overview. Adapter
// code: no Fixed/Added markers inside.
//
// Every successful present records the just-presented frame into the next slot of a fixed ring of
// Bgra8 color targets. stdRenderBabylon_RenderPresentToTexture writes to one recorder-owned Bgra8
// scratch target at the real presented size, the size the normal present uses, so the render
// frame owner's shared SMAA, sharpening and camera-effect targets are never resized. A plain
// fullscreen copy then scales that scratch target into the kFlightRecorderWidth-wide ring slot
// with linear filtering, adding no tone map. Nothing is read back while the game runs;
// stdDisplay_SaveFlightRecorder reads the ring only when the player asks for it, so the stall there
// is fine. The resources are made on first use and sized to the presented aspect at
// kFlightRecorderWidth; a size change (a window resize or a new video mode) frees and recreates
// them. The display owns the ring: Open reads engine.debug.flightRecorder and Close frees it.
// 300 frames is about two seconds at the 150 fps and faster rates the owner plays at.
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

#include <babylon/platform/Window.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "stdDisplayBabylonInternal.h"

namespace render = babylon::render;

using namespace stdDisplayBabylonInternal;

namespace
{
    struct StdDisplayFlightRecorderState
    {
        bool bEnabled       = true;
        bool bCreated       = false;
        bool bCreateFailed  = false;
        bool bFailureLogged = false;

        // The presented size the ring was made for and the kFlightRecorderWidth-wide target it
        // scales to.
        uint32_t sourceWidth  = 0;
        uint32_t sourceHeight = 0;
        uint32_t width        = stdDisplay_kFlightRecorderWidth;
        uint32_t height       = 0;

        std::vector<render::TextureHandle> aTextures;
        std::array<uint64_t, stdDisplay_kFlightRecorderFrames> aPresentIndices{};
        size_t nextSlot           = 0;
        size_t numFrames          = 0;
        uint64_t nextPresentIndex = 0;

        // The presented-size target RenderPresentToTexture draws into, then the plain copy pass
        // that scales it into a ring slot.
        render::TextureHandle scratchTexture;
        uint32_t scratchWidth     = 0;
        uint32_t scratchHeight    = 0;
        bool bScratchFailed       = false;
        render::PipelineHandle copyPipeline;
        render::SamplerHandle copySampler;
        bool bCopySetupFailed     = false;
    };

    StdDisplayFlightRecorderState stdDisplayFlightRecorder_state;

#ifdef J3D_TEST
    bool stdDisplayFlightRecorder_bEnabledOverride    = false;
    bool stdDisplayFlightRecorder_bEnabledOverrideSet = false;
#endif

    void StdDisplayFlightRecorder_LogOnce(const char* pMessage)
    {
        if ( stdDisplayFlightRecorder_state.bFailureLogged )
        {
            return;
        }

        stdDisplayFlightRecorder_state.bFailureLogged = true;
        STDLOG_WARNING("%s\n", pMessage);
    }

    void StdDisplayFlightRecorder_FreeTextures()
    {
        if ( stdDisplay_state.pDevice )
        {
            for ( render::TextureHandle texture : stdDisplayFlightRecorder_state.aTextures )
            {
                if ( !texture.isNull() )
                {
                    stdDisplay_state.pDevice->destroyTexture(texture);
                }
            }
        }

        stdDisplayFlightRecorder_state.aTextures.clear();
        stdDisplayFlightRecorder_state.aTextures.shrink_to_fit();
        stdDisplayFlightRecorder_state.bCreated = false;
    }

    void StdDisplayFlightRecorder_FreeScratchAndCopy()
    {
        if ( stdDisplay_state.pDevice )
        {
            render::Device* pDevice = stdDisplay_state.pDevice;
            if ( !stdDisplayFlightRecorder_state.scratchTexture.isNull() )
            {
                pDevice->destroyTexture(stdDisplayFlightRecorder_state.scratchTexture);
            }
            if ( !stdDisplayFlightRecorder_state.copyPipeline.isNull() )
            {
                pDevice->destroyPipeline(stdDisplayFlightRecorder_state.copyPipeline);
            }
            if ( !stdDisplayFlightRecorder_state.copySampler.isNull() )
            {
                pDevice->destroySampler(stdDisplayFlightRecorder_state.copySampler);
            }
        }

        stdDisplayFlightRecorder_state.scratchTexture = {};
        stdDisplayFlightRecorder_state.scratchWidth   = 0;
        stdDisplayFlightRecorder_state.scratchHeight  = 0;
        stdDisplayFlightRecorder_state.copyPipeline   = {};
        stdDisplayFlightRecorder_state.copySampler    = {};
        stdDisplayFlightRecorder_state.bScratchFailed = false;
        stdDisplayFlightRecorder_state.bCopySetupFailed = false;
    }

    // The height of a kFlightRecorderWidth-wide target with the presented aspect, rounded to the
    // nearest even number so the frame keeps its centre row.
    uint32_t StdDisplayFlightRecorder_TargetHeight(uint32_t sourceWidth, uint32_t sourceHeight)
    {
        if ( sourceWidth == 0u )
        {
            return 0u;
        }

        uint64_t height = ((uint64_t)stdDisplay_kFlightRecorderWidth * sourceHeight
            + sourceWidth / 2u) / sourceWidth;
        if ( height == 0u )
        {
            height = 1u;
        }
        height = (height + 1u) & ~uint64_t(1u);
        return height > UINT32_MAX ? 0u : (uint32_t)height;
    }

    // The size the swapchain presents: the window's drawable size when there is one, else the back
    // buffer's, as stdDisplay_SaveScreen's overlay capture uses.
    bool StdDisplayFlightRecorder_GetPresentedSize(uint32_t* pWidth, uint32_t* pHeight)
    {
        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( pContext && pContext->pWindow )
        {
            const babylon::platform::Size size = pContext->pWindow->size();
            if ( size.width > 0 && size.height > 0 )
            {
                *pWidth  = (uint32_t)size.width;
                *pHeight = (uint32_t)size.height;
                return true;
            }
        }

        const uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        const uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        if ( width == 0u || height == 0u )
        {
            return false;
        }

        *pWidth  = width;
        *pHeight = height;
        return true;
    }

    bool StdDisplayFlightRecorder_CreateTextures()
    {
        render::Device* pDevice = stdDisplay_state.pDevice;
        if ( !pDevice || stdDisplayFlightRecorder_state.width == 0u
            || stdDisplayFlightRecorder_state.height == 0u )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: the flight recorder has no render device for its targets.");
            return false;
        }

        const uint32_t maxSize = pDevice->info().limits.maxTextureDimension;
        if ( stdDisplayFlightRecorder_state.width > maxSize
            || stdDisplayFlightRecorder_state.height > maxSize )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: the flight recorder's ring targets exceed the device's texture limit.");
            return false;
        }

        StdDisplayFlightRecorder_FreeTextures();

        stdDisplayFlightRecorder_state.aTextures.reserve(stdDisplay_kFlightRecorderFrames);
        for ( size_t i = 0; i < stdDisplay_kFlightRecorderFrames; ++i )
        {
            const render::TextureDesc desc{ stdDisplayFlightRecorder_state.width,
                stdDisplayFlightRecorder_state.height, 1, render::TextureFormat::Bgra8,
                render::TextureUsage::ColorTarget };
            const render::TextureHandle texture = pDevice->createTexture(desc);
            if ( texture.isNull() )
            {
                StdDisplayFlightRecorder_LogOnce(
                    "stdDisplay: could not create the flight recorder's frame textures.");
                StdDisplayFlightRecorder_FreeTextures();
                return false;
            }

            stdDisplayFlightRecorder_state.aTextures.push_back(texture);
        }

        stdDisplayFlightRecorder_state.bCreated       = true;
        stdDisplayFlightRecorder_state.bCreateFailed  = false;
        stdDisplayFlightRecorder_state.nextSlot       = 0;
        stdDisplayFlightRecorder_state.numFrames      = 0;
        return true;
    }

    // One Bgra8 scratch target at the presented size, sampled by the copy pass. RenderPresentToTexture
    // draws into it, so its size is the size the shared present targets expect and they never resize.
    bool StdDisplayFlightRecorder_EnsureScratch(uint32_t width, uint32_t height)
    {
        render::Device* pDevice = stdDisplay_state.pDevice;
        if ( !pDevice )
        {
            return false;
        }

        if ( stdDisplayFlightRecorder_state.bScratchFailed
            && width == stdDisplayFlightRecorder_state.scratchWidth
            && height == stdDisplayFlightRecorder_state.scratchHeight )
        {
            return false;
        }

        if ( !stdDisplayFlightRecorder_state.scratchTexture.isNull()
            && width == stdDisplayFlightRecorder_state.scratchWidth
            && height == stdDisplayFlightRecorder_state.scratchHeight )
        {
            return true;
        }

        if ( !stdDisplayFlightRecorder_state.scratchTexture.isNull() )
        {
            pDevice->destroyTexture(stdDisplayFlightRecorder_state.scratchTexture);
            stdDisplayFlightRecorder_state.scratchTexture = {};
        }

        stdDisplayFlightRecorder_state.scratchWidth  = width;
        stdDisplayFlightRecorder_state.scratchHeight = height;

        const render::TextureUsage usage = render::TextureUsage::ColorTarget
            | render::TextureUsage::Sampled;
        if ( !pDevice->supportsFormat(render::TextureFormat::Bgra8, usage) )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: the flight recorder's scratch target cannot be sampled.");
            stdDisplayFlightRecorder_state.bScratchFailed = true;
            return false;
        }

        const render::TextureDesc desc{ width, height, 1,
            render::TextureFormat::Bgra8, usage };
        stdDisplayFlightRecorder_state.scratchTexture = pDevice->createTexture(desc);
        if ( stdDisplayFlightRecorder_state.scratchTexture.isNull() )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: could not create the flight recorder's scratch target.");
            stdDisplayFlightRecorder_state.bScratchFailed = true;
            return false;
        }

        stdDisplayFlightRecorder_state.scratchWidth  = width;
        stdDisplayFlightRecorder_state.scratchHeight = height;
        stdDisplayFlightRecorder_state.bScratchFailed = false;
        return true;
    }

    // The plain fullscreen copy from the present-sized scratch target to a Bgra8 ring slot. The
    // present fragment shader is just a texture sample, so the ring gets the presented image scaled
    // with linear filtering and no second tone map.
    bool StdDisplayFlightRecorder_EnsureCopyPipeline()
    {
        render::Device* pDevice = stdDisplay_state.pDevice;
        if ( !pDevice )
        {
            return false;
        }

        if ( !stdDisplayFlightRecorder_state.copyPipeline.isNull()
            && !stdDisplayFlightRecorder_state.copySampler.isNull() )
        {
            return true;
        }

        if ( stdDisplayFlightRecorder_state.bCopySetupFailed )
        {
            return false;
        }

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Linear;
        samplerDesc.magFilter = render::Filter::Linear;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;
        stdDisplayFlightRecorder_state.copySampler = pDevice->createSampler(samplerDesc);

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::presentFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, {} } };
        stdDisplayFlightRecorder_state.copyPipeline = pDevice->createPipeline(desc);

        if ( stdDisplayFlightRecorder_state.copySampler.isNull()
            || stdDisplayFlightRecorder_state.copyPipeline.isNull() )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: could not set up the flight recorder's copy pass.");
            StdDisplayFlightRecorder_FreeScratchAndCopy();
            stdDisplayFlightRecorder_state.bCopySetupFailed = true;
            return false;
        }

        return true;
    }

    bool StdDisplayFlightRecorder_CopyToSlot(render::TextureHandle slot)
    {
        render::Device* pDevice = stdDisplay_state.pDevice;
        if ( !pDevice )
        {
            return false;
        }

        render::CommandList list = pDevice->beginFrame();
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ slot, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(stdDisplayFlightRecorder_state.copyPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0,
            stdDisplayFlightRecorder_state.scratchTexture,
            stdDisplayFlightRecorder_state.copySampler);
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        return pDevice->submit(list);
    }
}

namespace stdDisplayBabylonInternal
{
    void stdDisplay_FlightRecorderOpen()
    {
        stdDisplay_FlightRecorderClose();

#ifdef J3D_TEST
        if ( stdDisplayFlightRecorder_bEnabledOverrideSet )
        {
            stdDisplayFlightRecorder_state.bEnabled = stdDisplayFlightRecorder_bEnabledOverride;
        }
        else
#endif
        {
            stdDisplayFlightRecorder_state.bEnabled = stdConfig_HasStarted()
                ? stdConfig_GetBool(stdDisplay_kFlightRecorderKey, true) : true;
        }
    }

    void stdDisplay_FlightRecorderClose()
    {
        StdDisplayFlightRecorder_FreeTextures();
        StdDisplayFlightRecorder_FreeScratchAndCopy();
        stdDisplayFlightRecorder_state = StdDisplayFlightRecorderState{};
    }

    void stdDisplay_FlightRecorderCapture()
    {
        if ( !stdDisplayFlightRecorder_state.bEnabled )
        {
            return;
        }

        uint32_t sourceWidth  = 0;
        uint32_t sourceHeight = 0;
        if ( !StdDisplayFlightRecorder_GetPresentedSize(&sourceWidth, &sourceHeight) )
        {
            return;
        }

        const uint32_t height = StdDisplayFlightRecorder_TargetHeight(sourceWidth, sourceHeight);
        if ( height == 0u )
        {
            return;
        }

        const bool bSizeChanged = sourceWidth != stdDisplayFlightRecorder_state.sourceWidth
            || sourceHeight != stdDisplayFlightRecorder_state.sourceHeight
            || height != stdDisplayFlightRecorder_state.height;
        if ( stdDisplayFlightRecorder_state.bCreateFailed && !bSizeChanged )
        {
            return;
        }

        if ( !stdDisplayFlightRecorder_state.bCreated || bSizeChanged )
        {
            stdDisplayFlightRecorder_state.sourceWidth  = sourceWidth;
            stdDisplayFlightRecorder_state.sourceHeight = sourceHeight;
            stdDisplayFlightRecorder_state.width        = stdDisplay_kFlightRecorderWidth;
            stdDisplayFlightRecorder_state.height       = height;
            stdDisplayFlightRecorder_state.bCreateFailed = false;

            if ( !StdDisplayFlightRecorder_CreateTextures() )
            {
                stdDisplayFlightRecorder_state.bCreateFailed = true;
                return;
            }
        }

        if ( stdDisplayFlightRecorder_state.bCreateFailed
            || stdDisplayFlightRecorder_state.aTextures.empty() )
        {
            return;
        }

        if ( !StdDisplayFlightRecorder_EnsureScratch(sourceWidth, sourceHeight)
            || !StdDisplayFlightRecorder_EnsureCopyPipeline() )
        {
            return;
        }

        // The render frame owner's shared targets are sized by this call. Passing the presented
        // size keeps them at the size the real present uses, so nothing is recreated here.
        if ( !stdRenderBabylon_RenderPresentToTexture(
                stdDisplayFlightRecorder_state.scratchTexture, sourceWidth, sourceHeight) )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: could not record a flight recorder frame.");
            return;
        }

        const render::TextureHandle target =
            stdDisplayFlightRecorder_state.aTextures[stdDisplayFlightRecorder_state.nextSlot];
        if ( !StdDisplayFlightRecorder_CopyToSlot(target) )
        {
            StdDisplayFlightRecorder_LogOnce(
                "stdDisplay: could not scale a flight recorder frame into its ring.");
            return;
        }

        stdDisplayFlightRecorder_state.aPresentIndices[stdDisplayFlightRecorder_state.nextSlot] =
            stdDisplayFlightRecorder_state.nextPresentIndex++;
        stdDisplayFlightRecorder_state.nextSlot =
            (stdDisplayFlightRecorder_state.nextSlot + 1u) % stdDisplay_kFlightRecorderFrames;
        if ( stdDisplayFlightRecorder_state.numFrames < stdDisplay_kFlightRecorderFrames )
        {
            ++stdDisplayFlightRecorder_state.numFrames;
        }
    }

#ifdef J3D_TEST
    void stdDisplay_FlightRecorderSetEnabledForTest(bool bEnabled)
    {
        stdDisplayFlightRecorder_bEnabledOverride    = bEnabled;
        stdDisplayFlightRecorder_bEnabledOverrideSet = true;
        stdDisplayFlightRecorder_state.bEnabled      = bEnabled;
    }

    void stdDisplay_FlightRecorderClearEnabledForTest()
    {
        stdDisplayFlightRecorder_bEnabledOverrideSet = false;
    }

    bool stdDisplay_FlightRecorderIsEnabledForTest()
    {
        return stdDisplayFlightRecorder_state.bEnabled;
    }

    size_t stdDisplay_FlightRecorderGetTextureCountForTest()
    {
        return stdDisplayFlightRecorder_state.aTextures.size();
    }
#endif
}

bool stdDisplay_SaveFlightRecorder(const char* pDirectory)
{
    if ( !pDirectory || pDirectory[0] == '\0' )
    {
        STDLOG_ERROR("stdDisplay: the flight recorder needs a directory.\n");
        return false;
    }

    if ( !stdDisplayFlightRecorder_state.bEnabled )
    {
        STDLOG_ERROR("stdDisplay: the flight recorder is off.\n");
        return false;
    }

    if ( stdDisplayFlightRecorder_state.aTextures.empty()
        || stdDisplayFlightRecorder_state.numFrames == 0u )
    {
        STDLOG_ERROR("stdDisplay: the flight recorder has no frames yet.\n");
        return false;
    }

    if ( !stdDisplay_state.pDevice )
    {
        STDLOG_ERROR("stdDisplay: the flight recorder has no render device.\n");
        return false;
    }

    std::string aDirectory(pDirectory);
    if ( !stdFileUtil_MkDir(pDirectory) && !stdFileUtil_FileExists(pDirectory) )
    {
        STDLOG_ERROR("stdDisplay: could not create the flight recorder directory '%s'.\n",
            pDirectory);
        return false;
    }

    std::string aIndex;
    const size_t oldest = stdDisplayFlightRecorder_state.numFrames < stdDisplay_kFlightRecorderFrames
        ? 0u : stdDisplayFlightRecorder_state.nextSlot;
    for ( size_t i = 0; i < stdDisplayFlightRecorder_state.numFrames; ++i )
    {
        const size_t slot   = (oldest + i) % stdDisplay_kFlightRecorderFrames;
        const render::TextureHandle texture = stdDisplayFlightRecorder_state.aTextures[slot];
        const std::vector<std::byte> pixels = stdDisplay_state.pDevice->readTexture(texture, 0);
        if ( pixels.size() != (size_t)stdDisplayFlightRecorder_state.width
                * stdDisplayFlightRecorder_state.height * 4u )
        {
            STDLOG_ERROR("stdDisplay: the flight recorder read %zu bytes for frame %zu.\n",
                pixels.size(), i);
            return false;
        }

        char aName[32];
        std::snprintf(aName, sizeof(aName), "frame-%04zu.bmp", i);

        std::string aPath = aDirectory;
        if ( !aPath.empty() && aPath.back() != '/' && aPath.back() != '\\' )
        {
            aPath += '/';
        }
        aPath += aName;

        if ( stdDisplay_WriteBgraBmp(aPath.c_str(),
                reinterpret_cast<const uint8_t*>(pixels.data()),
                stdDisplayFlightRecorder_state.width, stdDisplayFlightRecorder_state.height) != 0 )
        {
            STDLOG_ERROR("stdDisplay: could not write the flight recorder frame '%s'.\n",
                aPath.c_str());
            return false;
        }

        char aLine[96];
        std::snprintf(aLine, sizeof(aLine), "%s, %llu\n", aName,
            (unsigned long long)stdDisplayFlightRecorder_state.aPresentIndices[slot]);
        aIndex += aLine;
    }

    std::string aIndexPath = aDirectory;
    if ( !aIndexPath.empty() && aIndexPath.back() != '/' && aIndexPath.back() != '\\' )
    {
        aIndexPath += '/';
    }
    aIndexPath += "index.txt";

    tFileHandle fh = std_g_pHS->pFileOpen(aIndexPath.c_str(), "wb");
    if ( !fh )
    {
        STDLOG_ERROR("stdDisplay: could not create the flight recorder index '%s'.\n",
            aIndexPath.c_str());
        return false;
    }

    if ( std_g_pHS->pFileWrite(fh, aIndex.data(), aIndex.size()) != aIndex.size() )
    {
        STDLOG_ERROR("stdDisplay: could not write the flight recorder index '%s'.\n",
            aIndexPath.c_str());
        std_g_pHS->pFileClose(fh);
        return false;
    }

    std_g_pHS->pFileClose(fh);
    return true;
}
