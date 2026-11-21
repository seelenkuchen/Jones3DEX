// Added: M9.2 throwaway prototype. Fixed simulation tick with interpolated drawing, enabled only
// by J3D_SPIKE_FIXED_TICK_HZ. With the variable unset every entry point returns immediately and
// the game keeps the frame loop it had. See Docs/arc42/08-crosscutting/Frame-Time.md, "M9.2 spike
// results", for the measurements and the reasons behind the choices here.
#include "JonesSpike.h"

#include <rdroid/Math/rdMatrix.h>

#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>

#include <math.h>
#include <stdlib.h>

// Added: the environment variable and the maximum number of catch-up ticks in one rendered frame.
// The cap drops the remaining accumulator time instead of letting a long stall spiral.
#define JONESSPIKE_ENV_HZ      "J3D_SPIKE_FIXED_TICK_HZ"
#define JONESSPIKE_MAXTICKS    8
#define JONESSPIKE_MAXTHINGS   0x40000

// Added: one thing's previous or current tick state. renderFrame and flags are here because the
// draw writes them today (sithRender_RenderThing) and the simulation reads them (sithItem,
// sithActor, IsThingVisible). Indexed by world thing index.
typedef struct sJonesSpikeThingSnapshot
{
    bool             valid;
    SithThingType    type;
    uint32_t         signature;
    SithThingFlag    flags;
    size_t           renderFrame;
    rdVector3        pos;
    rdMatrix34       orient;
} JonesSpikeThingSnapshot;

typedef struct sJonesSpikeCameraSnapshot
{
    bool       valid;
    rdVector3  pos;
    rdMatrix34 orient;
    float      fov;
    float      aspectRatio;
} JonesSpikeCameraSnapshot;

static bool     JonesSpike_bReady;
static bool     JonesSpike_bEnabled;
static uint32_t JonesSpike_tickStepMsec;
static size_t   JonesSpike_numTicks;

static bool     JonesSpike_bHaveClock;
static uint32_t JonesSpike_lastClockMsec;
static double   JonesSpike_accumMsec;
static float    JonesSpike_alpha;

static JonesSpikeThingSnapshot*  JonesSpike_pPrevThings;
static JonesSpikeThingSnapshot*  JonesSpike_pCurThings;
static size_t                    JonesSpike_capacity;
static JonesSpikeCameraSnapshot  JonesSpike_prevCamera;
static JonesSpikeCameraSnapshot  JonesSpike_curCamera;
static JonesSpikeCameraSnapshot  JonesSpike_drawCamera;

static bool     JonesSpike_bDrawing;
static size_t   JonesSpike_drawFrameNumber;

static void JonesSpike_EnsureInit(void)
{
    if ( JonesSpike_bReady )
    {
        return;
    }

    JonesSpike_bReady = true;

    const char* pEnv = getenv(JONESSPIKE_ENV_HZ);
    if ( !pEnv || !*pEnv )
    {
        return;
    }

    char* pEnd     = NULL;
    long  tickHz   = strtol(pEnv, &pEnd, 10);
    if ( !pEnd || *pEnd || tickHz <= 0 || tickHz > 1000 )
    {
        return;
    }

    JonesSpike_tickStepMsec = (uint32_t)(1000 / tickHz);
    if ( JonesSpike_tickStepMsec < 1 )
    {
        JonesSpike_tickStepMsec = 1;
    }

    JonesSpike_bEnabled = true;
    sithTime_SetFixedStep(1);
}

bool JonesSpike_IsEnabled(void)
{
    JonesSpike_EnsureInit();
    return JonesSpike_bEnabled;
}

uint32_t JonesSpike_GetTickStepMsec(void)
{
    JonesSpike_EnsureInit();
    return JonesSpike_bEnabled ? JonesSpike_tickStepMsec : 0;
}

size_t JonesSpike_GetTickCount(void)
{
    return JonesSpike_numTicks;
}

float JonesSpike_GetAlpha(void)
{
    return JonesSpike_alpha;
}

static bool JonesSpike_EnsureCapacity(size_t numThings)
{
    if ( numThings > JONESSPIKE_MAXTHINGS )
    {
        return false;
    }

    if ( numThings <= JonesSpike_capacity )
    {
        return true;
    }

    size_t newCapacity = JonesSpike_capacity ? JonesSpike_capacity : 64;
    while ( newCapacity < numThings )
    {
        newCapacity *= 2;
    }

    JonesSpikeThingSnapshot* pPrev = (JonesSpikeThingSnapshot*)STDREALLOC(JonesSpike_pPrevThings, newCapacity * sizeof(*pPrev));
    if ( !pPrev )
    {
        return false;
    }

    JonesSpike_pPrevThings = pPrev;

    JonesSpikeThingSnapshot* pCur = (JonesSpikeThingSnapshot*)STDREALLOC(JonesSpike_pCurThings, newCapacity * sizeof(*pCur));
    if ( !pCur )
    {
        return false;
    }

    JonesSpike_pCurThings = pCur;
    JonesSpike_capacity   = newCapacity;
    return true;
}

static void JonesSpike_SnapshotThings(JonesSpikeThingSnapshot* pDst)
{
    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    size_t numThings  = pWorld ? pWorld->numThings : 0;
    if ( !pWorld || !pWorld->aThings || !JonesSpike_EnsureCapacity(numThings) )
    {
        return;
    }

    for ( size_t i = 0; i < numThings; ++i )
    {
        const SithThing* pThing = &pWorld->aThings[i];
        JonesSpikeThingSnapshot* pSnap = &pDst[i];
        if ( pThing->type == SITH_THING_FREE )
        {
            pSnap->valid = false;
            continue;
        }

        pSnap->valid       = true;
        pSnap->type        = pThing->type;
        pSnap->signature   = pThing->signature;
        pSnap->flags       = pThing->flags;
        pSnap->renderFrame = pThing->renderFrame;
        pSnap->pos         = pThing->pos;
        pSnap->orient      = pThing->orient;
    }
}

static void JonesSpike_SnapshotCamera(JonesSpikeCameraSnapshot* pDst)
{
    const SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( !pCamera )
    {
        pDst->valid = false;
        return;
    }

    pDst->valid       = true;
    pDst->pos         = pCamera->pos;
    pDst->orient      = pCamera->orient;
    pDst->fov         = pCamera->fov;
    pDst->aspectRatio = pCamera->aspectRatio;
}

int JonesSpike_BeginFrame(bool bPaused)
{
    JonesSpike_EnsureInit();
    if ( !JonesSpike_bEnabled )
    {
        return 0;
    }

    uint32_t now = stdPlatform_GetTimeMsec();
    if ( !JonesSpike_bHaveClock )
    {
        JonesSpike_bHaveClock   = true;
        JonesSpike_lastClockMsec = now;
        JonesSpike_accumMsec     = 0.0;
    }
    else
    {
        uint32_t delta = now - JonesSpike_lastClockMsec;
        JonesSpike_lastClockMsec = now;

        if ( bPaused )
        {
            JonesSpike_accumMsec = 0.0;
        }
        else
        {
            JonesSpike_accumMsec += (double)delta;
        }
    }

    if ( bPaused )
    {
        JonesSpike_alpha = 0.0f;
        return 0;
    }

    int numTicks = 0;
    while ( JonesSpike_accumMsec >= (double)JonesSpike_tickStepMsec && numTicks < JONESSPIKE_MAXTICKS )
    {
        JonesSpike_accumMsec -= (double)JonesSpike_tickStepMsec;
        ++numTicks;
    }

    // Added: a stall longer than the catch-up budget drops the backlog instead of compounding.
    if ( JonesSpike_accumMsec >= (double)JonesSpike_tickStepMsec )
    {
        JonesSpike_accumMsec = 0.0;
    }

    JonesSpike_alpha = (float)(JonesSpike_accumMsec / (double)JonesSpike_tickStepMsec);
    return numTicks;
}

void JonesSpike_BeforeTick(void)
{
    if ( !JonesSpike_bEnabled )
    {
        return;
    }

    JonesSpike_SnapshotThings(JonesSpike_pPrevThings);
    JonesSpike_SnapshotCamera(&JonesSpike_prevCamera);
}

void JonesSpike_AfterTick(void)
{
    if ( !JonesSpike_bEnabled )
    {
        return;
    }

    JonesSpike_SnapshotThings(JonesSpike_pCurThings);
    JonesSpike_SnapshotCamera(&JonesSpike_curCamera);
    ++JonesSpike_numTicks;
}

static void JonesSpike_LerpVector(rdVector3* pOut, const rdVector3* pA, const rdVector3* pB, float alpha)
{
    pOut->x = pA->x + (pB->x - pA->x) * alpha;
    pOut->y = pA->y + (pB->y - pA->y) * alpha;
    pOut->z = pA->z + (pB->z - pA->z) * alpha;
}

// Added: row-wise linear blend of rvec/lvec/dvec, then rdMatrix_Normalize34 rebuilds uvec and
// re-orthonormalises lvec/uvec/rvec. Not slerp: fine for the small per-tick rotations a fixed
// tick produces, and it degenerates only on a >=180-degree turn between two ticks.
static void JonesSpike_LerpOrient(rdMatrix34* pOut, const rdMatrix34* pA, const rdMatrix34* pB, float alpha)
{
    JonesSpike_LerpVector(&pOut->rvec, &pA->rvec, &pB->rvec, alpha);
    JonesSpike_LerpVector(&pOut->lvec, &pA->lvec, &pB->lvec, alpha);
    JonesSpike_LerpVector(&pOut->dvec, &pA->dvec, &pB->dvec, alpha);
    rdMatrix_Normalize34(pOut);
}

void JonesSpike_BeginDraw(void)
{
    JonesSpike_EnsureInit();
    if ( !JonesSpike_bEnabled || JonesSpike_bDrawing )
    {
        return;
    }

    JonesSpike_bDrawing       = true;
    JonesSpike_drawFrameNumber = sithMain_g_frameNumber;
    JonesSpike_SnapshotCamera(&JonesSpike_drawCamera);

    float alpha = JonesSpike_alpha;
    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( pWorld && pWorld->aThings && JonesSpike_EnsureCapacity(pWorld->numThings) )
    {
        for ( size_t i = 0; i < pWorld->numThings; ++i )
        {
            SithThing* pThing = &pWorld->aThings[i];
            const JonesSpikeThingSnapshot* pCur = &JonesSpike_pCurThings[i];
            if ( !pCur->valid || pThing->type == SITH_THING_FREE || pThing->signature != pCur->signature )
            {
                continue;
            }

            const JonesSpikeThingSnapshot* pPrev = &JonesSpike_pPrevThings[i];
            if ( pPrev->valid && pPrev->signature == pCur->signature )
            {
                JonesSpike_LerpVector(&pThing->pos, &pPrev->pos, &pCur->pos, alpha);
                JonesSpike_LerpOrient(&pThing->orient, &pPrev->orient, &pCur->orient, alpha);
            }
            else
            {
                pThing->pos    = pCur->pos;
                pThing->orient = pCur->orient;
            }
        }
    }

    SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( pCamera && JonesSpike_curCamera.valid )
    {
        if ( JonesSpike_prevCamera.valid )
        {
            JonesSpike_LerpVector(&pCamera->pos, &JonesSpike_prevCamera.pos, &JonesSpike_curCamera.pos, alpha);
            JonesSpike_LerpOrient(&pCamera->orient, &JonesSpike_prevCamera.orient, &JonesSpike_curCamera.orient, alpha);
        }
        else
        {
            pCamera->pos    = JonesSpike_curCamera.pos;
            pCamera->orient = JonesSpike_curCamera.orient;
        }
    }
}

void JonesSpike_EndDraw(void)
{
    if ( !JonesSpike_bEnabled || !JonesSpike_bDrawing )
    {
        return;
    }

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( pWorld && pWorld->aThings && JonesSpike_capacity >= pWorld->numThings )
    {
        for ( size_t i = 0; i < pWorld->numThings; ++i )
        {
            SithThing* pThing = &pWorld->aThings[i];
            const JonesSpikeThingSnapshot* pCur = &JonesSpike_pCurThings[i];
            if ( !pCur->valid || pThing->type == SITH_THING_FREE || pThing->signature != pCur->signature )
            {
                continue;
            }

            pThing->pos         = pCur->pos;
            pThing->orient      = pCur->orient;
            pThing->flags       = pCur->flags;
            pThing->renderFrame = pCur->renderFrame;
        }
    }

    SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( pCamera && JonesSpike_drawCamera.valid )
    {
        pCamera->pos         = JonesSpike_drawCamera.pos;
        pCamera->orient      = JonesSpike_drawCamera.orient;
        pCamera->fov         = JonesSpike_drawCamera.fov;
        pCamera->aspectRatio = JonesSpike_drawCamera.aspectRatio;
    }

    // Added: the draw does not own the simulation frame number. The render tick is deliberately
    // not restored: it is a monotonic presentation cache namespace, the simulation never reads it
    // (sithAIUtil_GetThingsInView advances it itself before use), and restoring it would make the
    // next draw hit caches keyed by the old value.
    sithMain_g_frameNumber = JonesSpike_drawFrameNumber;

    JonesSpike_bDrawing = false;
}

void JonesSpike_ResetGlobals(void)
{
    if ( JonesSpike_pPrevThings )
    {
        STDFREE(JonesSpike_pPrevThings);
    }

    if ( JonesSpike_pCurThings )
    {
        STDFREE(JonesSpike_pCurThings);
    }

    JonesSpike_bReady          = false;
    JonesSpike_bEnabled        = false;
    JonesSpike_tickStepMsec    = 0;
    JonesSpike_numTicks        = 0;
    JonesSpike_bHaveClock      = false;
    JonesSpike_lastClockMsec   = 0;
    JonesSpike_accumMsec       = 0.0;
    JonesSpike_alpha           = 0.0f;
    JonesSpike_pPrevThings     = NULL;
    JonesSpike_pCurThings      = NULL;
    JonesSpike_capacity        = 0;
    JonesSpike_prevCamera.valid = false;
    JonesSpike_curCamera.valid  = false;
    JonesSpike_drawCamera.valid = false;
    JonesSpike_bDrawing        = false;
    JonesSpike_drawFrameNumber = 0;

    sithTime_SetFixedStep(0);
}
