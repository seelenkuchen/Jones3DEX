// Added: the Babylon column's stdDisplay backend on the Babylon render device of the adapter context
// (ADR-0025), replacing std/Null/stdDisplayNull.c in that column. It defines the stdDisplayBackend_*
// functions through stdRenderBackend.h, so the capture forwarders of stdDisplayCapture.c stay in
// front (ADR-0024). The behavior reference is the removed DirectX 9 backend
// (std/Win95/DX9/stdDisplayDX9.c before 82b8f607): the device and video mode lists, SetMode and
// ClearMode, the back buffer and its locks, the VBuffers, the fills, the present and the memory
// answers follow it, with the render device and stdRenderBabylon.cpp's back buffer where it held a
// Direct3D 9 device and its swap chain. Adapter code: no Fixed/Added markers inside; divergences
// from stdDisplayDX9.c say so where they are.
//
// The back buffer lives on the GPU (stdRenderBabylon's color target), where std3D draws. Its CPU
// side is a copy in the video mode's pixel format that LockBackBuffer hands out, as DX9 handed out
// its lockable back buffer:
// - a lock reads the GPU back buffer into the copy only when the GPU changed it since the copy was
//   last read or written (stdRenderBabylon_GetBackBufferVersion), so the intro movie, which locks,
//   writes every pixel, unlocks and presents, never reads back after its first frame, and a lock
//   for the save game thumbnail or a screenshot sees the 3D frame;
// - the last unlock of a write lock uploads the copy, after the frame recorded so far
//   (stdRenderBabylon_WriteBackBuffer), so later draws come on top of the CPU pixels;
// - a read-only lock uploads nothing.
// Fills of the whole back buffer are clear passes; fills of a part go through the copy.
//
// stdDisplay_Update ends the frame: stdRenderBabylon_PresentFrame submits what std3D and the
// display recorded and, on a device with a swapchain, presents the back buffer.
//
// The display opens stdRenderBabylon at stdDisplay_Open and closes it at stdDisplay_Close; std3D
// draws through it while both are open.
// VBuffers and the pixel fills.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/Null/stdPlatformNull.h>

#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/time/FrameLimiter.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "stdBabylon.h"
#include "stdBabylonTour.h"
#include "stdDisplayBabylon.h"
#include "stdRenderBabylon.h"
#include "stdDisplayBabylonInternal.h"

namespace render = babylon::render;
namespace babylontime = babylon::time;

using namespace stdDisplayBabylonInternal;

namespace
{
    void stdDisplay_SetPixels16(uint16_t* pPixels16, uint16_t pixel, size_t size)
    {
        for ( size_t i = 0; i < size; ++i )
        {
            pPixels16[i] = pixel;
        }
    }

    void stdDisplay_SetPixels32(uint32_t* pPixels32, uint32_t pixel, size_t size)
    {
        for ( size_t i = 0; i < size; ++i )
        {
            pPixels32[i] = pixel;
        }
    }
}

namespace stdDisplayBabylonInternal
{
    // A rasterInfo packed like a freshly enumerated mode: rows of width pixels, no padding.
    void stdDisplay_PackRaster(tRasterInfo* pRaster)
    {
        const size_t bytesPerPixel = pRaster->colorInfo.bpp / 8u;
        pRaster->rowWidth = pRaster->width;
        pRaster->rowSize  = pRaster->width * bytesPerPixel;
        pRaster->size     = pRaster->rowSize * pRaster->height;
    }

    // A pixel of the back buffer's format as a clear color.
    render::ClearColor stdDisplay_DecodeColor(uint32_t pixel, const ColorInfo& ci)
    {
        const auto channel = [pixel](uint32_t bits, int32_t shift)
        {
            if ( bits == 0u || bits > 31u || shift < 0 || shift > 31 )
            {
                return 0.0f;
            }

            const uint32_t max = (1u << bits) - 1u;
            return (float)((pixel >> shift) & max) / (float)max;
        };

        return render::ClearColor{ channel(ci.redBPP, ci.redPosShift), channel(ci.greenBPP, ci.greenPosShift),
            channel(ci.blueBPP, ci.bluePosShift), 1.0f };
    }

    // Fills the rect (left, top, width, height, as stdDisplayDX9.c's fills read StdRect) of a
    // pixel buffer, clipped to it. stdDisplayDX9.c wrote past the buffer for a rect outside it.
    void stdDisplay_FillPixels(uint8_t* pPixels, const tRasterInfo& raster, uint32_t color, const StdRect* pRect)
    {
        int64_t left   = 0;
        int64_t top    = 0;
        int64_t right  = raster.width;
        int64_t bottom = raster.height;
        if ( pRect )
        {
            left   = std::max<int64_t>(pRect->left, 0);
            top    = std::max<int64_t>(pRect->top, 0);
            right  = std::min<int64_t>((int64_t)pRect->left + pRect->right, raster.width);
            bottom = std::min<int64_t>((int64_t)pRect->top + pRect->bottom, raster.height);
        }

        if ( left >= right || top >= bottom )
        {
            return;
        }

        const uint32_t bpp = raster.colorInfo.bpp;
        if ( bpp != 8u && bpp != 16u && bpp != 32u )
        {
            // stdDisplayDX9.c ended the program here ("24-bit fill not implemented").
            STDLOG_ERROR("stdDisplay: %u-bit fill not implemented.\n", bpp);
            return;
        }

        const size_t width = (size_t)(right - left);
        for ( int64_t y = top; y < bottom; ++y )
        {
            uint8_t* pRow = &pPixels[raster.rowSize * (size_t)y];
            switch ( bpp )
            {
                case 8:
                    std::memset(&pRow[left], (uint8_t)color, width);
                    break;

                case 16:
                    stdDisplay_SetPixels16(reinterpret_cast<uint16_t*>(&pRow[2 * left]), (uint16_t)color, width);
                    break;

                default:
                    stdDisplay_SetPixels32(reinterpret_cast<uint32_t*>(&pRow[4 * left]), color, width);
                    break;
            }
        }
    }

    // stdDisplayDX9.c's VBufferLockInternal: 1 when locked.
    int stdDisplay_VBufferLockInternal(tVBuffer* pVBuffer, bool bReadOnly)
    {
        STD_ASSERTREL(pVBuffer != NULL);

        if ( pVBuffer->type == VBUFFER_SOFTWARE )
        {
            STD_ASSERT(pVBuffer->pPixels);
            ++pVBuffer->lockRefCount;
            return 1;
        }

        if ( pVBuffer->type != VBUFFER_HARDWARE )
        {
            return 1;
        }

        if ( pVBuffer->lockRefCount )
        {
            if ( pVBuffer == &stdDisplay_g_backBuffer && !bReadOnly && !stdDisplay_state.bBackBufferWriteLock )
            {
                STDLOG_ERROR("Cannot upgrade an active read-only back-buffer lock to a write lock.\n");
                return 0;
            }

            ++pVBuffer->lockRefCount;
            return 1;
        }

        // The only hardware VBuffers are the back buffer and the front buffer wrapping the same
        // presentation buffer (stdDisplayDX9.c without MSAA), so both lock the back buffer.
        if ( pVBuffer != &stdDisplay_g_backBuffer && pVBuffer != &stdDisplay_g_frontBuffer )
        {
            return 0;
        }

        void* pPixels   = nullptr;
        uint32_t width  = 0;
        uint32_t height = 0;
        int32_t pitch   = 0;
        const int lockResult = bReadOnly
            ? stdDisplay_LockBackBufferInternal(&pPixels, &width, &height, &pitch, false)
            : stdDisplay_LockBackBufferInternal(&pPixels, &width, &height, &pitch, true);
        if ( lockResult || !pPixels )
        {
            return 0;
        }

        const size_t rowSize       = pitch < 0 ? (size_t)-pitch : (size_t)pitch;
        const size_t bytesPerPixel = pVBuffer->rasterInfo.colorInfo.bpp / 8u;
        pVBuffer->pPixels             = static_cast<uint8_t*>(pPixels);
        pVBuffer->rasterInfo.width    = width;
        pVBuffer->rasterInfo.height   = height;
        pVBuffer->rasterInfo.rowSize  = rowSize;
        pVBuffer->rasterInfo.rowWidth = bytesPerPixel ? rowSize / bytesPerPixel : 0u;
        pVBuffer->rasterInfo.size     = rowSize * height;
        ++pVBuffer->lockRefCount;
        return 1;
    }
}

tVBuffer* J3DAPI stdDisplay_VBufferNew(const tRasterInfo* pRasterInfo, int bUseVSurface, int bUseVideoMemory)
{
    // stdDisplayDX9.c made a Direct3D surface for bUseVSurface. The render device has no CPU
    // lockable surfaces and no caller asks for one, so every VBuffer takes the software path.
    J3D_UNUSED(bUseVSurface);
    J3D_UNUSED(bUseVideoMemory);

    if ( (pRasterInfo->colorInfo.bpp % 8) != 0 || pRasterInfo->colorInfo.bpp == 0 || pRasterInfo->colorInfo.bpp > 32 )
    {
        STDLOG_ERROR("stdDisplay_VBufferNew: Invalid bpp %u for %u x %u buffer.\n", pRasterInfo->colorInfo.bpp,
            pRasterInfo->width, pRasterInfo->height);
    }

    STD_ASSERTREL((pRasterInfo->colorInfo.bpp % 8) == 0);

    const uint64_t rowSize = (uint64_t)pRasterInfo->width * (pRasterInfo->colorInfo.bpp / 8u);
    const uint64_t size    = rowSize * pRasterInfo->height;
    if ( size > (uint64_t)SIZE_MAX )
    {
        STDLOG_ERROR("stdDisplay_VBufferNew: a %u x %u buffer is too large.\n", pRasterInfo->width, pRasterInfo->height);
        return nullptr;
    }

    tVBuffer* pVBuffer = (tVBuffer*)STDMALLOC(sizeof(tVBuffer));
    if ( !pVBuffer )
    {
        STDLOG_ERROR("Error allocating vbuffer.\n");
        return nullptr;
    }

    STD_ZEROMEM(pVBuffer, sizeof(*pVBuffer));
    pVBuffer->rasterInfo          = *pRasterInfo;
    pVBuffer->rasterInfo.rowSize  = (size_t)rowSize;
    pVBuffer->rasterInfo.rowWidth = pVBuffer->rasterInfo.width;
    pVBuffer->rasterInfo.size     = (size_t)size;

    pVBuffer->type         = VBUFFER_SOFTWARE;
    pVBuffer->bVideoMemory = 0;
    pVBuffer->pPixels      = (uint8_t*)STDMALLOC(pVBuffer->rasterInfo.size);
    if ( !pVBuffer->pPixels )
    {
        stdMemory_Free(pVBuffer);
        return nullptr;
    }

    pVBuffer->lockRefCount = 1; // Set it to 1 as pPixels is set, which is normally done in VBufferLock function.
    return pVBuffer;
}

void J3DAPI stdDisplay_VBufferFree(tVBuffer* pVBuffer)
{
    STD_ASSERTREL(pVBuffer != NULL);

    if ( pVBuffer->type == VBUFFER_SOFTWARE && pVBuffer->pPixels )
    {
        stdMemory_Free(pVBuffer->pPixels);
        pVBuffer->pPixels = nullptr;
    }

    stdMemory_Free(pVBuffer);
}

int J3DAPI stdDisplay_VBufferLock(tVBuffer* pVBuffer)
{
    return stdDisplay_VBufferLockInternal(pVBuffer, false);
}

int stdDisplay_VBufferLockReadOnly(tVBuffer* pVBuffer)
{
    return stdDisplay_VBufferLockInternal(pVBuffer, true);
}

int J3DAPI stdDisplay_VBufferUnlock(tVBuffer* pVBuffer)
{
    STD_ASSERTREL(pVBuffer != NULL);

    if ( pVBuffer->type == VBUFFER_SOFTWARE )
    {
        if ( pVBuffer->lockRefCount )
        {
            --pVBuffer->lockRefCount;
        }
        return 1; // 1 marks still locked
    }

    if ( pVBuffer->type != VBUFFER_HARDWARE )
    {
        return 1;
    }

    if ( pVBuffer->lockRefCount == 0 )
    {
        return 0;
    }

    if ( pVBuffer->lockRefCount > 1 )
    {
        --pVBuffer->lockRefCount;
        return 1;
    }

    stdDisplay_UnlockBackBuffer();
    --pVBuffer->lockRefCount;
    pVBuffer->pPixels = nullptr;
    return 0; // 0 unlocked
}

int J3DAPI stdDisplay_VBufferFill(tVBuffer* pVBuffer, uint32_t dwFillColor, const StdRect* pRect)
{
    STD_ASSERTREL(pVBuffer != NULL);

    // stdDisplayDX9.c's answers: 1 when filled, 0 when a hardware fill failed.
    if ( pVBuffer->type == VBUFFER_HARDWARE )
    {
        if ( pVBuffer != &stdDisplay_g_backBuffer && pVBuffer != &stdDisplay_g_frontBuffer )
        {
            return 0;
        }

        return stdDisplay_ColorFillBackBuffer(dwFillColor, pRect) == 0;
    }

    if ( pVBuffer->type != VBUFFER_SOFTWARE )
    {
        return 1;
    }

    if ( pVBuffer->pPixels )
    {
        stdDisplay_FillPixels(pVBuffer->pPixels, pVBuffer->rasterInfo, dwFillColor, pRect);
    }

    return 1;
}

tVBuffer* J3DAPI stdDisplay_VBufferConvertColorFormat(const ColorInfo* pDesiredColorFormat, tVBuffer* pSrc, int bColorKey, LPDDCOLORKEY pColorKey)
{
    STD_ASSERTREL(pSrc != NULL);

    if ( std::memcmp(pDesiredColorFormat, &pSrc->rasterInfo.colorInfo, sizeof(ColorInfo)) == 0 )
    {
        return pSrc;
    }

    if ( pSrc->rasterInfo.colorInfo.colorMode == STDCOLOR_PAL )
    {
        if ( pDesiredColorFormat->colorMode == STDCOLOR_PAL )
        {
            return pSrc;
        }
        STD_ASSERTREL(pSrc->rasterInfo.colorInfo.colorMode != STDCOLOR_PAL);
    }

    const ColorInfo* pSrcColorFormat = &pSrc->rasterInfo.colorInfo;
    STD_ASSERTREL(pDesiredColorFormat->colorMode != STDCOLOR_PAL);
    STD_ASSERTREL(pSrcColorFormat->redBPP != 0);
    STD_ASSERTREL(pSrcColorFormat->greenBPP != 0);
    STD_ASSERTREL(pSrcColorFormat->blueBPP != 0);
    STD_ASSERTREL(pDesiredColorFormat->redBPP != 0);
    STD_ASSERTREL(pDesiredColorFormat->greenBPP != 0);
    STD_ASSERTREL(pDesiredColorFormat->blueBPP != 0);
    STD_ASSERTREL(pSrcColorFormat->redPosShift != 0 || pSrcColorFormat->greenPosShift != 0 || pSrcColorFormat->bluePosShift != 0);
    STD_ASSERTREL(pDesiredColorFormat->redPosShift != 0 || pDesiredColorFormat->greenPosShift != 0 || pDesiredColorFormat->bluePosShift != 0);

    tVBuffer* pDest;
    if ( pSrc->rasterInfo.colorInfo.bpp == pDesiredColorFormat->bpp )
    {
        pDest = pSrc;
    }
    else
    {
        tRasterInfo rasterInfo;
        std::memcpy(&rasterInfo, &pSrc->rasterInfo, sizeof(rasterInfo));
        std::memcpy(&rasterInfo.colorInfo, pDesiredColorFormat, sizeof(rasterInfo.colorInfo));

        pDest = stdDisplay_VBufferNew(&rasterInfo, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
        if ( !pDest )
        {
            STDLOG_ERROR("Unable to allocate memory for new tVBuffer");
            return nullptr;
        }
    }

    STD_ASSERTREL(pSrc->pPixels != NULL);
    STD_ASSERTREL(pDest->pPixels != NULL);

    stdDisplay_VBufferLock(pSrc);
    stdDisplay_VBufferLock(pDest);

    // One row at a time, in place when the bit depth stays: stdColor_ColorConvertOneRow reads each
    // pixel before writing it.
    for ( size_t row = 0; row < pDest->rasterInfo.height; ++row )
    {
        stdColor_ColorConvertOneRow(
            &pDest->pPixels[pDest->rasterInfo.rowSize * row],
            pDesiredColorFormat,
            &pSrc->pPixels[pSrc->rasterInfo.rowSize * row],
            &pSrc->rasterInfo.colorInfo,
            (int)pDest->rasterInfo.width,
            bColorKey,
            pColorKey
        );
    }

    stdDisplay_VBufferUnlock(pSrc);
    stdDisplay_VBufferUnlock(pDest);

    std::memcpy(&pDest->rasterInfo.colorInfo, pDesiredColorFormat, sizeof(pDest->rasterInfo.colorInfo));

    if ( pDest != pSrc )
    {
        stdDisplay_VBufferFree(pSrc);
    }

    return pDest;
}

uint32_t J3DAPI stdDisplay_EncodeFromRGB565(uint16_t pixel)
{
    // stdDisplayDX9.c read the current mode without checking it; without one the channels go to 0.
    ColorInfo colorInfo;
    if ( stdDisplay_state.pCurVideoMode )
    {
        colorInfo = stdDisplay_state.pCurVideoMode->rasterInfo.colorInfo;
    }
    else
    {
        STD_ZEROMEM(&colorInfo, sizeof(colorInfo));
    }

    uint8_t red = (uint8_t)(((pixel >> 11) & 0x1F) << 3);
    red |= (uint8_t)(red >> 5);

    uint8_t green = (uint8_t)(((pixel >> 5) & 0x3F) << 2);
    green |= (uint8_t)(green >> 6);

    uint8_t blue = (uint8_t)((pixel & 0x1F) << 3);
    blue |= (uint8_t)(blue >> 5);

    return (red >> (colorInfo.redPosShiftRight & 0xFF) << (colorInfo.redPosShift & 0xFF))
        | (green >> (colorInfo.greenPosShiftRight & 0xFF) << (colorInfo.greenPosShift & 0xFF))
        | (blue >> (colorInfo.bluePosShiftRight & 0xFF) << (colorInfo.bluePosShift & 0xFF));
}
