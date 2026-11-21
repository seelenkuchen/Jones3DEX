#ifndef JONES3D_JONESDISPLAY_H
#define JONES3D_JONESDISPLAY_H
#include <Jones3D/types.h>
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <std/types.h>

J3D_EXTERN_C_START

#define JONESDISPLAY_CFG_GRAPHICS_ALLDEVICES       "graphics.allDevices"
#define JONESDISPLAY_CFG_GRAPHICS_WINDOW           "graphics.window"
#define JONESDISPLAY_CFG_GRAPHICS_DUALMONITOR      "graphics.dualMonitor"
#define JONESDISPLAY_CFG_GRAPHICS_BUFFERING        "graphics.buffering"
#define JONESDISPLAY_CFG_GRAPHICS_MIPMAPFILTER     "graphics.mipmapFilter"
#define JONESDISPLAY_CFG_GRAPHICS_DISPLAY          "graphics.display"
#define JONESDISPLAY_CFG_GRAPHICS_DEVICE           "graphics.device"
#define JONESDISPLAY_CFG_GRAPHICS_WIDTH            "graphics.width"
#define JONESDISPLAY_CFG_GRAPHICS_HEIGHT           "graphics.height"
#define JONESDISPLAY_CFG_GRAPHICS_BPP              "graphics.bpp"
#define JONESDISPLAY_CFG_GRAPHICS_REFRESHRATE      "graphics.refreshRate"
#define JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL "graphics.performanceLevel"

#define JONESDISPLAY_CFG_ENGINE_RENDER_HIPOLY                    "engine.renderer.hipoly"
#define JONESDISPLAY_CFG_ENGINE_RENDER_FOV                       "engine.renderer.fov" // Added: Options > Display, Field of view
#define JONESDISPLAY_CFG_ENGINE_RENDER_GEOMETRYMODE              "engine.renderer.geometry"
#define JONESDISPLAY_CFG_ENGINE_RENDER_LIGHTINGMODE              "engine.renderer.lighting"
#define JONESDISPLAY_CFG_ENGINE_RENDER_FOGENABLED                "engine.renderer.fog.enabled"
#define JONESDISPLAY_CFG_ENGINE_RENDER_FOGDENSITY                "engine.renderer.fog.density"
#define JONESDISPLAY_CFG_ENGINE_RENDER_PVSENABLED                "engine.renderer.culling.pvs.enabled"
#define JONESDISPLAY_CFG_ENGINE_RENDER_CULLEDSECTORTRAVERSALMODE "engine.renderer.culling.culledSectorTraversal.mode"
#define JONESDISPLAY_CFG_ENGINE_RENDER_MAXTHINGCOLLECTDISTANCE   "engine.renderer.culling.culledSectorTraversal.maxThingCollectDistance"
#define JONESDISPLAY_CFG_ENGINE_RENDER_MAXLIGHTCOLLECTDISTANCE   "engine.renderer.culling.culledSectorTraversal.maxLightCollectDistance"
#define JONESDISPLAY_CFG_ENGINE_RENDER_LITFACESENABLED           "engine.renderer.litFaces.enabled" // Added: option A L3 (ADR-0023)
#define JONESDISPLAY_CFG_ENGINE_RENDER_RENDERSCALE               "engine.renderer.renderScale" // Added: M9.4 internal 3D resolution


// Returns 0 for success and 1 indicating error
int J3DAPI JonesDisplay_Startup(JonesDisplaySettings* pSettings);
void JonesDisplay_Shutdown(void);

int J3DAPI JonesDisplay_Open(JonesDisplaySettings* pSettings);
void JonesDisplay_Close(void);

int J3DAPI JonesDisplay_Restart(JonesDisplaySettings* pSettings);
void J3DAPI JonesDisplay_SetDefaultVideoMode(const StdDisplayEnvironment* pEnv, JonesDisplaySettings* pDisplaySettings);

// Added: resolves the video mode a run starts with. A width or height already in Jones.cfg always
// wins; a missing one takes the desktop mode's size when pDesktopMode is usable (see
// stdDisplay_GetDesktopMode), else 640x480. Writes the resolved size to pWidth and pHeight. This is
// a first-start default only: it writes no config and opens no display.
void J3DAPI JonesDisplay_ResolveFirstRunMode(const StdVideoMode* pDesktopMode,
    int bHasWidth, int width, int bHasHeight, int height,
    int* pWidth, int* pHeight);

void J3DAPI JonesDisplay_EnableDualMonitor(int bEnable);
int J3DAPI JonesDisplay_UpdateDualScreenWindowSize(const JonesDisplaySettings* pSettings);

void J3DAPI JonesDisplay_OpenLoadScreen(const char* pMatFilePath, float wlStartX, float wlStartY, float wlEndX, float wlEndY, int bPrimaryMusicTheme);
void JonesDisplay_CloseLoadScreen(void);
void J3DAPI JonesDisplay_UpdateLoadProgress(float progress);

int JonesDisplay_ShowSplashDemo(void); // Added

void JonesDisplay_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESDISPLAY_H
