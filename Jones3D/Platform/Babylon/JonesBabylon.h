// Added: the Babylon column's windowed run. This is the run logic (JonesBabylon_Run), not the
// entry point: the windowed composition root Platform/Babylon/mainWindowed.cpp calls it after it
// has installed the adapter context with the SDL platform, clock, file system and game window.
// Same header contracts as the headless run.
#ifndef JONES3D_PLATFORM_BABYLON_JONESBABYLON_H
#define JONES3D_PLATFORM_BABYLON_JONESBABYLON_H

#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Runs the windowed engine loop until the engine quits, the window closes or a quit is requested.
// Same arguments as main: argv[0] is the program name; a "--load <save.nds>" pair answers the
// Null load-game dialog (needs the load-game start mode in Jones.cfg, like the headless runner);
// every other argument joins with single spaces into the command line of JonesMain_Startup. A
// "--load" without a value, or a joined command line longer than the engine's command line
// buffer, prints one error and returns 1 before the engine starts. pUserDir is the writable
// directory chosen by the composition root ("--user-dir", portable.txt or the platform user path;
// "" is the working directory). It is applied after the global reset, so it is created then; a
// path too long for stdUserDir prints one error and returns 1. Returns the process exit code.
// Call once.
int JonesBabylon_Run(int argc, char** argv, const char* pUserDir);

// Added: maps a JonesMain_Startup result to the process exit code. A user quit from the main menu
// reports JONESMAIN_STARTUP_USERQUIT and is a normal exit; every other nonzero result is a startup
// failure. Implemented in Jones3D/Platform/Babylon/JonesBabylonMainSys.cpp.
int JonesBabylon_StartupExitCode(int startupResult);

J3D_EXTERN_C_END
#endif // JONES3D_PLATFORM_BABYLON_JONESBABYLON_H
