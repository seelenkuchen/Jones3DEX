#ifndef JONES3D_JONESSCREENSHOT_H
#define JONES3D_JONESSCREENSHOT_H
#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Polls the screenshot control once per control update and calls JonesMain_OnScreenshotKey on
// each new press. It does not read the control's press state, so another reader still sees it.
void JonesScreenshot_ProcessKey(void);

void JonesScreenshot_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESSCREENSHOT_H
