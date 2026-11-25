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
// Video modes: the mode list, SetMode and ClearMode, and the mode queries.
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
    constexpr uint32_t stdDisplay_kRefreshRate   = 60u;

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
}

namespace stdDisplayBabylonInternal
{
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

                // Added: the display's desktop mode, so a first fullscreen run and the Display tab
                // can pick the size the window actually covers.
                const babylon::platform::Size desktopSize = pContext->pWindow->desktopSize();
                if ( desktopSize.width > 0 && desktopSize.height > 0
                    && (uint32_t)desktopSize.width <= maxSize && (uint32_t)desktopSize.height <= maxSize )
                {
                    stdDisplay_AddVideoMode((uint32_t)desktopSize.width, (uint32_t)desktopSize.height, *pFormat);
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

int J3DAPI stdDisplay_GetDesktopMode(StdVideoMode* pDestMode)
{
    if ( !pDestMode )
    {
        return 1;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        return 1;
    }

    const babylon::platform::Size desktopSize = pContext->pWindow->desktopSize();
    if ( desktopSize.width <= 0 || desktopSize.height <= 0 )
    {
        return 1;
    }

    stdDisplay_FillMode(pDestMode, (uint32_t)desktopSize.width, (uint32_t)desktopSize.height,
        stdColor_cfRGB8888);
    return 0;
}

void J3DAPI stdDisplay_SetDefaultResolution(uint32_t width, uint32_t height)
{
    stdDisplay_state.primaryVideoMode.rasterInfo.width  = width;
    stdDisplay_state.primaryVideoMode.rasterInfo.height = height;
}
