// Added: the Babylon column's JonesMainSys services whose Win32 originals act on the game window.
// PostCloseMessage records the same quit request a platform QuitRequested or a close of the game
// window records, so the next wkernel quit check ends the loop as the Win32 PostMessage(WM_QUIT)
// did. ShowFatalMessage keeps the stderr line and also shows the platform message box in the
// windowed run. The other JonesMainSys_* functions still come from JonesPlatformNull.c.
#include <cstdio>

#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/JonesMainSys.h>

#include <babylon/platform/Platform.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

extern "C" int JonesMainSys_PostCloseMessage(void)
{
    stdBabylonEvents_RequestQuit();
    return 1;
}

extern "C" int JonesMainSys_ShowFatalMessage(const char* pText, const char* pTitle, bool bIconStop)
{
    // Added: the text always goes to stderr, exactly as the Null column does, so the log is the
    // same in every run.
    fprintf(stderr, "%s: %s%s\n", pTitle ? pTitle : "", pText ? pText : "",
        bIconStop ? " [Fatal]" : "");

    // Added: the windowed run also shows the native box, like the error dialogs before rendering
    // works. The headless run has no game window, so it keeps the stderr line only.
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( pContext && pContext->pPlatform && pContext->pWindow )
    {
        static_cast<void>(pContext->pPlatform->showMessageBox(
            pTitle ? pTitle : "", pText ? pText : ""));
    }

    // Returns IDOK (1), the value the Windows MessageBox returns when its default OK button
    // closes the box.
    return 1;
}

extern "C" int JonesBabylon_StartupExitCode(int startupResult)
{
    // Added: a user quit from the main menu is a normal exit. The main menu reports its own
    // startup result, kept distinct because a real failure also requests a window close.
    if ( startupResult == JONESMAIN_STARTUP_USERQUIT )
    {
        return 0;
    }

    // The recovered convention: 0 is success, any failure is nonzero.
    return startupResult ? 1 : 0;
}
