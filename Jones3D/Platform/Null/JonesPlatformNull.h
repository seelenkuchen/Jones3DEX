#ifndef JONES3D_PLATFORM_NULL_JONESPLATFORMNULL_H
#define JONES3D_PLATFORM_NULL_JONESPLATFORMNULL_H
#include <j3dcore/j3d.h>

// Altered: Answers IDCANCEL (2), the value Windows dialogs hand back when the user cancels
// (WM_CLOSE/Cancel all call EndDialog(hwnd, 2) in jonesConfig.c). Callers treat everything but
// their success codes as cancel. Shared by the Null dialog stubs.
#define JONESPLATFORM_NULL_IDCANCEL 2

J3D_EXTERN_C_START

// Added: Null-platform-only control for jonesConfig_GetLoadGameFilePath. On Windows that
// function runs the load-game OpenFileName dialog; there is no dialog here, so the
// headless runner hands the chosen .nds path over before startup instead.
//
// When a path is set, jonesConfig_GetLoadGameFilePath copies it into pDestNdsPath the way
// the Windows dialog fills it (forcing the .nds extension) and returns 1 (the dialog's
// success value), then clears the path so a one-shot request is not reused. With no path
// set the dialog keeps answering the cancel value. The stored pointer is not copied: it
// must stay valid until the dialog is answered or the path is set again.
void JonesPlatformNull_SetLoadPath(const char* pPath);

// The path set above, or NULL; the dialog consumes it with JonesPlatformNull_ClearLoadPath after
// it has copied it into the caller's buffer. The Babylon column uses these from its load screen.
const char* JonesPlatformNull_GetLoadPath(void);
void JonesPlatformNull_ClearLoadPath(void);

J3D_EXTERN_C_END
#endif // JONES3D_PLATFORM_NULL_JONESPLATFORMNULL_H
