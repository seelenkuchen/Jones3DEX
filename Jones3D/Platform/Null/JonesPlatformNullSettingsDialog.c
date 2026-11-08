// Added: the Null column's Display and Sound settings dialog stubs, split out of
// JonesPlatformNull.c so the Babylon column can provide its in-game screens for exactly those two
// and keep the other Null stubs (Jones3D/Platform/Babylon/JonesUiDisplaySettings.cpp and
// JonesUiSoundSettings.cpp). The Null column and the Babylon headless runner keep these; the
// windowed Babylon game does not compile this file.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowDisplaySettingsDialog(tSysWindow hWnd, StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pDSettings)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pDisplayEnv);
    J3D_UNUSED(pDSettings);
    // Altered: 1 applies changed display settings; the Null dialog cancels like the
    // Windows WM_CLOSE path (EndDialog(hWnd, 2)), so no settings are applied.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowSoundSettingsDialog(tSysWindow hWnd, JonesSoundSettings* pData)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pData);
    // Altered: 1 applies changed sound settings; the Null dialog cancels like the
    // Windows cancel paths.
    return JONESPLATFORM_NULL_IDCANCEL;
}
