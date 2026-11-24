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
#define JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING           "engine.renderer.perPixelLighting" // Added: M10.2 Lighting: Classic / Per-pixel
#define JONESDISPLAY_CFG_ENGINE_RENDER_RENDERSCALE               "engine.renderer.renderScale" // Added: M9.4 internal 3D resolution
#define JONESDISPLAY_CFG_ENGINE_RENDER_ANTIALIASING              "engine.renderer.antiAliasing" // Added: M9.8 SMAA 1x scene anti-aliasing
#define JONESDISPLAY_CFG_ENGINE_RENDER_SHARPENING                "engine.renderer.sharpening" // Added: M9.9 contrast-adaptive sharpening
#define JONESDISPLAY_CFG_ENGINE_RENDER_TONEMAPPING               "engine.renderer.toneMapping" // Added: M9.10 tone-mapping operator
#define JONESDISPLAY_CFG_ENGINE_RENDER_EXPOSURE                  "engine.renderer.exposure" // Added: M9.10 exposure before the tone map
#define JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS                "engine.renderer.brightness" // Added: M9.11 calibration gamma after the tone map
#define JONESDISPLAY_CFG_ENGINE_RENDER_BLOOM                     "engine.renderer.bloom" // Added: M9.12 bloom level
#define JONESDISPLAY_CFG_ENGINE_RENDER_LIGHTSHAFTS               "engine.renderer.lightShafts" // Added: M9.16 light-shaft level
#define JONESDISPLAY_CFG_ENGINE_RENDER_SSAO                      "engine.renderer.ssao" // Added: M9.17b ambient-occlusion level
#define JONESDISPLAY_CFG_ENGINE_RENDER_MOTIONBLUR                "engine.renderer.motionBlur" // Added: M9.19 camera motion-blur level
#define JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE                 "engine.renderer.lensFlare" // Added: M9.18 lens flare
#define JONESDISPLAY_CFG_ENGINE_RENDER_VIGNETTE                  "engine.renderer.vignette" // Added: M9.18 vignette
#define JONESDISPLAY_CFG_ENGINE_RENDER_FILMGRAIN                 "engine.renderer.filmGrain" // Added: M9.18 film grain
#define JONESDISPLAY_CFG_ENGINE_RENDER_WATER_RIPPLES             "engine.renderer.water.ripples" // Added: M9.17 water ripples
#define JONESDISPLAY_CFG_ENGINE_RENDER_COLORGRADING              "engine.renderer.colorGrading" // Added: M9.13 color-grading preset
#define JONESDISPLAY_CFG_ENGINE_RENDER_DOF_CUTSCENES             "engine.renderer.dof.cutscenes" // Added: M9.14 depth of field in cutscenes
#define JONESDISPLAY_CFG_ENGINE_RENDER_DOF_GAMEPLAY              "engine.renderer.dof.gameplay" // Added: M9.14 depth of field in gameplay
#define JONESDISPLAY_CFG_ENGINE_RENDER_DOF_STRENGTH              "engine.renderer.dof.strength" // Added: M9.14 depth-of-field strength
#define JONESDISPLAY_CFG_ENGINE_RENDER_FRAMELIMIT                "engine.renderer.frameLimit" // Added: M9.23 frame pacing


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

// Added: M10.2. Applies the Lighting option at once: per-pixel mode needs lit faces, so it is
// forced off when they are off and in Retail/QOL-off builds. Presentation only.
void J3DAPI JonesDisplay_SetPerPixelLighting(int bEnable);

void J3DAPI JonesDisplay_EnableDualMonitor(int bEnable);
int J3DAPI JonesDisplay_UpdateDualScreenWindowSize(const JonesDisplaySettings* pSettings);

void J3DAPI JonesDisplay_OpenLoadScreen(const char* pMatFilePath, float wlStartX, float wlStartY, float wlEndX, float wlEndY, int bPrimaryMusicTheme);
void JonesDisplay_CloseLoadScreen(void);
void J3DAPI JonesDisplay_UpdateLoadProgress(float progress);

int JonesDisplay_ShowSplashDemo(void); // Added

void JonesDisplay_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESDISPLAY_H
