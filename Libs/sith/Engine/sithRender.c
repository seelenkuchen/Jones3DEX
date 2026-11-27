#include "sithRender.h"

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdClip.h>
#include <rdroid/Engine/rdKeyframe.h>
#include <rdroid/Engine/rdLight.h>
#include <rdroid/Engine/rdMaterial.h>
#include <rdroid/Engine/rdPuppet.h>
#include <rdroid/Main/rdroid.h>
#include <rdroid/Math/rdMath.h>
#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>
#include <rdroid/Primitives/rdModel3.h>
#include <rdroid/Primitives/rdPrimit3.h>
#include <rdroid/Raster/rdCache.h>
#include <rdroid/Raster/rdFace.h>

#include <sith/AI/sithAIUtil.h>
#include <sith/Cog/sithCog.h>
#include <sith/Devices/sithConsole.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Engine/sithRenderSky.h>
#include <sith/Engine/sithShadow.h>
#include <sith/Gameplay/sithFX.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithMaterial.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithPVS.h>
#include <sith/World/sithSprite.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithVoice.h>
#include <sith/World/sithWorld.h>

#include  <std/General/stdEffect.h>
#include  <std/General/stdFileUtil.h>
#include  <std/General/stdFnames.h>
#include  <std/General/stdMath.h>
#include  <std/General/stdMemory.h>
#include  <std/General/stdUserDir.h>
#include  <std/General/stdUtil.h>
#include  <std/General/std3D.h>
#include  <std/General/stdDisplay.h>

#include <float.h>
#include <math.h>
#include <stdint.h>

// Added: Standalone builds have no retail executable to hold these. *_ResetGlobals sets the starting values.
float sithRender_g_fogDensity;
size_t sithRender_g_numDrawnThings;
size_t sithRender_g_numArchPolys;
size_t sithRender_g_numAlphaArchPolys;
size_t sithRender_g_numThingPolys;
size_t sithRender_g_numAlphaThingPoly;
size_t sithRender_g_numVisibleAdjoins;
size_t sithRender_g_numVisibleSectors;

typedef struct sSithRenderSectorQueueEntry
{
    SithSector* pSector;
    rdClipFrustum* pClipFrustum;
    float distance;
} SithRenderSectorQueueEntry;

// Queue for BFS traversal
typedef struct sSithRenderSectorQueue
{
    size_t head;
    size_t tail;
    SithRenderSectorQueueEntry aEntries[SITHRENDER_MAX_VISIBLE_THING_SECTORS * 4];
} SithRenderSectorQueue;

#define SITHRENDER_MAXTHINGLIGHTS   RDCAMERA_MAX_LIGHTS / 2  // 64; note this var must not exceed RDCAMERA_MAX_LIGHTS - 1 (1 for sector light)
#define SITHRENDER_MAXSECTORLIGHTS  (RDCAMERA_MAX_LIGHTS - SITHRENDER_MAXTHINGLIGHTS)

// There are 2 types of thing light the dynamic light that affect emitting thing and surrounding area,
// and there is flat light that lits only the emitting thing.
#define SITHRENDER_FLATLIGHRANGE          0.01f
#define SITHRENDER_ISDYNAMICLIGHT(range) (range > SITHRENDER_FLATLIGHRANGE)
#define SITHRENDER_ISFLATLIGHT(range )   (range <= SITHRENDER_FLATLIGHRANGE)

#define SITHRENDER_MAXSCREENSHOTS 100u

// Added: option A L3 (ADR-0023). Surfaces whose normals are less than this many degrees apart share a
// smoothed normal at the vertices they share (rdFace_CalcSmoothNormals); wider edges stay hard. The
// threshold is in degrees; every lit draw uses the result (Classic's normal target and per-pixel N.L).
#define SITHRENDER_SMOOTHNORMAL_MAXANGLE 45.0f

// Render state vars
static int sithRender_renderflags;
rdLightMode sithRender_lightMode;

// Sector occlusion related vars
static size_t sithRender_numSectorFrustums;
static rdClipFrustum sithRender_aSectorFrustrums[SITHRENDER_MAX_VISIBLE_SECTORS];

static SithSector* sithRender_aVisibleSectors[SITHRENDER_MAX_VISIBLE_SECTORS];

// PVS vars
static bool sithRender_bPVSCull = true;
static size_t sithRender_curPVSIndex;
static size_t sithRender_lastPVSIndex;
static uint8_t* sithRender_aAdjoinTable;
static SithSurfaceAdjoin* sithRender_aVisibleAdjoins[SITHRENDER_MAX_VISIBLE_SECTORS];

// Visible thing sector(s) related vars
static SithRenderThingTraversal sithRender_culledSectorTraversalMode = SITHRENDER_CULLEDSECTOR_TRAVERSALMODE_DEFAULT; // Added

static size_t sithRender_totalVisibleThingSectors;
static size_t sithRender_numVisibleThingSectors;
static SithSector* sithRender_aVisibleThingSectors[SITHRENDER_MAX_VISIBLE_THING_SECTORS];

static float sithRender_maxThingCollectDistance = SITHRENDER_MAXTHINGCOLLECTDISTANCE_DEFAULT;
static float sithRender_maxLightCollectDistance = SITHRENDER_MAXLIGHTCOLLECTDISTANCE_DEFAULT;

// Queue for BFS traversal
static SithRenderSectorQueue sithRender_visitedSectorQueue = { 0 };

// Rendering vars
bool sithRender_bResetCameraAspect; // Note: Exported for sithDSS
static size_t sithRender_curCamSectorIdx; // Not used for anything particular

// Added: when the Field of view option is active the draw keeps its chosen FOV, but the
// simulation-facing marks (SITH_TF_SEEN, SITH_SECTOR_SEEN, renderFrame) come from a separate
// visibility pass pinned to the default FOV. sithRender_bSuppressSimulationMarks silences the
// draw pass, sithRender_bMarkSimulationOnly makes the thing loop mark without drawing.
static bool sithRender_bMarkSimulationOnly;
static bool sithRender_bSuppressSimulationMarks;

// Added: the motion-blur stage's camera-cut detection (M9.19, presentation only). A level or save
// load starts with a cut, and so does a different current camera or a cutscene camera switching on
// or off. These are read-only copies of render-facing state; nothing reaches the simulation.
static bool sithRender_bMotionBlurCut = true;
static SithCamera* sithRender_pMotionBlurCamera = NULL;
static bool sithRender_bMotionBlurCutscene = false;

static size_t sithRender_numRenderedSectors;

static size_t sithRender_numSectorPointLights;
static rdLight sithRender_aSectorPointLights[SITHRENDER_MAXSECTORLIGHTS];

static size_t sithRender_numAlphaAdjoins;
static SithSurface* sithRender_aAlphaAdjoins[SITHRENDER_MAX_VISIBLE_SECTORS / 2]; // Altered: Was fixed 64; I guess half of 128 visible sectors

// Transformers & Clipping vars
static rdPrimit3 sithRender_clipFaceView;
static rdPrimit3 sithRender_faceView;
static rdVector3 sithRender_aClipVertices[MAX_CLIP_VERTICIES]               = { 0 }; // Added: Init to 0
static rdVector3 sithRender_aTransformedClipVertices[MAX_CLIP_VERTICIES]    = { 0 }; // Added: Init to 0
static rdVector3 sithRender_aSurfaceTransformedVertices[MAX_CLIP_VERTICIES] = { 0 }; // Added: Init to 0

// Added: the lit path's smoothed corner normals of the world surfaces, in world space, made by
// sithRender_Open and freed by sithRender_Close (option A L3, ADR-0023). Surface s's corners start at
// sithRender_aSurfaceFirstCorner[s]. Renderer-side only: not part of the world, savegames or anything
// the simulation reads. NULL when they could not be made; lit surfaces then use their face normal.
static size_t sithRender_numSmoothSurfaces;
static size_t* sithRender_aSurfaceFirstCorner;
static rdVector3* sithRender_aCornerNormals;

// Thing render vars
static int (J3DAPI* sithRender_pExtraThingRenderFunc)(SithThing* pThing);

static size_t sithRender_numSpritesToDraw;

static size_t sithRender_numThingLights;
static rdLight sithRender_aThingLights[SITHRENDER_MAXTHINGLIGHTS];

static void sithRender_Draw(void);

static void J3DAPI sithRender_BuildVisibleSectorList(SithSector* pSector, rdClipFrustum* pFrustrum);
static void J3DAPI sithRender_BuildVisibleSurface(SithSurface* pSurface);
static void J3DAPI sithRender_BuildClipFrustrum(rdClipFrustum* pFrustrum, size_t numVertices, float orthLeft, float orthTop, float orthRight, float orthBottom);
static void J3DAPI sithRender_BuildVisibleSectorListPVS(SithSector* pSector, rdClipFrustum* pClipFrustum);
static void J3DAPI sithRender_BuildVisibleSectorPVS(SithSector* pSector);
static void J3DAPI sithRender_BuildVisibleSector(SithSector* pSector, const rdClipFrustum* pFrustrum);

static void sithRender_RenderSectors(void);

static void sithRender_BuildVisibleThingSectorList(void);
static void sithRender_BuildVisibleThingSectorListDFS(void); // New legacy sithRender_BuildVisibleThingSectorList put into this func
static void J3DAPI sithRender_CollectVisibleThingSector(SithSector* pSector, float curDistance, float startDistance);
static void sithRender_BuildVisibleThingSectorListBFS(void); // New

static inline void sithRender_CollectThingLights(const SithThing* pThing); // New

static void sithRender_BuildDynamicLights(void);
static void sithRender_RenderThings(void);
static int J3DAPI sithRender_RenderThing(SithThing* pThing);
static void sithRender_RenderAlphaAdjoins(void);

// Added: Field of view option support. BeginFrame resets the per-frame render counters,
// SetCameraProjection applies a base FOV (with the underwater wobble), MarkSimulationVisibility
// runs the default-FOV visibility and thing pass for the simulation marks, and MarkThingSeen is
// the shared mark block.
static void sithRender_BeginFrame(void);
static void sithRender_SetCameraProjection(float baseFov);
static void sithRender_MarkSimulationVisibility(void);
static void sithRender_MarkThingSeen(SithThing* pThing);

static void sithRender_BuildSmoothNormals(void); // Added
static void sithRender_FreeSmoothNormals(void); // Added
static void sithRender_ApplySmoothNormals(rdCacheProcEntry* pPoly, const SithWorld* pWorld, const SithSurface* pSurf, const rdMatrix34* pView); // Added
static int sithRender_SurfaceFaceToPlane(rdCacheProcEntry* pPoly, const SithSector* pSector, const SithSurface* pSurf, bool bLit); // Added

void sithRender_ResetGlobals(void)
{
    float jonesConfig_g_fogDensity_tmp = 100.0f;
    memcpy(&sithRender_g_fogDensity, &jonesConfig_g_fogDensity_tmp, sizeof(sithRender_g_fogDensity));

    STD_ZEROMEM(&sithRender_g_numDrawnThings, sizeof(sithRender_g_numDrawnThings));
    STD_ZEROMEM(&sithRender_g_numArchPolys, sizeof(sithRender_g_numArchPolys));
    STD_ZEROMEM(&sithRender_g_numAlphaArchPolys, sizeof(sithRender_g_numAlphaArchPolys));
    STD_ZEROMEM(&sithRender_g_numThingPolys, sizeof(sithRender_g_numThingPolys));
    STD_ZEROMEM(&sithRender_g_numAlphaThingPoly, sizeof(sithRender_g_numAlphaThingPoly));
    STD_ZEROMEM(&sithRender_g_numVisibleAdjoins, sizeof(sithRender_g_numVisibleAdjoins));
    STD_ZEROMEM(&sithRender_g_numVisibleSectors, sizeof(sithRender_g_numVisibleSectors));

    // Added: renderer settings changed during play start at their declared defaults again.
    sithRender_bPVSCull = true;
    sithRender_culledSectorTraversalMode = SITHRENDER_CULLEDSECTOR_TRAVERSALMODE_DEFAULT;
    sithRender_maxThingCollectDistance = SITHRENDER_MAXTHINGCOLLECTDISTANCE_DEFAULT;
    sithRender_maxLightCollectDistance = SITHRENDER_MAXLIGHTCOLLECTDISTANCE_DEFAULT;

    // Added: the motion-blur history starts empty, with the first frame treated as a cut.
    sithRender_bMotionBlurCut      = true;
    sithRender_pMotionBlurCamera   = NULL;
    sithRender_bMotionBlurCutscene = false;
}

int sithRender_Startup(void)
{
    rdMaterial_RegisterLoader(sithMaterial_Load);
    rdModel3_RegisterLoader(sithModel_Load);
    rdKeyframe_RegisterLoader(sithPuppet_LoadKeyframe);

    sithRender_renderflags           = 0;
    sithRender_pExtraThingRenderFunc = NULL;
    sithRender_aAdjoinTable          = NULL;
    return 1;
}

int sithRender_Open(void)
{
    sithRender_lightMode = RD_LIGHTING_GOURAUD;

    for ( size_t i = 0; i < STD_ARRAYLEN(sithRender_aThingLights); ++i )
    {
        rdLight_NewEntry(&sithRender_aThingLights[i]);
    }

    // TODO: Verify if sithRender_aSectorPointLights array also has to be initialized

    if ( sithRenderSky_Open(sithWorld_g_pCurrentWorld->horizonDistance, sithWorld_g_pCurrentWorld->ceilingSkyHeight) )
    {
        RDLOG_ERROR("Unable to Open sithRenderSky.\n"); // TODO: Why using RDLOG
        return 1;
    }

    sithRender_aAdjoinTable = (uint8_t*)STDMALLOC(sithWorld_g_pCurrentWorld->numAdjoins + 1);
    if ( !sithRender_aAdjoinTable )
    {
        RDLOG_ERROR("Unable to Alloc Adjoin Table.\n"); // TODO: Why using RDLOG
        return 1;
    }

    // Added: the lit path's smooth normals. Presentation only: a failure leaves the face normals.
    sithRender_BuildSmoothNormals();

    // Added: a level or save load is a camera cut for the motion-blur stage (M9.19).
    sithRender_bMotionBlurCut      = true;
    sithRender_pMotionBlurCamera   = NULL;
    sithRender_bMotionBlurCutscene = false;

    sithRender_bResetCameraAspect = false;
    return 0;
}

void sithRender_Close(void)
{
    if ( sithRender_aAdjoinTable )
    {
        stdMemory_Free(sithRender_aAdjoinTable);
    }

    sithRender_aAdjoinTable = NULL;
    sithRender_FreeSmoothNormals(); // Added
    sithRenderSky_Close();
}

void sithRender_Shutdown(void)
{
    sithRender_Close();
    sithRender_pExtraThingRenderFunc = NULL;
    sithRender_aAdjoinTable          = NULL;
}

void J3DAPI sithRender_SetRenderFlags(int flags)
{
    sithRender_renderflags = flags;
}

int sithRender_GetRenderFlags(void)
{
    return sithRender_renderflags;
}

void J3DAPI sithRender_SetLightingMode(rdLightMode mode)
{
    sithRender_lightMode = mode;
}

rdLightMode sithRender_GetLightingMode(void)
{
    return sithRender_lightMode;
}

void J3DAPI sithRender_SetCulledSectorTraversalMode(SithRenderThingTraversal mode)
{
    sithRender_culledSectorTraversalMode = mode;
}

SithRenderThingTraversal sithRender_GetCulledSectorTraversalMode(void)
{
    return sithRender_culledSectorTraversalMode;
}

float sithRender_GetMaxThingCollectDistance(void)
{
    return sithRender_maxThingCollectDistance;
}

void J3DAPI sithRender_SetMaxThingCollectDistance(float distance)
{
    sithRender_maxThingCollectDistance = distance;
}

float sithRender_GetMaxLightCollectDistance(void)
{
    return sithRender_maxLightCollectDistance;
}

void J3DAPI sithRender_SetMaxLightCollectDistance(float distance)
{
    sithRender_maxLightCollectDistance = distance;
}

void sithRender_RenderScene(void)
{
    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    sithRenderSky_Update();

    if ( (pWorld->state & SITH_WORLD_STATE_UPDATE_FOG) != 0 )
    {
        std3D_g_fogDensity = sithRender_g_fogDensity / 100.0f;
        std3D_SetFog(pWorld->fog.color.red, pWorld->fog.color.green, pWorld->fog.color.blue, pWorld->fog.startDepth, pWorld->fog.endDepth);
        pWorld->state &= ~SITH_WORLD_STATE_UPDATE_FOG;
    }

    if ( sithTime_IsPaused() )
    {
        stdEffect_SetFadeFactor(1, 0.5f);
    }

    sithRender_Draw();
    stdEffect_SetFadeFactor(0, 1.0f);
    sithVoice_Draw();
    sithConsole_Flush();
}

void J3DAPI sithRender_EnablePVSCull(bool bEnable)
{
    sithRender_bPVSCull = bEnable;
}

void sithRender_TogglePVSCull(void)
{
    sithRender_bPVSCull = !sithRender_bPVSCull;
}

// Added: resets the per-frame collection counters and camera state. Shared by the default-FOV
// simulation mark pass and the draw pass.
static void sithRender_BeginFrame(void)
{
    sithRender_g_numVisibleSectors      = 0;
    sithRender_numVisibleThingSectors   = 0;
    sithRender_numThingLights           = 0;
    sithRender_numSectorFrustums        = 0;
    sithRender_numAlphaAdjoins          = 0;
    sithRender_numSpritesToDraw         = 0;
    sithRender_totalVisibleThingSectors = 0;
    sithRender_numRenderedSectors       = 0;
    sithRender_g_numDrawnThings         = 0;
    sithRender_g_numVisibleAdjoins      = 0;

    rdCamera_ClearLights(rdCamera_g_pCurCamera);

    sithRender_curCamSectorIdx = sithCamera_g_pCurCamera->pSector - sithWorld_g_pCurrentWorld->aSectors; // TODO: ?? redundant
}

// Added: the camera setup the Field of view decoupling needs for both passes. Unlike the
// original block it always applies the projection, because the two passes use different FOVs.
static void sithRender_SetCameraProjection(float baseFov)
{
    SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( (pCamera->pSector->flags & SITH_SECTOR_UNDERWATER) != 0 )
    {
        // Little underwater effect
        float sin, cos;
        float angle = sithTime_GetRenderGameTime() * 70.0f; // Altered: M9.22 interpolated render time
        stdMath_SinCos(angle, &sin, &cos);

        rdCamera_SetFOV(&pCamera->rdCamera, sin * 1.0f + baseFov);

        angle = sithTime_GetRenderGameTime() * 100.0f; // Altered: M9.22 interpolated render time
        stdMath_SinCos(angle, &sin, &cos);

        float aspect = sin / 30.0f + pCamera->aspectRatio;
        rdCamera_SetAspectRatio(&pCamera->rdCamera, aspect);

        sithRender_bResetCameraAspect = true;
    }
    else
    {
        rdCamera_SetFOV(&pCamera->rdCamera, baseFov);
        rdCamera_SetAspectRatio(&pCamera->rdCamera, pCamera->aspectRatio);
        sithRender_bResetCameraAspect = false;
    }
}

// Added: walks visibility and the thing sectors at the default FOV and applies only the
// simulation-facing marks. The caller points the camera at SITHCAMERA_FOVDEFAULT first.
static void sithRender_MarkSimulationVisibility(void)
{
    sithRender_BeginFrame();

    if ( sithRender_bPVSCull && sithWorld_g_pCurrentWorld->aPVS )
    {
        sithRender_BuildVisibleSectorListPVS(sithCamera_g_pCurCamera->pSector, rdCamera_g_pCurCamera->pFrustum);
    }
    else
    {
        sithRender_BuildVisibleSectorList(sithCamera_g_pCurCamera->pSector, rdCamera_g_pCurCamera->pFrustum);
    }

    sithRender_BuildVisibleThingSectorList();

    if ( sithRender_numVisibleThingSectors > 0 )
    {
        bool bSavedMarkOnly = sithRender_bMarkSimulationOnly;
        sithRender_bMarkSimulationOnly = true;
        sithRender_RenderThings();
        sithRender_bMarkSimulationOnly = bSavedMarkOnly;
    }
}

// Added: the mark block sithRender_RenderThing used to contain inline. The mark pass calls it
// without drawing, and the draw pass calls it only when the FOV option is not decoupled.
static void sithRender_MarkThingSeen(SithThing* pThing)
{
    if ( (pThing->flags & SITH_TF_SEEN) == 0 )
    {
        if ( (pThing->flags & SITH_TF_COGLINKED) != 0 )
        {
            sithCog_ThingSendMessage(pThing, 0, SITHCOG_MSG_SIGHTED);
        }

        if ( pThing->controlType == SITH_CT_AI && pThing->controlInfo.aiControl.pLocal )
        {
            pThing->controlInfo.aiControl.pLocal->mode &= ~SITHAI_MODE_SLEEPING;
        }

        pThing->flags |= SITH_TF_SEEN;
    }

    // Set thing cur render frame num
    pThing->renderFrame = sithMain_g_frameNumber;
}

void sithRender_Draw(void)
{
    rdSetLightingMode(sithRender_lightMode);

    rdRoidFlags rdflags = rdGetRenterOptions();
    rdflags |= RDROID_USE_AMBIENT_CAMERA_LIGHT;
    rdSetRenderOptions(rdflags);

    // Altered: with the Field of view option active, the draw keeps its chosen FOV but the
    // simulation-facing marks come from a pass pinned to the default FOV, so SIGHTED messages,
    // AI wake and renderFrame behave exactly as they do at the default. On a fixed tick the marks
    // are already owned by sithRender_UpdateSimulationTick, so the draw must not run them again.
    bool bDecoupleFOV = !sithTime_g_bFixedStep
        && sithCamera_IsGameplayFOV(sithCamera_g_pCurCamera)
        && sithCamera_g_pCurCamera->fov != SITHCAMERA_FOVDEFAULT;

    if ( bDecoupleFOV )
    {
        sithRender_SetCameraProjection(SITHCAMERA_FOVDEFAULT);
        sithRender_MarkSimulationVisibility();

        // The mark pass stamped the sector and vertex caches with this tick; invalidate them so
        // the draw pass rebuilds its own at the chosen FOV.
        sithAdvanceRenderTick();
        sithRender_SetCameraProjection(sithCamera_g_pCurCamera->fov);
    }
    // Set camera
    else if ( (sithCamera_g_pCurCamera->pSector->flags & SITH_SECTOR_UNDERWATER) != 0 )
    {
        // Little underwater effect
        float sin, cos;
        float angle = sithTime_GetRenderGameTime() * 70.0f; // Altered: M9.22 interpolated render time
        stdMath_SinCos(angle, &sin, &cos);

        float fov = sin * 1.0f + sithCamera_g_pCurCamera->fov;
        rdCamera_SetFOV(&sithCamera_g_pCurCamera->rdCamera, fov);

        angle = sithTime_GetRenderGameTime() * 100.0f; // Altered: M9.22 interpolated render time
        stdMath_SinCos(angle, &sin, &cos);

        float aspect = sin / 30.0f + sithCamera_g_pCurCamera->aspectRatio;
        rdCamera_SetAspectRatio(&sithCamera_g_pCurCamera->rdCamera, aspect);

        sithRender_bResetCameraAspect = true;
    }
    else if ( sithRender_bResetCameraAspect )
    {
        rdCamera_SetFOV(&sithCamera_g_pCurCamera->rdCamera, sithCamera_g_pCurCamera->fov);
        rdCamera_SetAspectRatio(&sithCamera_g_pCurCamera->rdCamera, sithCamera_g_pCurCamera->aspectRatio);
        sithRender_bResetCameraAspect = false;
    }

    sithRender_BeginFrame();

    // Altered: M9.20. On a fixed tick the draw only samples the marks the tick already applied.
    bool bSavedSuppressMarks = sithRender_bSuppressSimulationMarks;
    sithRender_bSuppressSimulationMarks = bDecoupleFOV || sithTime_g_bFixedStep;

    // Collect sector and things to draw
    if ( sithRender_bPVSCull && sithWorld_g_pCurrentWorld->aPVS )
    {
        sithRender_BuildVisibleSectorListPVS(sithCamera_g_pCurCamera->pSector, rdCamera_g_pCurCamera->pFrustum);
    }
    else
    {
        sithRender_BuildVisibleSectorList(sithCamera_g_pCurCamera->pSector, rdCamera_g_pCurCamera->pFrustum);
    }

    sithRender_BuildVisibleThingSectorList();
    if ( sithRender_totalVisibleThingSectors > STD_ARRAYLEN(sithRender_aVisibleThingSectors) ) // Note, this was originally prolly put in place to inform level designers of too many visible thing sectors
    {
        RDLOG_ERROR("Too many sectors with things in view %d of %d\n", STD_ARRAYLEN(sithRender_aVisibleThingSectors), sithRender_totalVisibleThingSectors); // TODO: Why using RDLOG
    }

    if ( (sithRender_renderflags & RDROID_USE_AMBIENT_CAMERA_LIGHT) != 0 )
    {
        sithRender_BuildDynamicLights();
    }

    // Added: option A L4 (ADR-0023). With lit faces the renderer gets the frame's light list, the
    // camera's lights as the visibility pass collected them (thing lights and head lights), in view
    // space. Read only: the selection, the CPU lighting and the simulation stay as they are.
    if ( rdGetLitFaces() )
    {
        StdLight aLights[RDCAMERA_MAX_LIGHTS];
        size_t numLights = rdCamera_BuildStdLights(rdCamera_g_pCurCamera, aLights, STD_ARRAYLEN(aLights));
        std3D_SetLights(aLights, numLights);
    }

    // Set projection
    std3D_SetProjection(sithCamera_g_pCurCamera->rdCamera.fov, sithCamera_g_pCurCamera->rdCamera.pFrustum->nearPlane, sithCamera_g_pCurCamera->rdCamera.pFrustum->farPlane);

    // Added: the light-shaft stage's fade needs the frame's game time (M9.16, presentation only).
    // Altered: a paused game reports a zero step. The clock is frozen while paused, so the retained
    // frame time would otherwise keep the light-shaft fade, the water phase and the motion-blur
    // history advancing behind a paused world or a menu.
    std3D_SetFrameTime(sithTime_IsPaused() ? 0.0f : sithTime_g_frameTimeFlex);

    // Added: the depth-of-field stage's per-frame focus input (presentation only). It reads the
    // current camera and its focus thing, and nothing here marks a thing or sector.
    SithThing* pFocusThing = sithCamera_GetRenderFocusThing(sithCamera_g_pCurCamera);
    if ( pFocusThing )
    {
        const float focusDistance = rdVector_Dist3(&sithCamera_g_pCurCamera->lookPos, &pFocusThing->pos);
        std3D_SetFocus(focusDistance, sithCamera_IsCutsceneCamera(sithCamera_g_pCurCamera), sithTime_g_frameTimeFlex);
    }

    // Added: the motion-blur stage's per-frame camera input (M9.19, presentation only). It passes
    // the camera's world transform read-only and marks a camera cut when the current camera
    // changed, the cutscene camera switched on or off, or a level or save was just loaded.
    {
        const bool bCutscene = sithCamera_IsCutsceneCamera(sithCamera_g_pCurCamera);
        const bool bCut = sithRender_bMotionBlurCut
            || sithCamera_g_pCurCamera != sithRender_pMotionBlurCamera
            || bCutscene != sithRender_bMotionBlurCutscene;

        sithRender_bMotionBlurCut      = false;
        sithRender_pMotionBlurCamera   = sithCamera_g_pCurCamera;
        sithRender_bMotionBlurCutscene = bCutscene;

        const float aCameraMatrix[12] = {
            rdCamera_g_camMatrix.rvec.x, rdCamera_g_camMatrix.rvec.y, rdCamera_g_camMatrix.rvec.z,
            rdCamera_g_camMatrix.lvec.x, rdCamera_g_camMatrix.lvec.y, rdCamera_g_camMatrix.lvec.z,
            rdCamera_g_camMatrix.uvec.x, rdCamera_g_camMatrix.uvec.y, rdCamera_g_camMatrix.uvec.z,
            rdCamera_g_camMatrix.dvec.x, rdCamera_g_camMatrix.dvec.y, rdCamera_g_camMatrix.dvec.z };
        std3D_SetCameraTransform(aCameraMatrix, bCut);
    }

    // Now draw everything
    sithRender_RenderSectors();

    if ( sithRender_numVisibleThingSectors > 0 )
    {
        sithRender_RenderThings();
    }

    if ( sithRender_numAlphaAdjoins > 0 )
    {
        sithRender_RenderAlphaAdjoins();
    }

    // Altered: M9.20. The waypoint renderer creates laser things, a simulation mutation. On a
    // fixed tick it runs once per tick (sithRender_UpdateSimulationTick) instead of per draw.
    if ( !sithTime_g_bFixedStep )
    {
        sithAIUtil_RenderAIWaypoints();
    }

    sithRender_bSuppressSimulationMarks = bSavedSuppressMarks;
}

// Added: M9.20 fixed simulation tick (ADR-0039). This is the draw's simulation-facing work moved
// onto the tick. It points the camera at the default FOV for the marks (as the Field of view
// decoupling does), runs the mark-only visibility pass, advances the render cache tick so the draw
// rebuilds at its own projection, creates the AI waypoint lasers and updates the polyline things.
// The caller runs it after sithUpdate and the camera update, once per simulation tick.
void sithRender_UpdateSimulationTick(void)
{
    SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( !pCamera || !sithWorld_g_pCurrentWorld )
    {
        return;
    }

    rdCamera_SetCurrent(&pCamera->rdCamera);

    // Altered: the marks are pinned to the default FOV so a chosen Field of view never changes
    // what the simulation sees, matching the existing decoupled-FOV pass.
    sithRender_SetCameraProjection(SITHCAMERA_FOVDEFAULT);
    rdCamera_Update(&pCamera->orient);
    sithRender_MarkSimulationVisibility();

    // The mark pass stamped the sector and vertex caches at the default FOV; advance so the draw
    // pass rebuilds its own at the chosen projection.
    sithAdvanceRenderTick();

    sithAIUtil_RenderAIWaypoints();

    // Restore the camera's authored projection for the draws between this tick and the next.
    sithRender_SetCameraProjection(pCamera->fov);
    rdCamera_Update(&pCamera->orient);
}

void J3DAPI sithRender_BuildVisibleSectorList(SithSector* pSector, rdClipFrustum* pFrustrum)
{
    if ( pSector->renderTick == sithMain_g_curRenderTick )
    {
        pSector->pClipFrustum = rdCamera_g_pCurCamera->pFrustum;
    }
    else
    {
        sithRender_BuildVisibleSector(pSector, pFrustrum);
    }

    int bBuildingSector           = pSector->bBuildingSector;
    pSector->bBuildingSector      = 1;
    sithRender_faceView.aVertices = sithWorld_g_pCurrentWorld->aTransformedVertices;

    for ( SithSurfaceAdjoin* pAdjoin = pSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
    {
        if ( !pAdjoin->pAdjoinSector->bBuildingSector )
        {
            ++sithRender_g_numVisibleAdjoins;

            SithSurface* pSurface = pAdjoin->pAdjoinSurface;

            rdVector3 lookDir;
            rdVector_Sub3(&lookDir, &sithCamera_g_pCurCamera->lookPos, &sithWorld_g_pCurrentWorld->aVertices[*pSurface->face.aVertices]);
            float dot =  rdVector_Dot3(&pSurface->face.normal, &lookDir);
            if ( dot > 0.0f || (dot == 0.0f && pSector == sithCamera_g_pCurCamera->pSector) )
            {
                sithRender_BuildVisibleSurface(pSurface);

                sithRender_clipFaceView.aVertices  = sithRender_aClipVertices;
                sithRender_faceView.numVertices    = pSurface->face.numVertices;
                sithRender_faceView.aVertIdxs      = pSurface->face.aVertices;
                rdPrimit3_ClipFace(pFrustrum, RD_GEOMETRY_WIREFRAME, RD_LIGHTING_LIT, &sithRender_faceView, &sithRender_clipFaceView, &pSurface->face.texVertOffset);

                if ( (sithRender_clipFaceView.numVertices >= 3 || (rdClip_g_faceStatus & 0x40) != 0)
                    && ((rdClip_g_faceStatus & 0x41) != 0
                        || (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0
                        && (!pSurface->face.pMaterial || pSurface->face.geometryMode == RD_GEOMETRY_NONE
                            || (pSurface->face.flags & RD_FF_TEX_TRANSLUCENT) != 0
                            || pSurface->face.pMaterial->formatType != STDCOLOR_FORMAT_RGB)) )
                {
                    rdCamera_g_pCurCamera->pfProjectList(sithRender_aTransformedClipVertices, sithRender_aClipVertices, sithRender_clipFaceView.numVertices);
                    if ( (rdClip_g_faceStatus & 0x41) != 0 )
                    {
                        sithRender_BuildVisibleSectorList(pAdjoin->pAdjoinSector, pFrustrum);
                    }
                    else
                    {
                        rdClipFrustum frustrum;
                        sithRender_BuildClipFrustrum(&frustrum, sithRender_clipFaceView.numVertices, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX); // OG FLT_MAX was defined as 3.4028235e38f;
                        sithRender_BuildVisibleSectorList(pAdjoin->pAdjoinSector, &frustrum);
                    }
                }
            }
        }
    }

    pSector->bBuildingSector = bBuildingSector;
}

void J3DAPI sithRender_BuildVisibleSurface(SithSurface* pSurface)
{
    if ( pSurface->renderTick != sithMain_g_curRenderTick )
    {
        for ( size_t i = 0; i < pSurface->face.numVertices; ++i )
        {
            int vertIdx = pSurface->face.aVertices[i];
            if ( sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] != sithMain_g_curRenderTick )
            {
                // Transform vertices to view space
                rdMatrix_TransformPoint34(
                    &sithWorld_g_pCurrentWorld->aTransformedVertices[vertIdx],
                    &sithWorld_g_pCurrentWorld->aVertices[vertIdx],
                    &rdCamera_g_pCurCamera->viewMatrix
                );
                sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] = sithMain_g_curRenderTick;
            }
        }

        pSurface->renderTick = sithMain_g_curRenderTick;
    }
}

void J3DAPI sithRender_BuildClipFrustrum(rdClipFrustum* pFrustrum, size_t numVertices, float orthLeft, float orthTop, float orthRight, float orthBottom)
{
    for ( size_t i = 0; i < numVertices; ++i )
    {
        float x = sithRender_aTransformedClipVertices[i].x;
        float y = sithRender_aTransformedClipVertices[i].y;
        if ( orthLeft >= x )
        {
            orthLeft = x;
        }

        if ( orthRight <= x )
        {
            orthRight = x;
        }

        if ( orthTop >= y )
        {
            orthTop = y;
        }

        if ( orthBottom <= y )
        {
            orthBottom = y;
        }
    }

    pFrustrum->orthoLeftPlane   = orthLeft;
    pFrustrum->orthoTopPlane    = orthTop;
    pFrustrum->orthoRightPlane  = orthRight;
    pFrustrum->orthoBottomPlane = orthBottom;

    int bottom = (int)ceilf(orthBottom);
    int right  = (int)ceilf(orthRight);
    int top    = (int)(ceilf(orthTop) + 0.5f);
    int left   = (int)(ceilf(orthLeft) + 0.5f);
    rdCamera_SetFrustrum(rdCamera_g_pCurCamera, pFrustrum, left, top, right, bottom);
}

void J3DAPI sithRender_BuildVisibleSectorListPVS(SithSector* pSector, rdClipFrustum* pClipFrustum)
{
    sithPVS_SetTable(sithRender_aAdjoinTable, &sithWorld_g_pCurrentWorld->aPVS[pSector->pvsIdx], sithWorld_g_pCurrentWorld->numAdjoins);
    STD_ZEROMEM(sithRender_aVisibleAdjoins, sizeof(sithRender_aVisibleAdjoins));

    sithRender_curPVSIndex     = 1;
    sithRender_faceView.aVertices = sithWorld_g_pCurrentWorld->aTransformedVertices;

    uint32_t width, height;
    stdDisplay_GetBackBufferSize(&width, &height);
    pClipFrustum->orthoLeftPlane   = 0.0f;
    pClipFrustum->orthoTopPlane    = 0.0f;
    pClipFrustum->orthoRightPlane  = (float)width;
    pClipFrustum->orthoBottomPlane = (float)height;

    pSector->pClipFrustum = pClipFrustum;
    sithRender_BuildVisibleSectorPVS(pSector);

    while ( sithRender_curPVSIndex <= sithRender_lastPVSIndex )
    {
        SithSurfaceAdjoin* pAdjoin = sithRender_aVisibleAdjoins[sithRender_curPVSIndex];
        sithRender_aVisibleAdjoins[sithRender_curPVSIndex] = NULL;

        while ( pAdjoin )
        {
            ++sithRender_g_numVisibleAdjoins;

            rdClipFrustum* pFrustum = pAdjoin->pAdjoinSurface->pSector->pClipFrustum;
            if ( (sithRender_aAdjoinTable[pAdjoin - sithWorld_g_pCurrentWorld->aAdjoins] & 0x80) != 0 )
            {
                pFrustum = rdCamera_g_pCurCamera->pFrustum;
                pFrustum->orthoLeftPlane   = 0.0f;
                pFrustum->orthoTopPlane    = 0.0f;
                pFrustum->orthoRightPlane  = (float)width;
                pFrustum->orthoBottomPlane = (float)height;
            }

            SithSurface* pSurface = pAdjoin->pAdjoinSurface;

            rdVector3 lookDir;
            rdVector_Sub3(&lookDir, &sithCamera_g_pCurCamera->lookPos, &sithWorld_g_pCurrentWorld->aVertices[*pSurface->face.aVertices]);

            if ( rdVector_Dot3(&pSurface->face.normal, &lookDir) > 0.0f ) // If surface facing camera
            {
                sithRender_BuildVisibleSurface(pSurface);

                sithRender_clipFaceView.aVertices  = sithRender_aClipVertices;
                sithRender_faceView.numVertices    = pSurface->face.numVertices;
                sithRender_faceView.aVertIdxs      = pSurface->face.aVertices;

                rdClip_ClipFacePVS(pFrustum, &sithRender_faceView, &sithRender_clipFaceView);
                //numVertices = sithRender_clipFaceView.numVertices;

                if ( (sithRender_clipFaceView.numVertices || (rdClip_g_faceStatus & 0x01) != 0)
                    && (!pSurface->face.pMaterial || pSurface->face.pMaterial->formatType != STDCOLOR_FORMAT_RGB
                        || (pSurface->face.flags & RD_FF_TEX_TRANSLUCENT) != 0
                        || pSurface->face.geometryMode == RD_GEOMETRY_NONE)
                    && (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 )
                {
                    // Project vertices to camera space
                    rdCamera_g_pCurCamera->pfProjectList(sithRender_aTransformedClipVertices, sithRender_aClipVertices, sithRender_clipFaceView.numVertices);

                    SithSector* pAdjoinSector = pAdjoin->pAdjoinSector;
                    if ( pAdjoinSector->renderTick == sithMain_g_curRenderTick ) // If sector was already build at current frame then  build only it's clip frustum
                    {
                        if ( (rdClip_g_faceStatus & 0x01) != 0 )
                        {
                            sithRender_BuildClipFrustrum(
                                pAdjoinSector->pClipFrustum,
                                sithRender_clipFaceView.numVertices,
                                pFrustum->orthoLeftPlane,
                                pFrustum->orthoTopPlane,
                                pFrustum->orthoRightPlane,
                                pFrustum->orthoBottomPlane
                            );
                        }
                        else
                        {
                            sithRender_BuildClipFrustrum(
                                pAdjoinSector->pClipFrustum,
                                sithRender_clipFaceView.numVertices,
                                pAdjoinSector->pClipFrustum->orthoLeftPlane,
                                pAdjoinSector->pClipFrustum->orthoTopPlane,
                                pAdjoinSector->pClipFrustum->orthoRightPlane,
                                pAdjoinSector->pClipFrustum->orthoBottomPlane
                            );
                        }
                    }
                    else
                    {
                        rdClipFrustum frustum;
                        if ( (rdClip_g_faceStatus & 0x01) != 0 )
                        {
                            pAdjoinSector->pClipFrustum = pFrustum;
                        }
                        else
                        {
                            sithRender_BuildClipFrustrum(&frustum, sithRender_clipFaceView.numVertices, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX); // OG FLT_MAX was defined as 3.4028235e38f;
                            pAdjoinSector->pClipFrustum = &frustum;
                        }

                        sithRender_BuildVisibleSectorPVS(pAdjoinSector);
                    }
                }
            }

            pAdjoin = pAdjoin->pNextVisibleAdjoin;
        }

        if ( !sithRender_aVisibleAdjoins[sithRender_curPVSIndex] )
        {
            ++sithRender_curPVSIndex;
        }
    }
}

void J3DAPI sithRender_BuildVisibleSectorPVS(SithSector* pSector)
{
    // Update PVS stuff
    for ( SithSurfaceAdjoin* pAdjoin = pSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
    {
        size_t adjIdx = pAdjoin - sithWorld_g_pCurrentWorld->aAdjoins;
        if ( (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 && sithRender_aAdjoinTable[adjIdx] )
        {
            size_t idx = sithRender_aAdjoinTable[adjIdx] & ~0xFFFFFF80;
            if ( idx < sithRender_curPVSIndex )
            {
                idx = sithRender_curPVSIndex;
            }

            pAdjoin->pNextVisibleAdjoin = sithRender_aVisibleAdjoins[idx];
            sithRender_aVisibleAdjoins[idx] = pAdjoin;
            if ( sithRender_lastPVSIndex <= idx )
            {
                sithRender_lastPVSIndex = idx;
            }
        }
    }

    // Now build sector
    sithRender_BuildVisibleSector(pSector, pSector->pClipFrustum);
}

void J3DAPI sithRender_BuildVisibleSector(SithSector* pSector, const rdClipFrustum* pFrustrum)
{
    pSector->renderTick = sithMain_g_curRenderTick;
    if ( sithRender_g_numVisibleSectors >= STD_ARRAYLEN(sithRender_aVisibleSectors) )
    {
        RDLOG_WARNING("Max visible sectors exceeded!\n");
        return;
    }

    sithRender_aVisibleSectors[sithRender_g_numVisibleSectors++] = pSector;

    // Altered: the draw pass at the chosen FOV does not mark sectors seen; the default-FOV
    // simulation pass did that already.
    if ( !sithRender_bSuppressSimulationMarks && (pSector->flags & SITH_SECTOR_SEEN) == 0 )
    {
        pSector->flags |= SITH_SECTOR_SEEN;
        if ( (pSector->flags & SITH_SECTOR_COGLINKED) != 0 )
        {
            // Notify sector's cog
            sithCog_SectorSendMessage(pSector, NULL, SITHCOG_MSG_SIGHTED);
        }
    }

    // Assign sectors frustum
    sithRender_aSectorFrustrums[sithRender_numSectorFrustums] = *pFrustrum;
    pSector->pClipFrustum = &sithRender_aSectorFrustrums[sithRender_numSectorFrustums++];

    // Collect thing lights (ambient spot light & actor head light)
    for ( SithThing* pThing = pSector->pFirstThingInSector; pThing && sithRender_numThingLights < STD_ARRAYLEN(sithRender_aThingLights); pThing = pThing->pNextThingInSector )
    {
        if ( (pThing->flags & SITH_TF_EMITLIGHT) != 0 && (pThing->flags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_DESTROYED)) == 0 )
        {
            // Collect Thing's light if light range is > 0.01f
            // Altered: The original tests (r > 0 || g > 0 || b > 0), so a light with only negative or NaN color components is skipped; !IsZero3 collects it.
            if ( !rdVector_IsZero3((rdVector3*)&pThing->light.color)
                && SITHRENDER_ISDYNAMICLIGHT(pThing->light.color.alpha) )
            {
                SITH_ASSERT(!rdVector_IsNAN3((const rdVector3*)&pThing->light.color)); // Debug builds catch NaN components, which !rdVector_IsZero3 lets through
                sithRender_aThingLights[sithRender_numThingLights].color     = pThing->light.color;
                sithRender_aThingLights[sithRender_numThingLights].minRadius = pThing->light.minRadius;
                sithRender_aThingLights[sithRender_numThingLights].maxRadius = pThing->light.maxRadius;

                rdCamera_AddLight(rdCamera_g_pCurCamera, &sithRender_aThingLights[sithRender_numThingLights], &pThing->pos);
                ++sithRender_numThingLights;
            }

            // Collect actor's head light
            // Altered: Same color test difference as for the thing light above (original: any of headLightIntensity r, g, b > 0).
            if ( (pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER)
                && sithRender_numThingLights < STD_ARRAYLEN(sithRender_aThingLights)
                && (pThing->thingInfo.actorInfo.flags & SITH_AF_HEADLIGHT) != 0
                && !rdVector_IsZero3((rdVector3*)&pThing->thingInfo.actorInfo.headLightIntensity) )
            {
                SITH_ASSERT(!rdVector_IsNAN3((const rdVector3*)&pThing->thingInfo.actorInfo.headLightIntensity)); // Debug builds catch NaN components, which !rdVector_IsZero3 lets through
                sithRender_aThingLights[sithRender_numThingLights].color     = pThing->thingInfo.actorInfo.headLightIntensity;
                sithRender_aThingLights[sithRender_numThingLights].minRadius = pThing->thingInfo.actorInfo.headLightIntensity.alpha;
                sithRender_aThingLights[sithRender_numThingLights].maxRadius = pThing->thingInfo.actorInfo.headLightIntensity.alpha;

                rdVector3 lightPos;
                rdMatrix_TransformVector34(&lightPos, &pThing->thingInfo.actorInfo.lightOffset, &pThing->orient); // Fixed: Replaced clall to rdMatrix_TransformPoint34 with rdMatrix_TransformVector34
                rdVector_Add3Acc(&lightPos, &pThing->pos);                                                        //        Fixes light flickering as pThing->orient.dvec might not be zero

                rdCamera_AddLight(rdCamera_g_pCurCamera, &sithRender_aThingLights[sithRender_numThingLights], &lightPos);
                ++sithRender_numThingLights;
            }
        }
    }

    sithRender_aVisibleThingSectors[sithRender_numVisibleThingSectors++] = pSector;
}

void sithRender_RenderSectors(void)
{
    sithRender_g_numArchPolys = 0;

    rdFaceFlags extraFaceFlags = 0;
    if ( sithWorld_g_pCurrentWorld->fog.bEnabled )
    {
        extraFaceFlags = RD_FF_FOG_ENABLED;
    }

    sithRender_faceView.aVertices    = sithWorld_g_pCurrentWorld->aTransformedVertices;
    sithRender_faceView.aTexVertices = sithWorld_g_pCurrentWorld->aTexVerticies;
    sithRender_faceView.aVertLights  = sithWorld_g_pCurrentWorld->aVertDynamicLights;

    for ( size_t secNum = 0; secNum < sithRender_g_numVisibleSectors; ++secNum )
    {
        SithSector* pSector = sithRender_aVisibleSectors[secNum];
        for ( size_t surfNum = 0; surfNum < pSector->numSurfaces; ++surfNum )
        {
            SithSurface* pSurf = &pSector->pFirstSurface[surfNum];
            if ( pSurf->face.geometryMode == RD_GEOMETRY_NONE )
            {
                continue;
            }

            rdVector3 camDir;
            rdVector_Sub3(&camDir, &sithCamera_g_pCurCamera->lookPos, &sithWorld_g_pCurrentWorld->aVertices[*pSurf->face.aVertices]);
            if ( rdVector_Dot3(&pSurf->face.normal, &camDir) <= 0.0f ) // If surface is not facing camera
            {
                continue;
            }

            if ( pSurf->pAdjoin && (pSurf->face.flags & RD_FF_TEX_TRANSLUCENT) != 0 )
            {
                if ( sithRender_numAlphaAdjoins < STD_ARRAYLEN(sithRender_aAlphaAdjoins) )
                {
                    sithRender_aAlphaAdjoins[sithRender_numAlphaAdjoins++] = pSurf;
                }
                // Maybe add log when alpha adjoin couldn't be processed
            }
            else
            {
                // Transform vertices to view space (camera space)
                if ( pSurf->renderTick != sithMain_g_curRenderTick )
                {
                    for ( size_t i = 0; i < pSurf->face.numVertices; ++i )
                    {
                        int vertIdx = pSurf->face.aVertices[i];
                        if ( sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] != sithMain_g_curRenderTick )
                        {
                            rdMatrix_TransformPoint34(
                                &sithWorld_g_pCurrentWorld->aTransformedVertices[vertIdx],
                                &sithWorld_g_pCurrentWorld->aVertices[vertIdx],
                                &rdCamera_g_pCurCamera->viewMatrix
                            );
                            sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] = sithMain_g_curRenderTick;
                        }
                    }

                    pSurf->renderTick = sithMain_g_curRenderTick;
                }

                // Added: with lit faces on, non-sky surfaces carry view-space positions and normals
                bool bSky               = (pSurf->flags & (SITH_SURFACE_CEILINGSKY | SITH_SURFACE_HORIZONSKY)) != 0;
                bool bLit               = !bSky && rdGetLitFaces();
                rdCacheProcEntry* pPoly = bLit ? rdCache_GetLitProcEntry() : rdCache_GetProcEntry(); // Altered: was rdCache_GetProcEntry
                if ( !pPoly )
                {
                    // TODO: Add maybe log
                    continue;
                }

                // Render sky surface
                if ( bSky )
                {
                    // Clip vertices and transform to camera space
                    sithRender_clipFaceView.aVertices  = sithRender_aClipVertices;
                    sithRender_faceView.numVertices    = pSurf->face.numVertices;
                    sithRender_faceView.aVertIdxs      = pSurf->face.aVertices;

                    rdClip_QClipFaceW(pSector->pClipFrustum, &sithRender_faceView, &sithRender_clipFaceView);
                    if ( sithRender_clipFaceView.numVertices < 3 )
                    {
                        continue;
                    }
                    // There are enough clipped vertices to render n-gon,
                    // first let's project them to NDC space
                    rdCamera_g_pCurCamera->pfProjectList(sithRender_aSurfaceTransformedVertices, sithRender_aClipVertices, sithRender_clipFaceView.numVertices);

                    pPoly->flags     = pSurf->face.flags | RD_FF_SKY_SURFACE; // Added: surface tag for the normal target alpha (M9.16)
                    pPoly->pMaterial = pSurf->face.pMaterial;

                    // Now make sky poly from transformed vertices
                    if ( (pSurf->flags & SITH_SURFACE_HORIZONSKY) != 0 )
                    {
                        sithRenderSky_HorizonFaceToPlane(pPoly, &pSurf->face, sithRender_aSurfaceTransformedVertices, sithRender_clipFaceView.numVertices);
                    }
                    else if ( (pSurf->flags & SITH_SURFACE_CEILINGSKY) != 0 )
                    {
                        sithRenderSky_CeilingFaceToPlane(pPoly, &pSurf->face, sithRender_aClipVertices, sithRender_aSurfaceTransformedVertices, sithRender_clipFaceView.numVertices);
                    }

                    pPoly->matCelNum = pSurf->face.matCelNum;
                    rdCache_AddProcFace(sithRender_clipFaceView.numVertices);

                    ++sithRender_g_numArchPolys;
                }
                else // Not a sky surface
                {
                    // Clip vertices and transform to NDC screen space
                    if ( !sithRender_SurfaceFaceToPlane(pPoly, pSector, pSurf, bLit) ) // Altered: was rdClip_FaceToPlane; the lit path also fills the view-space normals
                    {
                        // Face is fully outside frustrum
                        continue;
                    }

                    pPoly->lightingMode = pSurf->face.lightingMode;
                    if ( pPoly->lightingMode >= sithRender_lightMode )
                    {
                        pPoly->lightingMode = sithRender_lightMode;
                    }

                    // TODO: Clamp maybe vector to 0.0f - 1.0f?
                    rdVector_Add4(&pPoly->extraLight, &pSurf->face.extraLight, &pSector->extraLight);

                    // TODO: Also camera ambient light is not set (rdCamera_SetAmbientLight), but the alpha adjoin surfaces has ambient light set to sed.extraLight + sec.ambientLight

                    pPoly->flags = pSurf->face.flags;
                    if ( (pSurf->flags & SITH_SURFACE_CEILINGSKY) != 0 ) // TODO: ???
                    {
                        sithRenderSky_CeilingFaceToPlane(pPoly, &pSurf->face, sithRender_aClipVertices, sithRender_aSurfaceTransformedVertices, pSurf->face.numVertices);
                    }
                    else
                    {
                        pPoly->flags |= extraFaceFlags;
                        // Added: water surfaces carry their tag to the normal target alpha (M9.16).
                        if ( (pSurf->flags & SITH_SURFACE_WATER) != 0 )
                        {
                            pPoly->flags |= RD_FF_WATER_SURFACE;
                        }
                    }

                    pPoly->pMaterial = pSurf->face.pMaterial;
                    pPoly->matCelNum = pSurf->face.matCelNum;
                    rdCache_AddProcFace(pSurf->face.numVertices);

                    ++sithRender_g_numArchPolys;
                }
            }
        }

        ++sithRender_numRenderedSectors;
    }

    rdCache_Flush();
}

void sithRender_BuildVisibleThingSectorList(void)
{
    // Altered: Added new BFS traversal option
    switch ( sithRender_culledSectorTraversalMode )
    {
        case SITHRENDER_THING_TRAVERSAL_BFS:
            sithRender_BuildVisibleThingSectorListBFS();
            break;

        case SITHRENDER_THING_TRAVERSAL_LDFS:
        default: // Legacy custom DFS alog
            sithRender_BuildVisibleThingSectorListDFS();
            break;
    };
}

void sithRender_BuildVisibleThingSectorListDFS(void)
{
    // Function builds a list of sectors with visible things (i.e. thing that should be rendered) and their lights.
    //
    // Algorithm: vanilla DFS-like recursive sector traversal
    //
    // Iterates through previously built visible sector list and recursively traverses adjoins of each visible sector.
    // Sectors are marked as processed on first encounter using a frame tick,
    // which prevents revisiting them through alternative paths.
    //
    // Traversal order depends on adjoin order and accumulated path distance.
    // Sector traversal is distance-limited and may miss sectors that are
    // reachable via shorter or different paths later in the traversal.
    //
    // This behavior is kept for legacy compatibility.

    for ( size_t i = 0; i < sithRender_g_numVisibleSectors; ++i )
    {
        SithSector* pSector = sithRender_aVisibleSectors[i];
        for ( SithSurfaceAdjoin* pAdjoin = pSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
        {
            // Legacy mode prevent crwaling through already processed sector
            if ( pAdjoin->pAdjoinSector->renderTick != sithMain_g_curRenderTick && (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 )
            {
                pAdjoin->pAdjoinSector->pClipFrustum = pSector->pClipFrustum;
                float distance = pAdjoin->distance + pAdjoin->pMirrorAdjoin->distance;
                sithRender_CollectVisibleThingSector(pAdjoin->pAdjoinSector, 0.0f, distance);
            }
        }
    }
}

void J3DAPI sithRender_CollectVisibleThingSector(SithSector* pSector, float curDistance, float startDistance)
{
    // Recursive legacy DFS-like visible thing sector collection function

    if ( pSector->renderTick != sithMain_g_curRenderTick )
    {
        pSector->renderTick = sithMain_g_curRenderTick;

        if ( pSector->pFirstThingInSector )
        {
            // Added: Added check for new light collection distance
            if ( curDistance < sithRender_maxLightCollectDistance )
            {
                for ( SithThing* pThing = pSector->pFirstThingInSector; pThing; pThing = pThing->pNextThingInSector )
                {
                    // Altered: Replaced inplace code with the new function
                    sithRender_CollectThingLights(pThing);
                }
            }

            if ( curDistance < sithRender_maxThingCollectDistance ) // Altered: Replaced with new max thing collection distance
            {
                ++sithRender_totalVisibleThingSectors;
                if ( sithRender_numVisibleThingSectors < SITHRENDER_MAX_VISIBLE_THING_SECTORS )
                {
                    sithRender_aVisibleThingSectors[sithRender_numVisibleThingSectors++] = pSector;
                }
            }
        }

        for ( SithSurfaceAdjoin* pAdjoin = pSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
        {
            if ( (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 && pAdjoin->pAdjoinSector->renderTick != sithMain_g_curRenderTick )
            {
                float distance = startDistance + curDistance + pAdjoin->distance + pAdjoin->pMirrorAdjoin->distance;
                if ( distance < J3DMAX(sithRender_maxThingCollectDistance, sithRender_maxLightCollectDistance) ) // Altered: Replaced with new max collection distance
                {
                    pAdjoin->pAdjoinSector->pClipFrustum = pSector->pClipFrustum;
                    sithRender_CollectVisibleThingSector(pAdjoin->pAdjoinSector, distance, 0.0f);
                }
            }
        }
    }
}

void sithRender_BuildVisibleThingSectorListBFS(void)
{
    // Function builds a list of sectors with visible things (i.e. thing that should be rendered) and their lights.
    //
    // Algorithm: Breadth-first sector traversal (BFS)
    //
    // Iterates through previously built visible sector list and traverses all adjoins of each sector level-by-level
    // using a queue. Multiple paths may reach the same sector, but traversal
    // is bounded by configured collection distances.
    //
    // Ensures all reachable sectors within range are processed regardless of
    // traversal order, preventing missed thing or dynamic light collection
    // caused by path-order dependency like in case of sithRender_BuildVisibleThingSectorListDFS function. 

    sithRender_visitedSectorQueue.head = 0;
    sithRender_visitedSectorQueue.tail = 0;

    float maxDistance = J3DMAX(sithRender_maxThingCollectDistance, sithRender_maxLightCollectDistance);

    // Initialize queue with already collected visible sectors
    for ( size_t i = 0; i < sithRender_g_numVisibleSectors; ++i )
    {
        SithSector* pSector = sithRender_aVisibleSectors[i];
        for ( SithSurfaceAdjoin* pAdjoin = pSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
        {
            SithSector* pNextSector = pAdjoin->pAdjoinSector;
            if ( (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 && pNextSector->renderTick != sithMain_g_curRenderTick )
            {
                float distance = pAdjoin->distance + pAdjoin->pMirrorAdjoin->distance;
                if ( distance < maxDistance && sithRender_visitedSectorQueue.tail < STD_ARRAYLEN(sithRender_visitedSectorQueue.aEntries) )
                {
                    sithRender_visitedSectorQueue.aEntries[sithRender_visitedSectorQueue.tail++] = (SithRenderSectorQueueEntry){
                          .pSector      = pNextSector,
                          .pClipFrustum = pSector->pClipFrustum,
                          .distance     = distance
                    };
                }
            }
        }
    }

    // Process queue
    while ( sithRender_visitedSectorQueue.head < sithRender_visitedSectorQueue.tail )
    {
        SithRenderSectorQueueEntry* pEntry = &sithRender_visitedSectorQueue.aEntries[sithRender_visitedSectorQueue.head++];
        SithSector* pCurSector = pEntry->pSector;
        float curDistance      = pEntry->distance;

        // Collect things and lights only once per sector
        if ( pCurSector->renderTick != sithMain_g_curRenderTick )
        {
            pCurSector->pClipFrustum = pEntry->pClipFrustum;
            pCurSector->renderTick = sithMain_g_curRenderTick;

            // Collect lights
            if ( curDistance < sithRender_maxLightCollectDistance )
            {
                for ( SithThing* pThing = pCurSector->pFirstThingInSector; pThing; pThing = pThing->pNextThingInSector )
                {
                    sithRender_CollectThingLights(pThing);
                }
            }

            // Collect things
            if ( curDistance < sithRender_maxThingCollectDistance )
            {
                ++sithRender_totalVisibleThingSectors;
                if ( sithRender_numVisibleThingSectors < SITHRENDER_MAX_VISIBLE_THING_SECTORS )
                {
                    sithRender_aVisibleThingSectors[sithRender_numVisibleThingSectors++] = pCurSector;
                }
            }

            // Add adjoining sectors to queue
            for ( SithSurfaceAdjoin* pAdjoin = pCurSector->pFirstAdjoin; pAdjoin; pAdjoin = pAdjoin->pNextAdjoin )
            {
                // Only add if adjoin is visible and it's sector is not already processed
                SithSector* pNextSector = pAdjoin->pAdjoinSector;
                if ( (pAdjoin->flags & SITH_ADJOIN_VISIBLE) != 0 && pNextSector->renderTick != sithMain_g_curRenderTick )
                {
                    float distance = curDistance + pAdjoin->distance + pAdjoin->pMirrorAdjoin->distance;
                    if ( distance < maxDistance && sithRender_visitedSectorQueue.tail < STD_ARRAYLEN(sithRender_visitedSectorQueue.aEntries) )
                    {
                        sithRender_visitedSectorQueue.aEntries[sithRender_visitedSectorQueue.tail++] = (SithRenderSectorQueueEntry){
                           .pSector      = pNextSector,
                           .pClipFrustum = pCurSector->pClipFrustum,
                           .distance     = distance
                        };
                    }
                }
            }
        }
    }
}

void sithRender_CollectThingLights(const SithThing* pThing)
{
    // Collect thing spot light & actor head light

    if ( sithRender_numThingLights < STD_ARRAYLEN(sithRender_aThingLights)
        && (pThing->flags & SITH_TF_EMITLIGHT) != 0
        && (pThing->flags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_DESTROYED)) == 0 ) // Fixed: Added check for SITH_TF_INVISIBLE flag, 
                                                                                               //        OG this flag was checked only in sithRender_BuildVisibleSector
                                                                                               //        but not in case of sithRender_BuildVisibleThingSector
    {
        // Collect thing light if range is > 0.01f
        // Altered: The original (sithRender_BuildSectorThingList) tests `r > 0 || g > 0 || b > 0 && SITHRENDER_ISDYNAMICLIGHT(alpha)`, where && binds tighter than ||.
        //          So a light with r > 0 or g > 0 was collected as dynamic regardless of its range, and negative or NaN components
        //          were skipped. Here the range test always applies and any nonzero color passes.
        if ( !rdVector_IsZero3((const rdVector3*)&pThing->light.color)
            && SITHRENDER_ISDYNAMICLIGHT(pThing->light.color.alpha) )
        {
            SITH_ASSERT(!rdVector_IsNAN3((const rdVector3*)&pThing->light.color)); // Debug builds catch NaN components, which !rdVector_IsZero3 lets through
            sithRender_aThingLights[sithRender_numThingLights].color     = pThing->light.color;
            sithRender_aThingLights[sithRender_numThingLights].minRadius = pThing->light.minRadius;
            sithRender_aThingLights[sithRender_numThingLights].maxRadius = pThing->light.maxRadius;

            rdCamera_AddLight(rdCamera_g_pCurCamera, &sithRender_aThingLights[sithRender_numThingLights], &pThing->pos);
            ++sithRender_numThingLights;
        }

        // Collect actor head light
        // Altered: Same color test difference as for the thing light above (original: any of headLightIntensity x, y, z > 0).
        if ( (pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER)
            && sithRender_numThingLights < STD_ARRAYLEN(sithRender_aThingLights)
            && (pThing->thingInfo.actorInfo.flags & SITH_AF_HEADLIGHT) != 0
            && !rdVector_IsZero3((const rdVector3*)&pThing->thingInfo.actorInfo.headLightIntensity) )
        {
            SITH_ASSERT(!rdVector_IsNAN3((const rdVector3*)&pThing->thingInfo.actorInfo.headLightIntensity)); // Debug builds catch NaN components, which !rdVector_IsZero3 lets through
            sithRender_aThingLights[sithRender_numThingLights].color     = pThing->thingInfo.actorInfo.headLightIntensity;
            sithRender_aThingLights[sithRender_numThingLights].minRadius = pThing->thingInfo.actorInfo.headLightIntensity.alpha; // Fixed: Replaced assigning pThing->light.minRadius with headLightIntensity.alpha to match code in sithRender_BuildVisibleSector
            sithRender_aThingLights[sithRender_numThingLights].maxRadius = pThing->thingInfo.actorInfo.headLightIntensity.alpha; // Fixed: Replaced assigning pThing->light.maxRadius with headLightIntensity.alpha to match code in sithRender_BuildVisibleSector

            rdVector3 lightPos;
            rdMatrix_TransformVector34(&lightPos, &pThing->thingInfo.actorInfo.lightOffset, &pThing->orient); // Fixed: Replaced clall to rdMatrix_TransformPoint34 with rdMatrix_TransformVector34
            rdVector_Add3Acc(&lightPos, &pThing->pos);                                                        //        Fixes light flickering as pThing->orient.dvec might not be zero

            rdCamera_AddLight(rdCamera_g_pCurCamera, &sithRender_aThingLights[sithRender_numThingLights], &lightPos);
            ++sithRender_numThingLights;
        }
    }
}

void sithRender_BuildDynamicLights(void)
{
    // Function calculates vertex intensities from collected dynamic lights for all visible sectors

    rdVector3* aVertices             = sithWorld_g_pCurrentWorld->aVertices;
    rdVector4* aVertDynamicLights    = sithWorld_g_pCurrentWorld->aVertDynamicLights;
    uint32_t* aVertLightsRenderTicks = sithWorld_g_pCurrentWorld->aVertDynamicLightsRenderTicks;
    float attenuationMax             = rdCamera_g_pCurCamera->attenuationMax;

    rdLight* aLights[RDCAMERA_MAX_LIGHTS];

    for ( size_t secNum = 0; secNum < sithRender_g_numVisibleSectors; ++secNum )
    {
        SithSector* pSector = sithRender_aVisibleSectors[secNum];
        size_t numLights    = 0;

        for ( size_t lightNum = 0; lightNum < rdCamera_g_pCurCamera->numLights; ++lightNum )
        {
            rdVector3 lightDir;
            rdVector_Sub3(&lightDir, &rdCamera_g_pCurCamera->aLightPositions[lightNum], &pSector->center);

            rdLight* pLight  = rdCamera_g_pCurCamera->aLights[lightNum];
            if ( (pSector->radius + pLight->minRadius) > rdVector_Len3(&lightDir) )
            {
                aLights[numLights++] = pLight;
            }
        }

        for ( size_t i = 0; i < pSector->numVertices; ++i )
        {
            int vertIdx = pSector->aVertIdxs[i];
            if ( aVertLightsRenderTicks[vertIdx] != sithMain_g_curRenderTick )
            {
                // Set default light color to black
                rdVector_Set4(&aVertDynamicLights[vertIdx], 0.0f, 0.0f, 0.0f, 0.0f);

                for ( size_t lightNum = 0; lightNum < numLights; ++lightNum )
                {
                    rdLight* pLight = aLights[lightNum];

                    rdVector3 lightDir;
                    rdVector_Sub3(&lightDir, &rdCamera_g_pCurCamera->aLightPositions[pLight->num], &aVertices[vertIdx]);

                    float dist = rdVector_Len3(&lightDir);
                    if ( dist < (double)pLight->maxRadius )
                    {
                        float att = dist * attenuationMax;
                        aVertDynamicLights[vertIdx].red   += pLight->color.red - att;
                        aVertDynamicLights[vertIdx].green += pLight->color.green - att;
                        aVertDynamicLights[vertIdx].blue  += pLight->color.blue - att;
                    }

                    rdMath_ClampVector3Acc((rdVector3*)&aVertDynamicLights[vertIdx], 0.0f, 1.0f);
                    // TODO: alpha value is not set
                }

                aVertLightsRenderTicks[vertIdx] = sithMain_g_curRenderTick;
            }
        }
    }
}

void sithRender_RenderThings(void)
{
    bool bLightSet = false;
    sithRender_g_numThingPolys     = 0;
    sithRender_g_numAlphaThingPoly = 0;

    if ( !sithRender_bMarkSimulationOnly )
    {
        rdModel3_EnableFogRendering(sithWorld_g_pCurrentWorld->fog.bEnabled);
    }

    for ( size_t i = 0; i < sithRender_numVisibleThingSectors; ++i )
    {
        SithSector* pSector = sithRender_aVisibleThingSectors[i];

        rdVector4 sectorAmbientLight;
        rdVector_Add4(&sectorAmbientLight, &pSector->ambientLight, &pSector->extraLight);
        rdMath_ClampVector4Acc(&sectorAmbientLight, 0.0f, 1.0f);
        sectorAmbientLight.alpha = 0.0f;

        for ( SithThing* pCurThing = pSector->pFirstThingInSector; pCurThing; pCurThing = pCurThing->pNextThingInSector )
        {
            if ( (pCurThing->flags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_DESTROYED)) == 0
                && ((sithCamera_g_pCurCamera->type & (SITHCAMERA_ORBITAL | SITHCAMERA_UNKNOWN_40 | SITHCAMERA_IDLE | SITHCAMERA_UNKNOWN_10 | SITHCAMERA_CINEMATIC | SITHCAMERA_EXTERNAL)) != 0
                    || pCurThing != sithCamera_g_pCurCamera->pPrimaryFocusThing) )
            {
                // Transform thing pos to view (camera) space
                rdMatrix_TransformPoint34(&pCurThing->cameraSpacePos, &pCurThing->pos, &rdCamera_g_pCurCamera->viewMatrix);

                rdThing* prdThing = &pCurThing->renderData;
                float radius = 0.0f; // Added: Init to 0;

                switch ( pCurThing->renderData.type )
                {
                    case RD_THING_MODEL3:
                    {
                        if ( pCurThing->collide.movesize >= pCurThing->collide.size )
                        {
                            radius = pCurThing->collide.movesize;
                        }
                        else
                        {
                            radius = pCurThing->collide.size;
                        }

                        if ( prdThing->data.pModel3->size >= radius )
                        {
                            radius = prdThing->data.pModel3->size;
                        }

                        // Set sector point light
                        //
                        // TODO [bug]: sithRender_numSectorPointLights is never incremented, but changing this breaks thin illumination as the light will affect also things from other sectors
                        //             Maybe just a temp var for light can be used instead and we can then increase limitation of max thing light?
                        if ( !sithRender_bMarkSimulationOnly )
                        {
                            sithRender_aSectorPointLights[sithRender_numSectorPointLights].color     = pSector->light.color;
                            sithRender_aSectorPointLights[sithRender_numSectorPointLights].minRadius = pSector->light.minRadius;
                            sithRender_aSectorPointLights[sithRender_numSectorPointLights].maxRadius = pSector->light.maxRadius;

                            const size_t numLightsBefore = rdCamera_g_pCurCamera->numLights;
                            rdCamera_AddLight(rdCamera_g_pCurCamera, &sithRender_aSectorPointLights[sithRender_numSectorPointLights], &pSector->light.pos);
                            rdModel3_SetSectorLightNum(rdCamera_g_pCurCamera->numLights > numLightsBefore
                                ? (int)rdCamera_g_pCurCamera->numLights - 1 : -1); // Added: M10.2 per-pixel keeps this light in the model base
                            bLightSet = true;
                        }
                    }break;

                    case RD_THING_SPRITE3:
                        radius = prdThing->data.pSprite3->radius;
                        ++sithRender_numSpritesToDraw;
                        break;

                    case RD_THING_PARTICLE:
                        radius = prdThing->data.pParticle->radius;
                        break;

                    case RD_THING_POLYLINE:
                        radius = prdThing->data.pPolyline->length;
                        break;

                    default:
                        break;
                }

                bool bRender = false;
                if ( prdThing->type == RD_THING_POLYLINE )
                {
                    bRender = true;
                }
                else
                {
                    // Render only if thing is inside frustum or intersects with frustum
                    prdThing->frustumCull = rdClip_SphereInFrustrum(pSector->pClipFrustum, &pCurThing->cameraSpacePos, radius);
                    if ( prdThing->frustumCull != RDFRUSTUMCULL_OUTSIDE )
                    {
                        bRender = true;
                    }
                }

                if ( bRender )
                {
                    if ( !sithRender_bMarkSimulationOnly )
                    {
                        // Collect thing flat light
                        // Altered: The original tests any of light.color r, g, b > 0 here; !IsZero3 also passes negative or NaN components.
                        if ( (pCurThing->flags & SITH_TF_EMITLIGHT) != 0
                            && !rdVector_IsZero3((rdVector3*)&pCurThing->light.color)
                            && SITHRENDER_ISFLATLIGHT(pCurThing->light.color.alpha) )// if light range is <= 0.01f
                        {
                            SITH_ASSERT(!rdVector_IsNAN3((const rdVector3*)&pCurThing->light.color)); // Debug builds catch NaN components, which !rdVector_IsZero3 lets through
                            rdVector4 ambientLight;
                            rdVector_Add4(&ambientLight, &sectorAmbientLight, &pCurThing->light.color);
                            rdMath_ClampVector4Acc(&ambientLight, 0.0f, 1.0f);
                            ambientLight.alpha = sectorAmbientLight.alpha;

                            rdCamera_SetAmbientLight(rdCamera_g_pCurCamera, &ambientLight);
                        }
                        else
                        {
                            rdCamera_SetAmbientLight(rdCamera_g_pCurCamera, &sectorAmbientLight);
                        }
                    }

                    // Now draw thing
                    if ( sithRender_RenderThing(pCurThing) )
                    {
                        if ( !sithRender_bMarkSimulationOnly )
                        {
                            ++sithRender_g_numDrawnThings;
                        }
                    }
                }

                if ( bLightSet == 1 )
                {
                    --rdCamera_g_pCurCamera->numLights;
                    rdModel3_SetSectorLightNum(-1); // Added: M10.2 the sector light is no longer in the list
                    bLightSet = false;
                }
            }
        }
    }

    if ( !sithRender_bMarkSimulationOnly )
    {
        rdModel3_EnableFogRendering(0);
        rdCache_Flush();
    }
}

int J3DAPI sithRender_RenderThing(SithThing* pThing)
{
    RD_ASSERTREL(pThing != NULL); // TODO: Why using RD_ASSERTREL

    // Altered: the default-FOV simulation pass marks without drawing.
    if ( sithRender_bMarkSimulationOnly )
    {
        sithRender_MarkThingSeen(pThing);

        // Added: M9.20. The draw used to destroy an out-of-water air-destroyed sprite; on a fixed
        // tick the mark pass owns that check so draw-only frames cannot destroy things.
        if ( sithTime_g_bFixedStep && pThing->type == SITH_THING_SPRITE
            && sithSprite_DestroyIfAirDestroyed(pThing) )
        {
            return 0;
        }

        // Added: M9.20. The polyline update used to run in sithThing_Draw; it consumes SITH_RAND
        // and can destroy things, so on a fixed tick the mark pass owns it and draw-only frames
        // leave it alone. Reset first so the tick's jitter replaces the previous tick's instead of
        // accumulating; the draws between ticks then sample one tick-owned offset.
        if ( sithTime_g_bFixedStep && pThing->type == SITH_THING_POLYLINE )
        {
            sithFX_ResetPolylineTexVertOffset(pThing);
            sithFX_UpdatePolyline(pThing);
        }

        return 1;
    }

    // Altered: the draw pass at the chosen FOV leaves the simulation marks to that pass.
    if ( !sithRender_bSuppressSimulationMarks )
    {
        sithRender_MarkThingSeen(pThing);
    }

    // Added: M11.2c. A thing that gets the blob shadow is also a shadow caster for the Babylon
    // adapter: mark every face of its model, then clear the mark before the blob and any other draw.
    // The flag is inert unless the adapter reads it, so both profiles set it.
    rdModel3_SetNextDrawFaceFlags((pThing->flags & SITH_TF_SHADOW) != 0 ? RD_FF_SHADOW_CASTER : 0);

    // Now draw thing
    pThing->orient.dvec = pThing->pos;
    int drawResult = sithThing_Draw(pThing);
    rdModel3_SetNextDrawFaceFlags(0);

    // Altered: M11.2d. When the platform draws real thing shadows from the overhead light, the
    // original blob shadow is not drawn; the blob stays in Retail/QOL-off and where shadows failed.
    if ( (pThing->flags & SITH_TF_SHADOW) != 0 && !std3D_DrawsThingShadows() ) // draw shadow
    {
        sithShadow_RenderThingShadow(pThing);
    }

    sithRender_g_numThingPolys     += rdModel3_g_numDrawnFaces;
    sithRender_g_numAlphaThingPoly += rdModel3_g_numDrawnAlphaFaces;

    // Clear cur dvec aka position
    pThing->orient.dvec = rdroid_g_zeroVector3;

    // Extra draw func
    if ( sithRender_pExtraThingRenderFunc && (pThing->flags & SITH_TF_UNKNOWN_200000) != 0 )
    {
        sithRender_pExtraThingRenderFunc(pThing);
    }

    if ( pThing->type != SITH_THING_EXPLOSION || (pThing->thingInfo.explosionInfo.flags & SITH_EF_BLINDPLAYER) == 0 )
    {
        return drawResult;
    }

    // Note: Looks like some part of the code is missing.
    // In JKDF2 (OpenJKDF2), here is section of code that adds light flashing effect to player view
    stdMath_Dist3D1(pThing->cameraSpacePos.x, pThing->cameraSpacePos.y, pThing->cameraSpacePos.z);

    pThing->thingInfo.explosionInfo.flags &= ~SITH_EF_BLINDPLAYER;// 0x100 - SITH_EF_BLINDPLAYER
    return drawResult;
}

void sithRender_RenderAlphaAdjoins(void)
{
    sithRender_g_numAlphaArchPolys = 0;

    rdFaceFlags extraFaceFlags = 0;
    if ( sithWorld_g_pCurrentWorld->fog.bEnabled )
    {
        extraFaceFlags = RD_FF_FOG_ENABLED;
    }

    bool bFlush = false;
    for ( size_t surfNum = 0; surfNum < sithRender_numAlphaAdjoins; ++surfNum )
    {
        SithSurface* pSurf  = sithRender_aAlphaAdjoins[surfNum];
        SithSector* pSector = pSurf->pSector;

        rdVector4 ambientLight = pSector->ambientLight; // TODO: Why the ambient light is applied? For normal surfs that is not the case
        rdVector_Add4Acc(&ambientLight, &pSector->extraLight);
        ambientLight.alpha = 0.0f;
        // TOCO: clamp vector to 0.0 - 1.0f?

        rdCamera_SetAmbientLight(rdCamera_g_pCurCamera, &ambientLight);

        // Transform vertices to view (camera) space
        if ( pSurf->renderTick != sithMain_g_curRenderTick )
        {
            for ( size_t vertNum = 0; vertNum < pSurf->face.numVertices; ++vertNum )
            {
                int vertIdx = pSurf->face.aVertices[vertNum];
                if ( sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] != sithMain_g_curRenderTick )
                {
                    rdMatrix_TransformPoint34(
                        &sithWorld_g_pCurrentWorld->aTransformedVertices[vertIdx],
                        &sithWorld_g_pCurrentWorld->aVertices[vertIdx],
                        &rdCamera_g_pCurCamera->viewMatrix
                    );
                    sithWorld_g_pCurrentWorld->aVertexRenderTickIds[vertIdx] = sithMain_g_curRenderTick;
                }
            }

            pSurf->renderTick = sithMain_g_curRenderTick;
        }

        bool bLit               = rdGetLitFaces(); // Added
        rdCacheProcEntry* pPoly = bLit ? rdCache_GetLitAlphaProcEntry() : rdCache_GetAlphaProcEntry(); // Altered: was rdCache_GetAlphaProcEntry
        if ( !pPoly )
        {
            // TODO: Maybe add log
            continue;
        }

        pPoly->lightingMode = pSurf->face.lightingMode >= sithRender_lightMode ? sithRender_lightMode : pSurf->face.lightingMode;

        if ( !sithRender_SurfaceFaceToPlane(pPoly, pSector, pSurf, bLit) ) // Altered: was rdClip_FaceToPlane
        {
            // Face is fully outside frustrum
            continue;
        }

        rdVector_Add4(&pPoly->extraLight, &pSurf->face.extraLight, &pSector->extraLight);
        // TODO: Maybe clamp [0.0,1.0]

        pPoly->matCelNum = pSurf->face.matCelNum;
        pPoly->flags     = extraFaceFlags | pSurf->face.flags;
        // Added: sky and water surface tags for the normal target alpha (M9.16).
        if ( (pSurf->flags & (SITH_SURFACE_CEILINGSKY | SITH_SURFACE_HORIZONSKY)) != 0 )
        {
            pPoly->flags |= RD_FF_SKY_SURFACE;
        }
        if ( (pSurf->flags & SITH_SURFACE_WATER) != 0 )
        {
            pPoly->flags |= RD_FF_WATER_SURFACE;
        }
        pPoly->pMaterial = pSurf->face.pMaterial;

        rdCache_AddAlphaProcFace(pSurf->face.numVertices);

        ++sithRender_g_numAlphaArchPolys;
        bFlush = true;
    }

    if ( bFlush )
    {
        rdCache_FlushAlpha();
    }
}

void sithRender_BuildSmoothNormals(void)
{
    // Added: option A L3 (ADR-0023). Levels store a face normal per surface only; the lit path's
    // vertex normals are the surfaces' corners smoothed across edges under
    // SITHRENDER_SMOOTHNORMAL_MAXANGLE. Sky surfaces are drawn classic and never smooth a
    // neighbour. Only renderer-side memory is touched: no world, thing or RNG state.
    sithRender_FreeSmoothNormals();

    const SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !pWorld || !pWorld->aSurfaces || pWorld->numSurfaces == 0 )
    {
        return;
    }

    // Added: counts come from the level data, so reject values the allocations below cannot hold.
    if ( pWorld->numSurfaces > SIZE_MAX / sizeof(const rdFace*)
        || pWorld->numSurfaces > SIZE_MAX / sizeof(bool)
        || pWorld->numSurfaces > SIZE_MAX / sizeof(size_t) )
    {
        SITHLOG_WARNING("sithRender: the surface count is too large for the smooth normals.\n");
        return;
    }

    size_t numCorners = 0;
    for ( size_t i = 0; i < pWorld->numSurfaces; ++i )
    {
        const size_t numVertices = pWorld->aSurfaces[i].face.numVertices;
        if ( numVertices > SIZE_MAX - numCorners )
        {
            SITHLOG_WARNING("sithRender: the surface vertex count overflows the smooth normals.\n");
            return;
        }

        numCorners += numVertices;
    }

    if ( numCorners > SIZE_MAX / sizeof(rdVector3) )
    {
        SITHLOG_WARNING("sithRender: the corner count is too large for the smooth normals.\n");
        return;
    }

    const rdFace** apFaces = (const rdFace**)STDMALLOC(sizeof(const rdFace*) * pWorld->numSurfaces);
    bool* abSky            = (bool*)STDMALLOC(sizeof(bool) * pWorld->numSurfaces);
    sithRender_aSurfaceFirstCorner = (size_t*)STDMALLOC(sizeof(size_t) * pWorld->numSurfaces);
    sithRender_aCornerNormals      = (rdVector3*)STDMALLOC(sizeof(rdVector3) * (numCorners ? numCorners : 1u));

    bool bBuilt = false;
    if ( apFaces && abSky && sithRender_aSurfaceFirstCorner && sithRender_aCornerNormals )
    {
        for ( size_t i = 0; i < pWorld->numSurfaces; ++i )
        {
            apFaces[i] = &pWorld->aSurfaces[i].face;
            abSky[i]   = (pWorld->aSurfaces[i].flags & (SITH_SURFACE_CEILINGSKY | SITH_SURFACE_HORIZONSKY)) != 0;
        }

        bBuilt = rdFace_CalcSmoothNormals(apFaces, abSky, pWorld->numSurfaces, pWorld->numVertices, SITHRENDER_SMOOTHNORMAL_MAXANGLE, sithRender_aSurfaceFirstCorner, sithRender_aCornerNormals);
    }

    if ( apFaces )
    {
        stdMemory_Free((void*)apFaces);
    }

    if ( abSky )
    {
        stdMemory_Free(abSky);
    }

    if ( !bBuilt )
    {
        SITHLOG_WARNING("sithRender: could not make the smooth surface normals; lit surfaces use their face normals.\n");
        sithRender_FreeSmoothNormals();
        return;
    }

    sithRender_numSmoothSurfaces = pWorld->numSurfaces;
}

void sithRender_FreeSmoothNormals(void)
{
    // Added
    if ( sithRender_aSurfaceFirstCorner )
    {
        stdMemory_Free(sithRender_aSurfaceFirstCorner);
    }

    if ( sithRender_aCornerNormals )
    {
        stdMemory_Free(sithRender_aCornerNormals);
    }

    sithRender_aSurfaceFirstCorner = NULL;
    sithRender_aCornerNormals      = NULL;
    sithRender_numSmoothSurfaces   = 0;
}

static void sithRender_ApplySmoothNormals(rdCacheProcEntry* pPoly, const SithWorld* pWorld, const SithSurface* pSurf, const rdMatrix34* pView)
{
    // Added: the smoothed level normals replace the face normal the clip call wrote, in Classic (the
    // normal target) and per-pixel lighting (N.L) alike. rdClip_FillViewVectors indexes aVertNormals by world vertex, but a world vertex has one smoothed
    // normal per surface; the corner array is added after the clip call, which keeps faces unclipped
    // and its vertices in face order.
    if ( !sithRender_aCornerNormals
        || !sithRender_aSurfaceFirstCorner
        || !pWorld
        || !pWorld->aSurfaces
        || !pSurf
        || !pView
        || !pPoly->aViewNormals
        || pSurf < pWorld->aSurfaces )
    {
        return;
    }

    const size_t surfNum = (size_t)(pSurf - pWorld->aSurfaces);
    if ( surfNum >= sithRender_numSmoothSurfaces || surfNum >= pWorld->numSurfaces )
    {
        return;
    }

    const rdVector3* aCorners = &sithRender_aCornerNormals[sithRender_aSurfaceFirstCorner[surfNum]];
    for ( size_t i = 0; i < pSurf->face.numVertices; ++i )
    {
        rdVector3 cornerNormal;
        rdMatrix_TransformVector34(&cornerNormal, &aCorners[i], pView);
        rdVector_Normalize3(&pPoly->aViewNormals[i], &cornerNormal);
    }
}

int sithRender_SurfaceFaceToPlane(rdCacheProcEntry* pPoly, const SithSector* pSector, const SithSurface* pSurf, bool bLit)
{
    // Added: rdClip_FaceToPlane of a world surface, or with bLit (an entry of a lit getter)
    // rdClip_FaceToPlaneLit with the view-space normals: the smoothed corner normals where they
    // exist, else the face normal, rotated by the view matrix like aTransformedVertices.
    const SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !bLit )
    {
        return rdClip_FaceToPlane(pSector->pClipFrustum, pPoly, &pSurf->face, pWorld->aTransformedVertices, pWorld->aTexVerticies, pWorld->aVertDynamicLights, pSurf->aIntensities);
    }

    const rdMatrix34* pView = &rdCamera_g_pCurCamera->viewMatrix;

    // Added: M10.2. Per-pixel world GOURAUD keeps its baked vertex light and the shader adds the
    // dynamic lights; LIT and DIFFUSE never read the intensities, so they stay on the classic call.
    rdLightMode resolvedMode = pSurf->face.lightingMode;
    if ( resolvedMode >= sithRender_lightMode )
    {
        resolvedMode = sithRender_lightMode;
    }
    const bool bBakedOnly = rdGetPerPixelLighting() && resolvedMode == RD_LIGHTING_GOURAUD;

    rdVector3 viewNormal;
    rdMatrix_TransformVector34(&viewNormal, &pSurf->face.normal, pView);
    const int clipResult = bBakedOnly
        ? rdClip_FaceToPlaneLitBaked(pSector->pClipFrustum, pPoly, &pSurf->face, pWorld->aTransformedVertices, pWorld->aTexVerticies, pWorld->aVertDynamicLights, pSurf->aIntensities, NULL, &viewNormal)
        : rdClip_FaceToPlaneLit(pSector->pClipFrustum, pPoly, &pSurf->face, pWorld->aTransformedVertices, pWorld->aTexVerticies, pWorld->aVertDynamicLights, pSurf->aIntensities, NULL, &viewNormal);
    if ( !clipResult )
    {
        return 0;
    }

    // rdClip_FaceToPlaneLit keeps every vertex in order, so corner i is vertex i of the entry.
    sithRender_ApplySmoothNormals(pPoly, pWorld, pSurf, pView);

    return 1;
}

#ifdef J3D_TEST
// Added: seams for the smoothed level normal tests. The builder and the normal swap are
// static here, so the test build observes the real code paths instead of a copy.
void sithRender_BuildSmoothNormalsForTest(void)
{
    sithRender_BuildSmoothNormals();
}

void sithRender_FreeSmoothNormalsForTest(void)
{
    sithRender_FreeSmoothNormals();
}

void sithRender_ApplySmoothNormalsForTest(rdCacheProcEntry* pPoly, const SithWorld* pWorld, const SithSurface* pSurf, const rdMatrix34* pView)
{
    sithRender_ApplySmoothNormals(pPoly, pWorld, pSurf, pView);
}
#endif

void J3DAPI sithRender_SetExtraThingRenderFunc(int (J3DAPI* pfFunc)(SithThing* pThing))
{
    // Note, in OpenJkdf this looks like to be a special weapon render function
    sithRender_pExtraThingRenderFunc = pfFunc;
}

// Added: joins a screenshot directory and name, refusing one that would not fit (M8.2b).
static bool sithRender_MakeScreenShotPath(char* pOut, size_t outSize, const char* pDir, const char* pName)
{
    if ( !pDir || pDir[0] == '\0' )
    {
        SITHLOG_ERROR("sithRender: the screenshot directory is unavailable.\n");
        return false;
    }

    if ( strlen(pDir) + strlen(pName) + 2u > outSize )
    {
        SITHLOG_ERROR("sithRender: the screenshot path is too long for '%s'.\n", pName);
        return false;
    }

    stdFnames_MakePath(pOut, (int)outSize, pDir, pName);
    return true;
}

int sithRender_MakeScreenShot(void)
{
    const char* pNameTemplate = sithGetScreenShotFileTemplate();

    char aFilename[64];
    size_t ssnum = 0;
    STD_FORMAT(aFilename, pNameTemplate, ssnum);
    const char* pDir = sithGetScreenShotsDir();

    char aFilePath[STDUSERDIR_MAXPATH]; // Altered: room for user-directory paths (M8.2b)
    if ( !sithRender_MakeScreenShotPath(aFilePath, sizeof(aFilePath), pDir, aFilename) )
    {
        return 1;
    }

    while ( stdFileUtil_FileExists(aFilePath) && ssnum < SITHRENDER_MAXSCREENSHOTS )
    {
        pNameTemplate = sithGetScreenShotFileTemplate();
        STD_FORMAT(aFilename, pNameTemplate, ++ssnum);

        pDir = sithGetScreenShotsDir();
        if ( !sithRender_MakeScreenShotPath(aFilePath, sizeof(aFilePath), pDir, aFilename) )
        {
            return 1;
        }
    }

    if ( ssnum <= SITHRENDER_MAXSCREENSHOTS )
    {
        return stdDisplay_SaveScreen(aFilePath);
    }

    ssnum = 0;
    pNameTemplate = sithGetScreenShotFileTemplate();
    STD_FORMAT(aFilename, pNameTemplate, ssnum);

    pDir = sithGetScreenShotsDir();
    if ( !sithRender_MakeScreenShotPath(aFilePath, sizeof(aFilePath), pDir, aFilename) )
    {
        return 1;
    }
    return stdDisplay_SaveScreen(aFilePath);
}