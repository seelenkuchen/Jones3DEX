// Added: the Null column's JonesMainSys_PostCloseMessage, split from JonesPlatformNull.c so the
// Babylon column can replace it with the real quit request. The other JonesMainSys_* services
// stay in JonesPlatformNull.c.
#include <Jones3D/Main/JonesMainSys.h>

int JonesMainSys_PostCloseMessage(void)
{
    // Returns the PostMessage success value; with no window the close request is
    // "delivered" so callers observe the Windows success path.
    return 1;
}
