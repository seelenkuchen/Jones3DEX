// Added: the Null column's pause menu stub, split out of JonesPlatformNull.c so the Babylon column
// can provide the in-game screen (Jones3D/Platform/Babylon/JonesUiPauseMenu.cpp). The Null column
// and the Babylon headless runner keep this; the windowed Babylon game does not compile it.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowPauseMenu(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
    // Altered: the pause menu has no Null screen; it answers the value a closed screen returns
    // (Resume or Back), so a headless run never blocks and its trace is unchanged.
    return JONESPLATFORM_NULL_IDCANCEL;
}
