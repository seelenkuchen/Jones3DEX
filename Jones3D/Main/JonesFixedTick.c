// Added: M9.20 fixed simulation tick with interpolated things and camera (ADR-0039). JonesMain
// feeds a game-time accumulator from the measured frame delta, runs zero or more fixed ticks per
// drawn frame and interpolates the world and camera between the last two ticks for the draw. The
// option is off unless engine.sim.fixedTick (or the headless --fixed-tick override) selects a rate,
// so the off path stays the measured-frame-time loop.
#include "JonesFixedTick.h"

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdPuppet.h>

#include <sith/Devices/sithSoundMixer.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>
#include <std/General/stdUtil.h>

#include <math.h>
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

// Added: M9.22 interpolated animation. The world's scroll and light state follows the same
// previous/current tick pair as the things and camera.
static JonesFixedTickSurfaceState* jonesFixedTick_pPrevSurfaces;
static JonesFixedTickSurfaceState* jonesFixedTick_pCurSurfaces;
static size_t                      jonesFixedTick_surfaceCapacity;
static size_t                      jonesFixedTick_prevNumSurfaces;
static size_t                      jonesFixedTick_curNumSurfaces;
static const SithWorld*            jonesFixedTick_prevSurfaceWorld;
static const SithWorld*            jonesFixedTick_curSurfaceWorld;

static JonesFixedTickSectorState*  jonesFixedTick_pPrevSectors;
static JonesFixedTickSectorState*  jonesFixedTick_pCurSectors;
static size_t                      jonesFixedTick_sectorCapacity;
static size_t                      jonesFixedTick_prevNumSectors;
static size_t                      jonesFixedTick_curNumSectors;
static const SithWorld*            jonesFixedTick_prevSectorWorld;
static const SithWorld*            jonesFixedTick_curSectorWorld;

static JonesFixedTickSkyState      jonesFixedTick_prevSky;
static JonesFixedTickSkyState      jonesFixedTick_curSky;
static const SithWorld*            jonesFixedTick_prevSkyWorld;
static const SithWorld*            jonesFixedTick_curSkyWorld;

static float                       jonesFixedTick_prevSecGameTime;
static float                       jonesFixedTick_curSecGameTime;
static bool                        jonesFixedTick_bHaveGameTime;

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

static bool JonesFixedTick_EnsureSurfaceCapacity(size_t numSurfaces)
{
    if ( numSurfaces <= jonesFixedTick_surfaceCapacity )
    {
        return true;
    }

    size_t newCapacity = jonesFixedTick_surfaceCapacity ? jonesFixedTick_surfaceCapacity : 64;
    while ( newCapacity < numSurfaces )
    {
        newCapacity *= 2;
    }

    JonesFixedTickSurfaceState* pPrev = (JonesFixedTickSurfaceState*)STDREALLOC(
        jonesFixedTick_pPrevSurfaces, newCapacity * sizeof(*pPrev));
    if ( !pPrev )
    {
        return false;
    }
    jonesFixedTick_pPrevSurfaces = pPrev;

    JonesFixedTickSurfaceState* pCur = (JonesFixedTickSurfaceState*)STDREALLOC(
        jonesFixedTick_pCurSurfaces, newCapacity * sizeof(*pCur));
    if ( !pCur )
    {
        return false;
    }
    jonesFixedTick_pCurSurfaces = pCur;

    jonesFixedTick_surfaceCapacity = newCapacity;
    return true;
}

static bool JonesFixedTick_EnsureSectorCapacity(size_t numSectors)
{
    if ( numSectors <= jonesFixedTick_sectorCapacity )
    {
        return true;
    }

    size_t newCapacity = jonesFixedTick_sectorCapacity ? jonesFixedTick_sectorCapacity : 64;
    while ( newCapacity < numSectors )
    {
        newCapacity *= 2;
    }

    JonesFixedTickSectorState* pPrev = (JonesFixedTickSectorState*)STDREALLOC(
        jonesFixedTick_pPrevSectors, newCapacity * sizeof(*pPrev));
    if ( !pPrev )
    {
        return false;
    }
    jonesFixedTick_pPrevSectors = pPrev;

    JonesFixedTickSectorState* pCur = (JonesFixedTickSectorState*)STDREALLOC(
        jonesFixedTick_pCurSectors, newCapacity * sizeof(*pCur));
    if ( !pCur )
    {
        return false;
    }
    jonesFixedTick_pCurSectors = pCur;

    jonesFixedTick_sectorCapacity = newCapacity;
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

    // Added: M9.22. Puppet pose, particle size and thing light for a drawn frame.
    JonesFixedTick_CapturePuppet(&pDst->puppet, pThing->renderData.pPuppet);

    pDst->bParticle = pThing->type == SITH_THING_PARTICLE && pThing->renderData.data.pParticle != NULL;
    pDst->particleRadius = pDst->bParticle ? pThing->renderData.data.pParticle->radius : 0.0f;

    pDst->lightColor     = pThing->light.color;
    pDst->lightMinRadius = pThing->light.minRadius;
    pDst->lightMaxRadius = pThing->light.maxRadius;
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

void JonesFixedTick_CapturePuppet(JonesFixedTickPuppetState* pDst, const rdPuppet* pPuppet)
{
    if ( !pDst )
    {
        return;
    }

    if ( !pPuppet )
    {
        pDst->valid   = false;
        pDst->pPuppet = NULL;
        return;
    }

    pDst->valid   = true;
    pDst->pPuppet = (rdPuppet*)pPuppet;

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        const rdPuppetTrack* pTrack = &pPuppet->aTracks[trackNum];
        JonesFixedTickPuppetTrackState* pState = &pDst->aTracks[trackNum];

        pState->valid       = pTrack->pKFTrack != NULL;
        pState->pKFTrack    = pTrack->pKFTrack;
        pState->curFrame    = pTrack->curFrame;
        pState->blendWeight = pTrack->blendWeight;
    }
}

void JonesFixedTick_BlendPuppet(const JonesFixedTickPuppetState* pPrev,
    const JonesFixedTickPuppetState* pCur, rdPuppet* pPuppet, float alpha)
{
    if ( !pCur || !pCur->valid || !pPuppet || pPuppet != pCur->pPuppet )
    {
        return;
    }

    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        const JonesFixedTickPuppetTrackState* pCurTrack = &pCur->aTracks[trackNum];
        if ( !pCurTrack->valid )
        {
            continue;
        }

        rdPuppetTrack* pTrack = &pPuppet->aTracks[trackNum];
        if ( pTrack->pKFTrack != pCurTrack->pKFTrack )
        {
            // Added: the track was replaced between the two ticks; draw its current state.
            continue;
        }

        float frame       = pCurTrack->curFrame;
        float blendWeight = pCurTrack->blendWeight;

        const JonesFixedTickPuppetTrackState* pPrevTrack = pPrev && pPrev->valid
            && pPrev->pPuppet == pCur->pPuppet
            ? &pPrev->aTracks[trackNum]
            : NULL;

        // Added: a loop resets curFrame behind the previous tick; blending across it would run
        // the whole track backwards, so the loop point snaps to the current tick.
        if ( pPrevTrack && pPrevTrack->valid && pPrevTrack->pKFTrack == pCurTrack->pKFTrack
            && pCurTrack->curFrame >= pPrevTrack->curFrame )
        {
            frame = pPrevTrack->curFrame + (pCurTrack->curFrame - pPrevTrack->curFrame) * alpha;
            blendWeight = pPrevTrack->blendWeight
                + (pCurTrack->blendWeight - pPrevTrack->blendWeight) * alpha;
        }

        rdPuppet_SetTrackDrawSample(pPuppet, (int32_t)trackNum, frame, blendWeight);
    }
}

void JonesFixedTick_RestorePuppet(const JonesFixedTickPuppetState* pCur, rdPuppet* pPuppet)
{
    if ( !pCur || !pCur->valid || !pPuppet || pPuppet != pCur->pPuppet )
    {
        return;
    }

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        const JonesFixedTickPuppetTrackState* pCurTrack = &pCur->aTracks[trackNum];
        if ( !pCurTrack->valid )
        {
            continue;
        }

        if ( pPuppet->aTracks[trackNum].pKFTrack != pCurTrack->pKFTrack )
        {
            continue;
        }

        rdPuppet_SetTrackDrawSample(pPuppet, (int32_t)trackNum, pCurTrack->curFrame, pCurTrack->blendWeight);
    }
}

void JonesFixedTick_CaptureSurface(JonesFixedTickSurfaceState* pDst, const SithSurface* pSurface)
{
    if ( !pDst || !pSurface )
    {
        if ( pDst )
        {
            pDst->valid = false;
        }
        return;
    }

    pDst->valid         = true;
    pDst->scrolling     = (pSurface->flags & SITH_SURFACE_SCROLLING) != 0;
    pDst->texVertOffset = pSurface->face.texVertOffset;
    pDst->extraLight    = pSurface->face.extraLight;
}

float JonesFixedTick_LerpWrapped(float prev, float cur, float alpha, float period)
{
    // Added: choose the representative of cur nearest prev, so a scroll that wrapped between
    // ticks animates through the boundary instead of the whole period backwards.
    float delta = cur - prev;
    if ( delta > period * 0.5f )
    {
        delta -= period;
    }
    else if ( delta < -period * 0.5f )
    {
        delta += period;
    }

    float value = prev + delta * alpha;
    return fmodf(value, period);
}

void JonesFixedTick_BlendSurface(const JonesFixedTickSurfaceState* pPrev,
    const JonesFixedTickSurfaceState* pCur, SithSurface* pSurface, float alpha)
{
    if ( !pCur || !pCur->valid || !pSurface )
    {
        return;
    }

    if ( !pPrev || !pPrev->valid )
    {
        pSurface->face.texVertOffset = pCur->texVertOffset;
        pSurface->face.extraLight    = pCur->extraLight;
        return;
    }

    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    if ( pPrev->scrolling || pCur->scrolling )
    {
        pSurface->face.texVertOffset.x = JonesFixedTick_LerpWrapped(
            pPrev->texVertOffset.x, pCur->texVertOffset.x, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
        pSurface->face.texVertOffset.y = JonesFixedTick_LerpWrapped(
            pPrev->texVertOffset.y, pCur->texVertOffset.y, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
    }
    else
    {
        pSurface->face.texVertOffset = pCur->texVertOffset;
    }

    // Added: light animation intensity is a scalar ramp, so it blends directly.
    pSurface->face.extraLight.red   = pPrev->extraLight.red
        + (pCur->extraLight.red - pPrev->extraLight.red) * alpha;
    pSurface->face.extraLight.green = pPrev->extraLight.green
        + (pCur->extraLight.green - pPrev->extraLight.green) * alpha;
    pSurface->face.extraLight.blue  = pPrev->extraLight.blue
        + (pCur->extraLight.blue - pPrev->extraLight.blue) * alpha;
    pSurface->face.extraLight.alpha = pPrev->extraLight.alpha
        + (pCur->extraLight.alpha - pPrev->extraLight.alpha) * alpha;
}

void JonesFixedTick_RestoreSurface(const JonesFixedTickSurfaceState* pCur, SithSurface* pSurface)
{
    if ( !pCur || !pCur->valid || !pSurface )
    {
        return;
    }

    pSurface->face.texVertOffset = pCur->texVertOffset;
    pSurface->face.extraLight    = pCur->extraLight;
}

void JonesFixedTick_CaptureSky(JonesFixedTickSkyState* pDst, const SithWorld* pWorld)
{
    if ( !pDst || !pWorld )
    {
        if ( pDst )
        {
            pDst->valid = false;
        }
        return;
    }

    pDst->valid         = true;
    pDst->horizonOffset = pWorld->horizonSkyOffset;
    pDst->ceilingOffset = pWorld->ceilingSkyOffset;
}

void JonesFixedTick_BlendSky(const JonesFixedTickSkyState* pPrev,
    const JonesFixedTickSkyState* pCur, SithWorld* pWorld, float alpha)
{
    if ( !pCur || !pCur->valid || !pWorld )
    {
        return;
    }

    if ( !pPrev || !pPrev->valid )
    {
        pWorld->horizonSkyOffset = pCur->horizonOffset;
        pWorld->ceilingSkyOffset = pCur->ceilingOffset;
        return;
    }

    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    pWorld->horizonSkyOffset.x = JonesFixedTick_LerpWrapped(
        pPrev->horizonOffset.x, pCur->horizonOffset.x, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
    pWorld->horizonSkyOffset.y = JonesFixedTick_LerpWrapped(
        pPrev->horizonOffset.y, pCur->horizonOffset.y, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
    pWorld->ceilingSkyOffset.x = JonesFixedTick_LerpWrapped(
        pPrev->ceilingOffset.x, pCur->ceilingOffset.x, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
    pWorld->ceilingSkyOffset.y = JonesFixedTick_LerpWrapped(
        pPrev->ceilingOffset.y, pCur->ceilingOffset.y, alpha, JONESFIXEDTICK_SCROLL_PERIOD);
}

void JonesFixedTick_RestoreSky(const JonesFixedTickSkyState* pCur, SithWorld* pWorld)
{
    if ( !pCur || !pCur->valid || !pWorld )
    {
        return;
    }

    pWorld->horizonSkyOffset = pCur->horizonOffset;
    pWorld->ceilingSkyOffset = pCur->ceilingOffset;
}

void JonesFixedTick_CaptureSector(JonesFixedTickSectorState* pDst, const SithSector* pSector)
{
    if ( !pDst || !pSector )
    {
        if ( pDst )
        {
            pDst->valid = false;
        }
        return;
    }

    pDst->valid      = true;
    pDst->extraLight = pSector->extraLight;
}

void JonesFixedTick_BlendSector(const JonesFixedTickSectorState* pPrev,
    const JonesFixedTickSectorState* pCur, SithSector* pSector, float alpha)
{
    if ( !pCur || !pCur->valid || !pSector )
    {
        return;
    }

    if ( !pPrev || !pPrev->valid )
    {
        pSector->extraLight = pCur->extraLight;
        return;
    }

    if ( alpha < 0.0f )
    {
        alpha = 0.0f;
    }
    else if ( alpha > 1.0f )
    {
        alpha = 1.0f;
    }

    pSector->extraLight.red   = pPrev->extraLight.red
        + (pCur->extraLight.red - pPrev->extraLight.red) * alpha;
    pSector->extraLight.green = pPrev->extraLight.green
        + (pCur->extraLight.green - pPrev->extraLight.green) * alpha;
    pSector->extraLight.blue  = pPrev->extraLight.blue
        + (pCur->extraLight.blue - pPrev->extraLight.blue) * alpha;
    pSector->extraLight.alpha = pPrev->extraLight.alpha
        + (pCur->extraLight.alpha - pPrev->extraLight.alpha) * alpha;
}

void JonesFixedTick_RestoreSector(const JonesFixedTickSectorState* pCur, SithSector* pSector)
{
    if ( !pCur || !pCur->valid || !pSector )
    {
        return;
    }

    pSector->extraLight = pCur->extraLight;
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

static void JonesFixedTick_CaptureSurfaces(JonesFixedTickSurfaceState** ppDst, size_t* pNumSurfaces,
    const SithWorld** ppWorld)
{
    *pNumSurfaces = 0;
    *ppWorld      = NULL;

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !pWorld || !pWorld->aSurfaces || !JonesFixedTick_EnsureSurfaceCapacity(pWorld->numSurfaces) )
    {
        return;
    }

    JonesFixedTickSurfaceState* pDst = *ppDst;
    for ( size_t i = 0; i < pWorld->numSurfaces; ++i )
    {
        JonesFixedTick_CaptureSurface(&pDst[i], &pWorld->aSurfaces[i]);
    }

    *pNumSurfaces = pWorld->numSurfaces;
    *ppWorld      = pWorld;
}

static void JonesFixedTick_CaptureSectors(JonesFixedTickSectorState** ppDst, size_t* pNumSectors,
    const SithWorld** ppWorld)
{
    *pNumSectors = 0;
    *ppWorld     = NULL;

    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !pWorld || !pWorld->aSectors || !JonesFixedTick_EnsureSectorCapacity(pWorld->numSectors) )
    {
        return;
    }

    JonesFixedTickSectorState* pDst = *ppDst;
    for ( size_t i = 0; i < pWorld->numSectors; ++i )
    {
        JonesFixedTick_CaptureSector(&pDst[i], &pWorld->aSectors[i]);
    }

    *pNumSectors = pWorld->numSectors;
    *ppWorld     = pWorld;
}

void JonesFixedTick_BeforeTick(void)
{
    if ( !jonesFixedTick_bEnabled )
    {
        return;
    }

    JonesFixedTick_CaptureThings(&jonesFixedTick_pPrevThings, &jonesFixedTick_prevNumThings);
    JonesFixedTick_CaptureCamera(&jonesFixedTick_prevCamera, sithCamera_g_pCurCamera);
    JonesFixedTick_CaptureSurfaces(&jonesFixedTick_pPrevSurfaces, &jonesFixedTick_prevNumSurfaces,
        &jonesFixedTick_prevSurfaceWorld);
    JonesFixedTick_CaptureSectors(&jonesFixedTick_pPrevSectors, &jonesFixedTick_prevNumSectors,
        &jonesFixedTick_prevSectorWorld);
    JonesFixedTick_CaptureSky(&jonesFixedTick_prevSky, sithWorld_g_pCurrentWorld);
    jonesFixedTick_prevSkyWorld = jonesFixedTick_prevSky.valid ? sithWorld_g_pCurrentWorld : NULL;

    jonesFixedTick_prevSecGameTime = sithTime_g_secGameTime;
    jonesFixedTick_bHaveGameTime   = true;
}

void JonesFixedTick_AfterTick(void)
{
    if ( !jonesFixedTick_bEnabled )
    {
        return;
    }

    JonesFixedTick_CaptureThings(&jonesFixedTick_pCurThings, &jonesFixedTick_curNumThings);
    JonesFixedTick_CaptureCamera(&jonesFixedTick_curCamera, sithCamera_g_pCurCamera);
    JonesFixedTick_CaptureSurfaces(&jonesFixedTick_pCurSurfaces, &jonesFixedTick_curNumSurfaces,
        &jonesFixedTick_curSurfaceWorld);
    JonesFixedTick_CaptureSectors(&jonesFixedTick_pCurSectors, &jonesFixedTick_curNumSectors,
        &jonesFixedTick_curSectorWorld);
    JonesFixedTick_CaptureSky(&jonesFixedTick_curSky, sithWorld_g_pCurrentWorld);
    jonesFixedTick_curSkyWorld = jonesFixedTick_curSky.valid ? sithWorld_g_pCurrentWorld : NULL;

    jonesFixedTick_curSecGameTime = sithTime_g_secGameTime;
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

        JonesFixedTick_RestorePuppet(&pCur->puppet, pThing->renderData.pPuppet);
        if ( pCur->bParticle && pThing->type == SITH_THING_PARTICLE && pThing->renderData.data.pParticle )
        {
            pThing->renderData.data.pParticle->radius = pCur->particleRadius;
        }
        pThing->light.color     = pCur->lightColor;
        pThing->light.minRadius = pCur->lightMinRadius;
        pThing->light.maxRadius = pCur->lightMaxRadius;
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

    JonesFixedTick_BlendPuppet(&pPrev->puppet, &pCur->puppet, pThing->renderData.pPuppet, alpha);

    if ( pPrev->bParticle && pCur->bParticle && pThing->type == SITH_THING_PARTICLE
        && pThing->renderData.data.pParticle )
    {
        pThing->renderData.data.pParticle->radius = pPrev->particleRadius
            + (pCur->particleRadius - pPrev->particleRadius) * alpha;
    }
    else if ( pCur->bParticle && pThing->type == SITH_THING_PARTICLE
        && pThing->renderData.data.pParticle )
    {
        // Added: a particle created this tick is drawn at its current size.
        pThing->renderData.data.pParticle->radius = pCur->particleRadius;
    }

    pThing->light.color.red   = pPrev->lightColor.red
        + (pCur->lightColor.red - pPrev->lightColor.red) * alpha;
    pThing->light.color.green = pPrev->lightColor.green
        + (pCur->lightColor.green - pPrev->lightColor.green) * alpha;
    pThing->light.color.blue  = pPrev->lightColor.blue
        + (pCur->lightColor.blue - pPrev->lightColor.blue) * alpha;
    pThing->light.color.alpha = pPrev->lightColor.alpha
        + (pCur->lightColor.alpha - pPrev->lightColor.alpha) * alpha;
    pThing->light.minRadius = pPrev->lightMinRadius
        + (pCur->lightMinRadius - pPrev->lightMinRadius) * alpha;
    pThing->light.maxRadius = pPrev->lightMaxRadius
        + (pCur->lightMaxRadius - pPrev->lightMaxRadius) * alpha;
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

    JonesFixedTick_RestorePuppet(&pCur->puppet, pThing->renderData.pPuppet);

    if ( pCur->bParticle && pThing->type == SITH_THING_PARTICLE && pThing->renderData.data.pParticle )
    {
        pThing->renderData.data.pParticle->radius = pCur->particleRadius;
    }

    pThing->light.color     = pCur->lightColor;
    pThing->light.minRadius = pCur->lightMinRadius;
    pThing->light.maxRadius = pCur->lightMaxRadius;
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

    // Added: M9.22. The underwater wobble runs on render-time game time rather than the tick.
    if ( jonesFixedTick_bHaveGameTime )
    {
        sithTime_SetRenderGameTime(jonesFixedTick_prevSecGameTime
            + (jonesFixedTick_curSecGameTime - jonesFixedTick_prevSecGameTime) * alpha);
    }
    else
    {
        sithTime_SetRenderGameTime(sithTime_g_secGameTime);
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

    // Added: M9.22 surface/sky scroll and light animation for the drawn frame.
    if ( pWorld && pWorld->aSurfaces && jonesFixedTick_curSurfaceWorld == pWorld )
    {
        const size_t numSurfaces = pWorld->numSurfaces < jonesFixedTick_curNumSurfaces
            ? pWorld->numSurfaces
            : jonesFixedTick_curNumSurfaces;
        const bool bHavePrev = jonesFixedTick_prevSurfaceWorld == pWorld;

        for ( size_t i = 0; i < numSurfaces; ++i )
        {
            const JonesFixedTickSurfaceState* pPrev = bHavePrev && i < jonesFixedTick_prevNumSurfaces
                ? &jonesFixedTick_pPrevSurfaces[i]
                : NULL;
            JonesFixedTick_BlendSurface(pPrev, &jonesFixedTick_pCurSurfaces[i], &pWorld->aSurfaces[i], alpha);
        }
    }

    if ( pWorld && pWorld->aSectors && jonesFixedTick_curSectorWorld == pWorld )
    {
        const size_t numSectors = pWorld->numSectors < jonesFixedTick_curNumSectors
            ? pWorld->numSectors
            : jonesFixedTick_curNumSectors;
        const bool bHavePrev = jonesFixedTick_prevSectorWorld == pWorld;

        for ( size_t i = 0; i < numSectors; ++i )
        {
            const JonesFixedTickSectorState* pPrev = bHavePrev && i < jonesFixedTick_prevNumSectors
                ? &jonesFixedTick_pPrevSectors[i]
                : NULL;
            JonesFixedTick_BlendSector(pPrev, &jonesFixedTick_pCurSectors[i], &pWorld->aSectors[i], alpha);
        }
    }

    if ( pWorld && jonesFixedTick_curSkyWorld == pWorld && jonesFixedTick_curSky.valid )
    {
        const JonesFixedTickSkyState* pPrev = jonesFixedTick_prevSkyWorld == pWorld && jonesFixedTick_prevSky.valid
            ? &jonesFixedTick_prevSky
            : NULL;
        JonesFixedTick_BlendSky(pPrev, &jonesFixedTick_curSky, pWorld, alpha);
    }

    // Added: M9.22. The listener follows the drawn camera; the sector/ambient switch stays on the tick.
    sithSoundMixer_UpdateListener();
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

    // Added: M9.22 restore the tick values of animation state after the drawn frame.
    if ( pWorld && pWorld->aSurfaces && jonesFixedTick_curSurfaceWorld == pWorld )
    {
        const size_t numSurfaces = pWorld->numSurfaces < jonesFixedTick_curNumSurfaces
            ? pWorld->numSurfaces
            : jonesFixedTick_curNumSurfaces;
        for ( size_t i = 0; i < numSurfaces; ++i )
        {
            JonesFixedTick_RestoreSurface(&jonesFixedTick_pCurSurfaces[i], &pWorld->aSurfaces[i]);
        }
    }

    if ( pWorld && pWorld->aSectors && jonesFixedTick_curSectorWorld == pWorld )
    {
        const size_t numSectors = pWorld->numSectors < jonesFixedTick_curNumSectors
            ? pWorld->numSectors
            : jonesFixedTick_curNumSectors;
        for ( size_t i = 0; i < numSectors; ++i )
        {
            JonesFixedTick_RestoreSector(&jonesFixedTick_pCurSectors[i], &pWorld->aSectors[i]);
        }
    }

    if ( pWorld && jonesFixedTick_curSkyWorld == pWorld )
    {
        JonesFixedTick_RestoreSky(&jonesFixedTick_curSky, pWorld);
    }

    sithTime_SetRenderGameTime(sithTime_g_secGameTime);

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

    if ( jonesFixedTick_pPrevSurfaces )
    {
        STDFREE(jonesFixedTick_pPrevSurfaces);
    }

    if ( jonesFixedTick_pCurSurfaces )
    {
        STDFREE(jonesFixedTick_pCurSurfaces);
    }

    if ( jonesFixedTick_pPrevSectors )
    {
        STDFREE(jonesFixedTick_pPrevSectors);
    }

    if ( jonesFixedTick_pCurSectors )
    {
        STDFREE(jonesFixedTick_pCurSectors);
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

    jonesFixedTick_pPrevSurfaces   = NULL;
    jonesFixedTick_pCurSurfaces    = NULL;
    jonesFixedTick_surfaceCapacity = 0;
    jonesFixedTick_prevNumSurfaces = 0;
    jonesFixedTick_curNumSurfaces  = 0;
    jonesFixedTick_prevSurfaceWorld = NULL;
    jonesFixedTick_curSurfaceWorld  = NULL;
    jonesFixedTick_prevSkyWorld     = NULL;
    jonesFixedTick_curSkyWorld      = NULL;

    jonesFixedTick_pPrevSectors   = NULL;
    jonesFixedTick_pCurSectors    = NULL;
    jonesFixedTick_sectorCapacity = 0;
    jonesFixedTick_prevNumSectors = 0;
    jonesFixedTick_curNumSectors  = 0;
    jonesFixedTick_prevSectorWorld = NULL;
    jonesFixedTick_curSectorWorld  = NULL;

    jonesFixedTick_prevSecGameTime = 0.0f;
    jonesFixedTick_curSecGameTime  = 0.0f;
    jonesFixedTick_bHaveGameTime   = false;

    memset(&jonesFixedTick_prevSky, 0, sizeof(jonesFixedTick_prevSky));
    memset(&jonesFixedTick_curSky, 0, sizeof(jonesFixedTick_curSky));
    memset(&jonesFixedTick_prevCamera, 0, sizeof(jonesFixedTick_prevCamera));
    memset(&jonesFixedTick_curCamera, 0, sizeof(jonesFixedTick_curCamera));

    JonesFixedTick_AccumReset(&jonesFixedTick_accumulator);

    // Added: the override is deliberately kept, so the headless runner can set it once for all runs.
    sithTime_SetFixedStep(0);
}
