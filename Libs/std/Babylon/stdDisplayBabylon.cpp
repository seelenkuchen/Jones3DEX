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

tVBuffer stdDisplay_g_frontBuffer;
tVBuffer stdDisplay_g_backBuffer;

namespace
{
    constexpr const char* stdDisplay_kDeviceName = "Jones3D Babylon Display";
    constexpr const char* stdDisplay_kDriverName = "stdDisplayBabylon";
}

namespace stdDisplayBabylonInternal
{
    StdDisplayBabylonState stdDisplay_state;
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

    // Added: M9.23. The frame limit is a presentation setting, read here after
    // Jones.cfg is open; the limiter runs on the real clock and stays off on
    // the headless virtual one.
    stdDisplay_state.frameLimit = stdDisplay_ClampFrameLimit(
        stdConfig_HasStarted() ? stdConfig_GetInt(STDDISPLAY_CFG_FRAMELIMIT, 0) : 0);
    if ( pContext->pClock )
    {
        stdDisplay_state.pFrameLimiter =
            std::make_unique<babylontime::FrameLimiter>(*pContext->pClock);
    }

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
    stdDisplay_ApplyFramePacing();
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

    // Added: M9.23. Stop pacing and leave the device on V-Sync while the
    // display is closed; the next Open applies the configured limit again.
    if ( stdDisplay_state.pFrameLimiter )
    {
        stdDisplay_state.pFrameLimiter->setRate(0);
    }
    if ( stdDisplay_state.pDevice
        && stdDisplay_state.pDevice->presentMode() != render::PresentMode::Vsync )
    {
        static_cast<void>(stdDisplay_state.pDevice->setPresentMode(render::PresentMode::Vsync));
    }

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

    // Added: the scripted tour counts every presented frame, logs it, and asks for the BMP of a
    // requested present after it has been presented.
    stdBabylonTour_OnPresent();
    if ( const char* pScreenshotPath = stdBabylonTour_TakeScreenshotPath() )
    {
        if ( stdDisplay_SaveScreen(pScreenshotPath) != 0 )
        {
            STDLOG_WARNING("stdDisplay: could not write the screenshot '%s'.\n", pScreenshotPath);
        }
    }

    // Added: M9.23 frame limiter. It runs after the present and uses only the
    // wall clock, never game time. Level loading (bNoSync), the virtual clock
    // and a device without a swapchain never wait.
    if ( stdDisplay_state.pFrameLimiter && stdDisplay_state.pDevice
        && stdDisplay_state.pDevice->info().hasSwapchain && !stdDisplay_state.bNoSync
        && !stdBabylon_VirtualClockActive() )
    {
        stdDisplay_state.pFrameLimiter->wait();
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
