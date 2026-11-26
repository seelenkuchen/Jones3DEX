// System textures, mip generation and the texture cache of the Babylon std3D backend.
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
    size_t std3D_frameCount            = 1;
    size_t std3D_numCachedTextures     = 0;
    tSystemTexture* std3D_pFirstTexCache = nullptr;
    tSystemTexture* std3D_pLastTexCache  = nullptr;
}

namespace
{
    // One texel of a generated mip in RGBA8, independent of the upload format.
    struct Std3DBabylonTexelRgba8
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

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

    // The upload bytes of one level as RGBA8 texels.
    void std3D_DecodeMipRgba8(const std::vector<std::byte>& data, const Std3DBabylonFormatEntry& entry,
        uint32_t width, uint32_t height, std::vector<Std3DBabylonTexelRgba8>& texels)
    {
        const size_t texelBytes = render::bytesPerTexel(entry.format);
        const uint8_t* pData    = reinterpret_cast<const uint8_t*>(data.data());
        texels.resize((size_t)width * height);
        for ( size_t i = 0; i < texels.size(); ++i )
        {
            uint8_t r = 0, g = 0, b = 0, a = 0xFF;
            stdColor_DecodeRGBA(std3D_ReadPixel(pData + i * texelBytes, texelBytes), entry.pColorInfo,
                &r, &g, &b, &a);
            if ( entry.bOpaque )
            {
                a = 0xFF;
            }

            texels[i] = Std3DBabylonTexelRgba8{ r, g, b, a };
        }
    }

    // RGBA8 texels as the upload bytes of one level, in the layout and bit depths of entry.
    void std3D_EncodeMipRgba8(const std::vector<Std3DBabylonTexelRgba8>& texels,
        const Std3DBabylonFormatEntry& entry, uint32_t width, uint32_t height,
        std::vector<std::byte>& data)
    {
        const size_t texelBytes = render::bytesPerTexel(entry.format);
        data.resize((size_t)width * height * texelBytes);
        uint8_t* pData = reinterpret_cast<uint8_t*>(data.data());
        for ( size_t i = 0; i < texels.size(); ++i )
        {
            const Std3DBabylonTexelRgba8& texel = texels[i];
            const uint8_t alpha = entry.bOpaque ? 0xFF : texel.a;
            uint32_t value = stdColor_EncodeRGBA(entry.pColorInfo, texel.r, texel.g, texel.b, alpha);
            if ( entry.bOpaque )
            {
                value |= 0xFF000000u;
            }

            std3D_WritePixel(pData + i * texelBytes, texelBytes, value);
        }
    }

    // Box-filters src (srcWidth x srcHeight) down to dstWidth x dstHeight. Each destination texel
    // averages the whole source area it covers, so odd sizes lose nothing.
    //
    // Alpha is respected. A texel of a texture with 1-bit alpha (a color-key texture) contributes
    // only when it is opaque, and the result is opaque when any contributor is: the transparent
    // colour cannot bleed into the visible texels. A texture with more than 1 alpha bit is averaged
    // premultiplied and then unpremultiplied, which likewise keeps transparent texels' colour out
    // of the result. A format without alpha averages every texel and stays opaque.
    void std3D_BoxFilterMip(const std::vector<Std3DBabylonTexelRgba8>& src, uint32_t srcWidth,
        uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight,
        const Std3DBabylonFormatEntry& entry, std::vector<Std3DBabylonTexelRgba8>& dst)
    {
        const bool bMaskAlpha   = !entry.bOpaque && entry.pColorInfo->alphaBPP == 1;
        const bool bPremultiply = !entry.bOpaque && entry.pColorInfo->alphaBPP > 1;
        dst.resize((size_t)dstWidth * dstHeight);
        for ( uint32_t dstY = 0; dstY < dstHeight; ++dstY )
        {
            uint32_t srcY0 = (uint32_t)((uint64_t)dstY * srcHeight / dstHeight);
            uint32_t srcY1 = (uint32_t)((uint64_t)(dstY + 1) * srcHeight / dstHeight);
            if ( srcY1 <= srcY0 )
            {
                srcY1 = srcY0 + 1;
            }

            for ( uint32_t dstX = 0; dstX < dstWidth; ++dstX )
            {
                uint32_t srcX0 = (uint32_t)((uint64_t)dstX * srcWidth / dstWidth);
                uint32_t srcX1 = (uint32_t)((uint64_t)(dstX + 1) * srcWidth / dstWidth);
                if ( srcX1 <= srcX0 )
                {
                    srcX1 = srcX0 + 1;
                }

                uint64_t sumR = 0;
                uint64_t sumG = 0;
                uint64_t sumB = 0;
                uint64_t sumA = 0;
                uint64_t count = 0;
                for ( uint32_t srcY = srcY0; srcY < srcY1 && srcY < srcHeight; ++srcY )
                {
                    for ( uint32_t srcX = srcX0; srcX < srcX1 && srcX < srcWidth; ++srcX )
                    {
                        const Std3DBabylonTexelRgba8& texel = src[(size_t)srcY * srcWidth + srcX];
                        if ( bMaskAlpha )
                        {
                            if ( texel.a < 128 )
                            {
                                continue;
                            }

                            sumR += texel.r;
                            sumG += texel.g;
                            sumB += texel.b;
                            ++count;
                        }
                        else if ( bPremultiply )
                        {
                            sumR += (uint64_t)texel.r * texel.a;
                            sumG += (uint64_t)texel.g * texel.a;
                            sumB += (uint64_t)texel.b * texel.a;
                            sumA += texel.a;
                            ++count;
                        }
                        else
                        {
                            sumR += texel.r;
                            sumG += texel.g;
                            sumB += texel.b;
                            sumA += 0xFF;
                            ++count;
                        }
                    }
                }

                Std3DBabylonTexelRgba8& out = dst[(size_t)dstY * dstWidth + dstX];
                out = Std3DBabylonTexelRgba8{ 0, 0, 0, 0 };
                if ( count == 0 )
                {
                    continue;
                }

                if ( bMaskAlpha )
                {
                    out.r = (uint8_t)((sumR + count / 2) / count);
                    out.g = (uint8_t)((sumG + count / 2) / count);
                    out.b = (uint8_t)((sumB + count / 2) / count);
                    out.a = 0xFF;
                }
                else if ( bPremultiply )
                {
                    if ( sumA == 0 )
                    {
                        continue; // fully transparent
                    }

                    // sumR is the sum of r * a; sumA the sum of a. Their ratio is the alpha-weighted
                    // average colour of the covered texels, in 0..255, so the transparent colour
                    // never reaches it.
                    out.r = (uint8_t)std::min<uint64_t>(0xFF, (sumR + sumA / 2) / sumA);
                    out.g = (uint8_t)std::min<uint64_t>(0xFF, (sumG + sumA / 2) / sumA);
                    out.b = (uint8_t)std::min<uint64_t>(0xFF, (sumB + sumA / 2) / sumA);
                    out.a = (uint8_t)((sumA + count / 2) / count);
                }
                else
                {
                    out.r = (uint8_t)((sumR + count / 2) / count);
                    out.g = (uint8_t)((sumG + count / 2) / count);
                    out.b = (uint8_t)((sumB + count / 2) / count);
                    out.a = 0xFF;
                }
            }
        }
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

}

namespace std3DBabylonInternal
{
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
    std::vector<Std3DBabylonTexelRgba8> prevTexels;
    std::vector<Std3DBabylonTexelRgba8> nextTexels;
    uint32_t numSourceLevels = 1;
    bool bGenerateMips = false;

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

    // Added: a material that brings only its top level gets a full chain generated on upload, so
    // minified world and model textures keep their detail instead of shimmering. graphics.mipmapFilter
    // 0 keeps the one level. Materials that bring their own levels are uploaded as they are.
    numSourceLevels = desc.mipLevels;
    bGenerateMips = pCacheTexture->numMipLevels == 1
        && std3D_mipmapFilter != STD3D_MIPMAPFILTER_NONE;
    if ( bGenerateMips )
    {
        desc.mipLevels = render::maxMipLevels(desc.width, desc.height);
    }

    texture = std3D_pRenderDevice->createTexture(desc);
    if ( texture.isNull() )
    {
        STDLOG_ERROR("Error creating %ux%u texture for cache.\n", desc.width, desc.height);
        goto error;
    }

    for ( uint32_t mmNum = 0; mmNum < numSourceLevels; ++mmNum )
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

    if ( bGenerateMips )
    {
        // Each level is box-filtered from the one above in RGBA8, so the alpha handling of
        // std3D_BoxFilterMip applies at every step. The top level's upload bytes are what that
        // starts from.
        std3D_DecodeMipRgba8(data, *pEntry, desc.width, desc.height, prevTexels);
        uint32_t prevWidth  = desc.width;
        uint32_t prevHeight = desc.height;
        for ( uint32_t mmNum = numSourceLevels; mmNum < desc.mipLevels; ++mmNum )
        {
            const uint32_t width  = render::mipDimension(desc.width, mmNum);
            const uint32_t height = render::mipDimension(desc.height, mmNum);
            std3D_BoxFilterMip(prevTexels, prevWidth, prevHeight, width, height, *pEntry, nextTexels);
            std3D_EncodeMipRgba8(nextTexels, *pEntry, width, height, data);
            if ( !std3D_pRenderDevice->uploadTexture(texture, mmNum, std::span<const std::byte>(data)) )
            {
                STDLOG_ERROR("Error uploading generated texture level %u.\n", mmNum);
                goto error;
            }

            prevTexels.swap(nextTexels);
            prevWidth  = width;
            prevHeight = height;
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

