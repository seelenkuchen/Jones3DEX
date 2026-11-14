// Added: the windowed adapter's game data prompt (ADR-0021, M8.4). The game layer asks through
// JonesDataLocate's prompt hook, so it never calls Babylon itself.
#ifndef JONES3D_PLATFORM_BABYLON_JONESBABYLONDATA_H
#define JONES3D_PLATFORM_BABYLON_JONESBABYLONDATA_H

#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Registers the Babylon prompt handler: askChoice with "Choose folder..." and "Quit", then
// Platform::chooseFolder. When no dialog can be shown it reports the message plus the --data hint
// through showMessageBox. Call before JonesMain_Startup, only in the windowed run.
void JonesBabylonData_InstallPromptHandler(void);

J3D_EXTERN_C_END
#endif // JONES3D_PLATFORM_BABYLON_JONESBABYLONDATA_H
