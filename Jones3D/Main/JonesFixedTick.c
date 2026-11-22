// Added: M9.20 fixed simulation tick with interpolated things and camera (ADR-0039). JonesMain
// feeds a game-time accumulator from the measured frame delta, runs zero or more fixed ticks per
// drawn frame and interpolates the world and camera between the last two ticks for the draw. The
// option is off unless engine.sim.fixedTick (or the headless --fixed-tick override) selects a rate,
// so the off path stays the measured-frame-time loop.
#include "JonesFixedTick.h"

#include <rdroid/Engine/rdCamera.h>

#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>
#include <std/General/stdUtil.h>

#include <string.h>

#define JONESFIXEDTICK_MAXTHINGS 0x40000

// Added: the rates a hand-edited cfg may select. Everything else snaps to the nearest; a tie snaps
// down, so 15 -> 0 and 45 -> 30.
static const int jonesFixedTick_aAllowedRates[] = { 0, 30, 50, 60, 120 };

static JonesFixedTickAccumulator jonesFixedTick_accumulator;
static int                       jonesFixedTick_overrideRateHz = -1;
static int                       jonesFixedTick_rateHz;
static uint32_t                  jonesFixedTick_stepMsec;
static bool                      jonesFixedTick_bEnabled;
static size_t                    jonesFixedTick_tickCount;
static size_t                    jonesFixedTick_tickLimit;
static float                     jonesFixedTick_alpha;

static JonesFixedTickThingState* jonesFixedTick_pPrevThings;
static JonesFixedTickThingState* jonesFixedTick_pCurThings;
static size_t                    jonesFixedTick_capacity;
static size_t                    jonesFixedTick_prevNumThings;
static size_t                    jonesFixedTick_curNumThings;
static JonesFixedTickCameraState jonesFixedTick_prevCamera;
static JonesFixedTickCameraState jonesFixedTick_curCamera;
static bool                      jonesFixedTick_bDrawing;

int JonesFixedTick_ClampRate(int rateHz)
{
    // Added: keep the distance arithmetic small; the cfg value can be any JSON integer.
    if ( rateHz <= 0 )
    {
        return 0;
    }

    int bestRate  = jonesFixedTick_aAllowedRates[0];
    int bestDelta = rateHz - bestRate;

    for ( size_t i = 1; i < STD_ARRAYLEN(jonesFixedTick_aAllowedRates); ++i )
    {
        const int rate  = jonesFixedTick_aAllowedRates[i];
        const int delta = rateHz >= rate ? rateHz - rate : rate - rateHz;
        if ( delta < bestDelta ) // Added: a tie keeps the lower rate, so the result is deterministic
        {
            bestRate  = rate;
            bestDelta = delta;
        }
    }

    return bestRate;
}

uint32_t JonesFixedTick_RateToStepMsec(int rateHz)
{
    if ( rateHz <= 0 )
    {
        return 0;
    }

    return (uint32_t)(1000 / rateHz);
}

void JonesFixedTick_SetRateOverride(int rateHz)
{
    // Added: preserved across ResetGlobals so the headless runner can set it once before the run.
    jonesFixedTick_overrideRateHz = rateHz;
}

void JonesFixedTick_Configure(void)
{
    // Added: QOL-off/Retail builds ignore the option and the override, so the measured frame time
    // stays in use. The Enhanced default is still 0 until the owner approves turning it on.
    int rate = 0;
#ifdef J3D_QOL_IMPROVEMENTS
    rate = jonesFixedTick_overrideRateHz;
    if ( rate < 0 )
    {
        rate = stdConfig_GetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, 0);
    }
#endif

    rate = JonesFixedTick_ClampRate(rate);

    jonesFixedTick_rateHz   = rate;
    jonesFixedTick_stepMsec = JonesFixedTick_RateToStepMsec(rate);
    jonesFixedTick_bEnabled = rate > 0;

    JonesFixedTick_AccumReset(&jonesFixedTick_accumulator);
    jonesFixedTick_alpha = 0.0f;

    // Added: tell sithTime to stop sampling the wall clock and to let clock time follow game time.
    sithTime_SetFixedStep(jonesFixedTick_bEnabled ? 1 : 0);
}

bool JonesFixedTick_IsEnabled(void)
{
    return jonesFixedTick_bEnabled;
}

int JonesFixedTick_GetRateHz(void)
{
    return jonesFixedTick_rateHz;
}

uint32_t JonesFixedTick_GetStepMsec(void)
{
    return jonesFixedTick_stepMsec;
}

void JonesFixedTick_SetTickLimit(size_t limit)
{
    jonesFixedTick_tickLimit = limit;
}

size_t JonesFixedTick_GetTickCount(void)
{
    return jonesFixedTick_tickCount;
}

void JonesFixedTick_AccumReset(JonesFixedTickAccumulator* pAccum)
{
    if ( !pAccum )
    {
        return;
    }

    pAccum->bHaveClock    = false;
    pAccum->lastClockMsec = 0;
    pAccum->accumMsec     = 0.0;
}

int JonesFixedTick_AccumAdvance(JonesFixedTickAccumulator* pAccum, uint32_t nowMsec, bool bPaused,
    uint32_t stepMsec, int maxTicks)
{
    if ( !pAccum )
    {
        return 0;
    }

    if ( !pAccum->bHaveClock )
    {
        pAccum->bHaveClock    = true;
        pAccum->lastClockMsec = nowMsec;
        pAccum->accumMsec     = 0.0;
    }
    else
    {
        uint32_t delta = nowMsec - pAccum->lastClockMsec;
        pAccum->lastClockMsec = nowMsec;

        if ( bPaused )
        {
            pAccum->accumMsec = 0.0;
        }
        else
        {
            // Added: clamp like sithTime_Advance does (1..200 ms) so a clock loop or a stall cannot
            // feed a huge delta. The catch-up cap below then bounds the burst.
            if ( delta < 1 )
            {
                delta = 1;
            }
            else if ( delta > 200 )
            {
                delta = 200;
            }

            pAccum->accumMsec += (double)delta;
        }
    }

    if ( bPaused || stepMsec == 0 )
    {
        return 0;
    }

    int numTicks = 0;
    while ( pAccum->accumMsec >= (double)stepMsec && numTicks < maxTicks )
    {
        pAccum->accumMsec -= (double)stepMsec;
        ++numTicks;
    }

    // Added: a stall longer than the catch-up budget drops the backlog instead of compounding.
    if ( pAccum->accumMsec >= (double)stepMsec )
    {
        pAccum->accumMsec = 0.0;
    }

    return numTicks;
}

float JonesFixedTick_AccumAlpha(const JonesFixedTickAccumulator* pAccum, uint32_t stepMsec)
{
    if ( !pAccum || stepMsec == 0 )
    {
        return 0.0f;
    }

    float alpha = (float)(pAccum->accumMsec / (double)stepMsec);
    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    return alpha;
}

int JonesFixedTick_BeginFrame(bool bPaused)
{
    if ( !jonesFixedTick_bEnabled )
    {
        return 0;
    }

    int maxTicks = JONESFIXEDTICK_MAX_CATCHUP;
    if ( jonesFixedTick_tickLimit )
    {
        const size_t remaining = jonesFixedTick_tickCount < jonesFixedTick_tickLimit
            ? jonesFixedTick_tickLimit - jonesFixedTick_tickCount
            : 0;
        if ( (size_t)maxTicks > remaining )
        {
            maxTicks = (int)remaining;
        }
    }

    const int numTicks = JonesFixedTick_AccumAdvance(&jonesFixedTick_accumulator,
        stdPlatform_GetTimeMsec(), bPaused, jonesFixedTick_stepMsec, maxTicks);
    jonesFixedTick_alpha = JonesFixedTick_AccumAlpha(&jonesFixedTick_accumulator, jonesFixedTick_stepMsec);
    return numTicks;
}

float JonesFixedTick_GetAlpha(void)
{
    return jonesFixedTick_alpha;
}

static bool JonesFixedTick_EnsureCapacity(size_t numThings)
{
    if ( numThings > JONESFIXEDTICK_MAXTHINGS )
    {
        return false;
    }

    if ( numThings <= jonesFixedTick_capacity )
    {
        return true;
    }

    size_t newCapacity = jonesFixedTick_capacity ? jonesFixedTick_capacity : 64;
    while ( newCapacity < numThings )
    {
        newCapacity *= 2;
    }

    JonesFixedTickThingState* pPrev = (JonesFixedTickThingState*)STDREALLOC(
        jonesFixedTick_pPrevThings, newCapacity * sizeof(*pPrev));
    if ( !pPrev )
    {
        return false;
    }

    jonesFixedTick_pPrevThings = pPrev;

    JonesFixedTickThingState* pCur = (JonesFixedTickThingState*)STDREALLOC(
        jonesFixedTick_pCurThings, newCapacity * sizeof(*pCur));
    if ( !pCur )
    {
        return false;
    }

    jonesFixedTick_pCurThings = pCur;
    jonesFixedTick_capacity   = newCapacity;
    return true;
}

void JonesFixedTick_CaptureThing(JonesFixedTickThingState* pDst, const SithThing* pThing)
{
    if ( !pDst || !pThing || pThing->type == SITH_THING_FREE )
    {
        if ( pDst )
        {
            pDst->valid = false;
        }
        return;
    }

    pDst->valid       = true;
    pDst->type        = pThing->type;
    pDst->signature   = pThing->signature;
    pDst->flags       = pThing->flags;
    pDst->renderFrame = pThing->renderFrame;
    pDst->pos         = pThing->pos;
    pDst->orient      = pThing->orient;
}

void JonesFixedTick_CaptureCamera(JonesFixedTickCameraState* pDst, const SithCamera* pCamera)
{
    if ( !pDst || !pCamera )
    {
        if ( pDst )
        {
            pDst->valid = false;
        }
        return;
    }

    pDst->valid       = true;
    pDst->pos         = pCamera->pos;
    pDst->orient      = pCamera->orient;
    pDst->fov         = pCamera->fov;
    pDst->aspectRatio = pCamera->aspectRatio;
}

static void JonesFixedTick_CaptureThings(JonesFixedTickThingState** ppDst, size_t* pNumThings)
{
    *pNumThings = 0;

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !pWorld || !pWorld->aThings )
    {
        return;
    }

    const size_t numThings = pWorld->numThings;
    if ( !JonesFixedTick_EnsureCapacity(numThings) )
    {
        return;
    }

    // Added: take the array after EnsureCapacity; it may have reallocated the caller's pointer.
    JonesFixedTickThingState* pDst = *ppDst;
    for ( size_t i = 0; i < numThings; ++i )
    {
        JonesFixedTick_CaptureThing(&pDst[i], &pWorld->aThings[i]);
    }

    *pNumThings = numThings;
}

void JonesFixedTick_BeforeTick(void)
{
    if ( !jonesFixedTick_bEnabled )
    {
        return;
    }

    JonesFixedTick_CaptureThings(&jonesFixedTick_pPrevThings, &jonesFixedTick_prevNumThings);
    JonesFixedTick_CaptureCamera(&jonesFixedTick_prevCamera, sithCamera_g_pCurCamera);
}

void JonesFixedTick_AfterTick(void)
{
    if ( !jonesFixedTick_bEnabled )
    {
        return;
    }

    JonesFixedTick_CaptureThings(&jonesFixedTick_pCurThings, &jonesFixedTick_curNumThings);
    JonesFixedTick_CaptureCamera(&jonesFixedTick_curCamera, sithCamera_g_pCurCamera);
    ++jonesFixedTick_tickCount;
}

void JonesFixedTick_LerpVector(rdVector3* pOut, const rdVector3* pA, const rdVector3* pB, float alpha)
{
    pOut->x = pA->x + (pB->x - pA->x) * alpha;
    pOut->y = pA->y + (pB->y - pA->y) * alpha;
    pOut->z = pA->z + (pB->z - pA->z) * alpha;
}

void JonesFixedTick_LerpOrient(rdMatrix34* pOut, const rdMatrix34* pA, const rdMatrix34* pB, float alpha)
{
    // Added: row-wise blend, then rdMatrix_Normalize34 rebuilds uvec and re-orthonormalises. Not
    // slerp: fine for the small per-tick rotations a fixed tick produces; it degenerates only for a
    // rotation of 180 degrees or more between two ticks.
    JonesFixedTick_LerpVector(&pOut->rvec, &pA->rvec, &pB->rvec, alpha);
    JonesFixedTick_LerpVector(&pOut->lvec, &pA->lvec, &pB->lvec, alpha);
    JonesFixedTick_LerpVector(&pOut->dvec, &pA->dvec, &pB->dvec, alpha);
    rdMatrix_Normalize34(pOut);
}

static bool JonesFixedTick_IsTeleport(const rdVector3* pPrev, const rdVector3* pCur)
{
    const float dx = pCur->x - pPrev->x;
    const float dy = pCur->y - pPrev->y;
    const float dz = pCur->z - pPrev->z;
    const float distSq = dx * dx + dy * dy + dz * dz;
    return distSq > JONESFIXEDTICK_TELEPORT_DISTANCE * JONESFIXEDTICK_TELEPORT_DISTANCE;
}

bool JonesFixedTick_BlendThing(const JonesFixedTickThingState* pPrev,
    const JonesFixedTickThingState* pCur, SithThing* pThing, float alpha)
{
    if ( !pCur || !pCur->valid || !pThing || pThing->type == SITH_THING_FREE )
    {
        return false;
    }

    // Added: a slot recreated between the two ticks must never be blended with the old occupant.
    if ( pThing->signature != pCur->signature )
    {
        return false;
    }

    if ( !pPrev || !pPrev->valid || pPrev->signature != pCur->signature || pPrev->type != pCur->type
        || JonesFixedTick_IsTeleport(&pPrev->pos, &pCur->pos) )
    {
        // Added: created between ticks, or a teleport: draw the exact post-tick state.
        pThing->pos    = pCur->pos;
        pThing->orient = pCur->orient;
        return true;
    }

    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    JonesFixedTick_LerpVector(&pThing->pos, &pPrev->pos, &pCur->pos, alpha);
    JonesFixedTick_LerpOrient(&pThing->orient, &pPrev->orient, &pCur->orient, alpha);
    return true;
}

void JonesFixedTick_RestoreThing(const JonesFixedTickThingState* pCur, SithThing* pThing)
{
    if ( !pCur || !pCur->valid || !pThing || pThing->type == SITH_THING_FREE
        || pThing->signature != pCur->signature )
    {
        return;
    }

    pThing->pos         = pCur->pos;
    pThing->orient      = pCur->orient;
    pThing->flags       = pCur->flags;
    pThing->renderFrame = pCur->renderFrame;
}

void JonesFixedTick_BlendCamera(const JonesFixedTickCameraState* pPrev,
    const JonesFixedTickCameraState* pCur, SithCamera* pCamera, float alpha)
{
    if ( !pCur || !pCur->valid || !pCamera )
    {
        return;
    }

    if ( !pPrev || !pPrev->valid || JonesFixedTick_IsTeleport(&pPrev->orient.dvec, &pCur->orient.dvec) )
    {
        pCamera->pos         = pCur->pos;
        pCamera->orient      = pCur->orient;
        pCamera->fov         = pCur->fov;
        pCamera->aspectRatio = pCur->aspectRatio;
    }
    else
    {
        if ( alpha < 0.0f )
        {
            alpha = 0.0f;
        }
        else if ( alpha > 1.0f )
        {
            alpha = 1.0f;
        }

        JonesFixedTick_LerpVector(&pCamera->pos, &pPrev->pos, &pCur->pos, alpha);
        JonesFixedTick_LerpOrient(&pCamera->orient, &pPrev->orient, &pCur->orient, alpha);
        pCamera->fov         = pPrev->fov + (pCur->fov - pPrev->fov) * alpha;
        pCamera->aspectRatio = pPrev->aspectRatio + (pCur->aspectRatio - pPrev->aspectRatio) * alpha;
    }

    // Added: the draw's projection comes from the camera's rdCamera, so apply the interpolated
    // fov/aspect there too. sithCamera_RenderScene still updates the view matrix from the orient.
    rdCamera_SetFOV(&pCamera->rdCamera, pCamera->fov);
    rdCamera_SetAspectRatio(&pCamera->rdCamera, pCamera->aspectRatio);
}

void JonesFixedTick_RestoreCamera(const JonesFixedTickCameraState* pCur, SithCamera* pCamera)
{
    if ( !pCur || !pCur->valid || !pCamera )
    {
        return;
    }

    pCamera->pos         = pCur->pos;
    pCamera->orient      = pCur->orient;
    pCamera->fov         = pCur->fov;
    pCamera->aspectRatio = pCur->aspectRatio;

    rdCamera_SetFOV(&pCamera->rdCamera, pCamera->fov);
    rdCamera_SetAspectRatio(&pCamera->rdCamera, pCamera->aspectRatio);
}

void JonesFixedTick_BeginDraw(void)
{
    if ( !jonesFixedTick_bEnabled || jonesFixedTick_bDrawing )
    {
        return;
    }

    jonesFixedTick_bDrawing = true;

    float alpha = jonesFixedTick_alpha;
    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( pWorld && pWorld->aThings && jonesFixedTick_capacity >= pWorld->numThings )
    {
        const size_t numThings = pWorld->numThings < jonesFixedTick_curNumThings
            ? pWorld->numThings
            : jonesFixedTick_curNumThings;

        for ( size_t i = 0; i < numThings; ++i )
        {
            const JonesFixedTickThingState* pCur = &jonesFixedTick_pCurThings[i];
            if ( !pCur->valid )
            {
                continue;
            }

            const JonesFixedTickThingState* pPrev = i < jonesFixedTick_prevNumThings
                ? &jonesFixedTick_pPrevThings[i]
                : NULL;
            JonesFixedTick_BlendThing(pPrev, pCur, &pWorld->aThings[i], alpha);
        }
    }

    if ( sithCamera_g_pCurCamera && jonesFixedTick_curCamera.valid )
    {
        const JonesFixedTickCameraState* pPrev = jonesFixedTick_prevCamera.valid
            ? &jonesFixedTick_prevCamera
            : NULL;
        JonesFixedTick_BlendCamera(pPrev, &jonesFixedTick_curCamera, sithCamera_g_pCurCamera, alpha);
    }
}

void JonesFixedTick_EndDraw(void)
{
    if ( !jonesFixedTick_bEnabled || !jonesFixedTick_bDrawing )
    {
        return;
    }

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( pWorld && pWorld->aThings && jonesFixedTick_capacity >= pWorld->numThings )
    {
        const size_t numThings = pWorld->numThings < jonesFixedTick_curNumThings
            ? pWorld->numThings
            : jonesFixedTick_curNumThings;

        for ( size_t i = 0; i < numThings; ++i )
        {
            const JonesFixedTickThingState* pCur = &jonesFixedTick_pCurThings[i];
            if ( pCur->valid )
            {
                JonesFixedTick_RestoreThing(pCur, &pWorld->aThings[i]);
            }
        }
    }

    if ( sithCamera_g_pCurCamera && jonesFixedTick_curCamera.valid )
    {
        JonesFixedTick_RestoreCamera(&jonesFixedTick_curCamera, sithCamera_g_pCurCamera);
    }

    jonesFixedTick_bDrawing = false;
}

void JonesFixedTick_ResetGlobals(void)
{
    if ( jonesFixedTick_pPrevThings )
    {
        STDFREE(jonesFixedTick_pPrevThings);
    }

    if ( jonesFixedTick_pCurThings )
    {
        STDFREE(jonesFixedTick_pCurThings);
    }

    jonesFixedTick_rateHz        = 0;
    jonesFixedTick_stepMsec      = 0;
    jonesFixedTick_bEnabled      = false;
    jonesFixedTick_tickCount     = 0;
    jonesFixedTick_tickLimit     = 0;
    jonesFixedTick_alpha         = 0.0f;
    jonesFixedTick_pPrevThings   = NULL;
    jonesFixedTick_pCurThings    = NULL;
    jonesFixedTick_capacity      = 0;
    jonesFixedTick_prevNumThings = 0;
    jonesFixedTick_curNumThings  = 0;
    jonesFixedTick_bDrawing      = false;

    memset(&jonesFixedTick_prevCamera, 0, sizeof(jonesFixedTick_prevCamera));
    memset(&jonesFixedTick_curCamera, 0, sizeof(jonesFixedTick_curCamera));

    JonesFixedTick_AccumReset(&jonesFixedTick_accumulator);

    // Added: the override is deliberately kept, so the headless runner can set it once for all runs.
    sithTime_SetFixedStep(0);
}
