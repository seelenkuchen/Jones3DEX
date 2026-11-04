#include <stdio.h>

#include <Jones3D/types.h> // Should be included first due to Windows.h / CommCtrl.h conflict

// Added: Moved here from Jones3D/Indy3D.h; this is the only file that defines the entry point
int __stdcall Indy3D_WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Win32/JonesDialog.h>
#include <Jones3D/Main/JonesCapture.h>
#include <Jones3D/Main/JonesGlobals.h>
#include <Jones3D/Main/JonesMain.h>

#ifndef J3D_DIRECTX6
#include <std/General/stdRenderCapture.h>
#endif

#include <sound/Driver.h>

#include <std/General/stdPlatform.h>
#include <std/Win95/stdComm.h>
#include <std/Win95/stdConsole.h>
#include <std/Win95/stdControl.h>
#include <std/Win95/stdWin95.h>

#include <w32util/wuRegistry.h>
#include <wkernel/wkernel.h>
#include <wkernel/wkernelWin32.h>

static const char* appName = "Open Jones 3D";

// Added: JonesMain_Startup keeps the capture path until it opens the file, and the command line
// without the -capture option stays valid for the whole startup call.
#ifndef J3D_DIRECTX6
static char WinMain_aCapturePath[1024];
static char WinMain_aCaptureCmdLine[4096];
#endif

bool ResetGlobals(void);

int Startup(const char* aCmd)
{
#ifndef J3D_DIRECTX6
    // Added: on Windows the frame window starts off so the player can arm it in the console for
    // just the scene to record. The optional -captureframes token starts it on, so scenes before
    // gameplay, such as the intro and the menu, can be recorded without the console. The headless
    // runner still records every frame.
    bool bFramesOn = false;
    if ( JonesCapture_ParseCommandLine(aCmd, WinMain_aCapturePath, sizeof(WinMain_aCapturePath), WinMain_aCaptureCmdLine, sizeof(WinMain_aCaptureCmdLine), &bFramesOn) )
    {
        stdRenderCapture_SetFramesEnabled(bFramesOn);
        JonesMain_SetCapture(WinMain_aCapturePath, "dx9");
        aCmd = WinMain_aCaptureCmdLine;
    }
#endif

    if ( JonesMain_Startup(aCmd) )
    {
        JonesMain_Shutdown();
        return -1;
    }
    return 0;
}

// Indy3D.exe entry point
int WINAPI Indy3D_WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    JonesMain_g_mainMutex = CreateMutex(NULL, /*CreateMutex=*/TRUE, "INDY3D");
    if ( GetLastError() != ERROR_ALREADY_EXISTS )
    {
         //Added: signal handler for unhandled exceptions
        stdPlatform_InstallSignalHandler();

        wkernel_SetProcessProc(JonesMain_Process);
        wkernel_SetStartupCallback(Startup);
        wkernel_SetShutdownCallback(JonesMain_Shutdown);
        return wkernel_Run(hInstance, hPrevInstance, lpCmdLine, nShowCmd, appName);
    }

    CloseHandle(JonesMain_g_mainMutex);
    return -1;
}

// Added: Standalone entry point. Does what DllMain does when injected, then what the retail exe's WinMain hook runs, with no hooks to install.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // Enable HIDPI
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if ( !ResetGlobals() )
    {
        printf("CRITICAL ERROR: Failed to reset global vars!\n");
        MessageBox(NULL, "Failed to reset global variables!", "Jones3D", MB_ICONINFORMATION | MB_DEFBUTTON1);
    }

    return Indy3D_WinMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}

bool ResetGlobals(void)
{
    // Altered: the shared resets moved to Jones3D/Main/JonesGlobals.c (the native Null
    // platform runs them through Jones3D_ResetAllGlobals). The modules that only exist
    // on Windows stay here, called in their original positions between the shared runs.
    Jones3D_ResetGlobalsBeforeSoundDriver();
    SoundDriver_ResetGlobals();
    Jones3D_ResetGlobalsBeforeJonesDialog();
    JonesDialog_ResetGlobals();
    Jones3D_ResetGlobalsBeforeJonesConfig();
    jonesConfig_ResetGlobals();
    Jones3D_ResetGlobalsBeforeStdComm();
    stdComm_ResetGlobals();
    Jones3D_ResetGlobalsBeforeStdConsole();
    stdConsole_ResetGlobals();
    stdControl_ResetGlobals();
    Jones3D_ResetGlobalsBeforeStdWin95();
    stdWin95_ResetGlobals();
    Jones3D_ResetGlobalsBeforeWkernel();
    wkernel_ResetGlobals();
    wuRegistry_ResetGlobals();

    // Altered: There is no executable to unprotect in standalone builds.
    return true;
}
