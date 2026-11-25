#pragma once

#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/time/FrameLimiter.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace stdDisplayBabylonInternal
{
    namespace render = babylon::render;
    namespace babylontime = babylon::time;

    // stdDisplayDX9.c's Startup default resolution, and its mode list bounds.
    inline constexpr uint32_t stdDisplay_kDefaultWidth  = 640u;
    inline constexpr uint32_t stdDisplay_kDefaultHeight = 480u;
    inline constexpr size_t stdDisplay_kMaxVideoModes   = 64u;

    struct StdDisplayBabylonState
    {
        bool bStartup    = false;
        bool bOpen       = false;
        bool bModeSet    = false;
        bool bFullscreen = false;
        bool bNoSync     = false;

        // Added: M9.23 frame pacing. frameLimit is STDDISPLAY_CFG_FRAMELIMIT:
        // 0 V-Sync, positive cap, -1 unlimited. bNoSync is the loading fast
        // path; the limiter waits only when both say a wait is wanted.
        int frameLimit = 0;
        std::unique_ptr<babylontime::FrameLimiter> pFrameLimiter;

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

    extern StdDisplayBabylonState stdDisplay_state;

    void stdDisplay_PackRaster(tRasterInfo* pRaster);
    void stdDisplay_FillMode(StdVideoMode* pMode, uint32_t width, uint32_t height, const ColorInfo& colorInfo);
    void stdDisplay_EnumerateVideoModes();
    bool stdDisplay_SyncBackPixels();
    bool stdDisplay_InitBuffers(StdVideoMode* pDisplayMode);
    void stdDisplay_ReleaseBuffers();
    render::ClearColor stdDisplay_DecodeColor(uint32_t pixel, const ColorInfo& ci);
    void stdDisplay_FillPixels(uint8_t* pPixels, const tRasterInfo& raster, uint32_t color, const StdRect* pRect);
    int stdDisplay_ColorFillBackBuffer(uint32_t color, const StdRect* pRect);
    int stdDisplay_LockBackBufferInternal(void** ppSurface, uint32_t* pWidth, uint32_t* pHeight, int32_t* pPitch, bool bWrite);
    int stdDisplay_VBufferLockInternal(tVBuffer* pVBuffer, bool bReadOnly);
    int stdDisplay_ClampFrameLimit(int frameLimit);
    void stdDisplay_ApplyFramePacing();
}
