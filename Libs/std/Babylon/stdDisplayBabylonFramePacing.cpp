// Frame pacing: the frame limit, V-Sync and the limiter.
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
