// Added: portable savegame thumbnail code, shared by every column. It replaces the GDI code of
// Libs/sith/Dss/sithGamesaveWin32.c and its Null stand-in. The frame is read through stdDisplay's
// back-buffer lock, scaled down with a box filter, and kept in memory as the bytes the Win32
// version wrote into its DIB section.
//
// Byte layout (verified against the removed Win32 code, git show 82b8f607^:Libs/sith/Dss/sithGamesaveWin32.c):
// - CreateThumbnail converted the clone to SITHSAVEGAME_THUMBCOLORFORMAT (stdColor_cfRGB888) and
//   created the DIB with a negative biHeight, so the rows are top-down. WriteThumbnail then wrote
//   bmp.bmBits for bmp.bmWidthBytes * bmp.bmHeight bytes unchanged.
// - stdColor_ColorConvertOneRow writes an RGB888 pixel as pDestRow[0] = blue, [1] = green,
//   [2] = red, so a DIB RGB byte is BGR in memory, as BI_RGB stores it.
// - The result is therefore 128 * 96 pixels, top row first, three bytes B, G, R per pixel.
#include <j3dcore/j3d.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>

#include <sith/Devices/sithComm.h>

#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>

#include <stddef.h>
#include <string.h>

// The thumbnail state lives in sithGamesave.c; this file fills in the byte image hBmpThumbnail
// points to. The image is static, so ReleaseThumbnail only clears the state.
extern tVBuffer* sithGamesave_pThumbnailImage;
extern int sithGamesave_bThumbnail;
extern tSysBitmap sithGamesave_hBmpThumbnail;

static uint8_t sithGamesave_aThumbnail[SITHSAVEGAME_THUMBSIZE];
_Static_assert(SITHSAVEGAME_THUMBSIZE == 0x9000, "SITHSAVEGAME_THUMBSIZE must be 0x9000 bytes");

static void sithGamesave_UnlockFrame(tVBuffer* pVBuffer, int bBackBuffer)
{
    if ( bBackBuffer )
    {
        stdDisplay_UnlockBackBuffer();
    }
    else
    {
        stdDisplay_VBufferUnlock(pVBuffer);
    }
}

void J3DAPI sithGamesave_SetThumbnailImage(tVBuffer* pVBuffer)
{
    if ( !pVBuffer )
    {
        return;
    }

    if ( sithGamesave_pThumbnailImage )
    {
        stdDisplay_VBufferFree(sithGamesave_pThumbnailImage);
        sithGamesave_pThumbnailImage = NULL;
    }

    // Altered: The back buffer can live on the GPU and has no pSysSurface or pPixels until it is
    // locked, so ask stdDisplay for the frame instead of reading the VBuffer directly. Other
    // VBuffers (the tests' synthetic frames) use the VBuffer lock, which for a hardware back
    // buffer is the same read-only lock.
    const int bBackBuffer = pVBuffer == &stdDisplay_g_backBuffer;
    const uint8_t* pPixels = NULL;
    uint32_t width         = 0;
    uint32_t height        = 0;
    int32_t pitch          = 0;

    if ( bBackBuffer )
    {
        void* pLocked = NULL;
        if ( stdDisplay_LockBackBufferReadOnly(&pLocked, &width, &height, &pitch) != 0 || !pLocked )
        {
            return;
        }
        pPixels = (const uint8_t*)pLocked;
    }
    else
    {
        if ( !stdDisplay_VBufferLockReadOnly(pVBuffer) )
        {
            return;
        }

        pPixels = pVBuffer->pPixels;
        width   = pVBuffer->rasterInfo.width;
        height  = pVBuffer->rasterInfo.height;
        pitch   = (int32_t)pVBuffer->rasterInfo.rowSize;
    }

    const uint32_t bytesPerPixel = pVBuffer->rasterInfo.colorInfo.bpp / 8u;
    const size_t rowBytes        = (size_t)width * bytesPerPixel;
    int64_t rowPitch             = pitch;
    if ( rowPitch < 0 )
    {
        rowPitch = -rowPitch;
    }

    if ( !pPixels || !width || !height || !bytesPerPixel || rowPitch < (int64_t)rowBytes )
    {
        sithGamesave_UnlockFrame(pVBuffer, bBackBuffer);
        return;
    }

    tRasterInfo rasterInfo;
    memset(&rasterInfo, 0, sizeof(rasterInfo));
    rasterInfo.width     = width;
    rasterInfo.height    = height;
    rasterInfo.rowSize   = rowBytes;
    rasterInfo.rowWidth  = width;
    rasterInfo.size      = rowBytes * height;
    rasterInfo.colorInfo = pVBuffer->rasterInfo.colorInfo;

    sithGamesave_pThumbnailImage = stdDisplay_VBufferNew(&rasterInfo, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
    if ( sithGamesave_pThumbnailImage && sithGamesave_pThumbnailImage->pPixels )
    {
        for ( uint32_t y = 0; y < height; ++y )
        {
            memcpy(
                &sithGamesave_pThumbnailImage->pPixels[sithGamesave_pThumbnailImage->rasterInfo.rowSize * y],
                pPixels + (int64_t)pitch * (int64_t)y,
                rowBytes
            );
        }
    }
    else
    {
        if ( sithGamesave_pThumbnailImage )
        {
            stdDisplay_VBufferFree(sithGamesave_pThumbnailImage);
            sithGamesave_pThumbnailImage = NULL;
        }
    }

    sithGamesave_UnlockFrame(pVBuffer, bBackBuffer);
}

// Added: deterministic box filter. Each destination pixel is the rounded average of the source
// pixels it covers; the ranges use integer arithmetic and always cover at least one pixel, so a
// source smaller than 128 x 96 still produces a full image. The source rows are already RGB888
// (B, G, R) and the destination keeps that order and top-down row order.
static void sithGamesave_BoxFilterRgb888(const uint8_t* pSrc, uint32_t srcWidth, uint32_t srcHeight, size_t srcPitch, uint8_t* pDst)
{
    for ( uint32_t dy = 0; dy < SITHSAVEGAME_THUMB_HEIGHT; ++dy )
    {
        uint32_t y0 = (uint32_t)(((uint64_t)dy * srcHeight) / SITHSAVEGAME_THUMB_HEIGHT);
        uint32_t y1 = (uint32_t)(((uint64_t)(dy + 1) * srcHeight) / SITHSAVEGAME_THUMB_HEIGHT);
        if ( y1 <= y0 )
        {
            y1 = y0 + 1;
        }
        if ( y1 > srcHeight )
        {
            y1 = srcHeight;
        }

        for ( uint32_t dx = 0; dx < SITHSAVEGAME_THUMB_WIDTH; ++dx )
        {
            uint32_t x0 = (uint32_t)(((uint64_t)dx * srcWidth) / SITHSAVEGAME_THUMB_WIDTH);
            uint32_t x1 = (uint32_t)(((uint64_t)(dx + 1) * srcWidth) / SITHSAVEGAME_THUMB_WIDTH);
            if ( x1 <= x0 )
            {
                x1 = x0 + 1;
            }
            if ( x1 > srcWidth )
            {
                x1 = srcWidth;
            }

            uint64_t sumBlue  = 0;
            uint64_t sumGreen = 0;
            uint64_t sumRed   = 0;
            uint64_t count    = 0;

            for ( uint32_t y = y0; y < y1; ++y )
            {
                const uint8_t* pRow = &pSrc[srcPitch * y];
                for ( uint32_t x = x0; x < x1; ++x )
                {
                    const uint8_t* pPixel = &pRow[(size_t)x * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL];
                    sumBlue  += pPixel[0];
                    sumGreen += pPixel[1];
                    sumRed   += pPixel[2];
                    ++count;
                }
            }

            uint8_t* pPixel = &pDst[((size_t)dy * SITHSAVEGAME_THUMB_WIDTH + dx) * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL];
            pPixel[0] = (uint8_t)((sumBlue + count / 2u) / count);
            pPixel[1] = (uint8_t)((sumGreen + count / 2u) / count);
            pPixel[2] = (uint8_t)((sumRed + count / 2u) / count);
        }
    }
}

tSysBitmap sithGamesave_CreateThumbnail(void)
{
    if ( sithGamesave_hBmpThumbnail )
    {
        sithGamesave_bThumbnail = 1;
        return sithGamesave_hBmpThumbnail;
    }

    if ( !sithGamesave_pThumbnailImage )
    {
        sithGamesave_bThumbnail    = 0;
        sithGamesave_hBmpThumbnail = NULL;
        return NULL;
    }

    // Convert the cached clone to the Win32 thumbnail format. The conversion returns the source
    // when it already has that format, otherwise a new buffer, and frees the source it replaces.
    tVBuffer* pImage = stdDisplay_VBufferConvertColorFormat(&SITHSAVEGAME_THUMBCOLORFORMAT, sithGamesave_pThumbnailImage, /*bColorKey=*/0, NULL);
    if ( !pImage )
    {
        stdDisplay_VBufferFree(sithGamesave_pThumbnailImage);
        sithGamesave_pThumbnailImage = NULL;
        sithGamesave_bThumbnail      = 0;
        sithGamesave_hBmpThumbnail   = NULL;
        return NULL;
    }

    // pImage owns the pixels now, whether it is the old clone or a replacement.
    sithGamesave_pThumbnailImage = NULL;

    const int bValid = pImage->pPixels
        && pImage->rasterInfo.width
        && pImage->rasterInfo.height
        && pImage->rasterInfo.colorInfo.bpp == SITHSAVEGAME_THUMBCOLORFORMAT.bpp;

    if ( bValid )
    {
        sithGamesave_BoxFilterRgb888(
            pImage->pPixels,
            pImage->rasterInfo.width,
            pImage->rasterInfo.height,
            pImage->rasterInfo.rowSize,
            sithGamesave_aThumbnail
        );
    }

    stdDisplay_VBufferFree(pImage);

    if ( !bValid )
    {
        sithGamesave_bThumbnail    = 0;
        sithGamesave_hBmpThumbnail = NULL;
        return NULL;
    }

    sithGamesave_bThumbnail    = 1;
    sithGamesave_hBmpThumbnail = (tSysBitmap)sithGamesave_aThumbnail;
    return sithGamesave_hBmpThumbnail;
}

int J3DAPI sithGamesave_WriteThumbnail(tFileHandle fh)
{
    if ( !sithGamesave_bThumbnail )
    {
        // Same "no thumbnail, nothing written" answer as the Win32 version.
        return 0;
    }

    if ( sith_g_pHS->pFileWrite(fh, sithGamesave_aThumbnail, SITHSAVEGAME_THUMBSIZE) != SITHSAVEGAME_THUMBSIZE )
    {
        return 1;
    }

    sithGamesave_bThumbnail = 0;
    return 0;
}

void sithGamesaveSys_ReleaseThumbnail(void)
{
    if ( sithGamesave_hBmpThumbnail )
    {
        sithGamesave_hBmpThumbnail = NULL;
        sithGamesave_bThumbnail    = 0;
    }
}
