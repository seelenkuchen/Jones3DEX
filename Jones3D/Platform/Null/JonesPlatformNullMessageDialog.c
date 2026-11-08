// Added: the Null column's message and insert-CD dialog stubs, split out of
// JonesPlatformNull.c so the Babylon column can replace them with its in-game screens
// (Jones3D/Platform/Babylon/JonesUiModalMessage.cpp) without duplicate symbols. The Null
// column and the Babylon headless runner keep these; the windowed Babylon game does not
// compile this file.
#include <stdio.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowDialogInsertCD(tSysWindow hWnd, intptr_t dwInitParam)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(dwInitParam);
    // Altered: The Windows "insert CD" prompt cancels with EndDialog(hwnd, 2); callers
    // treat the cancel answer as "do not retry, exit".
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowMessageDialog(tSysWindow hWnd, const char* pTitle, const char* pText, int iconID)
{
    J3D_UNUSED(hWnd);

    // Added: The Windows message dialog shows an icon, title and text; the text is
    // mirrored to stderr, and the cancel answer (EndDialog(hwnd, 2) of the WM_CLOSE
    // handler in jonesConfig.c MessageDialogProc) is returned.
    fprintf(stderr, "%s: %s\n", pTitle ? pTitle : "", pText ? pText : "");
    return JONESPLATFORM_NULL_IDCANCEL;
}
