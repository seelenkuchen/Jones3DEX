#ifndef JONES3D_JONESSCREENSHOT_H
#define JONES3D_JONESSCREENSHOT_H
#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Polls the screenshot control once per control update and calls JonesMain_OnScreenshotKey on
// each new press. It does not read the control's press state, so another reader still sees it.
void JonesScreenshot_ProcessKey(void);

// Added: picks the first free "<pRoot>/report-NNN" path, from 001 to 999, into pOut. Returns false
// when pRoot is empty, the path does not fit pOut, or every number is taken.
bool JonesScreenshot_SelectBugReportFolder(const char* pRoot, char* pOut, size_t outSize);

void JonesScreenshot_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESSCREENSHOT_H
