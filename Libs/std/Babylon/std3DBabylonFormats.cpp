// Pixel formats and colour layouts of the Babylon std3D backend.
// Part of the Babylon column's std3D backend; see std3DBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
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
