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
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/Null/stdPlatformNull.h>

#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "stdBabylon.h"
#include "stdRenderBabylon.h"

namespace render = babylon::render;

tVBuffer stdDisplay_g_frontBuffer;
tVBuffer stdDisplay_g_backBuffer;

namespace
{
    // stdDisplayDX9.c's Startup default resolution, and its mode list bounds.
    constexpr uint32_t stdDisplay_kDefaultWidth  = 640u;
    constexpr uint32_t stdDisplay_kDefaultHeight = 480u;
    constexpr uint32_t stdDisplay_kRefreshRate   = 60u;
    constexpr size_t stdDisplay_kMaxVideoModes   = 64u;

    constexpr const char* stdDisplay_kDeviceName = "Jones3D Babylon Display";
    constexpr const char* stdDisplay_kDriverName = "stdDisplayBabylon";

    // The sizes of a windowed run's video mode list, each in 16-bit (RGB565) and 32-bit (X8R8G8B8)
    // color: the display modes a Direct3D 9 adapter typically listed, as stdDisplayDX9.c's
    // EnumerateVideoModes took them from D3DFMT_R5G6B5 and D3DFMT_X8R8G8B8. Babylon's platform has
    // no display mode query, so the list is fixed, and the window's size is added.
    //
    // A headless run lists the default 640 x 480 in both formats only: what stdDisplayDX9.c listed
    // on a 640 x 480 display, such as the M6.1 reference recordings' (DX9 under wine on a 640 x 480
    // Xvfb screen), so their fullscreen SetMode of mode 1 replays at the recorded size. Every
    // requested size then resolves to 640 x 480 as with stdDisplayNull.c's single mode, so the back
    // buffer, the canvas and the camera of the headless run (and its traces) stay as recorded.
    struct StdDisplayModeSize
    {
        uint32_t width;
        uint32_t height;
    };

    constexpr StdDisplayModeSize stdDisplay_aModeSizes[] = {
        { 320u, 200u }, { 320u, 240u }, { 400u, 300u }, { 512u, 384u }, { 640u, 400u }, { 640u, 480u },
        { 800u, 600u }, { 1024u, 768u }, { 1152u, 864u }, { 1280u, 720u }, { 1280u, 960u }, { 1280u, 1024u },
        { 1600u, 900u }, { 1600u, 1200u }, { 1920u, 1080u }, { 1920u, 1200u },
    };

    struct StdDisplayBabylonState
    {
        bool bStartup    = false;
        bool bOpen       = false;
        bool bModeSet    = false;
        bool bFullscreen = false;
        bool bNoSync     = false;

        render::Device* pDevice = nullptr; // the context's, from Startup to Shutdown
        bool bWindowed          = false;   // the context has a game window

        size_t numDevices = 0;
        size_t curDevice  = 0;
        StdDisplayDevice aDevices[1]{};

        size_t numVideoModes = 0;
        StdVideoMode aVideoModes[stdDisplay_kMaxVideoModes]{};
        StdVideoMode primaryVideoMode{};
        StdVideoMode fullscreenVideoMode{};
        StdVideoMode* pCurVideoMode = nullptr;

        // The CPU copy of the back buffer in the video mode's format, rows packed, top first, and
        // the back buffer version it holds (valid only with bBackPixelsValid).
        std::vector<uint8_t> aBackPixels;
        bool bBackPixelsValid      = false;
        uint64_t backPixelsVersion = 0;

        size_t backLockRef         = 0;
        size_t frontLockRef        = 0;
        size_t backDCRef           = 0;
        bool bBackBufferWriteLock  = false;

        tDisplayDevicePreResetCallback pfPreResetCallback   = nullptr;
        tDisplayDevicePostResetCallback pfPostResetCallback = nullptr;
        tDisplayDeviceReleaseCallback pfReleaseCallback     = nullptr;
    };

    StdDisplayBabylonState stdDisplay_state;

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

    // A rasterInfo packed like a freshly enumerated mode: rows of width pixels, no padding.
    void stdDisplay_PackRaster(tRasterInfo* pRaster)
    {
        const size_t bytesPerPixel = pRaster->colorInfo.bpp / 8u;
        pRaster->rowWidth = pRaster->width;
        pRaster->rowSize  = pRaster->width * bytesPerPixel;
        pRaster->size     = pRaster->rowSize * pRaster->height;
    }

    // stdDisplayDX9.c's SetAspectRatio for a windowed run: the pixel aspect, 0.75 for the
    // non-square 320x200, 320x400 and 640x400 modes and 1 otherwise. A headless run keeps what
    // stdDisplayNull.c answers, width / height: the camera frustum follows it and decides which
    // sectors are seen, which reaches the simulation, so the headless traces stay as recorded.
    void stdDisplay_SetAspectRatio(StdVideoMode* pMode)
    {
        const uint32_t width  = pMode->rasterInfo.width;
        const uint32_t height = pMode->rasterInfo.height;
        if ( !stdDisplay_state.bWindowed )
        {
            pMode->aspectRatio = height ? (float)width / (float)height : 1.0f;
        }
        else if ( (width == 320u && height == 200u) || (width == 320u && height == 400u) || (width == 640u && height == 400u) )
        {
            pMode->aspectRatio = 0.75f;
        }
        else
        {
            pMode->aspectRatio = 1.0f;
        }
    }

    void stdDisplay_FillMode(StdVideoMode* pMode, uint32_t width, uint32_t height, const ColorInfo& colorInfo)
    {
        STD_ZEROMEM(pMode, sizeof(*pMode));
        pMode->refreshRate          = stdDisplay_kRefreshRate;
        pMode->rasterInfo.width     = width;
        pMode->rasterInfo.height    = height;
        pMode->rasterInfo.colorInfo = colorInfo;
        stdDisplay_PackRaster(&pMode->rasterInfo);
        stdDisplay_SetAspectRatio(pMode);
    }

    // stdDisplayDX9.c's VideoModeCompare: by bit depth, then width, then height.
    bool stdDisplay_VideoModeLess(const StdVideoMode& a, const StdVideoMode& b)
    {
        if ( a.rasterInfo.colorInfo.bpp != b.rasterInfo.colorInfo.bpp )
        {
            return a.rasterInfo.colorInfo.bpp < b.rasterInfo.colorInfo.bpp;
        }

        if ( a.rasterInfo.width != b.rasterInfo.width )
        {
            return a.rasterInfo.width < b.rasterInfo.width;
        }

        return a.rasterInfo.height < b.rasterInfo.height;
    }

    void stdDisplay_AddVideoMode(uint32_t width, uint32_t height, const ColorInfo& colorInfo)
    {
        if ( stdDisplay_state.numVideoModes >= STD_ARRAYLEN(stdDisplay_state.aVideoModes) )
        {
            return;
        }

        for ( size_t i = 0; i < stdDisplay_state.numVideoModes; ++i )
        {
            const tRasterInfo& raster = stdDisplay_state.aVideoModes[i].rasterInfo;
            if ( raster.width == width && raster.height == height && raster.colorInfo.bpp == colorInfo.bpp )
            {
                return;
            }
        }

        stdDisplay_FillMode(&stdDisplay_state.aVideoModes[stdDisplay_state.numVideoModes++], width, height, colorInfo);
    }

    // stdDisplayDX9.c's EnumerateVideoModes and the qsort after it in Open: every size of the list
    // (and the window's) the device can hold as a target, in both formats, sorted.
    void stdDisplay_EnumerateVideoModes()
    {
        stdDisplay_state.numVideoModes = 0;

        const StdDisplayModeSize aHeadlessSizes[] = { { stdDisplay_kDefaultWidth, stdDisplay_kDefaultHeight } };
        const std::span<const StdDisplayModeSize> aSizes = stdDisplay_state.bWindowed
            ? std::span<const StdDisplayModeSize>(stdDisplay_aModeSizes)
            : std::span<const StdDisplayModeSize>(aHeadlessSizes);

        const uint32_t maxSize = stdDisplay_state.pDevice->info().limits.maxTextureDimension;
        const ColorInfo* aFormats[] = { &stdColor_cfRGB565, &stdColor_cfRGB8888 };
        for ( const ColorInfo* pFormat : aFormats )
        {
            for ( const StdDisplayModeSize& size : aSizes )
            {
                if ( size.width <= maxSize && size.height <= maxSize )
                {
                    stdDisplay_AddVideoMode(size.width, size.height, *pFormat);
                }
            }

            StdBabylonContext* pContext = stdBabylon_GetContext();
            if ( stdDisplay_state.bWindowed && pContext && pContext->pWindow )
            {
                const babylon::platform::Size windowSize = pContext->pWindow->size();
                if ( windowSize.width > 0 && windowSize.height > 0
                    && (uint32_t)windowSize.width <= maxSize && (uint32_t)windowSize.height <= maxSize )
                {
                    stdDisplay_AddVideoMode((uint32_t)windowSize.width, (uint32_t)windowSize.height, *pFormat);
                }
            }
        }

        std::sort(stdDisplay_state.aVideoModes, stdDisplay_state.aVideoModes + stdDisplay_state.numVideoModes,
            stdDisplay_VideoModeLess);

        for ( size_t i = 0; i < stdDisplay_state.numVideoModes; ++i )
        {
            const StdVideoMode& mode = stdDisplay_state.aVideoModes[i];
            STDLOG_STATUS("Video Mode: %ux%u %u bit (%u Hz), Required: %zu bytes.\n", mode.rasterInfo.width,
                mode.rasterInfo.height, mode.rasterInfo.colorInfo.bpp, mode.refreshRate, 3u * mode.rasterInfo.size);
        }
    }

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

    // A 24-bit BMP of the back buffer, as std/Win95/stdBmpWin32.c's WriteVBuffer wrote it through
    // the host file services (before 82b8f607): bottom row first, rows padded to 4 bytes, 2834
    // pixels per meter. 0 on success, 1 on failure. The headers are written field by field.
    int stdDisplay_WriteBackBufferBmp(const char* pFilename)
    {
        tVBuffer* pVBuffer = &stdDisplay_g_backBuffer;
        if ( pVBuffer->rasterInfo.colorInfo.colorMode == STDCOLOR_PAL )
        {
            return 1;
        }

        const ColorInfo ciBmp = stdColor_cfRGB888;
        const uint64_t width  = pVBuffer->rasterInfo.width;
        const uint64_t height = pVBuffer->rasterInfo.height;
        if ( !width || !height || width > INT32_MAX || height > INT32_MAX )
        {
            return 1;
        }

        const uint64_t stride    = (width * 3u + 3u) & ~uint64_t(3u);
        const uint64_t imageSize = stride * height;
        if ( imageSize > UINT32_MAX - 54u )
        {
            return 1;
        }

        uint8_t aHeader[54] = {};
        const auto putU16 = [&aHeader](size_t at, uint32_t value)
        {
            aHeader[at]      = (uint8_t)value;
            aHeader[at + 1u] = (uint8_t)(value >> 8);
        };
        const auto putU32 = [&aHeader](size_t at, uint32_t value)
        {
            for ( size_t i = 0; i < 4u; ++i )
            {
                aHeader[at + i] = (uint8_t)(value >> (8u * i));
            }
        };
        putU16(0u, 0x4D42u);                            // bfType "BM"
        putU32(2u, (uint32_t)(54u + imageSize));        // bfSize
        putU32(10u, 54u);                               // bfOffBits
        putU32(14u, 40u);                               // biSize
        putU32(18u, (uint32_t)width);                   // biWidth
        putU32(22u, (uint32_t)height);                  // biHeight, positive: bottom up
        putU16(26u, 1u);                                // biPlanes
        putU16(28u, ciBmp.bpp);                         // biBitCount
        putU32(34u, (uint32_t)imageSize);               // biSizeImage (biCompression 0 = BI_RGB)
        putU32(38u, 2834u);                             // biXPelsPerMeter
        putU32(42u, 2834u);                             // biYPelsPerMeter

        tFileHandle fh = std_g_pHS->pFileOpen(pFilename, "wb");
        if ( !fh )
        {
            STDLOG_ERROR("Unable to open file '%s' for writing.\n", pFilename);
            return 1;
        }

        if ( std_g_pHS->pFileWrite(fh, aHeader, sizeof(aHeader)) != sizeof(aHeader) )
        {
            STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", sizeof(aHeader), pFilename);
            std_g_pHS->pFileClose(fh);
            return 1;
        }

        if ( !stdDisplay_VBufferLockReadOnly(pVBuffer) )
        {
            std_g_pHS->pFileClose(fh);
            return 1;
        }

        std::vector<uint8_t> aRow((size_t)stride, 0u);
        int result = 0;
        for ( uint64_t row = height; row-- > 0u; )
        {
            stdColor_ColorConvertOneRow(aRow.data(), &ciBmp, &pVBuffer->pPixels[pVBuffer->rasterInfo.rowSize * row],
                &pVBuffer->rasterInfo.colorInfo, (int)width, 0, nullptr);
            if ( std_g_pHS->pFileWrite(fh, aRow.data(), aRow.size()) != aRow.size() )
            {
                STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", aRow.size(), pFilename);
                result = 1;
                break;
            }
        }

        stdDisplay_VBufferUnlock(pVBuffer);
        std_g_pHS->pFileClose(fh);
        return result;
    }
}

void stdDisplay_ResetGlobals(void)
{
    STD_ZEROMEM(&stdDisplay_g_frontBuffer, sizeof(stdDisplay_g_frontBuffer));
    STD_ZEROMEM(&stdDisplay_g_backBuffer, sizeof(stdDisplay_g_backBuffer));
    stdDisplay_state.bBackBufferWriteLock = false;
}

int stdDisplay_Startup(void)
{
    STDLOG_STATUS("Starting display system using the Babylon render device ...\n");
    if ( stdDisplay_state.bStartup )
    {
        return 1;
    }

    STD_ZEROMEM(&stdDisplay_g_frontBuffer, sizeof(stdDisplay_g_frontBuffer));
    STD_ZEROMEM(&stdDisplay_g_backBuffer, sizeof(stdDisplay_g_backBuffer));
    stdDisplay_state = StdDisplayBabylonState{};

    // stdDisplayDX9.c's Direct3DCreate9: without the render device there is no display system.
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pRenderDevice )
    {
        STDLOG_ERROR("No render device in the Babylon context.\n");
        return 0;
    }

    stdDisplay_state.pDevice   = pContext->pRenderDevice;
    stdDisplay_state.bWindowed = pContext->pWindow != nullptr;

    // stdDisplayDX9.c's EnumerateDevices: one adapter, the render device, a HAL device that renders
    // windowed, its memory worked out from the largest texture like the DX9 caps were.
    const render::DeviceInfo& info = stdDisplay_state.pDevice->info();
    const size_t maxTexture        = info.limits.maxTextureDimension;
    StdDisplayDevice* pDevice      = &stdDisplay_state.aDevices[0];
    // stdUtil_Format, not STD_STRCPY: the CRT shim behind stdUtil_StringCopy has no C linkage for C++.
    stdUtil_Format(pDevice->aDeviceName, STD_ARRAYLEN(pDevice->aDeviceName), "%s", stdDisplay_kDeviceName);
    stdUtil_Format(pDevice->aDriverName, STD_ARRAYLEN(pDevice->aDriverName), "%s", stdDisplay_kDriverName);
    pDevice->bHAL                      = 1;
    pDevice->bWindowRenderNotSupported = 0;
    pDevice->totalVideoMemory          = maxTexture * maxTexture * 4u;
    pDevice->freeVideoMemory           = pDevice->totalVideoMemory / 2u;
    stdDisplay_state.numDevices        = 1;
    STDLOG_STATUS("Found HAL Babylon render device: %s [%s]\n", info.backend.c_str(), pDevice->aDriverName);

    // stdDisplayDX9.c's default resolution, in the desktop format X8R8G8B8. As stdDisplayNull.c
    // does, the back buffer already has that size before a mode is set (without pixels), so
    // stdDisplay_GetBackBufferSize answers the same before stdDisplay_SetMode in the headless run.
    stdDisplay_FillMode(&stdDisplay_state.primaryVideoMode, stdDisplay_kDefaultWidth, stdDisplay_kDefaultHeight, stdColor_cfRGB8888);
    stdDisplay_state.primaryVideoMode.refreshRate = 0;
    stdDisplay_g_backBuffer.type       = VBUFFER_HARDWARE;
    stdDisplay_g_backBuffer.rasterInfo = stdDisplay_state.primaryVideoMode.rasterInfo;
    stdDisplay_g_frontBuffer           = stdDisplay_g_backBuffer;

    stdDisplay_state.bStartup = true;
    STDLOG_STATUS("Found %zu Display Devices.\n", stdDisplay_state.numDevices);
    return 1;
}

void stdDisplay_Shutdown(void)
{
    if ( stdDisplay_state.bOpen )
    {
        stdDisplay_Close();
    }

    STD_ZEROMEM(&stdDisplay_g_frontBuffer, sizeof(stdDisplay_g_frontBuffer));
    STD_ZEROMEM(&stdDisplay_g_backBuffer, sizeof(stdDisplay_g_backBuffer));
    stdDisplay_state = StdDisplayBabylonState{};
}

int J3DAPI stdDisplay_Open(size_t deviceNum)
{
    STD_ASSERTREL(stdDisplay_state.bStartup == true);
    if ( stdDisplay_state.bOpen )
    {
        STDLOG_ERROR("Warning: System already open!\n");
        stdDisplay_Close();
    }

    if ( deviceNum >= stdDisplay_state.numDevices )
    {
        STDLOG_ERROR("Error: Invalid device num %zu (total: %zu)!\n", deviceNum, stdDisplay_state.numDevices);
        return 0;
    }

    // stdDisplayDX9.c's InitDirect3D9: here the frame owner on the render device.
    if ( !stdRenderBabylon_IsOpen() && !stdRenderBabylon_Open(stdDisplay_state.pDevice) )
    {
        return 0;
    }

    if ( stdRenderBabylon_GetDevice() != stdDisplay_state.pDevice )
    {
        STDLOG_ERROR("Error: the render frame owner is open on another device!\n");
        return 0;
    }

    stdDisplay_state.curDevice = deviceNum;
    stdDisplay_EnumerateVideoModes();
    stdDisplay_state.bOpen = true;
    return 1;
}

bool stdDisplay_IsOpen(void)
{
    return stdDisplay_state.bOpen;
}

void stdDisplay_Close(void)
{
    if ( !stdDisplay_state.bStartup )
    {
        STDLOG_ERROR("stdDisplay_Close when not started.\n");
        return;
    }

    if ( !stdDisplay_state.bOpen )
    {
        STDLOG_ERROR("Warning: System already closed!\n");
        return;
    }

    if ( stdDisplay_state.bModeSet )
    {
        stdDisplay_ClearMode();
    }

    stdDisplay_state.numVideoModes = 0;

    stdDisplay_state.pfPreResetCallback  = nullptr;
    stdDisplay_state.pfPostResetCallback = nullptr;
    stdDisplay_state.pfReleaseCallback   = nullptr;

    stdRenderBabylon_Close();

    stdDisplay_state.curDevice = 0;
    stdDisplay_state.bOpen     = false;
}

size_t stdDisplay_GetAdapterNum(size_t deviceNum)
{
    return deviceNum < stdDisplay_state.numDevices ? deviceNum : 0u;
}

tSysDisplayDevice* stdDisplay_GetSystemDevice(void)
{
    // stdDisplayDX9.c returned its Direct3D 9 device; the render device is no tSysDisplayDevice.
    return nullptr;
}

int J3DAPI stdDisplay_CreateZBuffer(const tSysPixelFormat* pPixelFormat, int bSystemMemory)
{
    J3D_UNUSED(pPixelFormat);
    J3D_UNUSED(bSystemMemory);

    // The depth target comes with the back buffer (stdRenderBabylon_SetBackBufferSize); 0 like a
    // successful stdDisplayDX9.c CreateZBuffer, 1 without a mode, as it answered without a device.
    if ( !stdDisplay_state.bOpen || !stdDisplay_state.bModeSet )
    {
        STDLOG_ERROR("Error creating zBuffer, system is closed!\n");
        return 1;
    }

    return 0;
}

void stdDisplay_DisableVSync(bool bDisable)
{
    // stdDisplayDX9.c reset the device with the other present interval; the render interface
    // presents at the device's own pace, so the setting is only remembered.
    stdDisplay_state.bNoSync = bDisable;
}

int stdDisplay_Update(void)
{
    // A present takes 1 ms of virtual time, as stdDisplayNull.c's does, so loops that wait on the
    // clock around it finish in the headless run. No-op on the real clock.
    stdPlatformNull_AdvanceClock(1);

    if ( !stdDisplay_state.bModeSet )
    {
        return 1;
    }

    if ( !stdRenderBabylon_PresentFrame() )
    {
        STDLOG_ERROR("Error when presenting the frame.\n");
        return 1;
    }

    return 0;
}

void J3DAPI stdDisplay_Refresh(int bReload)
{
    // stdDisplayDX9.c reset a lost device here. A render device is never lost, so there is nothing
    // to reload.
    J3D_UNUSED(bReload);
}

void stdDisplay_RegisterDevicePreResetCallback(tDisplayDevicePreResetCallback pCallback)
{
    stdDisplay_state.pfPreResetCallback = pCallback;
}

void stdDisplay_RegisterDevicePostResetCallback(tDisplayDevicePostResetCallback pCallback)
{
    stdDisplay_state.pfPostResetCallback = pCallback;
}

void stdDisplay_RegisterDeviceReleaseCallback(tDisplayDeviceReleaseCallback pCallback)
{
    stdDisplay_state.pfReleaseCallback = pCallback;
}

size_t stdDisplay_GetNumDevices(void)
{
    return stdDisplay_state.numDevices;
}

int J3DAPI stdDisplay_GetDevice(size_t deviceNum, StdDisplayDevice* pDest)
{
    if ( deviceNum >= stdDisplay_state.numDevices )
    {
        return 1;
    }

    *pDest = stdDisplay_state.aDevices[deviceNum];
    return 0;
}

int J3DAPI stdDisplay_GetCurrentDevice(StdDisplayDevice* pDevice)
{
    if ( stdDisplay_state.numDevices == 0 )
    {
        return 1;
    }

    *pDevice = stdDisplay_state.aDevices[stdDisplay_state.curDevice];
    return 0;
}

const StdDisplayDevice* stdDisplay_GetAllDevices(void)
{
    return stdDisplay_state.aDevices;
}

int J3DAPI stdDisplay_SetMode(size_t modeNum, int bFullscreen, size_t numBackBuffers)
{
    // The render device has one back buffer and presents it; numBackBuffers sized the DX9 swap chain.
    J3D_UNUSED(numBackBuffers);

    if ( bFullscreen && modeNum >= stdDisplay_state.numVideoModes )
    {
        return 1;
    }

    if ( stdDisplay_state.bModeSet )
    {
        stdDisplay_ClearMode();
    }

    if ( !stdDisplay_state.bOpen )
    {
        return 1;
    }

    // Fullscreen takes a copy of the enumerated mode, as stdDisplayDX9.c's SetMode fix does. The
    // window is neither switched to fullscreen nor resized here: the game sizes it to the back
    // buffer after the mode is set (JonesDisplay_Startup), and the present scales the back buffer
    // to the swapchain.
    if ( bFullscreen )
    {
        stdDisplay_state.fullscreenVideoMode = stdDisplay_state.aVideoModes[modeNum];
        stdDisplay_state.pCurVideoMode       = &stdDisplay_state.fullscreenVideoMode;
    }
    else
    {
        stdDisplay_state.pCurVideoMode = &stdDisplay_state.primaryVideoMode;
        stdDisplay_state.primaryVideoMode.rasterInfo.colorInfo = stdColor_cfRGB8888;
        stdDisplay_SetAspectRatio(&stdDisplay_state.primaryVideoMode);
    }

    if ( !stdDisplay_InitBuffers(stdDisplay_state.pCurVideoMode) )
    {
        stdDisplay_ReleaseBuffers();
        stdDisplay_state.pCurVideoMode = nullptr;
        return 1;
    }

    STDLOG_STATUS("Set video mode %u %u %u.\n", stdDisplay_state.pCurVideoMode->rasterInfo.width,
        stdDisplay_state.pCurVideoMode->rasterInfo.height, stdDisplay_state.pCurVideoMode->rasterInfo.colorInfo.bpp);

    stdDisplay_state.bModeSet    = true;
    stdDisplay_state.bFullscreen = bFullscreen != 0;

    stdDisplay_VBufferFill(&stdDisplay_g_backBuffer, 0, nullptr);
    stdDisplay_Update();

    if ( bFullscreen )
    {
        stdDisplay_VBufferFill(&stdDisplay_g_backBuffer, 0, nullptr);
    }

    return 0;
}

void stdDisplay_ClearMode(void)
{
    if ( stdDisplay_state.bModeSet )
    {
        // stdDisplayDX9.c's ReleaseDevice: the release callback, then the buffers go. A frame
        // still being recorded is dropped with the back buffer it draws to.
        if ( stdDisplay_state.pfReleaseCallback )
        {
            stdDisplay_state.pfReleaseCallback(nullptr);
        }

        stdDisplay_ReleaseBuffers();
    }

    stdDisplay_state.pCurVideoMode        = nullptr;
    stdDisplay_state.bModeSet             = false;
    stdDisplay_state.bFullscreen          = false;
    stdDisplay_state.bBackBufferWriteLock = false;
}

size_t stdDisplay_GetNumVideoModes(void)
{
    return stdDisplay_state.numVideoModes;
}

int J3DAPI stdDisplay_GetVideoMode(size_t modeNum, StdVideoMode* pDestMode)
{
    if ( modeNum >= stdDisplay_state.numVideoModes )
    {
        return 1;
    }

    std::memcpy(pDestMode, &stdDisplay_state.aVideoModes[modeNum], sizeof(StdVideoMode));
    return 0;
}

int J3DAPI stdDisplay_GetCurrentVideoMode(StdVideoMode* pDisplayMode)
{
    if ( !stdDisplay_state.pCurVideoMode )
    {
        return 1;
    }

    std::memcpy(pDisplayMode, stdDisplay_state.pCurVideoMode, sizeof(StdVideoMode));
    return 0;
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

int J3DAPI stdDisplay_GetTextureMemory(size_t* pTotal, size_t* pFree)
{
    // stdDisplayDX9.c: 1 without a device; the total approximated from the largest texture.
    if ( !stdDisplay_state.bStartup || !stdDisplay_state.pDevice )
    {
        return 1;
    }

    *pTotal = stdDisplay_state.aDevices[0].totalVideoMemory;
    *pFree  = stdDisplay_state.aDevices[0].freeVideoMemory;
    return 0;
}

int J3DAPI stdDisplay_GetTotalMemory(size_t* pTotal, size_t* pFree)
{
    // stdDisplayDX9.c's approximation: 1 MB per X8R8G8B8 display mode (here those listed at Open),
    // half of it free.
    if ( !stdDisplay_state.bStartup )
    {
        return 1;
    }

    size_t numModes = 0;
    for ( size_t i = 0; i < stdDisplay_state.numVideoModes; ++i )
    {
        if ( stdDisplay_state.aVideoModes[i].rasterInfo.colorInfo.bpp == 32u )
        {
            ++numModes;
        }
    }

    *pTotal = numModes * 1024u * 1024u;
    *pFree  = *pTotal / 2u;
    return 0;
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

int J3DAPI stdDisplay_SaveScreen(const char* pFilename)
{
    if ( !pFilename || !stdDisplay_state.bModeSet || stdRenderBabylon_GetColorTarget().isNull() )
    {
        return 1;
    }

    return stdDisplay_WriteBackBufferBmp(pFilename);
}

void J3DAPI stdDisplay_SetDefaultResolution(uint32_t width, uint32_t height)
{
    stdDisplay_state.primaryVideoMode.rasterInfo.width  = width;
    stdDisplay_state.primaryVideoMode.rasterInfo.height = height;
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

int stdDisplay_CanRenderWindowed(void)
{
    // stdDisplayDX9.c: -1 without Direct3D, else whether the adapter renders windowed. Only a run
    // with a game window has one to render into.
    if ( !stdDisplay_state.bStartup )
    {
        return -1;
    }

    return stdDisplay_state.bWindowed ? 1 : 0;
}

int stdDisplay_IsFullscreen(void)
{
    return stdDisplay_state.bFullscreen;
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
