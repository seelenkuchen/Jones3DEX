// Added: the Null column's JonesMainSys_PostCloseMessage and JonesMainSys_ShowFatalMessage, split
// from JonesPlatformNull.c so the Babylon column can replace them (a real quit request, and a
// platform message box). The other JonesMainSys_* services stay in JonesPlatformNull.c.
#include <stdio.h>

#include <Jones3D/Main/JonesMainSys.h>

int JonesMainSys_PostCloseMessage(void)
{
    // Returns the PostMessage success value; with no window the close request is
    // "delivered" so callers observe the Windows success path.
    return 1;
}

int JonesMainSys_ShowFatalMessage(const char* pText, const char* pTitle, bool bIconStop)
{
    // Added: No message box: the text goes to stderr. Returns IDOK (1), the value the
    // Windows MessageBox returns when its default OK button closes the box.
    fprintf(stderr, "%s: %s%s\n", pTitle ? pTitle : "", pText ? pText : "",
        bIconStop ? " [Fatal]" : "");
    return 1;
}
