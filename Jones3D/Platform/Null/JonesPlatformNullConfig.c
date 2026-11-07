// Added: the Null column's jonesConfig startup, shutdown and active-scheme hook. Split from
// JonesPlatformNull.c so the Babylon column's control scheme module (Platform/Babylon/
// JonesControlSchemes.c) can provide the same symbols; the Babylon executables and tests link
// this file's replacement instead of this one.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>

static int jonesConfig_bStartup = 0;

int jonesConfig_Startup(void)
{
    if ( jonesConfig_bStartup )
    {
        return 1;
    }

    // Added: The Windows Startup loads the dialog icon bitmaps through stdBmp_Load and the
    // key-set/key-action tables of the settings dialogs. None of those exist on the Null
    // platform, so the startup keeps the same "started" answer (returns 1) with no dialog
    // resources behind it.
    jonesConfig_bStartup = 1;
    return 1;
}

void jonesConfig_Shutdown(void)
{
    jonesConfig_bStartup = 0;
}

void J3DAPI jonesConfig_BindActiveControlScheme(void)
{
    // Added: the Null column has no control scheme files and no sithControl binding pass.
}

int J3DAPI jonesConfig_HasActiveControlScheme(void)
{
    // Added: the Null column never loads a scheme.
    return 0;
}
