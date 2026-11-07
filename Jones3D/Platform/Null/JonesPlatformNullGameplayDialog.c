// Added: the Null column's Gameplay settings dialog stub, split out of JonesPlatformNull.c so the
// Babylon column can provide its in-game screen (Jones3D/Platform/Babylon/
// JonesUiGameplaySettings.cpp). The Null column and the Babylon headless runner keep this; the
// windowed Babylon game does not compile this file.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowGamePlayOptions(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
    // Altered: 1 applies changed gameplay settings; the Null dialog cancels like the Windows
    // cancel path (EndDialog(hWnd, 2)), so nothing is applied.
    return JONESPLATFORM_NULL_IDCANCEL;
}
