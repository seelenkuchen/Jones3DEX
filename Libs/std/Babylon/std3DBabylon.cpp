// Added: the Babylon column's std3D backend on the Babylon render device of the adapter context
// (ADR-0025), replacing std/Null/std3DNull.c in that column. It defines the std3DBackend_* functions
// through stdRenderBackend.h, so the capture forwarders of std3DCapture.c stay in front (ADR-0024).
// The behavior reference is the removed DirectX 9 backend (std/Win95/DX9/std3DX9.c before 82b8f607):
// the device list, texture formats, system textures, the texture cache with its frame counts, the
// fog and projection state and the scene clears follow it, with Babylon textures where it held
// Direct3D ones. Frames go through stdRenderBabylon.cpp, which owns the frame's command list and
// the back buffer; the display adapter (stdDisplayBabylon.cpp) opens it with the display, sizes the
// back buffer with the video mode and submits the frame at stdDisplay_Update, as std3DX9.c drew into
// the display's device and stdDisplayDX9.c presented it.
//
// Draws follow std3DX9.c's shader path (std3DX9.c with stdShaderDX9.c and Shaders/*.hlsl), which
// ran on every Direct3D 9 device with shader model 3: the shaders are ports of its default and
// wireframe shaders (Shaders/std3d*.hlsl), and the Direct3D state its SetRenderState and
// InitRenderState set becomes pipelines (blending, depth, primitive), samplers (filter, mip filter,
// clamp) and uniforms (viewport, fog, alpha reference), worked out from the render state bits at
// each draw (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms).
//
// A scene is one or more passes on the back buffer. CommandList::updateBuffer can't be recorded
// inside a pass, so a draw streams its vertices and indices into the frame's vertex and index
// buffer right away, at growing offsets like std3DX9.c's buffer path, and the pass that draws them
// is recorded when it ends: at std3D_EndScene and at a std3D_ClearZBuffer inside the scene.
//
// Adapter code: no Fixed/Added markers inside; divergences from std3DX9.c say so where they are.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "std3DBabylon.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"

namespace render = babylon::render;

// The constant std3DX9.c's SetProjection returns on bad arguments; no Windows header here.
#ifndef E_INVALIDARG
#define E_INVALIDARG ((int)0x80070057L)
#endif

float std3D_g_fogDensity   = 0.0f;
size_t std3D_g_maxVertices = 0u;

namespace
{
    // What the backend stores in the opaque tSysPixelFormat of StdTextureFormat::ddPixelFmt and
    // tSystemTexture::format, where std3DX9.c stored the D3DFORMAT.
    struct Std3DBabylonPixelFormat
    {
        uint8_t bValid;
        uint8_t format;  // render::TextureFormat
        uint8_t bOpaque; // the ColorInfo has no alpha, but the texture format has: alpha is set to 255
    };
    static_assert(sizeof(Std3DBabylonPixelFormat) <= sizeof(tSysPixelFormat), "Std3DBabylonPixelFormat must fit tSysPixelFormat");

    // The DX9 texture formats InitTextureFormats checks, in its order, that have a Babylon format with
    // the same bit layout (Texture.h): D3DFMT_X8R8G8B8 and D3DFMT_A8R8G8B8 are bytes B, G, R, A
    // (Bgra8; X8 reads as opaque), and the 16-bit ones match B5G6R5, B5G5R5A1 and B4G4R4A4 bit for
    // bit. D3DFMT_R8G8B8 is left out: no GPU API samples 24-bit texels. The luminance formats were
    // skipped by std3DX9.c too.
    struct Std3DBabylonFormatEntry
    {
        const ColorInfo* pColorInfo;
        render::TextureFormat format;
        bool bOpaque;
        const char* pName;
    };

    const Std3DBabylonFormatEntry std3D_aFormatEntries[] =
    {
        { &stdColor_cfRGB8888,  render::TextureFormat::Bgra8,    true,  "32-bit RGB (X8R8G8B8)" },
        { &stdColor_cfARGB8888, render::TextureFormat::Bgra8,    false, "32-bit ARGB (A8R8G8B8)" },
        { &stdColor_cfRGB565,   render::TextureFormat::B5G6R5,   false, "16-bit RGB (R5G6B5)" },
        { &stdColor_cfARGB5551, render::TextureFormat::B5G5R5A1, false, "16-bit ARGB (A1R5G5B5)" },
        { &stdColor_cfARGB4444, render::TextureFormat::B4G4R4A4, false, "16-bit ARGB (A4R4G4B4)" },
    };

    // The largest texture side reported when the device names none.
    constexpr uint32_t std3D_kDefaultMaxTextureSize = 4096u;

    bool std3D_bStartup          = false;
    bool std3D_bOpen             = false;
    bool std3D_bFindAllD3Devices = false;
    render::Device* std3D_pRenderDevice = nullptr;

    size_t std3D_numDevices      = 0;
    size_t std3D_curDevice       = 0;
    Device3D* std3D_pCurDevice   = nullptr;
    Device3D std3D_aDevices[4];

    size_t std3D_numTextureFormats = 0;
    StdTextureFormat std3D_aTextureFormats[8];
    bool std3D_bHasRGBTextureFormat = false;
    size_t std3D_RGBTextureFormat     = 0;
    size_t std3D_RGBATextureFormat    = 0;
    size_t std3D_RGBAKeyTextureFormat = 0;

    size_t std3D_frameCount            = 1;
    size_t std3D_numCachedTextures     = 0;
    tSystemTexture* std3D_pFirstTexCache = nullptr;
    tSystemTexture* std3D_pLastTexCache  = nullptr;

    // The render state of the draws; see std3D_SetRenderState.
    Std3DRenderState std3D_renderState      = (Std3DRenderState)0;
    Std3DMipmapFilterType std3D_mipmapFilter = (Std3DMipmapFilterType)-1;
    tSysTexture* std3D_pCurTex             = nullptr;
    float std3D_zDepth                     = 0.0f;
    bool std3D_bRenderFog                  = true;
    float std3D_fogStartDepth              = 0.0f;
    float std3D_fogEndDepth                = 0.0f;
    float std3D_fogDepthFactor             = 0.0f;
    float std3D_aFogColor[4]               = { 0.0f, 0.0f, 0.0f, 0.0f };

    // The frame: a scene is begun between StartScene and EndScene. The clears std3DX9.c made with
    // IDirect3DDevice9_Clear become the load operations of the next pass.
    bool std3D_bInScene           = false;
    bool std3D_bClearColorPending = false;
    bool std3D_bClearDepthPending = false;
    render::TextureHandle std3D_sceneColorTarget; // the back buffer the last scene drew to

    // The primitives std3DX9.c drew: triangle lists with its default shader, line strips and point
    // lists with its wireframe shader.
    enum class Std3DBabylonPrimitive : uint8_t
    {
        Triangles,
        Lines,
        Points,
    };
    constexpr size_t std3D_kNumPrimitives = 3;

    // Fragment uniform block 0 of Shaders/std3d.frag.hlsl and std3d_wireframe.frag.hlsl.
    struct Std3DBabylonFragmentUniforms
    {
        float aFogParams[4]; // DX9's g_fogParams: start, end, 1 / (end - start), enabled
        float aFogColor[4];  // DX9's g_fogColor
        float aAlphaTest[4]; // D3DRS_ALPHAREF / 255, then unused
    };
    static_assert(sizeof(Std3DBabylonFragmentUniforms) == 48, "Std3DBabylonFragmentUniforms must match the shaders' block");

    // One draw of the scene's current pass, recorded into it when the pass ends. Its vertices and
    // indices are already streamed into vertexBuffer and indexBuffer, at firstVertex and firstIndex.
    struct Std3DBabylonDraw
    {
        Std3DBabylonPrimitive primitive;
        render::PipelineHandle pipeline;
        render::BufferHandle vertexBuffer;
        render::BufferHandle indexBuffer; // triangles only
        render::TextureHandle texture;    // triangles only
        render::SamplerHandle sampler;    // triangles only
        Std3DBabylonFragmentUniforms uniforms;
        uint32_t firstVertex;
        uint32_t numVertices;
        uint32_t firstIndex;
        uint32_t numIndices;
    };

    // std3DX9.c's buffer sizes were STD3D_MAXVERTICES * STD3D_MAXFACEVERTICES vertices (64 MiB) and
    // three indices per vertex; these start smaller and grow when a frame needs more.
    constexpr uint32_t std3D_kInitialVertexBufferSize = 32768u * (uint32_t)sizeof(StdVertexTL);
    constexpr uint32_t std3D_kInitialIndexBufferSize  = 3u * 32768u * (uint32_t)sizeof(uint16_t);

    // D3DRS_ALPHAREF with STD3D_RS_ALPHAREF_SET (std3DX9.c's SetRenderState), else 0.
    constexpr uint32_t std3D_kAlphaRef = 160u;

    // Created at std3D_Open (the white texture std3DX9.c bound for untextured draws, the stream
    // buffers) or on first use (pipelines, samplers), destroyed at std3D_Close.
    render::PipelineHandle std3D_aPipelines[std3D_kNumPrimitives][2]; // [primitive][z write]
    render::SamplerHandle std3D_aSamplers[16];                         // std3D_GetSampler's index
    render::TextureHandle std3D_whiteTexture;
    render::BufferHandle std3D_vertexBuffer;
    render::BufferHandle std3D_indexBuffer;
    uint32_t std3D_vertexBufferSize   = 0; // bytes
    uint32_t std3D_indexBufferSize    = 0;
    uint32_t std3D_vertexBufferOffset = 0; // bytes the frame has streamed so far
    uint32_t std3D_indexBufferOffset  = 0;

    // The pass being collected: its load operations and its draws.
    render::LoadOp std3D_sceneColorLoad = render::LoadOp::Load;
    render::LoadOp std3D_sceneDepthLoad = render::LoadOp::Load;
    std::vector<Std3DBabylonDraw> std3D_aSceneDraws;

    // The fog values the shaders see. stdShader_SetFog copied them into the shader constants only
    // when the fog bit of the render state changed (std3DX9.c's SetRenderState), so they are kept
    // here and only std3D_SetRenderState updates them.
    float std3D_aShaderFogParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float std3D_aShaderFogColor[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };

    Std3DBabylonPixelFormat std3D_GetPixelFormat(const tSysPixelFormat& sysFormat)
    {
        Std3DBabylonPixelFormat format{};
        std::memcpy(&format, &sysFormat, sizeof(format));
        return format;
    }

    void std3D_SetPixelFormat(tSysPixelFormat* pSysFormat, render::TextureFormat textureFormat, bool bOpaque)
    {
        Std3DBabylonPixelFormat format{};
        format.bValid  = 1;
        format.format  = static_cast<uint8_t>(textureFormat);
        format.bOpaque = bOpaque ? 1 : 0;
        STD_ZEROMEM(pSysFormat, sizeof(*pSysFormat));
        std::memcpy(pSysFormat, &format, sizeof(format));
    }

    bool std3D_SameLayout(const ColorInfo& a, const ColorInfo& b)
    {
        return a.colorMode == b.colorMode && a.bpp == b.bpp
            && a.redBPP == b.redBPP && a.greenBPP == b.greenBPP && a.blueBPP == b.blueBPP && a.alphaBPP == b.alphaBPP
            && a.redPosShift == b.redPosShift && a.greenPosShift == b.greenPosShift && a.bluePosShift == b.bluePosShift
            && (a.alphaBPP == 0 || a.alphaPosShift == b.alphaPosShift);
    }

    uint32_t std3D_ReadPixel(const uint8_t* pSrc, size_t numBytes)
    {
        uint32_t value = 0;
        for ( size_t i = 0; i < numBytes; ++i )
        {
            value |= (uint32_t)pSrc[i] << (8u * i);
        }

        return value;
    }

    void std3D_WritePixel(uint8_t* pDst, size_t numBytes, uint32_t value)
    {
        for ( size_t i = 0; i < numBytes; ++i )
        {
            pDst[i] = (uint8_t)(value >> (8u * i));
        }
    }

    // The texture format entry whose ColorInfo has the same layout as pCi, or nullptr.
    const Std3DBabylonFormatEntry* std3D_FindFormatEntry(render::TextureFormat format, bool bOpaque)
    {
        for ( const Std3DBabylonFormatEntry& entry : std3D_aFormatEntries )
        {
            if ( entry.format == format && entry.bOpaque == bOpaque )
            {
                return &entry;
            }
        }

        return nullptr;
    }

    // One mip level of a system texture in the byte layout uploadTexture() takes for format: rows of
    // width texels, top row first, no padding. The pixels are copied when the VBuffer has the
    // layout of the format's ColorInfo and converted through stdColor otherwise; formats without
    // alpha in their ColorInfo get alpha 255. Returns false, logging why, for a VBuffer that can't
    // hold width x height pixels of its ColorInfo.
    bool std3D_GetMipData(const tVBuffer* pVBuffer, const Std3DBabylonFormatEntry& entry, std::vector<std::byte>& data)
    {
        const tRasterInfo& raster = pVBuffer->rasterInfo;
        const ColorInfo& srcCi    = raster.colorInfo;
        const size_t srcBytes     = srcCi.bpp / 8u;
        const size_t dstBytes     = render::bytesPerTexel(entry.format);
        const size_t width        = raster.width;
        const size_t height       = raster.height;

        if ( (srcCi.bpp != 16 && srcCi.bpp != 24 && srcCi.bpp != 32)
            || (srcCi.colorMode != STDCOLOR_RGB && srcCi.colorMode != STDCOLOR_RGBA) )
        {
            STDLOG_ERROR("Unsupported texture pixel format (%u bpp, color mode %d).\n", srcCi.bpp, (int)srcCi.colorMode);
            return false;
        }

        if ( !pVBuffer->pPixels || raster.rowSize < width * srcBytes || raster.rowSize * height > raster.size )
        {
            STDLOG_ERROR("Texture VBuffer of %zux%zu pixels has too few bytes.\n", width, height);
            return false;
        }

        data.resize(width * height * dstBytes);
        const bool bSameLayout = std3D_SameLayout(srcCi, *entry.pColorInfo);
        for ( size_t y = 0; y < height; ++y )
        {
            const uint8_t* pSrcRow = pVBuffer->pPixels + y * raster.rowSize;
            uint8_t* pDstRow       = reinterpret_cast<uint8_t*>(data.data()) + y * width * dstBytes;
            if ( bSameLayout )
            {
                std::memcpy(pDstRow, pSrcRow, width * dstBytes);
                if ( entry.bOpaque )
                {
                    for ( size_t x = 0; x < width; ++x )
                    {
                        pDstRow[x * dstBytes + 3] = 0xFF;
                    }
                }

                continue;
            }

            for ( size_t x = 0; x < width; ++x )
            {
                uint8_t r = 0, g = 0, b = 0, a = 0xFF;
                stdColor_DecodeRGBA(std3D_ReadPixel(pSrcRow + x * srcBytes, srcBytes), &srcCi, &r, &g, &b, &a);
                if ( srcCi.alphaBPP == 0 || entry.bOpaque )
                {
                    a = 0xFF;
                }

                uint32_t value = stdColor_EncodeRGBA(entry.pColorInfo, r, g, b, a);
                if ( entry.bOpaque )
                {
                    value |= 0xFF000000u;
                }

                std3D_WritePixel(pDstRow + x * dstBytes, dstBytes, value);
            }
        }

        return true;
    }

    void std3D_ReleaseCachedTexture(tSystemTexture* pTexture)
    {
        if ( pTexture->pCachedTexture )
        {
            stdRenderBabylon_DestroyTexture(pTexture->pCachedTexture->texture);
            stdMemory_Free(pTexture->pCachedTexture);
            pTexture->pCachedTexture = nullptr;
        }
    }

    void std3D_AddTextureToCacheList(tSystemTexture* pTexture)
    {
        if ( std3D_pFirstTexCache )
        {
            std3D_pLastTexCache->pNextCachedTexture = pTexture;
            pTexture->pPrevCachedTexture            = std3D_pLastTexCache;
            pTexture->pNextCachedTexture            = nullptr;
            std3D_pLastTexCache                     = pTexture;
        }
        else
        {
            std3D_pLastTexCache          = pTexture;
            std3D_pFirstTexCache         = pTexture;
            pTexture->pPrevCachedTexture = nullptr;
            pTexture->pNextCachedTexture = nullptr;
        }

        ++std3D_numCachedTextures;
        std3D_pCurDevice->availableMemory -= pTexture->textureSize;
    }

    void std3D_RemoveTextureFromCacheList(tSystemTexture* pCacheTexture)
    {
        if ( pCacheTexture == std3D_pFirstTexCache )
        {
            std3D_pFirstTexCache = pCacheTexture->pNextCachedTexture;
            if ( std3D_pFirstTexCache )
            {
                std3D_pFirstTexCache->pPrevCachedTexture = nullptr;
                if ( !std3D_pFirstTexCache->pNextCachedTexture )
                {
                    std3D_pLastTexCache = std3D_pFirstTexCache;
                }
            }
            else
            {
                std3D_pLastTexCache = nullptr;
            }
        }
        else if ( pCacheTexture == std3D_pLastTexCache )
        {
            std3D_pLastTexCache = pCacheTexture->pPrevCachedTexture;
            pCacheTexture->pPrevCachedTexture->pNextCachedTexture = nullptr;
        }
        else
        {
            pCacheTexture->pPrevCachedTexture->pNextCachedTexture = pCacheTexture->pNextCachedTexture;
            pCacheTexture->pNextCachedTexture->pPrevCachedTexture = pCacheTexture->pPrevCachedTexture;
        }

        pCacheTexture->pNextCachedTexture = nullptr;
        pCacheTexture->pPrevCachedTexture = nullptr;
        pCacheTexture->frameNum           = 0;

        --std3D_numCachedTextures;
        std3D_pCurDevice->availableMemory += pCacheTexture->textureSize;
    }

    int std3D_PurgeTextureCache(size_t size)
    {
        for ( tSystemTexture* pCacheTexture = std3D_pFirstTexCache; pCacheTexture && pCacheTexture->frameNum != std3D_frameCount; pCacheTexture = pCacheTexture->pNextCachedTexture )
        {
            if ( pCacheTexture->textureSize == size )
            {
                std3D_ReleaseCachedTexture(pCacheTexture);
                std3D_RemoveTextureFromCacheList(pCacheTexture);
                return 1;
            }
        }

        size_t purgedBytes = 0;
        tSystemTexture* pNextCachedTexture = nullptr;
        for ( tSystemTexture* pCacheTexture = std3D_pFirstTexCache; pCacheTexture && purgedBytes < size; pCacheTexture = pNextCachedTexture )
        {
            pNextCachedTexture = pCacheTexture->pNextCachedTexture;
            if ( pCacheTexture->frameNum != std3D_frameCount )
            {
                std3D_ReleaseCachedTexture(pCacheTexture);
                purgedBytes += pCacheTexture->textureSize;
                std3D_RemoveTextureFromCacheList(pCacheTexture);
            }
        }

        return purgedBytes != 0;
    }

    void std3D_ResetTextureCacheInternal()
    {
        STDLOG_DEBUG("Clearing texture cache....\n");
        std3D_pCurTex = nullptr;

        tSystemTexture* pCurTex = std3D_pFirstTexCache;
        while ( pCurTex )
        {
            std3D_ReleaseCachedTexture(pCurTex);

            tSystemTexture* pNextTex    = pCurTex->pNextCachedTexture;
            pCurTex->frameNum           = 0;
            pCurTex->pNextCachedTexture = nullptr;
            pCurTex->pPrevCachedTexture = nullptr;

            pCurTex = pNextTex;
        }

        std3D_pFirstTexCache    = nullptr;
        std3D_pLastTexCache     = nullptr;
        std3D_numCachedTextures = 0;

        if ( std3D_pCurDevice )
        {
            std3D_pCurDevice->availableMemory = std3D_pCurDevice->totalMemory;
        }

        std3D_frameCount = 1;
    }

    // The DX9 BuildDeviceList with the render device in place of Direct3D: one device, its limits
    // from the render device, its memory from the display device as std3DX9.c took it.
    int std3D_BuildDeviceList()
    {
        std3D_numDevices = 0;

        StdDisplayDevice displayDevice;
        STD_ZEROMEM(&displayDevice, sizeof(displayDevice));
        if ( !stdDisplay_IsOpen() || stdDisplay_GetCurrentDevice(&displayDevice) )
        {
            return 0;
        }

        if ( !std3D_bFindAllD3Devices && !displayDevice.bHAL )
        {
            return 0;
        }

        const render::DeviceInfo& info = std3D_pRenderDevice->info();
        const bool bHasZBuffer = std3D_pRenderDevice->supportsFormat(render::TextureFormat::D24S8, render::TextureUsage::DepthTarget)
            || std3D_pRenderDevice->supportsFormat(render::TextureFormat::D32F, render::TextureUsage::DepthTarget);
        const uint32_t maxTexSize = info.limits.maxTextureDimension ? info.limits.maxTextureDimension : std3D_kDefaultMaxTextureSize;

        Device3D* pDriver = &std3D_aDevices[0];
        STD_ZEROMEM(pDriver, sizeof(*pDriver));

        stdUtil_Format(pDriver->deviceDescription, STD_ARRAYLEN(pDriver->deviceDescription), "%s (Babylon render, %s)",
            displayDevice.aDeviceName, info.backend.c_str());
        stdUtil_Format(pDriver->deviceName, STD_ARRAYLEN(pDriver->deviceName), "%s", displayDevice.aDriverName);

        pDriver->bHAL                           = displayDevice.bHAL;
        pDriver->bTexturePerspectiveSupported   = 1;
        pDriver->hasZBuffer                     = bHasZBuffer;
        pDriver->bSqareOnlyTexture              = 0;
        pDriver->bAlphaTextureSupported         = 1;
        pDriver->bColorkeyTextureSupported      = 1;
        pDriver->bStippledShadeSupported        = 0;
        pDriver->minTexWidth                    = 1;
        pDriver->minTexHeight                   = 1;
        pDriver->maxTexWidth                    = maxTexSize;
        pDriver->maxTexHeight                   = maxTexSize;
        pDriver->bAnisotropicFilteringSupported = false; // render::SamplerDesc has no anisotropy
        pDriver->bMipmapAutoGenSupported        = false; // nor mip generation
        pDriver->bMSAASupported                 = false; // nor multisampled targets
        pDriver->bAlphaBlendSupported           = 1;
        pDriver->maxVertexCount                 = 65535u; // 16-bit indices

        pDriver->totalMemory     = displayDevice.totalVideoMemory;
        pDriver->availableMemory = displayDevice.freeVideoMemory;

        std3D_numDevices = 1u;

        STDLOG_STATUS("Found |%s|%s|%s|%s| Babylon render device\n",
            pDriver->hasZBuffer ? "Z" : "Non-Z",
            pDriver->bAlphaTextureSupported ? "Alpha" : "No Alpha",
            pDriver->bStippledShadeSupported ? "Stippled" : "Blend",
            pDriver->bColorkeyTextureSupported ? "Colorkey" : "No Colorkey"
        );
        STDLOG_STATUS("Description: %s [%s]\n", pDriver->deviceName, pDriver->deviceDescription);
        return 1;
    }

    // The DX9 InitTextureFormats: each format the device can sample, in the DX9 order.
    void std3D_InitTextureFormats()
    {
        std3D_numTextureFormats    = 0;
        std3D_bHasRGBTextureFormat = false;

        for ( const Std3DBabylonFormatEntry& entry : std3D_aFormatEntries )
        {
            if ( std3D_numTextureFormats >= STD_ARRAYLEN(std3D_aTextureFormats)
                || !std3D_pRenderDevice->supportsFormat(entry.format, render::TextureUsage::Sampled) )
            {
                continue;
            }

            StdTextureFormat* pTexFormat = &std3D_aTextureFormats[std3D_numTextureFormats];
            STD_ZEROMEM(pTexFormat, sizeof(StdTextureFormat));
            pTexFormat->ci = *entry.pColorInfo;
            std3D_SetPixelFormat(&pTexFormat->ddPixelFmt, entry.format, entry.bOpaque);
            std3D_bHasRGBTextureFormat = true;

            STDLOG_STATUS("Found texture format: %s (%d:%d:%d:%d)\n", entry.pName,
                pTexFormat->ci.redBPP, pTexFormat->ci.greenBPP, pTexFormat->ci.blueBPP, pTexFormat->ci.alphaBPP);
            ++std3D_numTextureFormats;
        }
    }

    // Starts collecting the next pass of the scene. std3DX9.c drew into the device's back buffer
    // directly; here the clears it made with IDirect3DDevice9_Clear are the load operations.
    void std3D_BeginScenePass(bool bClearColor, bool bClearDepth)
    {
        std3D_sceneColorLoad = bClearColor ? render::LoadOp::Clear : render::LoadOp::Load;
        std3D_sceneDepthLoad = bClearDepth ? render::LoadOp::Clear : render::LoadOp::Load;
        std3D_aSceneDraws.clear();
    }

    // Records the collected pass into the frame: the back buffer with the pass's load operations,
    // then its draws, each with only the bindings and uniforms that changed since the draw before.
    // Returns false when the command list refuses the pass. The pass is done either way.
    bool std3D_EndScenePass()
    {
        render::CommandList& frame = stdRenderBabylon_GetFrame();

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_GetColorTarget(), 0,
            std3D_sceneColorLoad, render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.depthTarget = render::DepthAttachment{ stdRenderBabylon_GetDepthTarget(),
            std3D_sceneDepthLoad, render::StoreOp::Store, 1.0f, 0 };

        bool bRecorded = frame.beginPass(pass);
        if ( bRecorded && !std3D_aSceneDraws.empty() )
        {
            // std3DX9.c's g_viewport, set at each BeginScene: the viewport rectangle, the whole back buffer.
            const float aViewport[4] = { 0.0f, 0.0f, (float)stdRenderBabylon_GetBackBufferWidth(), (float)stdRenderBabylon_GetBackBufferHeight() };
            frame.pushUniforms(render::ShaderStage::Vertex, 0, std::as_bytes(std::span<const float>(aViewport)));

            const Std3DBabylonDraw* pPrev = nullptr;
            render::BufferHandle boundIndexBuffer;
            render::TextureHandle boundTexture;
            render::SamplerHandle boundSampler;
            for ( const Std3DBabylonDraw& draw : std3D_aSceneDraws )
            {
                if ( !pPrev || pPrev->pipeline != draw.pipeline )
                {
                    frame.setPipeline(draw.pipeline);
                }

                if ( !pPrev || pPrev->vertexBuffer != draw.vertexBuffer )
                {
                    frame.bindVertexBuffer(0, draw.vertexBuffer, 0);
                }

                if ( !pPrev || std::memcmp(&pPrev->uniforms, &draw.uniforms, sizeof(draw.uniforms)) != 0 )
                {
                    frame.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const Std3DBabylonFragmentUniforms, 1>(&draw.uniforms, 1)));
                }

                if ( draw.primitive == Std3DBabylonPrimitive::Triangles )
                {
                    if ( boundIndexBuffer != draw.indexBuffer )
                    {
                        frame.bindIndexBuffer(draw.indexBuffer, render::IndexType::Uint16, 0);
                        boundIndexBuffer = draw.indexBuffer;
                    }

                    if ( boundTexture != draw.texture || boundSampler != draw.sampler )
                    {
                        frame.bindTexture(render::ShaderStage::Fragment, 0, draw.texture, draw.sampler);
                        boundTexture = draw.texture;
                        boundSampler = draw.sampler;
                    }

                    frame.drawIndexed(draw.numIndices, draw.firstIndex, (int32_t)draw.firstVertex);
                }
                else
                {
                    frame.draw(draw.numVertices, draw.firstVertex);
                }

                pPrev = &draw;
            }
        }

        std3D_aSceneDraws.clear();
        return bRecorded && frame.endPass();
    }

    // Makes room for numBytes at *pOffset of a stream buffer. When the rest of the buffer is too
    // small for them, a buffer twice as big (or as big as numBytes) takes over from offset 0; the old
    // one is destroyed after the frame's submit, since the frame's earlier draws still read it.
    bool std3D_ReserveStream(render::BufferHandle* pBuffer, uint32_t* pSize, uint32_t* pOffset, uint64_t numBytes,
        render::BufferUsage usage, const char* pName)
    {
        if ( (uint64_t)*pOffset + numBytes <= *pSize )
        {
            return true;
        }

        const uint64_t maxSize = std3D_pRenderDevice->info().limits.maxBufferSize;
        if ( numBytes > maxSize )
        {
            STDLOG_ERROR("Error %llu bytes don't fit a %s buffer (limit %llu).\n", (unsigned long long)numBytes, pName, (unsigned long long)maxSize);
            return false;
        }

        const uint32_t newSize = (uint32_t)std::min(maxSize, std::max(2u * (uint64_t)*pSize, numBytes));
        const render::BufferHandle buffer = std3D_pRenderDevice->createBuffer(render::BufferDesc{ newSize, usage });
        if ( buffer.isNull() )
        {
            STDLOG_ERROR("Error creating a %u byte %s buffer.\n", newSize, pName);
            return false;
        }

        stdRenderBabylon_DestroyBuffer(*pBuffer);
        *pBuffer = buffer;
        *pSize   = newSize;
        *pOffset = 0;
        return true;
    }

    // std3DX9.c's Direct3D state as a pipeline: its default shader for triangles, its wireframe
    // shader for lines and points; alpha blending with SRCALPHA, INVSRCALPHA for color and alpha
    // (D3DRS_ALPHABLENDENABLE, never turned off, without separate alpha blending); the Z test
    // LESSEQUAL (D3DRS_ZENABLE, D3DRS_ZFUNC), Z writes as STD3D_RS_ZWRITE_DISABLED says; no culling.
    // The pipeline of each primitive and Z write is created once, on first use.
    render::PipelineHandle std3D_GetPipeline(Std3DBabylonPrimitive primitive, bool bZWrite)
    {
        render::PipelineHandle& pipeline = std3D_aPipelines[(size_t)primitive][bZWrite ? 1 : 0];
        if ( !pipeline.isNull() )
        {
            return pipeline;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::std3dVert();
        desc.fragmentShader = primitive == Std3DBabylonPrimitive::Triangles ? stdBabylon::shaders::std3dFrag() : stdBabylon::shaders::std3dWireframeFrag();
        desc.vertexLayout.bufferStrides = { (uint32_t)sizeof(StdVertexTL) };
        desc.vertexLayout.attributes    = {
            render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, (uint32_t)offsetof(StdVertexTL, sx) },
            render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, color) },
            render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, specular) },
            render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, (uint32_t)offsetof(StdVertexTL, tu) },
        };

        switch ( primitive )
        {
            case Std3DBabylonPrimitive::Triangles:
                desc.primitive = render::PrimitiveType::TriangleList;
                break;
            case Std3DBabylonPrimitive::Lines:
                desc.primitive = render::PrimitiveType::LineStrip;
                break;
            case Std3DBabylonPrimitive::Points:
                desc.primitive = render::PrimitiveType::PointList;
                break;
        }

        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::SrcAlpha;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        blend.srcAlpha = render::BlendFactor::SrcAlpha;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        desc.colorTargets = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, blend } };
        desc.depthFormat  = stdRenderBabylon_GetDepthFormat();
        desc.depth        = render::DepthState{ true, bZWrite, render::CompareOp::LessOrEqual };
        desc.cull         = render::CullMode::None;

        pipeline = std3D_pRenderDevice->createPipeline(desc);
        if ( pipeline.isNull() )
        {
            STDLOG_ERROR("Error creating the pipeline of primitive %d.\n", (int)primitive);
        }

        return pipeline;
    }

    // std3DX9.c's sampler state of a render state: linear filtering with STD3D_RS_TEXFILTER_BILINEAR
    // and with STD3D_RS_TEXFILTER_ANISOTROPIC (no device here filters anisotropically, and
    // std3DX9.c then filtered linearly), else point; the mip filter of std3D_SetMipmapFilter
    // (linear for trilinear, else point: render::SamplerDesc has no D3DTEXF_NONE, and textures
    // allocated with STD3D_MIPMAPFILTER_NONE have one level); clamp or wrap in U and V. Each of
    // the 16 is created once, on first use.
    render::SamplerHandle std3D_GetSampler(Std3DRenderState state)
    {
        const bool bLinear    = (state & (STD3D_RS_TEXFILTER_BILINEAR | STD3D_RS_TEXFILTER_ANISOTROPIC)) != 0;
        const bool bMipLinear = std3D_mipmapFilter == STD3D_MIPMAPFILTER_TRILINEAR;
        const bool bClampU    = (state & STD3D_RS_TEX_CPAMP_U) != 0;
        const bool bClampV    = (state & STD3D_RS_TEX_CPAMP_V) != 0;

        render::SamplerHandle& sampler = std3D_aSamplers[(bLinear ? 1u : 0u) | (bMipLinear ? 2u : 0u) | (bClampU ? 4u : 0u) | (bClampV ? 8u : 0u)];
        if ( !sampler.isNull() )
        {
            return sampler;
        }

        render::SamplerDesc desc;
        desc.minFilter = bLinear ? render::Filter::Linear : render::Filter::Nearest;
        desc.magFilter = desc.minFilter;
        desc.mipFilter = bMipLinear ? render::Filter::Linear : render::Filter::Nearest;
        desc.addressU  = bClampU ? render::AddressMode::ClampToEdge : render::AddressMode::Repeat;
        desc.addressV  = bClampV ? render::AddressMode::ClampToEdge : render::AddressMode::Repeat;

        sampler = std3D_pRenderDevice->createSampler(desc);
        if ( sampler.isNull() )
        {
            STDLOG_ERROR("Error creating a sampler.\n");
        }

        return sampler;
    }

    // The fragment uniforms of a draw: the shader fog std3D_SetRenderState keeps and the alpha
    // reference of the render state.
    Std3DBabylonFragmentUniforms std3D_GetFragmentUniforms()
    {
        Std3DBabylonFragmentUniforms uniforms;
        STD_ZEROMEM(&uniforms, sizeof(uniforms));
        std::memcpy(uniforms.aFogParams, std3D_aShaderFogParams, sizeof(uniforms.aFogParams));
        std::memcpy(uniforms.aFogColor, std3D_aShaderFogColor, sizeof(uniforms.aFogColor));
        uniforms.aAlphaTest[0] = (std3D_renderState & STD3D_RS_ALPHAREF_SET) != 0 ? (float)std3D_kAlphaRef / 255.0f : 0.0f;
        return uniforms;
    }

    // Streams one draw's vertices and indices into the frame and adds the draw to the pass being
    // collected, with the pipeline, texture, sampler and uniforms of the current render state.
    // Logs and draws nothing outside a scene (std3DX9.c's draws failed with D3DERR_INVALIDCALL there)
    // and when a resource can't be made.
    void std3D_RecordDraw(Std3DBabylonPrimitive primitive, render::TextureHandle texture, const StdVertexTL* aVerts, size_t numVerts,
        const uint16_t* aIndices, size_t numIndices)
    {
        if ( !std3D_bInScene )
        {
            STDLOG_ERROR("Error drawing outside a scene.\n");
            return;
        }

        const bool bTriangles = primitive == Std3DBabylonPrimitive::Triangles;
        const render::PipelineHandle pipeline = std3D_GetPipeline(primitive, (std3D_renderState & STD3D_RS_ZWRITE_DISABLED) == 0);
        const render::SamplerHandle sampler   = bTriangles ? std3D_GetSampler(std3D_renderState) : render::SamplerHandle{};
        if ( pipeline.isNull() || (bTriangles && sampler.isNull()) )
        {
            return;
        }

        const uint64_t numVertexBytes = (uint64_t)numVerts * sizeof(StdVertexTL);
        const uint64_t numIndexBytes  = (uint64_t)numIndices * sizeof(uint16_t);
        if ( !std3D_ReserveStream(&std3D_vertexBuffer, &std3D_vertexBufferSize, &std3D_vertexBufferOffset, numVertexBytes, render::BufferUsage::Vertex, "vertex")
            || (bTriangles && !std3D_ReserveStream(&std3D_indexBuffer, &std3D_indexBufferSize, &std3D_indexBufferOffset, numIndexBytes, render::BufferUsage::Index, "index")) )
        {
            return;
        }

        render::CommandList& frame = stdRenderBabylon_GetFrame();
        frame.updateBuffer(std3D_vertexBuffer, std3D_vertexBufferOffset, std::span<const std::byte>(reinterpret_cast<const std::byte*>(aVerts), (size_t)numVertexBytes));

        Std3DBabylonDraw draw;
        draw.primitive    = primitive;
        draw.pipeline     = pipeline;
        draw.vertexBuffer = std3D_vertexBuffer;
        draw.texture      = bTriangles ? texture : render::TextureHandle{};
        draw.sampler      = sampler;
        draw.uniforms     = std3D_GetFragmentUniforms();
        draw.firstVertex  = std3D_vertexBufferOffset / (uint32_t)sizeof(StdVertexTL);
        draw.numVertices  = (uint32_t)numVerts;
        draw.firstIndex   = 0;
        draw.numIndices   = 0;
        std3D_vertexBufferOffset += (uint32_t)numVertexBytes;

        if ( bTriangles )
        {
            frame.updateBuffer(std3D_indexBuffer, std3D_indexBufferOffset, std::span<const std::byte>(reinterpret_cast<const std::byte*>(aIndices), (size_t)numIndexBytes));
            draw.indexBuffer = std3D_indexBuffer;
            draw.firstIndex  = std3D_indexBufferOffset / (uint32_t)sizeof(uint16_t);
            draw.numIndices  = (uint32_t)numIndices;
            std3D_indexBufferOffset += (uint32_t)numIndexBytes;
        }

        std3D_aSceneDraws.push_back(draw);
    }

    // The resources of the draws made at std3D_Open: the white texture std3DX9.c's
    // CreateWhiteTexture made for untextured draws in shader mode (one opaque white texel), and the
    // stream buffers.
    bool std3D_InitDrawResources()
    {
        render::TextureDesc whiteDesc;
        whiteDesc.width     = 1;
        whiteDesc.height    = 1;
        whiteDesc.mipLevels = 1;
        whiteDesc.format    = render::TextureFormat::Bgra8;
        whiteDesc.usage     = render::TextureUsage::Sampled;

        const std::byte aWhite[4] = { std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF } };
        std3D_whiteTexture = std3D_pRenderDevice->createTexture(whiteDesc);
        if ( std3D_whiteTexture.isNull() || !std3D_pRenderDevice->uploadTexture(std3D_whiteTexture, 0, std::span<const std::byte>(aWhite)) )
        {
            STDLOG_ERROR("Failed to create white texture for shader\n");
            return false;
        }

        std3D_vertexBuffer       = std3D_pRenderDevice->createBuffer(render::BufferDesc{ std3D_kInitialVertexBufferSize, render::BufferUsage::Vertex });
        std3D_indexBuffer        = std3D_pRenderDevice->createBuffer(render::BufferDesc{ std3D_kInitialIndexBufferSize, render::BufferUsage::Index });
        std3D_vertexBufferSize   = std3D_kInitialVertexBufferSize;
        std3D_indexBufferSize    = std3D_kInitialIndexBufferSize;
        std3D_vertexBufferOffset = 0;
        std3D_indexBufferOffset  = 0;
        if ( std3D_vertexBuffer.isNull() || std3D_indexBuffer.isNull() )
        {
            STDLOG_ERROR("Failed to create the vertex and index buffers.\n");
            return false;
        }

        return true;
    }

    void std3D_ReleaseDrawResources()
    {
        render::Device* pDevice = stdRenderBabylon_GetDevice();
        if ( pDevice )
        {
            for ( auto& aPipelines : std3D_aPipelines )
            {
                for ( render::PipelineHandle& pipeline : aPipelines )
                {
                    pDevice->destroyPipeline(pipeline);
                }
            }

            for ( render::SamplerHandle& sampler : std3D_aSamplers )
            {
                pDevice->destroySampler(sampler);
            }

            stdRenderBabylon_DestroyTexture(std3D_whiteTexture);
            stdRenderBabylon_DestroyBuffer(std3D_vertexBuffer);
            stdRenderBabylon_DestroyBuffer(std3D_indexBuffer);
        }

        for ( auto& aPipelines : std3D_aPipelines )
        {
            for ( render::PipelineHandle& pipeline : aPipelines )
            {
                pipeline = {};
            }
        }

        for ( render::SamplerHandle& sampler : std3D_aSamplers )
        {
            sampler = {};
        }

        std3D_whiteTexture       = {};
        std3D_vertexBuffer       = {};
        std3D_indexBuffer        = {};
        std3D_vertexBufferSize   = 0;
        std3D_indexBufferSize    = 0;
        std3D_vertexBufferOffset = 0;
        std3D_indexBufferOffset  = 0;
        std3D_aSceneDraws.clear();
        STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams));
        STD_ZEROMEM(std3D_aShaderFogColor, sizeof(std3D_aShaderFogColor));
    }

    void std3D_ReleaseSystemResources()
    {
        std3D_ResetTextureCacheInternal();
        std3D_ReleaseDrawResources();
        std3D_sceneColorTarget   = {};
        std3D_bInScene           = false;
        std3D_bClearColorPending = false;
        std3D_bClearDepthPending = false;
    }

    // The DX9 InitSystem minus the Direct3D state: formats, the back buffer for the viewport, the
    // cache, the render state and the texture memory.
    bool std3D_InitSystem()
    {
        std3D_InitTextureFormats();
        if ( !std3D_numTextureFormats || !std3D_bHasRGBTextureFormat )
        {
            STDLOG_ERROR("Error no texture formats found.\n");
            return false;
        }

        // std3DX9.c's CreateViewport: the viewport is the display's back buffer, cleared to black
        // with Z 1. The clear happens in the first pass on it.
        uint32_t width  = 0;
        uint32_t height = 0;
        stdDisplay_GetBackBufferSize(&width, &height);
        if ( !stdRenderBabylon_SetBackBufferSize(width, height, nullptr) )
        {
            STDLOG_ERROR("Error creating viewport.\n");
            return false;
        }

        std3D_bClearColorPending = true;
        std3D_bClearDepthPending = true;
        std3D_sceneColorTarget   = stdRenderBabylon_GetColorTarget();

        std3D_g_maxVertices = std3D_pCurDevice->maxVertexCount;
        if ( std3D_g_maxVertices == 0 )
        {
            std3D_g_maxVertices = STD3D_MAXVERTICES;
        }
        STDLOG_STATUS("Max vertices: %zu.\n", std3D_g_maxVertices);

        std3D_frameCount        = 1;
        std3D_numCachedTextures = 0;
        std3D_pFirstTexCache    = nullptr;
        std3D_pLastTexCache     = nullptr;

        std3D_RGBTextureFormat     = std3D_FindClosestFormat(&stdColor_cfBGR8888);
        std3D_RGBAKeyTextureFormat = std3D_FindClosestFormat(&stdColor_cfRGBA8888);
        std3D_RGBATextureFormat    = std3D_FindClosestFormat(&stdColor_cfRGBA8888);

        if ( !std3D_InitDrawResources() )
        {
            return false;
        }

        // std3DX9.c's InitRenderState: the trilinear mip filter, linear filtering (no device here
        // filters anisotropically), wrapping, shader fog off.
        std3D_mipmapFilter = STD3D_MIPMAPFILTER_TRILINEAR;
        std3D_renderState  = (Std3DRenderState)(STD3D_RS_UNKNOWN_1 | STD3D_RS_TEXFILTER_BILINEAR | STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2);
        std3D_pCurTex      = nullptr;
        STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams));

        if ( stdDisplay_GetTextureMemory(&std3D_pCurDevice->totalMemory, &std3D_pCurDevice->availableMemory) )
        {
            return false;
        }

        return true;
    }

    // Arguments of the draw calls.
    bool std3D_CheckVertices(const StdVertexTL* aVerts, size_t numVerts)
    {
        if ( numVerts > std3D_g_maxVertices )
        {
            STDLOG_ERROR("Error %zu > %zu maxVertices.\n", numVerts, std3D_g_maxVertices);
            return false;
        }

        if ( numVerts && !aVerts )
        {
            STDLOG_ERROR("Error %zu vertices without a vertex array.\n", numVerts);
            return false;
        }

        return true;
    }
}

void std3D_ResetGlobals(void)
{
    float fogDensity = 1.0f;
    std::memcpy(&std3D_g_fogDensity, &fogDensity, sizeof(std3D_g_fogDensity));
    STD_ZEROMEM(&std3D_g_maxVertices, sizeof(std3D_g_maxVertices));
}

int std3D_Startup(void)
{
    // std3DX9.c asserted that it wasn't started; a second startup is answered like std3DNull.c does.
    if ( std3D_bStartup )
    {
        return 1;
    }

    STD_ZEROMEM(std3D_aTextureFormats, sizeof(std3D_aTextureFormats));
    STD_ZEROMEM(std3D_aDevices, sizeof(std3D_aDevices));

    StdBabylonContext* pContext = stdBabylon_GetContext();
    std3D_pRenderDevice = pContext ? pContext->pRenderDevice : nullptr;
    if ( !std3D_pRenderDevice )
    {
        STDLOG_ERROR("No render device in the Babylon context.\n");
        return 0;
    }

    if ( !std3D_BuildDeviceList() )
    {
        STDLOG_ERROR("Error building device list.\n");
        std3D_pRenderDevice = nullptr;
        return 0;
    }

    if ( std3D_numDevices == 0 )
    {
        std3D_pRenderDevice = nullptr;
        return 0;
    }

    std3D_bStartup = true;
    return 1;
}

void std3D_Shutdown(void)
{
    if ( !std3D_bStartup )
    {
        STDLOG_WARNING("Warning: System already shutdown!\n");
        return;
    }

    if ( std3D_bOpen )
    {
        std3D_Close();
    }

    STD_ZEROMEM(std3D_aTextureFormats, sizeof(std3D_aTextureFormats));
    STD_ZEROMEM(std3D_aDevices, sizeof(std3D_aDevices));

    std3D_pRenderDevice = nullptr;
    std3D_numDevices    = 0;
    std3D_bStartup      = false;
}

const Device3D* std3D_GetCurrentDevice(void)
{
    return std3D_pCurDevice;
}

size_t std3D_GetNumDevices(void)
{
    return std3D_numDevices;
}

const Device3D* std3D_GetAllDevices(void)
{
    return std3D_aDevices;
}

void J3DAPI std3D_SetFindAllDevices(int bFindAll)
{
    std3D_bFindAllD3Devices = bFindAll != 0;
}

int J3DAPI std3D_Open(size_t deviceNum)
{
    STD_ASSERTREL(std3D_bStartup);
    if ( std3D_bOpen )
    {
        STDLOG_ERROR("Warning: System already open!\n");
        return 0;
    }

    if ( !stdDisplay_IsOpen() )
    {
        STDLOG_ERROR("std3D_Open: Display system is not opened!\n");
        return 0;
    }

    if ( deviceNum >= std3D_numDevices )
    {
        return 0;
    }

    std3D_curDevice  = deviceNum;
    std3D_pCurDevice = &std3D_aDevices[deviceNum];

    // The display opened the frame owner on the same device (stdDisplay_Open).
    if ( stdRenderBabylon_GetDevice() != std3D_pRenderDevice )
    {
        STDLOG_ERROR("std3D_Open: the display does not draw with the render device of the context.\n");
        std3D_pCurDevice = nullptr;
        std3D_curDevice  = 0;
        return 0;
    }

    if ( !std3D_InitSystem() )
    {
        STDLOG_ERROR("Failed to initialize system, closing...\n");
        std3D_ReleaseSystemResources();
        std3D_numTextureFormats    = 0;
        std3D_bHasRGBTextureFormat = false;
        std3D_pCurDevice           = nullptr;
        std3D_curDevice            = 0;
        return 0;
    }

    size_t memFree  = 0;
    size_t memTotal = 0;
    stdDisplay_GetTotalMemory(&memTotal, &memFree);

    STDLOG_STATUS("Texture Ram  Total: %zu bytes  Free: %zu bytes.\n", std3D_pCurDevice->totalMemory, std3D_pCurDevice->availableMemory);
    STDLOG_STATUS("Video Ram Total: %zu bytes  Free: %zu bytes.\n", memTotal, memFree);

    std3D_bOpen = true;
    return 1;
}

void std3D_Close(void)
{
    if ( !std3D_bOpen )
    {
        STDLOG_WARNING("Warning: System already closed!\n");
        return;
    }

    std3D_ReleaseSystemResources();

    std3D_mipmapFilter         = (Std3DMipmapFilterType)-1;
    std3D_numTextureFormats    = 0;
    std3D_curDevice            = 0;
    std3D_pCurDevice           = nullptr;
    std3D_bHasRGBTextureFormat = false;
    std3D_bOpen                = false;
}

void J3DAPI std3D_GetTextureFormat(StdColorFormatType type, ColorInfo* pDest, int* pbColorKeySet, LPDDCOLORKEY* ppColorKey)
{
    if ( type == STDCOLOR_FORMAT_RGBA_1BITALPHA )
    {
        *pbColorKeySet = std3D_aTextureFormats[std3D_RGBAKeyTextureFormat].bColorKey;
        *ppColorKey    = std3D_aTextureFormats[std3D_RGBAKeyTextureFormat].pColorKey;
        *pDest         = std3D_aTextureFormats[std3D_RGBAKeyTextureFormat].ci;
    }
    else if ( type == STDCOLOR_FORMAT_RGBA )
    {
        *pbColorKeySet = std3D_aTextureFormats[std3D_RGBATextureFormat].bColorKey;
        *ppColorKey    = std3D_aTextureFormats[std3D_RGBATextureFormat].pColorKey;
        *pDest         = std3D_aTextureFormats[std3D_RGBATextureFormat].ci;
    }
    else
    {
        // std3DX9.c left *ppColorKey as it was here; no color key is set, so it is NULL.
        *pbColorKeySet = 0;
        *ppColorKey    = nullptr;
        *pDest         = std3D_aTextureFormats[std3D_RGBTextureFormat].ci;
    }
}

StdColorFormatType J3DAPI std3D_GetColorFormat(const ColorInfo* pCi)
{
    if ( pCi->alphaBPP == 0 )
    {
        return STDCOLOR_FORMAT_RGB;
    }

    if ( pCi->alphaBPP == 1 )
    {
        return STDCOLOR_FORMAT_RGBA_1BITALPHA;
    }

    return STDCOLOR_FORMAT_RGBA;
}

size_t std3D_GetNumTextureFormats(void)
{
    return std3D_numTextureFormats;
}

int std3D_StartScene(void)
{
    ++std3D_frameCount;
    std3D_pCurTex = nullptr;

    // std3DX9.c's BeginScene error paths: no device, or a scene already begun (D3DERR_INVALIDCALL).
    if ( !std3D_bOpen )
    {
        STDLOG_ERROR("Error beginning scene: std3D is not open.\n");
        return 1;
    }

    if ( std3D_bInScene )
    {
        STDLOG_ERROR("Error beginning scene: the scene is already begun.\n");
        return 1;
    }

    // The back buffer follows the display's size; new targets start cleared like the DX9 viewport,
    // also when the display made them (a new video mode).
    uint32_t width  = 0;
    uint32_t height = 0;
    bool bRecreated = false;
    stdDisplay_GetBackBufferSize(&width, &height);
    if ( !stdRenderBabylon_SetBackBufferSize(width, height, &bRecreated) )
    {
        STDLOG_ERROR("Error beginning scene: no %ux%u back buffer.\n", width, height);
        return 1;
    }

    if ( stdRenderBabylon_GetColorTarget() != std3D_sceneColorTarget )
    {
        bRecreated             = true;
        std3D_sceneColorTarget = stdRenderBabylon_GetColorTarget();
    }

    if ( bRecreated )
    {
        std3D_bClearColorPending = true;
        std3D_bClearDepthPending = true;
    }

    // A new frame streams its vertices and indices from the start of the buffers again.
    if ( !stdRenderBabylon_HasFrame() )
    {
        std3D_vertexBufferOffset = 0;
        std3D_indexBufferOffset  = 0;
    }

    if ( stdRenderBabylon_GetFrame().inPass() )
    {
        STDLOG_ERROR("Error beginning scene: the frame has a pass open.\n");
        return 1;
    }

    std3D_BeginScenePass(std3D_bClearColorPending, std3D_bClearDepthPending);
    std3D_bClearColorPending = false;
    std3D_bClearDepthPending = false;
    std3D_bInScene           = true;
    return 0;
}

void std3D_EndScene(void)
{
    std3D_pCurTex = nullptr;
    if ( !std3D_bInScene )
    {
        STDLOG_ERROR("Error ending scene: no scene begun.\n");
        return;
    }

    std3D_bInScene = false;
    if ( !std3D_EndScenePass() )
    {
        STDLOG_ERROR("Error ending scene.\n");
    }

    // The frame stays open: stdDisplay_Update submits and presents it, like stdDisplayDX9.c's Present.
}

void J3DAPI std3D_DrawRenderList(tSysTexture* pTex, Std3DRenderState rdflags, StdVertexTL* aVerts, size_t numVerts, uint16_t* aIndices, size_t numIndices)
{
    if ( !std3D_CheckVertices(aVerts, numVerts) )
    {
        return;
    }

    std3D_SetRenderState(rdflags);
    std3D_pCurTex = pTex;

    if ( numIndices && !aIndices )
    {
        STDLOG_ERROR("Error %zu indices without an index array.\n", numIndices);
        return;
    }

    for ( size_t i = 0; i < numIndices; ++i )
    {
        if ( aIndices[i] >= numVerts )
        {
            STDLOG_ERROR("Error index %u at %zu is not below %zu vertices.\n", (unsigned)aIndices[i], i, numVerts);
            return;
        }
    }

    // numIndices / 3 triangles, as std3DX9.c drew; an untextured draw samples the white texture.
    const size_t numTriangleIndices = numIndices / 3u * 3u;
    if ( !numVerts || !numTriangleIndices )
    {
        return;
    }

    std3D_RecordDraw(Std3DBabylonPrimitive::Triangles, pTex ? pTex->texture : std3D_whiteTexture, aVerts, numVerts, aIndices, numTriangleIndices);
}

void std3D_SetWireframeRenderState(void)
{
    Std3DRenderState rdstate = (Std3DRenderState)(std3D_renderState & ~(STD3D_RS_FOG_ENABLED | STD3D_RS_UNKNOWN_400 | STD3D_RS_UNKNOWN_200));
    std3D_SetRenderState(rdstate);
    std3D_pCurTex = nullptr;
}

void J3DAPI std3D_DrawLineStrip(StdVertexTL* aVerts, size_t numVerts)
{
    if ( !std3D_CheckVertices(aVerts, numVerts) )
    {
        return;
    }

    // numVerts - 1 lines in the current render state (std3D_SetWireframeRenderState sets it), with
    // std3DX9.c's wireframe shader.
    if ( numVerts < 2u )
    {
        return;
    }

    std3D_RecordDraw(Std3DBabylonPrimitive::Lines, render::TextureHandle{}, aVerts, numVerts, nullptr, 0);
}

void J3DAPI std3D_DrawPointList(StdVertexTL* aVerts, size_t numVerts)
{
    if ( !std3D_CheckVertices(aVerts, numVerts) )
    {
        return;
    }

    // Points of one pixel, as Direct3D 9's default point size drew them, with std3DX9.c's wireframe
    // shader.
    if ( !numVerts )
    {
        return;
    }

    std3D_RecordDraw(Std3DBabylonPrimitive::Points, render::TextureHandle{}, aVerts, numVerts, nullptr, 0);
}

void J3DAPI std3D_SetRenderState(Std3DRenderState rdflags)
{
    if ( std3D_renderState == rdflags )
    {
        return;
    }

    // std3DX9.c translated each changed bit into Direct3D render and sampler state here. The draws
    // read Z write, clamping, filtering and the alpha reference from the bits themselves
    // (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms), which ends in the same
    // state. Fog is the exception: the shader fog took the fog values of the moment the fog bit
    // changed (stdShader_SetFog, stdShader_DisableFog), and kept them until it changed again.
    if ( ((std3D_renderState ^ rdflags) & STD3D_RS_FOG_ENABLED) != 0 )
    {
        if ( (rdflags & STD3D_RS_FOG_ENABLED) != 0 && std3D_bRenderFog )
        {
            std3D_aShaderFogParams[0] = std3D_fogStartDepth;
            std3D_aShaderFogParams[1] = std3D_fogEndDepth;
            std3D_aShaderFogParams[2] = std3D_fogDepthFactor;
            std3D_aShaderFogParams[3] = 1.0f;
            std::memcpy(std3D_aShaderFogColor, std3D_aFogColor, sizeof(std3D_aShaderFogColor));
        }
        else
        {
            STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams)); // the color stays
        }
    }

    std3D_renderState = rdflags;
}

void J3DAPI std3D_AllocSystemTexture(tSystemTexture* pTexture, tVBuffer** apVBuffers, size_t numMipLevels, StdColorFormatType formatType)
{
    STD_ZEROMEM(pTexture, sizeof(tSystemTexture));

    if ( !std3D_numTextureFormats )
    {
        return;
    }

    // Get mipmap buffer at LOD 0
    tVBuffer* pVBuffer = *apVBuffers;
    uint32_t texHeight = 0, texWidth = 0;
    std3D_GetValidDimensions(pVBuffer->rasterInfo.width, pVBuffer->rasterInfo.height, &texWidth, &texHeight);
    while ( numMipLevels > 1 && (pVBuffer->rasterInfo.width > texWidth || pVBuffer->rasterInfo.height > texHeight) )
    {
        --numMipLevels;
        pVBuffer = *++apVBuffers;
        std3D_GetValidDimensions(pVBuffer->rasterInfo.width, pVBuffer->rasterInfo.height, &texWidth, &texHeight);
    }

    size_t texSize = (pVBuffer->rasterInfo.colorInfo.bpp * texHeight * texWidth) / 8;
    if ( std3D_mipmapFilter == STD3D_MIPMAPFILTER_NONE )
    {
        numMipLevels = 1;
    }

    // std3DX9.c took the texture format of formatType and copied the mip bytes into it as they were.
    // A texture format with the layout of the mips is taken here instead, so they are uploaded
    // unchanged; that is the format of formatType whenever the caller converted the mips to the
    // ColorInfo std3D_GetTextureFormat named, as rdMaterial and sithMaterial do. Mips of any other
    // layout are converted to the format of formatType when they are uploaded.
    size_t formatIndex = std3D_RGBTextureFormat;
    if ( formatType == STDCOLOR_FORMAT_RGBA_1BITALPHA )
    {
        formatIndex = std3D_RGBAKeyTextureFormat;
    }
    else if ( formatType == STDCOLOR_FORMAT_RGBA )
    {
        formatIndex = std3D_RGBATextureFormat;
    }

    for ( size_t i = 0; i < std3D_numTextureFormats; ++i )
    {
        if ( std3D_SameLayout(pVBuffer->rasterInfo.colorInfo, std3D_aTextureFormats[i].ci) )
        {
            formatIndex = i;
            break;
        }
    }

    pTexture->apMipmaps = (tVBuffer**)STDMALLOC(numMipLevels * sizeof(tVBuffer*));
    if ( !pTexture->apMipmaps )
    {
        STDLOG_ERROR("Failed to allocate memory for VBuffer array.\n");
        goto error;
    }

    STD_ZEROMEM(pTexture->apMipmaps, numMipLevels * sizeof(tVBuffer*));
    pTexture->numMipLevels = numMipLevels;
    pTexture->format       = std3D_aTextureFormats[formatIndex].ddPixelFmt;
    pTexture->textureSize  = texSize;

    // Owned VBuffer copies of every mip level, in system memory: the texture outlives cache resets.
    for ( size_t mmNum = 0; mmNum < numMipLevels; ++mmNum )
    {
        pTexture->apMipmaps[mmNum] = stdDisplay_VBufferNew(&apVBuffers[mmNum]->rasterInfo, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
        if ( !pTexture->apMipmaps[mmNum] )
        {
            STDLOG_ERROR("Failed to create VBuffer for mip level %zu.\n", mmNum);
            goto error;
        }

        tColorMode colorMode = apVBuffers[mmNum]->rasterInfo.colorInfo.colorMode;
        if ( colorMode )
        {
            if ( colorMode > STDCOLOR_PAL && colorMode <= STDCOLOR_RGBA )
            {
                stdDisplay_VBufferLock(apVBuffers[mmNum]);
                stdDisplay_VBufferLock(pTexture->apMipmaps[mmNum]);

                size_t pixelDataSize = apVBuffers[mmNum]->rasterInfo.height * apVBuffers[mmNum]->rasterInfo.rowSize;
                std::memcpy(pTexture->apMipmaps[mmNum]->pPixels, apVBuffers[mmNum]->pPixels, pixelDataSize);

                stdDisplay_VBufferUnlock(pTexture->apMipmaps[mmNum]);
                stdDisplay_VBufferUnlock(apVBuffers[mmNum]);
            }
        }
        else
        {
            STDLOG_ERROR("Can't use paletized textures.\n");
            goto error;
        }
    }

    return;

error:
    // std3DX9.c freed only the array here and leaked the mip copies made so far.
    if ( pTexture->apMipmaps )
    {
        for ( size_t mmNum = 0; mmNum < pTexture->numMipLevels; ++mmNum )
        {
            if ( pTexture->apMipmaps[mmNum] )
            {
                stdDisplay_VBufferFree(pTexture->apMipmaps[mmNum]);
            }
        }

        stdMemory_Free(pTexture->apMipmaps);
    }
    STD_ZEROMEM(pTexture, sizeof(tSystemTexture));

    STDLOG_ERROR("Done error exit from std3D_AllocSystemTexture.\n");
}

void J3DAPI std3D_GetValidDimensions(uint32_t width, uint32_t height, uint32_t* pOutWidth, uint32_t* pOutHeight)
{
    // std3DX9.c read the open device only; before std3D_Open the started device answers.
    const Device3D* pDevice = std3D_pCurDevice ? std3D_pCurDevice : (std3D_numDevices ? &std3D_aDevices[0] : nullptr);
    const uint32_t maxWidth  = pDevice ? (uint32_t)pDevice->maxTexWidth : std3D_kDefaultMaxTextureSize;
    const uint32_t maxHeight = pDevice ? (uint32_t)pDevice->maxTexHeight : std3D_kDefaultMaxTextureSize;
    const uint32_t minWidth  = pDevice ? (uint32_t)pDevice->minTexWidth : 1u;
    const uint32_t minHeight = pDevice ? (uint32_t)pDevice->minTexHeight : 1u;

    uint32_t texWidth  = STDMATH_CLAMP(width, minWidth, maxWidth);
    uint32_t texHeight = STDMATH_CLAMP(height, minHeight, maxHeight);

    if ( !pDevice || !pDevice->bSqareOnlyTexture || texWidth == texHeight )
    {
        *pOutWidth  = texWidth;
        *pOutHeight = texHeight;
    }
    else
    {
        if ( texWidth > texHeight )
        {
            texHeight = texWidth;
        }

        *pOutWidth  = texHeight;
        *pOutHeight = texHeight;
    }
}

void J3DAPI std3D_ClearSystemTexture(tSystemTexture* pTex)
{
    while ( pTex->numMipLevels > 0 )
    {
        stdDisplay_VBufferFree(pTex->apMipmaps[--pTex->numMipLevels]);
    }

    if ( pTex->apMipmaps )
    {
        stdMemory_Free(pTex->apMipmaps);
        pTex->apMipmaps = nullptr;
    }

    if ( pTex->pCachedTexture )
    {
        std3D_RemoveTextureFromCacheList(pTex);
        std3D_ReleaseCachedTexture(pTex);
    }

    STD_ZEROMEM(pTex, sizeof(tSystemTexture));
}

void J3DAPI std3D_AddToTextureCache(tSystemTexture* pCacheTexture, StdColorFormatType format)
{
    J3D_UNUSED(format);

    STD_ASSERTREL(pCacheTexture);

    render::TextureHandle texture;
    tSysTexture* pSysTexture = nullptr;
    const Std3DBabylonFormatEntry* pEntry = nullptr;
    Std3DBabylonPixelFormat pixelFormat{};
    tVBuffer* pTop = nullptr;
    render::TextureDesc desc;
    std::vector<std::byte> data;

    if ( pCacheTexture->numMipLevels == 0 || !pCacheTexture->apMipmaps )
    {
        STDLOG_ERROR("No Source texture.\n");
        goto error;
    }

    if ( !std3D_bOpen )
    {
        STDLOG_ERROR("Error: std3D is not open.\n");
        goto error;
    }

    // std3DX9.c created a second Direct3D texture and linked the system texture twice when it was
    // already cached; the old one is released first here.
    if ( pCacheTexture->pCachedTexture )
    {
        std3D_RemoveTextureFromCacheList(pCacheTexture);
        std3D_ReleaseCachedTexture(pCacheTexture);
    }

    if ( pCacheTexture->textureSize > std3D_pCurDevice->availableMemory )
    {
        std3D_PurgeTextureCache(pCacheTexture->textureSize);
    }

    pixelFormat = std3D_GetPixelFormat(pCacheTexture->format);
    pEntry      = pixelFormat.bValid ? std3D_FindFormatEntry((render::TextureFormat)pixelFormat.format, pixelFormat.bOpaque != 0) : nullptr;
    if ( !pEntry )
    {
        STDLOG_ERROR("System texture has no texture format.\n");
        goto error;
    }

    // Mip levels after the first must halve like Babylon's mip chain; a level that doesn't ends it.
    pTop = pCacheTexture->apMipmaps[0];
    desc.width     = pTop->rasterInfo.width;
    desc.height    = pTop->rasterInfo.height;
    desc.format    = pEntry->format;
    desc.usage     = render::TextureUsage::Sampled;
    desc.mipLevels = 1;
    while ( desc.mipLevels < pCacheTexture->numMipLevels
        && desc.mipLevels < render::maxMipLevels(desc.width, desc.height)
        && pCacheTexture->apMipmaps[desc.mipLevels]->rasterInfo.width == render::mipDimension(desc.width, desc.mipLevels)
        && pCacheTexture->apMipmaps[desc.mipLevels]->rasterInfo.height == render::mipDimension(desc.height, desc.mipLevels) )
    {
        ++desc.mipLevels;
    }

    if ( desc.mipLevels < pCacheTexture->numMipLevels )
    {
        STDLOG_WARNING("Texture mip level %u isn't half of the one above; %u of %zu levels are used.\n",
            desc.mipLevels, desc.mipLevels, pCacheTexture->numMipLevels);
    }

    texture = std3D_pRenderDevice->createTexture(desc);
    if ( texture.isNull() )
    {
        STDLOG_ERROR("Error creating %ux%u texture for cache.\n", desc.width, desc.height);
        goto error;
    }

    for ( uint32_t mmNum = 0; mmNum < desc.mipLevels; ++mmNum )
    {
        tVBuffer* pMip = pCacheTexture->apMipmaps[mmNum];
        if ( !stdDisplay_VBufferLockReadOnly(pMip) )
        {
            STDLOG_ERROR("Failed to lock source texture level %u.\n", mmNum);
            goto error;
        }

        const bool bConverted = std3D_GetMipData(pMip, *pEntry, data);
        stdDisplay_VBufferUnlock(pMip);
        if ( !bConverted || !std3D_pRenderDevice->uploadTexture(texture, mmNum, std::span<const std::byte>(data)) )
        {
            STDLOG_ERROR("Error uploading texture level %u.\n", mmNum);
            goto error;
        }
    }

    pSysTexture = (tSysTexture*)STDMALLOC(sizeof(tSysTexture));
    if ( !pSysTexture )
    {
        STDLOG_ERROR("Failed to allocate memory for the cached texture.\n");
        goto error;
    }

    pSysTexture->texture          = texture;
    pCacheTexture->pCachedTexture = pSysTexture;
    pCacheTexture->frameNum       = std3D_frameCount;
    std3D_AddTextureToCacheList(pCacheTexture);
    return;

error:
    stdRenderBabylon_DestroyTexture(texture);
    pCacheTexture->pCachedTexture = nullptr;
    pCacheTexture->frameNum       = 0;
    STDLOG_ERROR("Done error exit from std3D_AddToTextureCache.\n");
}

void std3D_ResetTextureCache(void)
{
    std3D_ResetTextureCacheInternal();
}

size_t J3DAPI std3D_GetMipMapCount(const tSystemTexture* pTexture)
{
    if ( !pTexture )
    {
        return 0;
    }

    return pTexture->numMipLevels;
}

void J3DAPI std3D_UpdateFrameCount(tSystemTexture* pTexture)
{
    // std3DX9.c relinked every texture, which breaks the list for one that isn't in it.
    if ( pTexture->pCachedTexture )
    {
        std3D_RemoveTextureFromCacheList(pTexture);
        std3D_AddTextureToCacheList(pTexture);
    }

    pTexture->frameNum = std3D_frameCount;
}

size_t J3DAPI std3D_FindClosestFormat(const ColorInfo* pMatch)
{
    if ( !std3D_numTextureFormats )
    {
        return 0;
    }

    size_t bestMatchLevel = 0;
    size_t closestMatch   = 0;
    for ( size_t i = 0; i < std3D_numTextureFormats; ++i )
    {
        StdTextureFormat* pFormat = &std3D_aTextureFormats[i];
        size_t matchLevel = 0;

        if ( pFormat->ci.colorMode == pMatch->colorMode )
        {
            matchLevel = 1;
            if ( pFormat->ci.bpp == pMatch->bpp )
            {
                matchLevel = 2;
                if ( pMatch->colorMode == STDCOLOR_RGB )
                {
                    if ( pFormat->ci.redBPP == pMatch->redBPP && pFormat->ci.greenBPP == pMatch->greenBPP && pFormat->ci.blueBPP == pMatch->blueBPP )
                    {
                        return i;
                    }
                }
                else
                {
                    if ( pMatch->colorMode != STDCOLOR_RGBA )
                    {
                        STDLOG_STATUS("Found a perfect mode matchLevel #%zu!\n", i);
                        return i;
                    }

                    if ( pFormat->ci.colorMode == STDCOLOR_RGBA )
                    {
                        matchLevel = 3;
                    }

                    if ( pFormat->ci.redBPP == pMatch->redBPP
                        && pFormat->ci.greenBPP == pMatch->greenBPP
                        && pFormat->ci.blueBPP == pMatch->blueBPP
                        && pFormat->ci.alphaBPP == pMatch->alphaBPP )
                    {
                        return i;
                    }
                }
            }
        }

        if ( matchLevel > bestMatchLevel )
        {
            closestMatch   = i;
            bestMatchLevel = matchLevel;
        }
    }

    STDLOG_STATUS("Settling for a closest matchLevel #%zu..\n", closestMatch);
    return closestMatch;
}

int J3DAPI std3D_SetMipmapFilter(Std3DMipmapFilterType filter)
{
    // Every render device filters mips linearly and by nearest level (render::SamplerDesc), so the
    // filter is never lowered like std3DX9.c did on devices without the caps; 0 is D3D_OK.
    std3D_mipmapFilter = filter;
    return 0;
}

int J3DAPI std3D_SetProjection(float fov, float nearPlane, float farPlane)
{
    float distance = farPlane - nearPlane;
    if ( std::fabs(distance) < 0.009999999776482582 )
    {
        return E_INVALIDARG;
    }

    float hfov    = fov / 2.0f;
    float sinhfov = std::sin(hfov);
    if ( std::fabs(sinhfov) < 0.009999999776482582f )
    {
        return E_INVALIDARG;
    }

    // The projection matrix std3DX9.c set is unused by its pre-transformed vertices; only its
    // fixed-function fog table used the depth scale.
    std3D_zDepth = distance * 10.0f;
    return 0;
}

void J3DAPI std3D_EnableFog(int bEnabled, float density)
{
    // std3DX9.c's shader path: the density is kept only with an open device.
    std3D_bRenderFog = bEnabled != 0;
    if ( !std3D_pCurDevice )
    {
        return;
    }

    std3D_g_fogDensity = density;
}

void J3DAPI std3D_SetFog(float red, float green, float blue, float startDepth, float endDepth)
{
    // std3DX9.c's shader path: the values reach the shaders at the next change of the fog bit
    // (std3D_SetRenderState).
    std3D_EnableFog(std3D_bRenderFog, std3D_g_fogDensity);

    std3D_fogStartDepth  = startDepth;
    std3D_fogEndDepth    = (2.0f - std3D_g_fogDensity) * endDepth;
    std3D_fogDepthFactor = 1.0f / (std3D_fogEndDepth - std3D_fogStartDepth);

    std3D_aFogColor[0] = red;
    std3D_aFogColor[1] = green;
    std3D_aFogColor[2] = blue;
    std3D_aFogColor[3] = 1.0f;
}

void std3D_ClearZBuffer(void)
{
    if ( !std3D_bOpen )
    {
        return;
    }

    // std3DX9.c cleared depth (and stencil) of the viewport at once. A pass can only clear when it
    // begins, so inside a scene the pass is ended and another one begun with depth cleared; outside
    // one the next scene's pass clears it.
    if ( !std3D_bInScene )
    {
        std3D_bClearDepthPending = true;
        return;
    }

    if ( !std3D_EndScenePass() )
    {
        STDLOG_ERROR("Error when clearing Z.\n");
    }

    std3D_BeginScenePass(false, true);
}

StdDisplayEnvironment* J3DAPI std3D_BuildDisplayEnvironment()
{
    StdDisplayEnvironment* pDeviceList = (StdDisplayEnvironment*)STDMALLOC(sizeof(StdDisplayEnvironment));
    STD_ZEROMEM(pDeviceList, sizeof(StdDisplayEnvironment));

    if ( !stdDisplay_Startup() )
    {
        STDLOG_ERROR("Error starting stdDisplay system.\n");
        std3D_FreeDisplayEnvironment(pDeviceList);
        return nullptr;
    }

    pDeviceList->numInfos      = stdDisplay_GetNumDevices();
    pDeviceList->aDisplayInfos = nullptr;

    if ( pDeviceList->numInfos )
    {
        pDeviceList->aDisplayInfos = (StdDisplayInfo*)STDMALLOC(sizeof(StdDisplayInfo) * pDeviceList->numInfos);

        StdDisplayInfo* pCurInfo = pDeviceList->aDisplayInfos;
        for ( size_t deviceNum = 0; deviceNum < pDeviceList->numInfos; ++deviceNum )
        {
            STD_ZEROMEM(pCurInfo, sizeof(StdDisplayInfo));

            if ( stdDisplay_GetDevice(deviceNum, &pCurInfo->displayDevice) )
            {
                STDLOG_ERROR("Error getting stdDisplay device.\n");
                std3D_FreeDisplayEnvironment(pDeviceList);
                return nullptr;
            }

            if ( !stdDisplay_Open(deviceNum) )
            {
                STDLOG_ERROR("Error opening stdDisplay device.\n");
                std3D_FreeDisplayEnvironment(pDeviceList);
                return nullptr;
            }

            pCurInfo->numModes = stdDisplay_GetNumVideoModes();
            pCurInfo->aModes   = nullptr;

            if ( pCurInfo->numModes )
            {
                pCurInfo->aModes = (StdVideoMode*)STDMALLOC(sizeof(StdVideoMode) * pCurInfo->numModes);
                StdVideoMode* pCurMode = pCurInfo->aModes;
                for ( size_t modeNum = 0; modeNum < pCurInfo->numModes; ++modeNum )
                {
                    if ( !stdDisplay_GetVideoMode(modeNum, pCurMode) )
                    {
                        ++pCurMode;
                    }
                }

                pCurInfo->numDevices = 0;
                pCurInfo->aDevices   = nullptr;
                if ( pCurInfo->displayDevice.bHAL )
                {
                    if ( !std3D_Startup() )
                    {
                        STDLOG_ERROR("Error starting std3D system.\n");
                        std3D_FreeDisplayEnvironment(pDeviceList);
                        return nullptr;
                    }

                    pCurInfo->numDevices = std3D_GetNumDevices();
                    if ( pCurInfo->numDevices )
                    {
                        size_t listSize = sizeof(Device3D) * pCurInfo->numDevices;
                        pCurInfo->aDevices = (Device3D*)STDMALLOC(listSize);
                        std::memcpy(pCurInfo->aDevices, std3D_GetAllDevices(), listSize);
                    }

                    std3D_Shutdown();
                }
            }

            stdDisplay_Close();
            ++pCurInfo;
        }
    }

    stdDisplay_Shutdown();
    return pDeviceList;
}

void J3DAPI std3D_FreeDisplayEnvironment(StdDisplayEnvironment* pEnv)
{
    if ( pEnv->aDisplayInfos )
    {
        StdDisplayInfo* pCurDevice = pEnv->aDisplayInfos;
        for ( size_t i = 0; i < pEnv->numInfos; ++i )
        {
            if ( pCurDevice->aDevices )
            {
                stdMemory_Free(pCurDevice->aDevices);
                pCurDevice->aDevices = nullptr;
            }

            if ( pCurDevice->aModes )
            {
                stdMemory_Free(pCurDevice->aModes);
                pCurDevice->aModes = nullptr;
            }

            ++pCurDevice;
        }

        stdMemory_Free(pEnv->aDisplayInfos);
        pEnv->aDisplayInfos = nullptr;
    }

    stdMemory_Free(pEnv);
}
