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
// Frame pacing: the frame limit, V-Sync and the limiter.
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
    const char* stdDisplay_PresentModeName(render::PresentMode mode)
    {
        switch ( mode )
        {
            case render::PresentMode::Vsync: return "vsync";
            case render::PresentMode::Immediate: return "immediate";
            case render::PresentMode::Mailbox: return "mailbox";
        }
        return "unknown";
    }
}

namespace stdDisplayBabylonInternal
{
    // Added: M9.23. The rates the Display row offers; any other cfg value
    // falls back to V-Sync.
    int stdDisplay_ClampFrameLimit(int frameLimit)
    {
        switch ( frameLimit )
        {
            case 0:
            case -1:
            case 30:
            case 60:
            case 120:
            case 144:
            case 240:
                return frameLimit;
            default:
                return 0;
        }
    }

    // Added: M9.23. Applies frameLimit and the loading flag to the device and
    // the limiter. A numeric cap turns V-Sync off (immediate present); V-Sync
    // is the only mode used when the limit is 0 and loading is done.
    void stdDisplay_ApplyFramePacing()
    {
        if ( !stdDisplay_state.pDevice )
        {
            return;
        }

        const bool bVsync = stdDisplay_state.frameLimit == 0 && !stdDisplay_state.bNoSync;
        const render::PresentMode wanted = bVsync ? render::PresentMode::Vsync
            : render::PresentMode::Immediate;
        if ( stdDisplay_state.pDevice->presentMode() != wanted )
        {
            // A mode the device lacks falls back to V-Sync with its own log line.
            static_cast<void>(stdDisplay_state.pDevice->setPresentMode(wanted));
        }

        if ( stdDisplay_state.pFrameLimiter )
        {
            const bool bWait = stdDisplay_state.frameLimit > 0 && !stdDisplay_state.bNoSync;
            stdDisplay_state.pFrameLimiter->setRate(
                bWait ? stdDisplay_state.frameLimit : 0);
        }

        STDLOG_STATUS("display: frame limit %d, presenting %s.\n",
            stdDisplay_state.frameLimit,
            stdDisplay_PresentModeName(stdDisplay_state.pDevice->presentMode()));
    }
}

void stdDisplay_DisableVSync(bool bDisable)
{
    // stdDisplayDX9.c reset the device with the other present interval. Here the
    // request switches the swapchain to immediate presenting (or back to the
    // configured mode) and bypasses the frame cap while level loading runs.
    stdDisplay_state.bNoSync = bDisable;
    stdDisplay_ApplyFramePacing();
}

void stdDisplayBabylon_SetFrameLimit(int frameLimit)
{
    stdDisplay_state.frameLimit = stdDisplay_ClampFrameLimit(frameLimit);
    if ( stdDisplay_state.bStartup )
    {
        stdDisplay_ApplyFramePacing();
    }
}
