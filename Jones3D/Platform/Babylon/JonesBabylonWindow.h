#ifndef JONES3D_PLATFORM_BABYLON_JONESBABYLONWINDOW_H
#define JONES3D_PLATFORM_BABYLON_JONESBABYLONWINDOW_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Added: the windowed/headless test the M7.10 screens use (StdBabylonContext::pWindow). The
// windowed composition root sets a window; the headless runner leaves it null. The control
// schemes module needs the answer at HUD startup, before the display is open, so it cannot use
// stdDisplay_CanRenderWindowed.
bool JonesBabylon_HasWindow(void);

#ifdef __cplusplus
}
#endif
#endif // JONES3D_PLATFORM_BABYLON_JONESBABYLONWINDOW_H
