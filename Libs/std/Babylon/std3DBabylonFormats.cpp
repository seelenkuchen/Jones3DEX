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
// anisotropy, clamp) and uniforms (viewport, fog, alpha reference), worked out from the render
// state bits at each draw (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms).
//
// std3D_DrawRenderListLit draws like std3D_DrawRenderList, with the lit shaders (Shaders/std3d_lit*.hlsl):
// they compute the color exactly as the default shaders do and write each fragment's interpolated
// view-space normal into the normal target, where the default shaders write a constant. Lit vertices
// (StdVertexLit, 56 bytes) stream into a vertex buffer of their own, made on the first lit draw, so the
// classic draws' device calls stay what they were.
//
// std3D_SetLights keeps the frame's light list (StdLight, 48 bytes each) for the lit draws. A pass
// with lit draws, or the first pass after the list changed, copies it into the light buffer, a
// storage buffer made on first use (std3D_kLightBufferSize: a 16-byte header with the count, then
// up to STD3D_MAXLIGHTS lights as they are), and the pass binds it to fragment storage slot 0 of
// the lit draws. std3d_lit.frag.hlsl reads it for the vertices per-pixel lighting flags (M10.2);
// a Classic vertex adds nothing, so its pixel is unchanged. Without lit draws and std3D_SetLights
// (the litFaces option off) there is no light buffer and the classic draws' device calls stay what
// they were.
//
// A scene is one or more passes on the back buffer. CommandList::updateBuffer can't be recorded
// inside a pass, so a draw streams its vertices and indices into the frame's vertex and index
// buffer right away, at growing offsets like std3DX9.c's buffer path, and the pass that draws them
// is recorded when it ends: at std3D_EndScene and at a std3D_ClearZBuffer inside the scene.
//
// Adapter code: no Fixed/Added markers inside; divergences from std3DX9.c say so where they are.
// Pixel formats and colour layouts of the Babylon std3D backend.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdEffect.h>
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
#include <limits>
#include <span>
#include <vector>

#include "std3DBabylon.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "std3DBabylonInternal.h"

namespace render = babylon::render;
using namespace std3DBabylonInternal;

namespace std3DBabylonInternal
{
    size_t std3D_numTextureFormats = 0;
    StdTextureFormat std3D_aTextureFormats[8];
    bool std3D_bHasRGBTextureFormat = false;
    size_t std3D_RGBTextureFormat     = 0;
    size_t std3D_RGBATextureFormat    = 0;
    size_t std3D_RGBAKeyTextureFormat = 0;
}

namespace
{
    const Std3DBabylonFormatEntry std3D_aFormatEntries[] =
    {
        { &stdColor_cfRGB8888,  render::TextureFormat::Bgra8,    true,  "32-bit RGB (X8R8G8B8)" },
        { &stdColor_cfARGB8888, render::TextureFormat::Bgra8,    false, "32-bit ARGB (A8R8G8B8)" },
        { &stdColor_cfRGB565,   render::TextureFormat::B5G6R5,   false, "16-bit RGB (R5G6B5)" },
        { &stdColor_cfARGB5551, render::TextureFormat::B5G5R5A1, false, "16-bit ARGB (A1R5G5B5)" },
        { &stdColor_cfARGB4444, render::TextureFormat::B4G4R4A4, false, "16-bit ARGB (A4R4G4B4)" },
    };

    void std3D_SetPixelFormat(tSysPixelFormat* pSysFormat, render::TextureFormat textureFormat, bool bOpaque)
    {
        Std3DBabylonPixelFormat format{};
        format.bValid  = 1;
        format.format  = static_cast<uint8_t>(textureFormat);
        format.bOpaque = bOpaque ? 1 : 0;
        STD_ZEROMEM(pSysFormat, sizeof(*pSysFormat));
        std::memcpy(pSysFormat, &format, sizeof(format));
    }
}

namespace std3DBabylonInternal
{
    Std3DBabylonPixelFormat std3D_GetPixelFormat(const tSysPixelFormat& sysFormat)
    {
        Std3DBabylonPixelFormat format{};
        std::memcpy(&format, &sysFormat, sizeof(format));
        return format;
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
