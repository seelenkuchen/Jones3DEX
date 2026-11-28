#include "JonesScreenshot.h"

#include "JonesBugReport.h"

#include <Jones3D/Main/JonesMainSys.h>

#include <sith/Devices/sithControl.h>

#include <std/General/stdControl.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdFnames.h>
#include <std/General/stdKeys.h>

#include <string.h>

static bool JonesScreenshot_bKeyActive = false;

void JonesScreenshot_ResetGlobals(void)
{
    JonesScreenshot_bKeyActive = false;
}

// Added: picks the first free "<pRoot>/report-NNN" path so each Shift+F12 report is a new folder.
bool JonesScreenshot_SelectBugReportFolder(const char* pRoot, char* pOut, size_t outSize)
{
    if ( !pRoot || pRoot[0] == '\0' || !pOut || outSize == 0u )
    {
        return false;
    }

    for ( unsigned int i = 1u; i <= 999u; ++i )
    {
        char aName[32];
        STD_FORMAT(aName, "report-%03u", i);

        if ( strlen(pRoot) + strlen(aName) + 2u > outSize )
        {
            return false;
        }

        stdFnames_MakePath(pOut, (int)outSize, pRoot, aName);
        if ( !stdFileUtil_FileExists(pOut) )
        {
            return true;
        }
    }

    return false;
}

void JonesScreenshot_ProcessKey(void)
{
    const bool bPressed = sithControl_GetKey(SITHCONTROL_SCREENSHOT, NULL) != 0;

    // Fixed: the removed Win32 window procedure called JonesMain_OnScreenshotKey on VK_F12; poll
    //        the bound control and act only on the press edge, so holding the key does not repeat.
    if ( bPressed && !JonesScreenshot_bKeyActive )
    {
        JonesMain_OnScreenshotKey();

        // Added: Shift+F12 also writes a bug report with the frame recorder, a savegame and a pose
        // file. ReadKey reports the held state of each key here, not just the press edge.
        if ( stdControl_ReadKey(STDKEY_LSHIFT, NULL) || stdControl_ReadKey(STDKEY_RSHIFT, NULL) )
        {
            JonesScreenshot_WriteBugReport();
        }
    }

    JonesScreenshot_bKeyActive = bPressed;
}
