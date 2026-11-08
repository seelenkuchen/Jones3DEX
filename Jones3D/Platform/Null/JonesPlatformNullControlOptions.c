// Added: the Null column's Controls dialog stub, split out of JonesPlatformNull.c so the Babylon
// column can provide its in-game screen (Jones3D/Platform/Babylon/JonesUiControls.cpp). The Null
// column and the Babylon headless runner keep this; the windowed Babylon game does not compile it.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowControlOptions(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
    // Altered: DialogBoxParam with the user cancelling (EndDialog(hWnd, 2) trackers in
    // jonesConfig.c) hands back IDCANCEL; the control dialog has no Null substitute.
    return JONESPLATFORM_NULL_IDCANCEL;
}
