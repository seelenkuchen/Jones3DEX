// The back buffer: the CPU copy, its locks, fills, DCs and clippers.
// Part of the Babylon column's display adapter; see stdDisplayBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
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
    // The back buffer's pixels as Bgra8 for the render device: alpha 255, as X8R8G8B8 has none.
    std::vector<uint8_t> stdDisplay_BackPixelsToBgra()
    {
        const tRasterInfo& raster = stdDisplay_g_backBuffer.rasterInfo;
        std::vector<uint8_t> aBgra((size_t)raster.width * raster.height * 4u);
        const bool bBgra = std::memcmp(&raster.colorInfo, &stdColor_cfRGB8888, sizeof(ColorInfo)) == 0;
        for ( uint32_t y = 0; y < raster.height; ++y )
        {
            const uint8_t* pSrc = &stdDisplay_state.aBackPixels[raster.rowSize * y];
            uint8_t* pDest      = &aBgra[(size_t)raster.width * 4u * y];
            if ( bBgra )
            {
                std::memcpy(pDest, pSrc, (size_t)raster.width * 4u);
            }
            else
            {
                stdColor_ColorConvertOneRow(pDest, &stdColor_cfRGB8888, pSrc, &raster.colorInfo, (int)raster.width, 0, nullptr);
            }

            for ( uint32_t x = 0; x < raster.width; ++x )
            {
                pDest[x * 4u + 3u] = 0xFF;
            }
        }

        return aBgra;
    }

    // Uploads the CPU copy to the GPU back buffer, after the frame recorded so far.
    bool stdDisplay_UploadBackPixels()
    {
        const std::vector<uint8_t> aBgra = stdDisplay_BackPixelsToBgra();
        if ( !stdRenderBabylon_WriteBackBuffer(aBgra.data()) )
        {
            STDLOG_ERROR("stdDisplay: could not upload the back buffer.\n");
            stdDisplay_state.bBackPixelsValid = false;
            return false;
        }

        stdDisplay_state.bBackPixelsValid  = true;
        stdDisplay_state.backPixelsVersion = stdRenderBabylon_GetBackBufferVersion();
        return true;
    }
}

namespace stdDisplayBabylonInternal
{
    // Makes the CPU copy hold the GPU back buffer, reading it back only when the GPU changed it.
    bool stdDisplay_SyncBackPixels()
    {
        if ( stdDisplay_state.bBackPixelsValid && stdDisplay_state.backPixelsVersion == stdRenderBabylon_GetBackBufferVersion() )
        {
            return true;
        }

        const tRasterInfo& raster = stdDisplay_g_backBuffer.rasterInfo;
        std::vector<uint8_t> aBgra((size_t)raster.width * raster.height * 4u);
        uint32_t width  = 0;
        uint32_t height = 0;
        if ( !stdRenderBabylon_ReadBackBuffer(aBgra.data(), &width, &height) || width != raster.width || height != raster.height )
        {
            STDLOG_ERROR("stdDisplay: could not read the %ux%u back buffer back.\n", raster.width, raster.height);
            return false;
        }

        const bool bBgra = std::memcmp(&raster.colorInfo, &stdColor_cfRGB8888, sizeof(ColorInfo)) == 0;
        for ( uint32_t y = 0; y < raster.height; ++y )
        {
            const uint8_t* pSrc = &aBgra[(size_t)raster.width * 4u * y];
            uint8_t* pDest      = &stdDisplay_state.aBackPixels[raster.rowSize * y];
            if ( bBgra )
            {
                std::memcpy(pDest, pSrc, (size_t)raster.width * 4u);
            }
            else
            {
                stdColor_ColorConvertOneRow(pDest, &raster.colorInfo, pSrc, &stdColor_cfRGB8888, (int)raster.width, 0, nullptr);
            }
        }

        stdDisplay_state.bBackPixelsValid  = true;
        stdDisplay_state.backPixelsVersion = stdRenderBabylon_GetBackBufferVersion();
        return true;
    }

    // stdDisplayDX9.c's InitBuffers: the back buffer at the size of the mode, the front buffer
    // wrapping the same presentation buffer, both hardware VBuffers without pixels until locked.
    bool stdDisplay_InitBuffers(StdVideoMode* pDisplayMode)
    {
        stdDisplay_PackRaster(&pDisplayMode->rasterInfo);
        bool bRecreated = false;
        if ( !stdRenderBabylon_SetBackBufferSize(pDisplayMode->rasterInfo.width, pDisplayMode->rasterInfo.height, &bRecreated) )
        {
            STDLOG_ERROR("Error creating the %ux%u back buffer.\n", pDisplayMode->rasterInfo.width, pDisplayMode->rasterInfo.height);
            return false;
        }

        tVBuffer* aBuffers[2] = { &stdDisplay_g_frontBuffer, &stdDisplay_g_backBuffer };
        for ( tVBuffer* pBuffer : aBuffers )
        {
            STD_ZEROMEM(pBuffer, sizeof(*pBuffer));
            pBuffer->type         = VBUFFER_HARDWARE;
            pBuffer->bVideoMemory = 1;
            pBuffer->rasterInfo   = pDisplayMode->rasterInfo;
        }

        stdDisplay_state.aBackPixels.assign(stdDisplay_g_backBuffer.rasterInfo.size, 0u);
        stdDisplay_state.bBackPixelsValid     = false;
        stdDisplay_state.backLockRef          = 0;
        stdDisplay_state.frontLockRef         = 0;
        stdDisplay_state.backDCRef            = 0;
        stdDisplay_state.bBackBufferWriteLock = false;
        return true;
    }

    void stdDisplay_ReleaseBuffers()
    {
        stdRenderBabylon_FreeBackBuffer();
        STD_ZEROMEM(&stdDisplay_g_backBuffer, sizeof(stdDisplay_g_backBuffer));
        STD_ZEROMEM(&stdDisplay_g_frontBuffer, sizeof(stdDisplay_g_frontBuffer));
        stdDisplay_state.aBackPixels.clear();
        stdDisplay_state.aBackPixels.shrink_to_fit();
        stdDisplay_state.bBackPixelsValid     = false;
        stdDisplay_state.backLockRef          = 0;
        stdDisplay_state.frontLockRef         = 0;
        stdDisplay_state.backDCRef            = 0;
        stdDisplay_state.bBackBufferWriteLock = false;
    }

    // stdDisplayDX9.c's ColorFillSurface on the back buffer: 0 on success, 1 on failure, also for
    // a rect of width or height 0. The whole buffer is a clear pass; a part of it goes through the
    // CPU copy (read back when the GPU changed the buffer) and an upload.
    int stdDisplay_ColorFillBackBuffer(uint32_t color, const StdRect* pRect)
    {
        if ( !stdDisplay_state.bModeSet || stdRenderBabylon_GetColorTarget().isNull() )
        {
            return 1;
        }

        if ( pRect && (pRect->right == 0 || pRect->bottom == 0) )
        {
            return 1;
        }

        if ( stdDisplay_state.backLockRef )
        {
            STDLOG_ERROR("stdDisplay: cannot fill the back buffer while it is locked.\n");
            return 1;
        }

        const tRasterInfo& raster = stdDisplay_g_backBuffer.rasterInfo;
        const bool bWhole = !pRect
            || (pRect->left <= 0 && pRect->top <= 0
                && (int64_t)pRect->left + pRect->right >= (int64_t)raster.width
                && (int64_t)pRect->top + pRect->bottom >= (int64_t)raster.height);
        if ( bWhole )
        {
            return stdRenderBabylon_ClearBackBuffer(stdDisplay_DecodeColor(color, raster.colorInfo)) ? 0 : 1;
        }

        if ( !stdDisplay_SyncBackPixels() )
        {
            return 1;
        }

        stdDisplay_FillPixels(stdDisplay_state.aBackPixels.data(), raster, color, pRect);
        return stdDisplay_UploadBackPixels() ? 0 : 1;
    }

    // stdDisplayDX9.c's LockBackBufferInternal: 0 on success. bWrite marks the copy for upload at
    // the last unlock.
    int stdDisplay_LockBackBufferInternal(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch, bool bWrite)
    {
        if ( !stdDisplay_state.bOpen || !stdDisplay_state.bModeSet || stdDisplay_state.aBackPixels.empty() )
        {
            return 1;
        }

        if ( stdDisplay_state.backLockRef > 0 )
        {
            if ( bWrite && !stdDisplay_state.bBackBufferWriteLock )
            {
                STDLOG_ERROR("Cannot upgrade an active read-only back-buffer lock to a write lock.\n");
                return 1;
            }
        }
        else
        {
            if ( !stdDisplay_SyncBackPixels() )
            {
                return 1;
            }

            stdDisplay_state.bBackBufferWriteLock = bWrite;
        }

        ++stdDisplay_state.backLockRef;
        *pWidth    = stdDisplay_g_backBuffer.rasterInfo.width;
        *pHeight   = stdDisplay_g_backBuffer.rasterInfo.height;
        *pPitch    = (int32_t)stdDisplay_g_backBuffer.rasterInfo.rowSize;
        *ppSurface = stdDisplay_state.aBackPixels.data();
        return 0;
    }
}

void J3DAPI stdDisplay_GetBackBufferSize(uint32_t* pWidth, uint32_t* pHeight)
{
    *pWidth  = stdDisplay_g_backBuffer.rasterInfo.width;
    *pHeight = stdDisplay_g_backBuffer.rasterInfo.height;
}

int J3DAPI stdDisplay_BackBufferFill(uint32_t color, const StdRect* lpRect)
{
    return stdDisplay_ColorFillBackBuffer(color, lpRect);
}

tSysDrawContext stdDisplay_GetFrontBufferDC(void)
{
    // No GDI: the reference counts stay consistent for the release, every caller gets no context.
    if ( !stdDisplay_state.bOpen || !stdDisplay_state.bModeSet )
    {
        return nullptr;
    }

    ++stdDisplay_state.frontLockRef;
    return nullptr;
}

void J3DAPI stdDisplay_ReleaseFrontBufferDC(tSysDrawContext hdc)
{
    J3D_UNUSED(hdc);
    if ( stdDisplay_state.frontLockRef )
    {
        --stdDisplay_state.frontLockRef;
    }
}

tSysDrawContext stdDisplay_GetBackBufferDC(void)
{
    if ( !stdDisplay_state.bOpen || !stdDisplay_state.bModeSet )
    {
        return nullptr;
    }

    ++stdDisplay_state.backDCRef;
    return nullptr;
}

void J3DAPI stdDisplay_ReleaseBackBufferDC(tSysDrawContext hdc)
{
    J3D_UNUSED(hdc);
    if ( stdDisplay_state.backDCRef )
    {
        --stdDisplay_state.backDCRef;
    }
}

int stdDisplay_FlipToGDISurface(void)
{
    return 0;
}

int J3DAPI stdDisplay_SetBufferClipper(int bFrontBuffer)
{
    // 0 on success as the header says (stdDisplayDX9.c returned 1 here, stdDisplayNull.c 0).
    J3D_UNUSED(bFrontBuffer);
    return 0;
}

int J3DAPI stdDisplay_RemoveBufferClipper(int bFrontBuffer)
{
    J3D_UNUSED(bFrontBuffer);
    return 0;
}

int J3DAPI stdDisplay_LockBackBuffer(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch)
{
    return stdDisplay_LockBackBufferInternal(ppSurface, pWidth, pHeight, pPitch, true);
}

int stdDisplay_LockBackBufferReadOnly(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch)
{
    return stdDisplay_LockBackBufferInternal(ppSurface, pWidth, pHeight, pPitch, false);
}

void stdDisplay_UnlockBackBuffer(void)
{
    if ( stdDisplay_state.bOpen && stdDisplay_state.bModeSet && stdDisplay_state.backLockRef == 1 )
    {
        if ( stdDisplay_state.bBackBufferWriteLock )
        {
            (void)stdDisplay_UploadBackPixels();
        }

        stdDisplay_state.bBackBufferWriteLock = false;
    }

    if ( stdDisplay_state.backLockRef )
    {
        --stdDisplay_state.backLockRef;
    }
}
