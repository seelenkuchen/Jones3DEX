// Added: the Null column's start-mode mapping and main menu (ADR-0026). Split from
// JonesPlatformNull.c so the Babylon column can provide its real menu; the Null column and the
// Babylon headless run keep the original startMode values and never show a screen.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ResolveStartMode(int startMode, int bHasStartMode)
{
    J3D_UNUSED(bHasStartMode);
    // Added: no windowed build here, so the configured value is used as before.
    return startMode;
}

int J3DAPI jonesConfig_DefersStartMode(int startMode)
{
    J3D_UNUSED(startMode);
    // Added: the Null column runs every start-mode dialog where it always did.
    return 0;
}

int J3DAPI jonesConfig_ShowMainMenu(tSysWindow hWnd, JonesState* pConfig, char* pOutLoadPath)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pConfig);
    J3D_UNUSED(pOutLoadPath);

    // Added: there is no screen; the menu cancels like the developer dialog did.
    return JONES_MAINMENU_QUIT;
}
