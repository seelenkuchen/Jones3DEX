// Shared state, types and helpers of the Babylon display adapter.
// Part of the Babylon column's display adapter; see stdDisplayBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
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

    // Added: the flight recorder's ring of presented frames, and the engine key that turns it off.
    inline constexpr size_t stdDisplay_kFlightRecorderFrames     = 120u;
    inline constexpr uint32_t stdDisplay_kFlightRecorderWidth    = 640u;
    inline constexpr const char* stdDisplay_kFlightRecorderKey   = "engine.debug.flightRecorder";

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

    // Added: the flight recorder. Open reads engine.debug.flightRecorder and frees any ring left
    // from a previous run; Close frees the ring; Capture records the just-presented frame into the
    // ring, creating or resizing it on first use or when the presented size changes.
    void stdDisplay_FlightRecorderOpen();
    void stdDisplay_FlightRecorderClose();
    void stdDisplay_FlightRecorderCapture();

    // The BGRA8 BMP writer shared with stdDisplay_SaveScreen's overlay capture.
    int stdDisplay_WriteBgraBmp(const char* pFilename, const uint8_t* pBgra,
        uint32_t width, uint32_t height);

#ifdef J3D_TEST
    // Test-only: forces the flight recorder on or off until cleared, regardless of the config.
    // The shared std3D test main turns it off so the other display tests keep their present streams.
    void stdDisplay_FlightRecorderSetEnabledForTest(bool bEnabled);
    void stdDisplay_FlightRecorderClearEnabledForTest();
    bool stdDisplay_FlightRecorderIsEnabledForTest();
    size_t stdDisplay_FlightRecorderGetTextureCountForTest();
#endif
}

// Added: writes the flight recorder's ring oldest to newest as frame-0000.bmp and index.txt into
// pDirectory, which is created when missing. False, with a log line, on any failure.
bool stdDisplay_SaveFlightRecorder(const char* pDirectory);
