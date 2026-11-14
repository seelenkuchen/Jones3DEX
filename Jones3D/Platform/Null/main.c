// Added: the Null column's headless entry point. The run logic lives in JonesHeadless.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesDataLocate.h>
#include <Jones3D/Platform/Null/JonesHeadless.h>

int main(int argc, char** argv)
{
    // Added: --version prints the build's version and exits before any engine code runs.
    for ( int i = 1; i < argc; ++i )
    {
        if ( strcmp(argv[i], "--version") == 0 )
        {
            printf("%s\n", J3D_VERSION_FULL);
            return 0;
        }
    }

    // Added: the Null platform has no executable path service, so the executable directory stays
    // unknown and the working directory is the current one. getenv is read here in the platform
    // layer, never in the game data search.
    JonesDataLocate_SetHostDirs(NULL, ".");
    JonesDataLocate_SetHostEnvironment(getenv("HOME"), NULL, NULL);

    return JonesHeadless_Run(argc, argv);
}
