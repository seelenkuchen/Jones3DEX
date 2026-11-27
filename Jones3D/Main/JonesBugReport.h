#ifndef JONES3D_JONESBUGREPORT_H
#define JONES3D_JONESBUGREPORT_H
#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Added: writes a bug report folder with the flight recorder's last frames, a savegame and a pose
// file. Called from the Shift+F12 press edge; uses JONES3D_REPORT_DIR or the screenshots directory.
void JonesScreenshot_WriteBugReport(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESBUGREPORT_H
