// Added: the Null column's save, load and exit dialog stubs, split out of JonesPlatformNull.c so
// the Babylon column can provide its in-game screens for exactly those three and keep the other
// Null stubs (Jones3D/Platform/Babylon/JonesUiSaveLoad.cpp). The Null column and the Babylon
// headless runner keep these; the windowed Babylon game does not compile this file.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

#include <std/General/std.h>
#include <std/General/stdFnames.h>
#include <std/General/stdUtil.h>

// Added: the Null column holds no display resources behind its dialogs. JonesDisplay calls this
// before it shuts the display down; the Babylon column replaces this no-op with the shared ui
// context teardown.
void J3DAPI jonesConfig_ReleaseDisplayResources(void)
{
}

int J3DAPI jonesConfig_GetLoadGameFilePath(tSysWindow hWnd, char* pDestNdsPath)
{
    J3D_UNUSED(hWnd);

    // Added: JonesPlatformNull_SetLoadPath lets the headless runner answer the dialog
    // the way a chosen file does; without one it still cancels.
    const char* pLoadPath = JonesPlatformNull_GetLoadPath();
    if ( !pLoadPath || !pLoadPath[0] || !pDestNdsPath )
    {
        // Altered: The Windows OpenFileName dialog returns 1 with a chosen path and 2 when
        // the user cancels; there is no game list on this platform, so the dialog cancels.
        return JONESPLATFORM_NULL_IDCANCEL;
    }

    // Added: Fill pDestNdsPath the way the Windows dialog does on success (the OpenFileName
    // result forced to the .nds extension, then copied) and return its success value.
    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    stdUtil_StringCopy(aPath, STD_ARRAYLEN(aPath), pLoadPath);
    stdFnames_ChangeExtEx(aPath, STD_ARRAYLEN(aPath), "nds");
    stdUtil_StringCopy(pDestNdsPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, aPath);

    // Added: One-shot, so a later load-game request (from a menu) cancels again.
    JonesPlatformNull_ClearLoadPath();
    return 1;
}

int J3DAPI jonesConfig_GetSaveGameFilePath(tSysWindow hWnd, char* pOutFilePath)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pOutFilePath);
    // Altered: Same DialogBoxParam cancel answer as GetLoadGameFilePath (1 = saved).
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowExitGameDialog(tSysWindow hWnd, char* pSaveGameFilePath)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pSaveGameFilePath);
    // Altered: The Windows exit dialog answers 1 ("exit"), 1187 ("exit and save") or 2
    // (cancel); the Null dialog always answers the cancel value, so the game does not
    // exit through this path.
    return JONESPLATFORM_NULL_IDCANCEL;
}
