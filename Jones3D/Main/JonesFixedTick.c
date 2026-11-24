// Added: M9.20 fixed simulation tick with interpolated things and camera (ADR-0039). JonesMain
// feeds a game-time accumulator from the measured frame delta, runs zero or more fixed ticks per
// drawn frame and interpolates the world and camera between the last two ticks for the draw. The
// option is either off or on at 50 Hz, the game's physics step (ADR-0030); Enhanced defaults it
// on, Retail and QOL-off builds always keep the measured-frame-time loop, and the headless runner
// stays off unless --fixed-tick asks for a rate.
#include "JonesFixedTick.h"

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdPuppet.h>
#include <rdroid/Primitives/rdModel3.h>
#include <rdroid/Raster/rdCache.h>

#include <sith/Devices/sithSoundMixer.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>
#include <std/General/stdUtil.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define JONESFIXEDTICK_MAXTHINGS 0x40000

#ifdef J3D_QOL_IMPROVEMENTS
// Added: the rates the dev-only --fixed-tick override may select for comparisons, nearest with a
// tie snapping down. The cfg offers only Off and 50 Hz now, but the headless dev path keeps 30,
// 60 and 120 so two builds can be compared at other rates.
static const int jonesFixedTick_aDevRates[] = { 0, 30, 50, 60, 120 };
#endif

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

// Added: dev-only --draw-trace state. The file stays NULL (and every trace call inert) unless the
// headless runner opens it. The joint indices are resolved once from the player model's node names.
static FILE*  jonesFixedTick_pDrawTraceFile;
static size_t jonesFixedTick_drawTraceCount;
static int    jonesFixedTick_drawTraceRootJoint = -1;
static int    jonesFixedTick_drawTraceHeadJoint = -1;
static int    jonesFixedTick_drawTraceHandJoint = -1;
static int    jonesFixedTick_drawTraceFootJoint = -1;
static bool   jonesFixedTick_bDrawTraceJointsResolved;

// Added: the previous and current tick's complete joint positions, so the trace can prove that a
// drawn joint lies on the segment between them. Only the player's model is captured, only while the
// trace is open.
static rdVector3 jonesFixedTick_aDrawTracePrevJoints[RDPUPPET_MAX_KFNODES];
static rdVector3 jonesFixedTick_aDrawTraceCurJoints[RDPUPPET_MAX_KFNODES];
static size_t    jonesFixedTick_drawTraceNumJoints;
static bool      jonesFixedTick_bDrawTraceHaveTickJoints;

// Added: the trace builds the tick endpoint skeletons to record them, which would overwrite the
// drawn state. Save the model's joint matrices and frame stamp before that and put them back, so a
// trace run draws exactly what a run without --draw-trace draws.
static rdMatrix34 jonesFixedTick_aDrawTraceSavedMatrices[RDPUPPET_MAX_KFNODES];
static int        jonesFixedTick_drawTraceSavedFrameNum;

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

int JonesFixedTick_DefaultRate(void)
{
    return JONESFIXEDTICK_DEFAULT_RATE;
}

int JonesFixedTick_ClampRate(int rateHz)
{
    // Added: the option is Off or On at 50 Hz. Any positive value (an old 30/60/120 from an
    // earlier test build) becomes 50; the same 20 ms step the physics already integrates in.
    if ( rateHz <= 0 )
    {
        return 0;
    }

    return JONESFIXEDTICK_RATE_HZ;
}

int JonesFixedTick_ResolveConfigRate(int rateHz)
{
    // Added: a hand-edited negative value is garbage; keep the build's default instead.
    if ( rateHz < 0 )
    {
        return JonesFixedTick_DefaultRate();
    }

    return JonesFixedTick_ClampRate(rateHz);
}

#ifdef J3D_QOL_IMPROVEMENTS
// Added: the dev-only override keeps the old 30/50/60/120 comparison rates instead of collapsing
// them to 50, so a headless run can still compare two builds at another tick rate.
static int JonesFixedTick_ClampDevRate(int rateHz)
{
    if ( rateHz <= 0 )
    {
        return 0;
    }

    int bestRate  = jonesFixedTick_aDevRates[0];
    int bestDelta = rateHz - bestRate;

    for ( size_t i = 1; i < STD_ARRAYLEN(jonesFixedTick_aDevRates); ++i )
    {
        const int rate  = jonesFixedTick_aDevRates[i];
        const int delta = rateHz >= rate ? rateHz - rate : rate - rateHz;
        if ( delta < bestDelta ) // Added: a tie keeps the lower rate, so the result is deterministic
        {
            bestRate  = rate;
            bestDelta = delta;
        }
    }

    return bestRate;
}
#endif

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
    // stays in use. Enhanced runs at 50 Hz by default; the headless runner pins the override to 0
    // unless --fixed-tick asks for a rate, so the trace references stay on the measured loop.
    int rate = 0;
#ifdef J3D_QOL_IMPROVEMENTS
    if ( jonesFixedTick_overrideRateHz >= 0 )
    {
        // Added: dev/headless override, clamped to the dev comparison rates.
        rate = JonesFixedTick_ClampDevRate(jonesFixedTick_overrideRateHz);
    }
    else
    {
        rate = JonesFixedTick_ResolveConfigRate(stdConfig_GetInt(
            JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, JonesFixedTick_DefaultRate()));
    }
#endif

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

void JonesFixedTick_SetDrawAlpha(float alpha)
{
    // Added: tests and the headless analysis sample a drawn frame at an exact alpha. BeginDraw
    // clamps the value, so no range check is needed here.
    jonesFixedTick_alpha = alpha;
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
    pDst->pModel3     = pThing->renderData.type == RD_THING_MODEL3
        ? pThing->renderData.data.pModel3
        : NULL;

    // Added: M9.22. Puppet pose, particle size and thing light for a drawn frame.
    JonesFixedTick_CapturePuppet(&pDst->puppet, pThing->renderData.pPuppet);
    pDst->bSkipBuildingJoints = pThing->renderData.bSkipBuildingJoints != 0;

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

    // Added: M9.24. Presentation values the scene draw reads from the camera, blended with the
    // pose so the sky, culling and focus distance do not snap at tick boundaries.
    pDst->lookPos     = pCamera->lookPos;
    pDst->focusPos    = pCamera->focusPos;
    pDst->camSpot     = sithCamera_g_camSpot;
    pDst->camLookSpot = sithCamera_g_camLookSpot;
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
        pState->lowPriority  = pTrack->lowPriority;
        pState->highPriority = pTrack->highPriority;
    }
}

void JonesFixedTick_BlendPuppet(const JonesFixedTickPuppetState* pPrev,
    JonesFixedTickPuppetState* pCur, rdPuppet* pPuppet, float alpha)
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

    const bool bHavePrev = pPrev && pPrev->valid && pPrev->pPuppet == pCur->pPuppet;

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        const JonesFixedTickPuppetTrackState* pCurTrack = &pCur->aTracks[trackNum];
        if ( !pCurTrack->valid )
        {
            continue;
        }

        if ( pPuppet->aTracks[trackNum].pKFTrack != pCurTrack->pKFTrack )
        {
            // The live slot no longer matches the snapshot. The simulation owns it.
            continue;
        }

        float frame       = pCurTrack->curFrame;
        float blendWeight = pCurTrack->blendWeight;

        // Added: the same track continues normally. A replaced, removed, appeared or looped track
        // is a discontinuity owned by JonesFixedTick_BlendPuppetPose, so this path only samples the
        // current tick there instead of inventing an intermediate keyframe pose.
        const JonesFixedTickPuppetTrackState* pPrevTrack = bHavePrev
            ? &pPrev->aTracks[trackNum]
            : NULL;
        if ( pPrevTrack && pPrevTrack->valid
            && pPrevTrack->pKFTrack == pCurTrack->pKFTrack
            && pCurTrack->curFrame >= pPrevTrack->curFrame )
        {
            frame = pPrevTrack->curFrame
                + (pCurTrack->curFrame - pPrevTrack->curFrame) * alpha;
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

// Added: M9.24b. The previous tick's complete skeleton while a pose crossfade is built. One thing
// is drawn at a time, so one array is enough; the model node limit matches the puppet cache.
static rdMatrix34 jonesFixedTick_aPoseCrossfadePrevMatrices[RDPUPPET_MAX_KFNODES];

// Added: whether the last JonesFixedTick_BlendPuppetPose call wrote the drawn joints for a pose
// crossfade. The stamp alone cannot tell: a tick build in the same rdCache frame also matches it.
static bool jonesFixedTick_bPoseCrossfadeBuilt;

// Added: the drawn frame rebuilds a thing's joints from the interpolated placement and sampled
// pose, which overwrites the world-space joint matrices and frame stamp the tick left. The
// simulation may read them on the next tick (path movement, attached meshes, the internal
// camera), so BeginDraw keeps them and EndDraw puts them back exactly.
typedef struct sJonesFixedTickSavedJoints
{
    bool   valid;
    size_t numJoints;
    size_t poolOffset;
    int    rdFrameNum;
    int    bSkipBuildingJoints;
} JonesFixedTickSavedJoints;

static JonesFixedTickSavedJoints* jonesFixedTick_pSavedJoints;
static size_t                     jonesFixedTick_savedJointsCapacity;
static size_t                     jonesFixedTick_numSavedJoints;
static rdMatrix34*                jonesFixedTick_pSavedJointPool;
static size_t                     jonesFixedTick_savedJointPoolCapacity;
static size_t                     jonesFixedTick_savedJointPoolUsed;

static bool JonesFixedTick_PuppetTrackFrameValid(const JonesFixedTickPuppetTrackState* pTrack)
{
    return !pTrack->valid || (pTrack->pKFTrack
        && isfinite(pTrack->curFrame)
        && pTrack->curFrame >= 0.0f
        && pTrack->curFrame <= (float)pTrack->pKFTrack->numFrames);
}

bool JonesFixedTick_PuppetNeedsPoseCrossfade(const JonesFixedTickPuppetState* pPrev,
    const JonesFixedTickPuppetState* pCur)
{
    if ( !pPrev || !pPrev->valid || !pCur || !pCur->valid || pPrev->pPuppet != pCur->pPuppet )
    {
        return true;
    }

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        const JonesFixedTickPuppetTrackState* pPrevTrack = &pPrev->aTracks[trackNum];
        const JonesFixedTickPuppetTrackState* pCurTrack  = &pCur->aTracks[trackNum];
        const bool bPrevValid = pPrevTrack->valid && pPrevTrack->pKFTrack;
        const bool bCurValid  = pCurTrack->valid && pCurTrack->pKFTrack;

        if ( bPrevValid != bCurValid )
        {
            // Added: a track appeared or was removed; there is no matching keyframe to sample.
            return true;
        }

        if ( !bPrevValid )
        {
            continue;
        }

        if ( pPrevTrack->pKFTrack != pCurTrack->pKFTrack )
        {
            // Added: a replaced track. Sampling the old keyframe at the new track's frame would
            // show a wrong pose, so the complete tick poses are crossfaded instead.
            return true;
        }

        if ( pCurTrack->curFrame < pPrevTrack->curFrame - 0.001f )
        {
            // Added: the track looped or was rewound; the new start pose is not the continuation
            // of the old frame. Ignore sub-frame float noise on a paused track.
            return true;
        }
    }

    return false;
}

bool JonesFixedTick_PuppetCanPoseCrossfade(const JonesFixedTickPuppetState* pPrev,
    const JonesFixedTickPuppetState* pCur)
{
    if ( !pPrev || !pPrev->valid || !pCur || !pCur->valid || pPrev->pPuppet != pCur->pPuppet )
    {
        return false;
    }

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        if ( !JonesFixedTick_PuppetTrackFrameValid(&pPrev->aTracks[trackNum])
            || !JonesFixedTick_PuppetTrackFrameValid(&pCur->aTracks[trackNum]) )
        {
            // Added: an unknown or out-of-range old frame cannot be sampled; snap instead.
            return false;
        }
    }

    return true;
}

void JonesFixedTick_BlendJointMatrix(rdMatrix34* pOut, const rdMatrix34* pPrev,
    const rdMatrix34* pCur, float alpha)
{
    // Added: blend the complete world matrices. The translation is linear, so every joint position
    // lands on the segment between the two tick poses; Normalize34 re-orthonormalises the rotation.
    JonesFixedTick_LerpOrient(pOut, pPrev, pCur, alpha);
}

static void JonesFixedTick_SetPuppetToSnapshot(rdPuppet* pPuppet,
    const JonesFixedTickPuppetState* pState)
{
    // The snapshot describes a tick pose; the live pause flag belongs to the simulation and must
    // not hide it from the drawn skeleton.
    pPuppet->bPaused = false;

    for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
    {
        rdPuppetTrack* pTrack = &pPuppet->aTracks[trackNum];
        const JonesFixedTickPuppetTrackState* pSrc = &pState->aTracks[trackNum];

        STD_ZEROMEM(pTrack->aCurKfNodeEntryNums, sizeof(pTrack->aCurKfNodeEntryNums));
        pTrack->pfCallback = NULL;

        if ( pSrc->valid && pSrc->pKFTrack )
        {
            pTrack->status        = RDPUPPET_TRACK_PLAYING | RDPUPPET_TRACK_UNKNOWN_1;
            pTrack->pKFTrack      = (rdKeyframe*)pSrc->pKFTrack;
            pTrack->curFrame      = pSrc->curFrame;
            pTrack->prevFrame     = pSrc->curFrame;
            pTrack->blendWeight   = pSrc->blendWeight;
            pTrack->lowPriority   = pSrc->lowPriority;
            pTrack->highPriority  = pSrc->highPriority;
            pTrack->fps           = pSrc->pKFTrack->fps;
            pTrack->playbackSpeed = 1.0f;
            pTrack->guid          = 1;
        }
        else
        {
            pTrack->status     = 0;
            pTrack->pKFTrack   = NULL;
        }
    }
}

static void JonesFixedTick_BuildPuppetSnapshotPose(const JonesFixedTickPuppetState* pPose,
    const JonesFixedTickThingState* pThingState, SithThing* pThing)
{
    rdThing* pRender = &pThing->renderData;
    rdPuppet* pLive  = pRender->pPuppet;

    rdPuppet snapshotPuppet = *pLive;
    JonesFixedTick_SetPuppetToSnapshot(&snapshotPuppet, pPose);

    // Added: a force-move or path pose (skipped joints) was built by the simulation at the origin
    // and shifted so its root joint sits on the thing position; the animation's root motion is
    // already in the position. Building it at the position would add that offset a second time and
    // draw the old pose displaced (Indy pops up when a ledge hang or climb ends), so rebuild it the
    // same way.
    rdMatrix34 placement = pThingState->orient;
    placement.dvec       = pThingState->bSkipBuildingJoints ? rdroid_g_zeroVector3 : pThingState->pos;

    const int bSkipBuildingJoints = pRender->bSkipBuildingJoints;
    pRender->pPuppet              = &snapshotPuppet;
    pRender->bSkipBuildingJoints  = 0;
    rdPuppet_BuildJointMatrices(pRender, &placement);
    pRender->bSkipBuildingJoints = bSkipBuildingJoints;
    pRender->pPuppet             = pLive;

    if ( pThingState->bSkipBuildingJoints && pRender->data.pModel3 )
    {
        rdVector3 rootShift;
        rdVector_Sub3(&rootShift, &pThingState->pos, &pRender->paJointMatrices[0].dvec);
        for ( size_t jointNum = 0; jointNum < pRender->data.pModel3->numHNodes; ++jointNum )
        {
            rdVector_Add3Acc(&pRender->paJointMatrices[jointNum].dvec, &rootShift);
        }
    }
}

// Added: M9.24b. Draw the complete previous and current tick skeletons and blend their world
// matrices. Returns true when it has written the drawn joints (the scene draw must not rebuild
// them); false leaves the normal per-track sample in place for the draw.
static bool JonesFixedTick_BlendPuppetPose(const JonesFixedTickThingState* pPrevThing,
    JonesFixedTickThingState* pCurThing, SithThing* pThing, float alpha)
{
    jonesFixedTick_bPoseCrossfadeBuilt = false;

    rdThing*  pRender = &pThing->renderData;
    rdPuppet* pPuppet = pRender->pPuppet;
    const JonesFixedTickPuppetState* pPrev = pPrevThing ? &pPrevThing->puppet : NULL;
    JonesFixedTickPuppetState*       pCur  = pCurThing ? &pCurThing->puppet : NULL;

    if ( !pCur || !pCur->valid || !pPuppet || pPuppet != pCur->pPuppet )
    {
        return false;
    }

    if ( pRender->type != RD_THING_MODEL3 || !pRender->data.pModel3 || !pRender->paJointMatrices )
    {
        JonesFixedTick_BlendPuppet(pPrev, pCur, pPuppet, alpha);
        return false;
    }

    if ( !JonesFixedTick_PuppetNeedsPoseCrossfade(pPrev, pCur) )
    {
        JonesFixedTick_BlendPuppet(pPrev, pCur, pPuppet, alpha);
        return false;
    }

    const size_t numJoints = pRender->data.pModel3->numHNodes;
    if ( !JonesFixedTick_PuppetCanPoseCrossfade(pPrev, pCur)
        || numJoints > RDPUPPET_MAX_KFNODES || pRender->bSkipBuildingJoints )
    {
        // Added: different skeleton/node sets, an unknown old frame or a skipped model cannot be
        // blended correctly; draw the exact current tick pose instead.
        JonesFixedTick_BlendPuppet(NULL, pCur, pPuppet, 1.0f);
        return false;
    }

    // Build the previous tick's skeleton, keep it, then build the current one in place.
    JonesFixedTick_BuildPuppetSnapshotPose(pPrev, pPrevThing, pThing);
    memcpy(jonesFixedTick_aPoseCrossfadePrevMatrices, pRender->paJointMatrices,
        numJoints * sizeof(jonesFixedTick_aPoseCrossfadePrevMatrices[0]));

    JonesFixedTick_BuildPuppetSnapshotPose(pCur, pCurThing, pThing);

    for ( size_t jointNum = 0; jointNum < numJoints; ++jointNum )
    {
        JonesFixedTick_BlendJointMatrix(&pRender->paJointMatrices[jointNum],
            &jonesFixedTick_aPoseCrossfadePrevMatrices[jointNum],
            &pRender->paJointMatrices[jointNum], alpha);
    }

    // Added: tell the model draw the joints are already built for this frame.
    pRender->rdFrameNum = (int)rdCache_GetFrameNum();
    jonesFixedTick_bPoseCrossfadeBuilt = true;
    return true;
}

static bool JonesFixedTick_EnsureSavedJointsCapacity(size_t numThings)
{
    if ( numThings <= jonesFixedTick_savedJointsCapacity )
    {
        return true;
    }

    size_t newCapacity = jonesFixedTick_savedJointsCapacity ? jonesFixedTick_savedJointsCapacity : 64;
    while ( newCapacity < numThings )
    {
        newCapacity *= 2;
    }

    JonesFixedTickSavedJoints* pSaved = (JonesFixedTickSavedJoints*)STDREALLOC(
        jonesFixedTick_pSavedJoints, newCapacity * sizeof(*pSaved));
    if ( !pSaved )
    {
        return false;
    }

    jonesFixedTick_pSavedJoints          = pSaved;
    jonesFixedTick_savedJointsCapacity   = newCapacity;
    return true;
}

static bool JonesFixedTick_EnsureSavedJointPoolCapacity(size_t numJoints)
{
    if ( numJoints <= jonesFixedTick_savedJointPoolCapacity )
    {
        return true;
    }

    size_t newCapacity = jonesFixedTick_savedJointPoolCapacity ? jonesFixedTick_savedJointPoolCapacity : 256;
    while ( newCapacity < numJoints )
    {
        newCapacity *= 2;
    }

    rdMatrix34* pPool = (rdMatrix34*)STDREALLOC(
        jonesFixedTick_pSavedJointPool, newCapacity * sizeof(*pPool));
    if ( !pPool )
    {
        return false;
    }

    jonesFixedTick_pSavedJointPool         = pPool;
    jonesFixedTick_savedJointPoolCapacity  = newCapacity;
    return true;
}

static void JonesFixedTick_BeginDrawJoints(size_t numThings)
{
    jonesFixedTick_numSavedJoints     = 0;
    jonesFixedTick_savedJointPoolUsed = 0;

    if ( !JonesFixedTick_EnsureSavedJointsCapacity(numThings) )
    {
        return;
    }

    jonesFixedTick_numSavedJoints = numThings;
    for ( size_t i = 0; i < numThings; ++i )
    {
        jonesFixedTick_pSavedJoints[i].valid = false;
    }
}

// Added: keep the tick's joint matrices and stamp for one model thing before BlendThing or a draw
// rebuild overwrites them.
static void JonesFixedTick_SaveThingJoints(size_t thingNum, const SithThing* pThing)
{
    if ( thingNum >= jonesFixedTick_numSavedJoints || !pThing )
    {
        return;
    }

    JonesFixedTickSavedJoints* pSaved = &jonesFixedTick_pSavedJoints[thingNum];
    pSaved->valid = false;

    const rdThing* pRender = &pThing->renderData;
    if ( pRender->type != RD_THING_MODEL3 || !pRender->data.pModel3 || !pRender->paJointMatrices )
    {
        return;
    }

    const size_t numJoints = pRender->data.pModel3->numHNodes;
    if ( !numJoints )
    {
        return;
    }

    if ( !JonesFixedTick_EnsureSavedJointPoolCapacity(jonesFixedTick_savedJointPoolUsed + numJoints) )
    {
        return;
    }

    // Added: take the pool after EnsureSavedJointPoolCapacity; it may have reallocated it.
    pSaved->valid                = true;
    pSaved->numJoints            = numJoints;
    pSaved->poolOffset           = jonesFixedTick_savedJointPoolUsed;
    pSaved->rdFrameNum           = pRender->rdFrameNum;
    pSaved->bSkipBuildingJoints  = pRender->bSkipBuildingJoints;

    memcpy(&jonesFixedTick_pSavedJointPool[pSaved->poolOffset], pRender->paJointMatrices,
        numJoints * sizeof(rdMatrix34));
    jonesFixedTick_savedJointPoolUsed += numJoints;
}

// Added: a skipped-joint thing was built by the simulation with the placement at the origin and
// then shifted so its root joint sits exactly on the thing position (the animation's root motion is
// consumed as thing movement). It must not be rebuilt by the draw: rebuilding with the thing's
// position as the placement would add the root animation offset again. Instead move the stored
// world matrices from the tick placement to the drawn one: rotate about the tick root by the
// interpolated orientation and translate the root to the drawn position.
static void JonesFixedTick_TransformSkippedThingJoints(const JonesFixedTickSavedJoints* pSaved,
    const JonesFixedTickThingState* pCur, SithThing* pThing)
{
    rdThing* pRender = &pThing->renderData;
    if ( !pRender->paJointMatrices || !pRender->data.pModel3 )
    {
        return;
    }

    size_t numJoints = pRender->data.pModel3->numHNodes;
    if ( numJoints > pSaved->numJoints )
    {
        numJoints = pSaved->numJoints;
    }

    rdMatrix34 tickOrient = pCur->orient;
    tickOrient.dvec       = rdroid_g_zeroVector3;

    rdMatrix34 invTickOrient;
    rdMatrix_InvertOrtho34(&invTickOrient, &tickOrient);

    rdMatrix34 drawOrient = pThing->orient;
    drawOrient.dvec       = rdroid_g_zeroVector3;

    // R_delta = R_draw * inverse(R_tick); the translation is applied about the tick root below.
    rdMatrix34 deltaOrient;
    rdMatrix_Multiply34(&deltaOrient, &drawOrient, &invTickOrient);

    // Added: pivot on the root the build actually produced, so the drawn root lands exactly on the
    // drawn thing position even if a later tick step nudged the thing after the shift.
    const rdVector3 tickRoot = jonesFixedTick_pSavedJointPool[pSaved->poolOffset].dvec;
    const rdVector3 negTickRoot = { -tickRoot.x, -tickRoot.y, -tickRoot.z };
    for ( size_t jointNum = 0; jointNum < numJoints; ++jointNum )
    {
        rdMatrix34 shifted;
        rdMatrix_Copy34(&shifted, &pRender->paJointMatrices[jointNum]);
        rdMatrix_PostTranslate34(&shifted, &negTickRoot);

        rdMatrix34 rotated;
        rdMatrix_Multiply34(&rotated, &deltaOrient, &shifted);
        rdVector_Add3Acc(&rotated.dvec, &pThing->pos);

        rdMatrix_Copy34(&pRender->paJointMatrices[jointNum], &rotated);
    }
}

// Added: after the pose blend, make the drawn frame use the interpolated placement. A pose
// crossfade already built the interpolated skeleton and stamped the draw's cache frame; leave it.
// A skipped-joint thing keeps its flag so the draw uses the stored pose, which is moved from the
// tick placement to the interpolated one. Everything else lets the scene draw rebuild from the
// interpolated placement and sampled pose by invalidating its frame stamp.
static void JonesFixedTick_InvalidateStaleThingJoints(size_t thingNum,
    const JonesFixedTickThingState* pCur, SithThing* pThing)
{
    if ( thingNum >= jonesFixedTick_numSavedJoints || !pCur || !pThing
        || !jonesFixedTick_pSavedJoints[thingNum].valid )
    {
        return;
    }

    if ( jonesFixedTick_bPoseCrossfadeBuilt )
    {
        // The pose crossfade built the interpolated skeleton and stamped it for this draw.
        return;
    }

    if ( jonesFixedTick_pSavedJoints[thingNum].bSkipBuildingJoints )
    {
        JonesFixedTick_TransformSkippedThingJoints(&jonesFixedTick_pSavedJoints[thingNum], pCur,
            pThing);
        return;
    }

    pThing->renderData.rdFrameNum = -1;
}

static void JonesFixedTick_EndDrawJoints(SithThing* pThings, size_t numThings)
{
    const size_t numSaved = jonesFixedTick_numSavedJoints < numThings
        ? jonesFixedTick_numSavedJoints
        : numThings;

    for ( size_t i = 0; i < numSaved; ++i )
    {
        const JonesFixedTickSavedJoints* pSaved = &jonesFixedTick_pSavedJoints[i];
        if ( !pSaved->valid )
        {
            continue;
        }

        rdThing* pRender = &pThings[i].renderData;
        if ( !pRender->paJointMatrices
            || !pRender->data.pModel3
            || pRender->data.pModel3->numHNodes != pSaved->numJoints )
        {
            continue;
        }

        memcpy(pRender->paJointMatrices, &jonesFixedTick_pSavedJointPool[pSaved->poolOffset],
            pSaved->numJoints * sizeof(rdMatrix34));
        pRender->rdFrameNum          = pSaved->rdFrameNum;
        pRender->bSkipBuildingJoints = pSaved->bSkipBuildingJoints;
    }

    jonesFixedTick_numSavedJoints     = 0;
    jonesFixedTick_savedJointPoolUsed = 0;
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
    JonesFixedTickThingState* pCur, SithThing* pThing, float alpha)
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
        || pPrev->pModel3 != pCur->pModel3 || JonesFixedTick_IsTeleport(&pPrev->pos, &pCur->pos) )
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

    JonesFixedTick_BlendPuppetPose(pPrev, pCur, pThing, alpha);

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
        pCamera->lookPos     = pCur->lookPos;
        pCamera->focusPos    = pCur->focusPos;
        sithCamera_g_camSpot     = pCur->camSpot;
        sithCamera_g_camLookSpot = pCur->camLookSpot;
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

        // Added: M9.24. The scene draw reads lookPos (sky, culling, DoF distance) and the camera
        // spot globals; blend them with the pose instead of leaving the post-tick values in place.
        JonesFixedTick_LerpVector(&pCamera->lookPos, &pPrev->lookPos, &pCur->lookPos, alpha);
        JonesFixedTick_LerpVector(&pCamera->focusPos, &pPrev->focusPos, &pCur->focusPos, alpha);
        JonesFixedTick_LerpVector(&sithCamera_g_camSpot, &pPrev->camSpot, &pCur->camSpot, alpha);
        JonesFixedTick_LerpVector(&sithCamera_g_camLookSpot, &pPrev->camLookSpot, &pCur->camLookSpot, alpha);
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
    pCamera->lookPos     = pCur->lookPos;
    pCamera->focusPos    = pCur->focusPos;
    sithCamera_g_camSpot     = pCur->camSpot;
    sithCamera_g_camLookSpot = pCur->camLookSpot;

    rdCamera_SetFOV(&pCamera->rdCamera, pCamera->fov);
    rdCamera_SetAspectRatio(&pCamera->rdCamera, pCamera->aspectRatio);
}

// Added: copy the player's world joint positions into one of the trace endpoint arrays.
static void JonesFixedTick_CopyTraceJoints(rdVector3* pDst, const SithThing* pPlayer)
{
    const size_t numJoints = jonesFixedTick_drawTraceNumJoints;
    for ( size_t i = 0; i < numJoints; ++i )
    {
        pDst[i] = pPlayer->renderData.paJointMatrices[i].dvec;
    }
}

// Added: dev-only. Capture the complete previous- and current-tick skeleton poses before the draw
// blends them, so the trace can check a drawn joint against the segment between the two ticks.
// The live puppet is left exactly as the tick left it.
static void JonesFixedTick_CaptureTraceTickJoints(const JonesFixedTickThingState* pPrevThing,
    JonesFixedTickThingState* pCurThing, SithThing* pPlayer)
{
    jonesFixedTick_bDrawTraceHaveTickJoints = false;

    if ( !jonesFixedTick_pDrawTraceFile || !pPlayer || !pCurThing
        || pPlayer->renderData.type != RD_THING_MODEL3
        || !pPlayer->renderData.paJointMatrices || !pPlayer->renderData.pPuppet
        || !pPlayer->renderData.data.pModel3 )
    {
        return;
    }

    const size_t numJoints = pPlayer->renderData.data.pModel3->numHNodes;
    if ( numJoints <= 3 || numJoints > RDPUPPET_MAX_KFNODES )
    {
        return;
    }

    jonesFixedTick_drawTraceNumJoints = numJoints;

    // Added: the endpoint builds below overwrite the joints the scene draw will inspect. Keep the
    // state the tick left so the draw and TraceDraw see the real drawn skeleton, not the trace's.
    memcpy(jonesFixedTick_aDrawTraceSavedMatrices, pPlayer->renderData.paJointMatrices,
        numJoints * sizeof(jonesFixedTick_aDrawTraceSavedMatrices[0]));
    jonesFixedTick_drawTraceSavedFrameNum = pPlayer->renderData.rdFrameNum;

    // The render pass places a thing with its position in orient.dvec; the tick state keeps that
    // zero. Use the same placement the scene draw will use for each tick.
    const JonesFixedTickPuppetState* pPrevPuppet = pPrevThing ? &pPrevThing->puppet : NULL;
    JonesFixedTickPuppetState*       pCurPuppet  = &pCurThing->puppet;

    if ( !pCurPuppet->valid )
    {
        memcpy(pPlayer->renderData.paJointMatrices, jonesFixedTick_aDrawTraceSavedMatrices,
            numJoints * sizeof(jonesFixedTick_aDrawTraceSavedMatrices[0]));
        pPlayer->renderData.rdFrameNum = jonesFixedTick_drawTraceSavedFrameNum;
        return;
    }

    // Build each tick's complete skeleton exactly as the pose crossfade does, so the recorded
    // endpoints are the true tick poses regardless of track replacement, removal or looping.
    JonesFixedTick_BuildPuppetSnapshotPose(pCurPuppet, pCurThing, pPlayer);
    JonesFixedTick_CopyTraceJoints(jonesFixedTick_aDrawTraceCurJoints, pPlayer);

    if ( pPrevPuppet && pPrevPuppet->valid && pPrevPuppet->pPuppet == pCurPuppet->pPuppet )
    {
        JonesFixedTick_BuildPuppetSnapshotPose(pPrevPuppet, pPrevThing, pPlayer);
        JonesFixedTick_CopyTraceJoints(jonesFixedTick_aDrawTracePrevJoints, pPlayer);
    }
    else
    {
        for ( size_t i = 0; i < numJoints; ++i )
        {
            jonesFixedTick_aDrawTracePrevJoints[i] = jonesFixedTick_aDrawTraceCurJoints[i];
        }
    }

    // Put the tick's joints and stamp back; the scene draw and TraceDraw must see the drawn state.
    memcpy(pPlayer->renderData.paJointMatrices, jonesFixedTick_aDrawTraceSavedMatrices,
        numJoints * sizeof(jonesFixedTick_aDrawTraceSavedMatrices[0]));
    pPlayer->renderData.rdFrameNum = jonesFixedTick_drawTraceSavedFrameNum;
    jonesFixedTick_bDrawTraceHaveTickJoints = true;
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

        // Added: keep the tick's joint matrices and frame stamp before the blend or the scene draw
        // overwrites them, so EndDraw can put the simulation-visible state back exactly.
        JonesFixedTick_BeginDrawJoints(numThings);

        for ( size_t i = 0; i < numThings; ++i )
        {
            JonesFixedTickThingState* pCur = &jonesFixedTick_pCurThings[i];
            if ( !pCur->valid )
            {
                continue;
            }

            SithThing* pThing = &pWorld->aThings[i];

            // Added: the dev trace may build endpoint skeletons; saving first keeps its restore and
            // the draw from clobbering the tick state EndDraw has to return.
            JonesFixedTick_SaveThingJoints(i, pThing);

            const JonesFixedTickThingState* pPrev = i < jonesFixedTick_prevNumThings
                ? &jonesFixedTick_pPrevThings[i]
                : NULL;

            // Added: dev-only skeleton endpoints for the trace's wrong-frame check.
            if ( jonesFixedTick_pDrawTraceFile && pThing == sithPlayer_g_pLocalPlayerThing )
            {
                JonesFixedTick_CaptureTraceTickJoints(pPrev, pCur, pThing);
            }

            // Added: reset the crossfade flag; BlendThing may return before calling the pose blend.
            jonesFixedTick_bPoseCrossfadeBuilt = false;
            JonesFixedTick_BlendThing(pPrev, pCur, pThing, alpha);

            // Added: a job built at the tick placement, or a skipped-joint model, would otherwise
            // be drawn from the stored tick pose while the camera and everything else interpolate.
            JonesFixedTick_InvalidateStaleThingJoints(i, pCur, pThing);
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

        // Added: the draw rebuilt the joints from the interpolated placement; give the simulation
        // the exact matrices and stamp the tick left.
        JonesFixedTick_EndDrawJoints(pWorld->aThings, numThings);
    }
    else
    {
        // Added: no thing loop ran this draw, so nothing was saved; keep the save state clean.
        jonesFixedTick_numSavedJoints     = 0;
        jonesFixedTick_savedJointPoolUsed = 0;
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

bool JonesFixedTick_OpenDrawTrace(const char* pPath)
{
    JonesFixedTick_CloseDrawTrace();

    if ( !pPath )
    {
        return false;
    }

    FILE* pFile = NULL;
    if ( fopen_s(&pFile, pPath, "wb") != 0 || !pFile )
    {
        fprintf(stderr, "ERROR: JonesFixedTick: cannot open draw trace '%s' for writing!\n", pPath);
        return false;
    }

    jonesFixedTick_pDrawTraceFile             = pFile;
    jonesFixedTick_drawTraceCount             = 0;
    jonesFixedTick_drawTraceRootJoint         = -1;
    jonesFixedTick_drawTraceHeadJoint         = -1;
    jonesFixedTick_drawTraceHandJoint         = -1;
    jonesFixedTick_drawTraceFootJoint         = -1;
    jonesFixedTick_bDrawTraceJointsResolved   = false;
    return true;
}

void JonesFixedTick_CloseDrawTrace(void)
{
    if ( jonesFixedTick_pDrawTraceFile )
    {
        fclose(jonesFixedTick_pDrawTraceFile);
        jonesFixedTick_pDrawTraceFile = NULL;
    }

    jonesFixedTick_drawTraceNumJoints     = 0;
    jonesFixedTick_bDrawTraceHaveTickJoints = false;

    jonesFixedTick_drawTraceCount           = 0;
    jonesFixedTick_drawTraceRootJoint       = -1;
    jonesFixedTick_drawTraceHeadJoint       = -1;
    jonesFixedTick_drawTraceHandJoint       = -1;
    jonesFixedTick_drawTraceFootJoint       = -1;
    jonesFixedTick_bDrawTraceJointsResolved = false;
}

// Added: case-insensitive substring test for the joint-name lookup below.
static bool JonesFixedTick_NameContains(const char* pName, const char* pNeedle)
{
    if ( !pName || !pNeedle )
    {
        return false;
    }

    for ( const char* pCur = pName; *pCur; ++pCur )
    {
        const char* pA = pCur;
        const char* pB = pNeedle;
        while ( *pA && *pB && tolower((unsigned char)*pA) == tolower((unsigned char)*pB) )
        {
            ++pA;
            ++pB;
        }

        if ( *pB == '\0' )
        {
            return true;
        }
    }

    return false;
}

// Added: find the first model node whose name contains one of the preferred tokens, in order, so
// "lhand" wins over a later "rhand" when both exist.
static int JonesFixedTick_FindJointByName(const rdModel3* pModel3, const char* const* apTokens,
    size_t numTokens)
{
    if ( !pModel3 || !pModel3->aHierarchyNodes )
    {
        return -1;
    }

    for ( size_t tokenNum = 0; tokenNum < numTokens; ++tokenNum )
    {
        for ( size_t nodeNum = 0; nodeNum < pModel3->numHNodes; ++nodeNum )
        {
            if ( JonesFixedTick_NameContains(pModel3->aHierarchyNodes[nodeNum].aName, apTokens[tokenNum]) )
            {
                return (int)nodeNum;
            }
        }
    }

    return -1;
}

// Added: resolve Indy's head/hand/foot model nodes once per trace. The name order prefers the left
// limb when the model carries both sides.
static void JonesFixedTick_ResolveDrawTraceJoints(const SithThing* pPlayer)
{
    // Indy's gen_in_shirt.3do names the three sample joints inhead (8), inlhand (12) and inlshoe
    // (25). The name tokens keep the trace working with the other Indy costumes.
    static const char* const aHeadTokens[] = { "head", "neck" };
    static const char* const aHandTokens[] = { "lhand", "rhand", "hand" };
    static const char* const aFootTokens[] = { "lshoe", "rshoe", "shoe", "lfoot", "rfoot", "foot" };

    jonesFixedTick_bDrawTraceJointsResolved = true;

    if ( !pPlayer || pPlayer->renderData.type != RD_THING_MODEL3 )
    {
        return;
    }

    const rdModel3* pModel3 = pPlayer->renderData.data.pModel3;
    jonesFixedTick_drawTraceRootJoint = (pModel3 && pModel3->numHNodes) ? 0 : -1;
    jonesFixedTick_drawTraceHeadJoint = JonesFixedTick_FindJointByName(pModel3, aHeadTokens, STD_ARRAYLEN(aHeadTokens));
    jonesFixedTick_drawTraceHandJoint = JonesFixedTick_FindJointByName(pModel3, aHandTokens, STD_ARRAYLEN(aHandTokens));
    jonesFixedTick_drawTraceFootJoint = JonesFixedTick_FindJointByName(pModel3, aFootTokens, STD_ARRAYLEN(aFootTokens));
}

static void JonesFixedTick_TraceTickJoint(FILE* pFile, const char* pLabel, int jointNum)
{
    if ( !jonesFixedTick_bDrawTraceHaveTickJoints || jointNum < 0
        || (size_t)jointNum >= jonesFixedTick_drawTraceNumJoints )
    {
        fprintf(pFile, " tickjoint %s -1 0 0 0 0 0 0 0", pLabel);
        return;
    }

    const rdVector3* pPrev = &jonesFixedTick_aDrawTracePrevJoints[jointNum];
    const rdVector3* pCur  = &jonesFixedTick_aDrawTraceCurJoints[jointNum];
    fprintf(pFile, " tickjoint %s %d %.6f %.6f %.6f %.6f %.6f %.6f", pLabel, jointNum,
        (double)pPrev->x, (double)pPrev->y, (double)pPrev->z,
        (double)pCur->x, (double)pCur->y, (double)pCur->z);
}

static void JonesFixedTick_TraceJoint(FILE* pFile, const char* pLabel, int jointNum,
    const SithThing* pPlayer)
{
    if ( jointNum < 0 || !pPlayer || pPlayer->renderData.type != RD_THING_MODEL3
        || !pPlayer->renderData.paJointMatrices )
    {
        fprintf(pFile, " joint %s -1 0.000000 0.000000 0.000000", pLabel);
        return;
    }

    const rdVector3* pPos = &pPlayer->renderData.paJointMatrices[jointNum].dvec;
    fprintf(pFile, " joint %s %d %.6f %.6f %.6f", pLabel, jointNum,
        (double)pPos->x, (double)pPos->y, (double)pPos->z);
}

void JonesFixedTick_TraceDraw(void)
{
    FILE* pFile = jonesFixedTick_pDrawTraceFile;
    if ( !pFile )
    {
        return;
    }

    const SithThing*  pPlayer = sithPlayer_g_pLocalPlayerThing;
    const SithCamera* pCamera = sithCamera_g_pCurCamera;

    if ( !jonesFixedTick_bDrawTraceJointsResolved )
    {
        JonesFixedTick_ResolveDrawTraceJoints(pPlayer);
    }

    rdVector3 playerPos  = { 0.0f, 0.0f, 0.0f };
    rdVector3 playerLVec = { 0.0f, 0.0f, 0.0f };
    if ( pPlayer )
    {
        playerPos  = pPlayer->pos;
        playerLVec = pPlayer->orient.lvec;
    }

    rdVector3 cameraPos  = { 0.0f, 0.0f, 0.0f };
    rdVector3 cameraLook = { 0.0f, 0.0f, 0.0f };
    if ( pCamera )
    {
        cameraPos  = pCamera->pos;
        cameraLook = pCamera->lookPos;
    }

    fprintf(pFile, "draw %zu tick=%zu alpha=%.6f"
        " ppos %.6f %.6f %.6f plvec %.6f %.6f %.6f"
        " cpos %.6f %.6f %.6f clook %.6f %.6f %.6f ctarget %.6f %.6f %.6f",
        jonesFixedTick_drawTraceCount,
        jonesFixedTick_tickCount,
        (double)jonesFixedTick_alpha,
        (double)playerPos.x, (double)playerPos.y, (double)playerPos.z,
        (double)playerLVec.x, (double)playerLVec.y, (double)playerLVec.z,
        (double)cameraPos.x, (double)cameraPos.y, (double)cameraPos.z,
        (double)cameraLook.x, (double)cameraLook.y, (double)cameraLook.z,
        (double)sithCamera_g_camLookSpot.x, (double)sithCamera_g_camLookSpot.y,
        (double)sithCamera_g_camLookSpot.z);

    // Added: the stamp and current rdCache frame show whether the scene draw rebuilt the player's
    // joints this frame (rdFrameNum != current frame) or kept the stored ones.
    if ( pPlayer )
    {
        fprintf(pFile, " rdframe %d cache %zu skip %d",
            pPlayer->renderData.rdFrameNum, rdCache_GetFrameNum(),
            pPlayer->renderData.bSkipBuildingJoints);
    }

    JonesFixedTick_TraceJoint(pFile, "root", jonesFixedTick_drawTraceRootJoint, pPlayer);
    JonesFixedTick_TraceJoint(pFile, "head", jonesFixedTick_drawTraceHeadJoint, pPlayer);
    JonesFixedTick_TraceJoint(pFile, "hand", jonesFixedTick_drawTraceHandJoint, pPlayer);
    JonesFixedTick_TraceJoint(pFile, "foot", jonesFixedTick_drawTraceFootJoint, pPlayer);

    JonesFixedTick_TraceTickJoint(pFile, "root", jonesFixedTick_drawTraceRootJoint);
    JonesFixedTick_TraceTickJoint(pFile, "head", jonesFixedTick_drawTraceHeadJoint);
    JonesFixedTick_TraceTickJoint(pFile, "hand", jonesFixedTick_drawTraceHandJoint);
    JonesFixedTick_TraceTickJoint(pFile, "foot", jonesFixedTick_drawTraceFootJoint);

    if ( pPlayer && pPlayer->renderData.pPuppet )
    {
        const rdPuppet* pPuppet = pPlayer->renderData.pPuppet;
        for ( size_t trackNum = 0; trackNum < RDPUPPET_MAX_TRACKS; ++trackNum )
        {
            const rdPuppetTrack* pTrack = &pPuppet->aTracks[trackNum];
            if ( pTrack->status && pTrack->pKFTrack )
            {
                fprintf(pFile, " track %zu %s %.6f %.6f 0x%x sim=%d joints=%zu len=%zu",
                    trackNum,
                    pTrack->pKFTrack->aName, (double)pTrack->curFrame,
                    (double)pTrack->blendWeight, (unsigned int)pTrack->status,
                    pTrack->guid != 0 ? 1 : 0, pTrack->pKFTrack->numJoints,
                    pTrack->pKFTrack->numFrames);
            }
        }
    }

    fprintf(pFile, "\n");
    ++jonesFixedTick_drawTraceCount;
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

    if ( jonesFixedTick_pSavedJoints )
    {
        STDFREE(jonesFixedTick_pSavedJoints);
    }

    if ( jonesFixedTick_pSavedJointPool )
    {
        STDFREE(jonesFixedTick_pSavedJointPool);
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

    jonesFixedTick_pSavedJoints              = NULL;
    jonesFixedTick_savedJointsCapacity       = 0;
    jonesFixedTick_numSavedJoints            = 0;
    jonesFixedTick_pSavedJointPool           = NULL;
    jonesFixedTick_savedJointPoolCapacity    = 0;
    jonesFixedTick_savedJointPoolUsed        = 0;
    jonesFixedTick_bPoseCrossfadeBuilt       = false;

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

    // Added: a global reset starts a fresh trace sequence but keeps the file the runner opened.
    jonesFixedTick_drawTraceCount           = 0;
    jonesFixedTick_drawTraceRootJoint       = -1;
    jonesFixedTick_drawTraceHeadJoint       = -1;
    jonesFixedTick_drawTraceHandJoint       = -1;
    jonesFixedTick_drawTraceFootJoint       = -1;
    jonesFixedTick_bDrawTraceJointsResolved = false;
    jonesFixedTick_drawTraceNumJoints       = 0;
    jonesFixedTick_bDrawTraceHaveTickJoints = false;

    // Added: the override is deliberately kept, so the headless runner can set it once for all runs.
    sithTime_SetFixedStep(0);
}
