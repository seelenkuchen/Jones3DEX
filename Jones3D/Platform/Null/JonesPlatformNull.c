// Added: Null version of Jones3D/Platform/Win32 (JonesMainWin32.c, JonesHudWin32.c,
// JonesDevDialogWin32.c) and the Win32 side of jonesConfig.c. There is no window, no
// message pump and no modal dialog box on this platform: the window-proc switch is a
// no-op, every dialog answers the DialogBoxParam value of the Windows user pressing
// Cancel (EndDialog(hwnd, IDCANCEL) == 2), and the timer/mutex/ComputerName services
// degrade to safe stubs. Same header contracts.
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/JonesMainSys.h>
#include <Jones3D/Display/JonesHudSys.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>
#include <sith/Dss/sithGamesave.h>

#include <std/General/stdFnames.h>
#include <std/General/stdUtil.h>

#include <wkernel/wkernel.h>

// JonesMainSys_*: the Win32 message services moved out of JonesMain.c.

void JonesMainSys_StartQuickSaveTimer(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
    // Added: The Windows SetTimer quicksave-text timer; a headless render keeps nothing
    // that shows on-screen, so there is nothing to schedule.
}

void JonesMainSys_ShowWindow(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
}

int JonesMainSys_PostCloseMessage(void)
{
    // Returns the PostMessage success value; with no window the close request is
    // "delivered" so callers observe the Windows success path.
    return 1;
}

void JonesMainSys_UseGameWindowProc(void)
{
    // Added: Switches wkernel to JonesMain_GameWndProc on Windows; there is no window or
    // message queue on the Null platform, and the wkernel proc hook is Win32-only, so
    // nothing to switch. The intro's skip keys end with the intro window procedure.
    wkernel_SetIntroSkipCallback(NULL);
}

void JonesMainSys_UseIntroWindowProc(void)
{
    // Added: See JonesMainSys_UseGameWindowProc. The one part of JonesMain_IntroWndProc that
    // a window-less column keeps: its skip keys (Enter, Escape, left mouse button up) call
    // JonesMain_OnSkipIntro, through the wkernel callback the Babylon column's event pump
    // calls (wkernelNull.c never does).
    wkernel_SetIntroSkipCallback(JonesMain_OnSkipIntro);
}

void JonesMainSys_ClearWindowProc(void)
{
    // Added: See JonesMainSys_UseGameWindowProc.
    wkernel_SetIntroSkipCallback(NULL);
}

bool JonesMainSys_GetComputerName(char* pDst, size_t size)
{
    if ( !pDst || !size )
    {
        return false;
    }

    // Added: gethostname fills the same host-name contract as the Windows
    // GetComputerName call (POSIX name, truncated to the buffer).
    char aName[128] = { 0 };
#ifdef _WIN32
    // Added: the Babylon column on Windows reads the COMPUTERNAME variable Windows sets to the
    // GetComputerName name (gethostname there needs Winsock started first). getenv_s is the CRT
    // form MSVC does not deprecate; a missing or oversized value leaves the buffer empty.
    size_t numRequired = 0;
    if ( getenv_s(&numRequired, aName, STD_ARRAYLEN(aName), "COMPUTERNAME") != 0 )
    {
        aName[0] = '\0';
    }
    if ( !aName[0] )
#else
    if ( gethostname(aName, STD_ARRAYLEN(aName) - 1) != 0 || !aName[0] )
#endif
    {
        aName[0] = 'J';
    }

    size_t nameLen = strlen(aName);
    if ( nameLen >= size )
    {
        return false;
    }

    memcpy(pDst, aName, nameLen + 1);
    return true;
}

bool JonesMainSys_CloseHandle(tSysHandle handle)
{
    J3D_UNUSED(handle);
    // Added: No kernel handle table on this platform; nothing owns a Null handle yet, so
    // closing stays a no-op that keeps the true-on-success convention.
    return true;
}

int JonesMainSys_ShowFatalMessage(const char* pText, const char* pTitle, bool bIconStop)
{
    // Added: No message box: the text goes to stderr. Returns IDOK (1), the value the
    // Windows MessageBox returns when its default OK button closes the box.
    fprintf(stderr, "%s: %s%s\n", pTitle ? pTitle : "", pText ? pText : "",
        bIconStop ? " [Fatal]" : "");
    return 1;
}

// JonesMain_ShowDevDialog: developer startup dialog (devMode only).

int J3DAPI JonesMain_ShowDevDialog(tSysWindow hWnd, JonesState* pConfig)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pConfig);
    // Altered: The Windows dialog returns what DialogBoxParam hands back; on cancel that
    // is IDCANCEL (2), and JonesMain treats everything but 1 as "exit", so the Null
    // startup dialog always cancels.
    return JONESPLATFORM_NULL_IDCANCEL;
}

// JonesHudSys_*: the Windows help-viewer launcher and process check.

// Altered: No help viewer can be launched on this platform, so no viewer process handle
// is ever created (the Windows version returns NULL the same way on a bad help string).
tSysHandle J3DAPI JonesHud_OpenHelp(tSysHandle process)
{
    J3D_UNUSED(process);
    return NULL;
}

bool JonesHudSys_IsProcessRunning(tSysHandle hProcess)
{
    J3D_UNUSED(hProcess);
    // Added: No process was ever launched, so no process is running.
    return false;
}

// jonesConfig_*: the Win32 side of Jones3D/Display/jonesConfig.c.

static int jonesConfig_bStartup = 0;

// Added: --load path handed to the Null load-game dialog by JonesPlatformNull_SetLoadPath.
static const char* jonesPlatformNull_pLoadPath = NULL;

void JonesPlatformNull_SetLoadPath(const char* pPath)
{
    jonesPlatformNull_pLoadPath = pPath;
}

const char* JonesPlatformNull_GetLoadPath(void)
{
    return jonesPlatformNull_pLoadPath;
}

void JonesPlatformNull_ClearLoadPath(void)
{
    jonesPlatformNull_pLoadPath = NULL;
}

int jonesConfig_Startup(void)
{
    if ( jonesConfig_bStartup )
    {
        return 1;
    }

    // Added: The Windows Startup loads the dialog icon bitmaps through stdBmp_Load and
    // the key-set/key-action tables of the settings dialogs. None of those exist on the
    // Null platform, so the startup keeps the same "started" answer (returns 1) with no
    // dialog resources behind it.
    jonesConfig_bStartup = 1;
    return 1;
}

void jonesConfig_Shutdown(void)
{
    jonesConfig_bStartup = 0;
}

int J3DAPI jonesConfig_ShowControlOptions(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
    // Altered: DialogBoxParam with the user cancelling (EndDialog(hWnd, 2) trackers in
    // jonesConfig.c) hands back IDCANCEL; the control dialog has no Null substitute.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowDisplaySettingsDialog(tSysWindow hWnd, StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pDSettings)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pDisplayEnv);
    J3D_UNUSED(pDSettings);
    // Altered: 1 applies changed display settings; the Null dialog cancels like the
    // Windows WM_CLOSE path (EndDialog(hWnd, 2)), so no settings are applied.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowGamePlayOptions(tSysWindow hWnd)
{
    J3D_UNUSED(hWnd);
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
