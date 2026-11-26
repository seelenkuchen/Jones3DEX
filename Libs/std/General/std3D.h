#ifndef STD_STD3D_H
#define STD_STD3D_H
#include <j3dcore/j3d.h>
#include <std/types.h>

#include "stdShader.h"

// Added following macros from rdCache to use for VBO & IBO sizes
#define STD3D_MAXFACEVERTICES 64 
#define STD3D_MAXVERTICES     32768

// Added: the most lights std3D_SetLights takes, RDCAMERA_MAX_LIGHTS (rdroid checks they agree)
#define STD3D_MAXLIGHTS 128u

#define STD3D_CFG_MIPMAPAUTOGEN     "graphics.mipmapAutoGen"
#define STD3D_CFG_ANISOTROPICFILTER "graphics.anisotropicFilter"
// Added: nearest min/mag for world and model textures, the original look (Options > Display).
#define STD3D_CFG_TEXTUREPIXELATED  "engine.renderer.texturePixelated"

#define STD3D_CFG_MSAAENABLED "graphics.msaa.enabled"
#define STD3D_CFG_MSAASAMPLES "graphics.msaa.samples"

J3D_EXTERN_C_START

extern float std3D_g_fogDensity;
extern size_t std3D_g_maxVertices;

int std3D_Startup(void);
void std3D_Shutdown(void);

const Device3D* std3D_GetCurrentDevice(void); // Added

size_t std3D_GetNumDevices(void);
const Device3D* std3D_GetAllDevices(void);
void J3DAPI std3D_SetFindAllDevices(int bFindAll);

int J3DAPI std3D_Open(size_t deviceNum);
void std3D_Close(void);

void J3DAPI std3D_GetTextureFormat(StdColorFormatType type, ColorInfo* pDest, int* pbColorKeySet, LPDDCOLORKEY* ppColorKey);
StdColorFormatType J3DAPI std3D_GetColorFormat(const ColorInfo* pCi);
size_t std3D_GetNumTextureFormats(void);

int std3D_StartScene(void);
void std3D_EndScene(void);

// Added: directs the draws until std3D_EndHudLayer to the display-sized HUD layer instead of the
// scene targets, so the gameplay HUD stays sharp at any render scale and is never touched by the
// scene's post effects. Must be called inside a scene; the draws collected so far are recorded into
// the scene first. The HUD layer is cleared to transparent, drawn with its own depth (the HUD camera's
// overlay z range is below the scene's, so its ordering is unchanged), and composited over the
// presented scene after the tone map. The game marks the HUD section with this pair. The
// Null backend accepts both as no-ops, and std3D_EndScene ends the layer if std3D_EndHudLayer was
// not called. std3D_EndHudLayer outside the layer is a no-op.
void std3D_BeginHudLayer(void);
void std3D_EndHudLayer(void);

void J3DAPI std3D_DrawRenderList(tSysTexture* pTex, Std3DRenderState rdflags, StdVertexTL* aVerts, size_t numVerts, uint16_t* aIndices, size_t numIndices);

// Added: std3D_DrawRenderList for vertices that carry their view-space position and normal
// (StdVertexLit, option A L1). Everything std3D_DrawRenderList promises holds unchanged for the
// tl part of each vertex: the render state it sets, the texture, the limits on numVerts and the
// indices, numIndices / 3 triangles, and the color it draws, bit for bit. The position and normal
// only feed the backend's per-pixel targets (a backend without them ignores them). The arrays are
// read during the call only; a draw it refuses draws nothing, as std3D_DrawRenderList.
void J3DAPI std3D_DrawRenderListLit(tSysTexture* pTex, Std3DRenderState rdflags, StdVertexLit* aVerts, size_t numVerts, uint16_t* aIndices, size_t numIndices);

// Added: the light list of the frame (option A L4, ADR-0023): numLights lights in the order of the
// rdroid camera's list, positions in view space (StdLight). A backend with per-pixel lighting keeps a
// copy for its lit draws (std3D_DrawRenderListLit): the lit draws of a scene pass see the last list
// set before the pass ends. The per-pixel lit shader reads it for vertices whose face asks for
// lights (M10.2); a Classic vertex adds nothing, so its pixel is unchanged. Backends without
// per-pixel lighting (Null) ignore the call. aLights is read during the call only and may be NULL
// when numLights is 0, which sets the empty list. A backend that keeps the list refuses more than
// STD3D_MAXLIGHTS lights, or a NULL aLights with lights, with a logged error, and sets the empty
// list. May be called outside a scene. The game calls it once per sithRender_Draw, and only with
// engine.renderer.litFaces.enabled (rdGetLitFaces).
void J3DAPI std3D_SetLights(const StdLight* aLights, size_t numLights);

void J3DAPI std3D_DrawLineStrip(StdVertexTL* aVerts, size_t numVerts);
void J3DAPI std3D_DrawPointList(StdVertexTL* aVerts, size_t numVerts);
void J3DAPI std3D_SetRenderState(Std3DRenderState rdflags);
void std3D_SetWireframeRenderState(void);

void J3DAPI std3D_AllocSystemTexture(tSystemTexture* pTexture, tVBuffer** apVBuffers, size_t numMipLevels, StdColorFormatType formatType);
void J3DAPI std3D_GetValidDimensions(uint32_t width, uint32_t height, uint32_t* pOutWidth, uint32_t* pOutHeight);
void J3DAPI std3D_ClearSystemTexture(tSystemTexture* pTex);

void J3DAPI std3D_AddToTextureCache(tSystemTexture* pCacheTexture, StdColorFormatType format);
void std3D_ResetTextureCache(void);

size_t J3DAPI std3D_GetMipMapCount(const tSystemTexture* pTexture);
void J3DAPI std3D_UpdateFrameCount(tSystemTexture* pTexture);
size_t J3DAPI std3D_FindClosestFormat(const ColorInfo* pMatch);

// Added: the per-frame focus input of the optional depth-of-field stage (M9.14, presentation
// only). distance is the view-space distance from the camera to the thing the current camera looks
// at, bCutscene true for a cutscene camera (near and far blur) and false for a gameplay camera
// (background blur only). deltaTime, in seconds of game time, smooths the distance towards the
// target; a non-positive value snaps it. The backend keeps the last near and far plane of
// std3D_SetProjection for the depth linearisation. Backends without depth of field (Null, and the
// replay path) ignore the call, and it records nothing in a capture. It changes no simulation
// state.
void J3DAPI std3D_SetFocus(float distance, bool bCutscene, float deltaTime);

// Added: the frame's game time in seconds, for the optional light-shaft stage's fade (M9.16,
// presentation only). Called once per rendered frame; the backend keeps it for the renderer's
// smoothing. A non-finite value is ignored and a long step is clamped. Backends without light
// shafts (Null, and the replay path) ignore the call, and it records nothing in a capture.
void J3DAPI std3D_SetFrameTime(float deltaTime);

// Added: the current camera's world transform, for the optional motion-blur stage (M9.19,
// presentation only). aMatrix is the camera's model matrix as 12 floats in rdCamera's order:
// right, forward and up basis vectors, then the world position (three floats each). The backend
// keeps it and the previous frame's transform, which is presentation state only. bCut marks a
// camera cut (the current camera changed, the cutscene camera switched on or off, or a level or
// save was just loaded); a cut, a zero game-time step, or a large jump or snap skips the blur and
// resets the previous transform. The first frame after open has no previous transform and does not
// blur. Backends without motion blur (Null, and the replay path) ignore the call, and it records
// nothing in a capture. It changes no simulation state.
void J3DAPI std3D_SetCameraTransform(const float aMatrix[12], bool bCut);

// Added: whether the backend draws real thing shadows this frame (M11.2d, presentation only).
// Returns 1 when it does and the game must skip sithShadow_RenderThingShadow, else 0 and the game
// draws the original blob. The Babylon backend answers 1 only when engine.renderer.shadows is on
// and its shadow map and receive effect opened; the Null backend answers 0. The query has no side
// effects and records nothing in a capture.
int std3D_DrawsThingShadows(void);

// Added: M11.2e. Adds one convex polygon of a shadow-casting model to this frame's shadow casters
// (presentation only). aPositions holds numVertices points as x, y, z floats in rdroid camera space
// (x right, y forward, z up); the polygon is a fan from its first vertex. Only meaningful between
// std3D_StartScene and std3D_EndScene while std3D_DrawsThingShadows() returns 1; otherwise, with fewer
// than 3 vertices, a null pointer or any non-finite coordinate, the whole polygon is ignored. The
// pointer is read during the call only. Records nothing in a capture.
void J3DAPI std3D_AddShadowCasterPolygon(const float* aPositions, size_t numVertices);

// On success 0 is returned
int J3DAPI std3D_SetMipmapFilter(Std3DMipmapFilterType mode);
int J3DAPI std3D_SetProjection(float fov, float nearPlane, float farPlane);
void J3DAPI std3D_EnableFog(int bEnabled, float density);
void J3DAPI std3D_SetFog(float red, float green, float blue, float startDepth, float endDepth);
void std3D_ClearZBuffer(void);

StdDisplayEnvironment* J3DAPI std3D_BuildDisplayEnvironment();
void J3DAPI std3D_FreeDisplayEnvironment(StdDisplayEnvironment* pEnv);

// New funcs
bool std3D_IsShaderSystemActive(void);
bool std3D_IsAnisotropicFilteringSupported(void); // Checks if underlying GAPI supports anisotropic texute filtering. Note, device might still not support it.
bool std3D_IsMipmapAutoGenSupported(void);        // Checks if underlying GAPI supports automatic generation of Mipmaps. Note, device might still not support it.
bool std3D_IsMSAASupported(void);                 // Checks if underlying GAPI supports Multisample anti-aliasing (MSAA). Note, device might still not support it.

void std3D_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // STD_STD3D_H
