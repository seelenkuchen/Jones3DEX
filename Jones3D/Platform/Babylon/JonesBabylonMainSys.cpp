// Added: the Babylon column's JonesMainSys services whose Win32 originals act on the game window.
// PostCloseMessage records the same quit request a platform QuitRequested or a close of the game
// window records, so the next wkernel quit check ends the loop as the Win32 PostMessage(WM_QUIT)
// did. The other JonesMainSys_* functions still come from JonesPlatformNull.c.
#include <Jones3D/Main/JonesMainSys.h>

#include <std/Babylon/stdBabylonEvents.h>

extern "C" int JonesMainSys_PostCloseMessage(void)
{
    stdBabylonEvents_RequestQuit();
    return 1;
}
