#include "JonesScreenshot.h"

#include <Jones3D/Main/JonesMainSys.h>

#include <sith/Devices/sithControl.h>

static bool JonesScreenshot_bKeyActive = false;

void JonesScreenshot_ResetGlobals(void)
{
    JonesScreenshot_bKeyActive = false;
}

void JonesScreenshot_ProcessKey(void)
{
    const bool bPressed = sithControl_GetKey(SITHCONTROL_SCREENSHOT, NULL) != 0;

    // Fixed: the removed Win32 window procedure called JonesMain_OnScreenshotKey on VK_F12; poll
    //        the bound control and act only on the press edge, so holding the key does not repeat.
    if ( bPressed && !JonesScreenshot_bKeyActive )
    {
        JonesMain_OnScreenshotKey();
    }

    JonesScreenshot_bKeyActive = bPressed;
}
