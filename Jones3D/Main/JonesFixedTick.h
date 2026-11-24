#ifndef JONES3D_JONESFIXEDTICK_H
#define JONES3D_JONESFIXEDTICK_H

#include <j3dcore/j3d.h>

#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>

#include <sith/types.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

J3D_EXTERN_C_START

// Added: M9.20 fixed simulation tick with interpolated things and camera (ADR-0039). The option is
// either off (0, the measured frame time) or on at 50 Hz. The 50 Hz rate matches the game's fixed
// physics step (ADR-0030). A hand-edited old rate (30, 60 or 120) maps to 50, and a negative value
// keeps the build's default. QOL-off/Retail builds ignore the option.
#define JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK "engine.sim.fixedTick"

// Added: the only rate the option runs at. The simulation integrates in 20 ms (50 Hz) physics
// steps, so any other tick makes some ticks run no physics step (ADR-0030, ADR-0039).
#define JONESFIXEDTICK_RATE_HZ 50

// Added: the rate a missing engine.sim.fixedTick selects: on in Enhanced, off in Retail and
// QOL-off builds.
#define JONESFIXEDTICK_DEFAULT_RATE J3D_QOL_VALUE(JONESFIXEDTICK_RATE_HZ, 0)

// Added: the maximum number of catch-up ticks one rendered frame may run. A longer stall drops the
// remaining accumulator instead of spiralling.
#define JONESFIXEDTICK_MAX_CATCHUP 5

// Added: a thing that moves farther than this many world units in one tick is treated as a teleport
// and is drawn at the post-tick state instead of being interpolated across the level.
#define JONESFIXEDTICK_TELEPORT_DISTANCE 5.0f

// Added: the accumulator state is exposed so the tick-count and alpha rules can be tested without a
// running game. The module owns one instance internally.
typedef struct sJonesFixedTickAccumulator
{
    bool     bHaveClock;
    uint32_t lastClockMsec;
    double   accumMsec;
} JonesFixedTickAccumulator;

// Added: M9.22. One track's tick state for draw-time pose interpolation. The keyframe pointer
// identifies the track, so a replaced track is never sampled at the other track's frame; the
// complete tick poses are crossfaded instead.
typedef struct sJonesFixedTickPuppetTrackState
{
    bool              valid;
    const rdKeyframe* pKFTrack;
    float             curFrame;
    float             blendWeight;
    int               lowPriority;  // Added: preserved for building a complete tick pose
    int               highPriority;
} JonesFixedTickPuppetTrackState;

typedef struct sJonesFixedTickPuppetState
{
    bool                           valid;
    rdPuppet*                      pPuppet;
    JonesFixedTickPuppetTrackState aTracks[RDPUPPET_MAX_TRACKS];
} JonesFixedTickPuppetState;

// Added: one active thing's previous or current tick state. renderFrame and flags are included
// because the draw used to write them; the draw now only reads them, but restoring them keeps a
// drawn frame from leaking any future draw-side change into the simulation.
typedef struct sJonesFixedTickThingState
{
    bool                        valid;
    SithThingType               type;
    uint32_t                    signature;
    SithThingFlag               flags;
    size_t                      renderFrame;
    rdVector3                   pos;
    rdMatrix34                  orient;
    const rdModel3*             pModel3; // Added: a model swap between ticks cannot be pose-blended
    JonesFixedTickPuppetState   puppet;
    bool                        bSkipBuildingJoints; // Added: the tick's joints carry consumed root motion
    bool                        bParticle;
    float                       particleRadius;
    rdVector4                   lightColor;
    float                       lightMinRadius;
    float                       lightMaxRadius;
} JonesFixedTickThingState;

// Added: M9.22 surface, sky and sector light scroll values captured at the tick.
typedef struct sJonesFixedTickSurfaceState
{
    bool       valid;
    bool       scrolling;
    rdVector2  texVertOffset;
    rdVector4  extraLight;
} JonesFixedTickSurfaceState;

typedef struct sJonesFixedTickSkyState
{
    bool      valid;
    rdVector2 horizonOffset;
    rdVector2 ceilingOffset;
} JonesFixedTickSkyState;

typedef struct sJonesFixedTickSectorState
{
    bool      valid;
    rdVector4 extraLight;
} JonesFixedTickSectorState;

// Added: M9.22. The scroll offsets wrap at this period (sithAnimate uses fmodf 1024).
#define JONESFIXEDTICK_SCROLL_PERIOD 1024.0f

typedef struct sJonesFixedTickCameraState
{
    bool       valid;
    rdVector3  pos;
    rdMatrix34 orient;
    float      fov;
    float      aspectRatio;

    // Added: M9.24 (jitter). The draw reads lookPos for the sky, backface culling and the DoF
    // focus distance, and camSpot/camLookSpot are exposed to the frame. Blend them with the pose.
    rdVector3  lookPos;
    rdVector3  focusPos;
    rdVector3  camSpot;
    rdVector3  camLookSpot;
} JonesFixedTickCameraState;

// Configuration and lifecycle.
int JonesFixedTick_ClampRate(int rateHz);         // 0 stays off, every positive rate is 50
int JonesFixedTick_ResolveConfigRate(int rateHz); // ClampRate, but a negative value keeps the default
int JonesFixedTick_DefaultRate(void);             // JONESFIXEDTICK_DEFAULT_RATE
uint32_t JonesFixedTick_RateToStepMsec(int rateHz); // 1000 / rate, 0 when rate is 0
void JonesFixedTick_SetRateOverride(int rateHz);  // dev/headless --fixed-tick; -1 keeps the cfg
void JonesFixedTick_Configure(void);              // read the cfg (or the override) and enable/disable
bool JonesFixedTick_IsEnabled(void);
int JonesFixedTick_GetRateHz(void);
uint32_t JonesFixedTick_GetStepMsec(void);

// Exact stop for the headless runner: at most limit ticks in total; 0 means no limit.
void JonesFixedTick_SetTickLimit(size_t limit);

// Accumulator core (testable without a game).
void JonesFixedTick_AccumReset(JonesFixedTickAccumulator* pAccum);
int JonesFixedTick_AccumAdvance(JonesFixedTickAccumulator* pAccum, uint32_t nowMsec, bool bPaused,
    uint32_t stepMsec, int maxTicks);
float JonesFixedTick_AccumAlpha(const JonesFixedTickAccumulator* pAccum, uint32_t stepMsec);

// Per rendered frame.
int JonesFixedTick_BeginFrame(bool bPaused); // measured delta -> number of ticks to run
float JonesFixedTick_GetAlpha(void);
// Added: set the interpolation alpha for the next BeginDraw. The frame loop derives it from the
// accumulator; tests and the headless analysis set it directly to sample a drawn frame.
void JonesFixedTick_SetDrawAlpha(float alpha);
size_t JonesFixedTick_GetTickCount(void);

// Per simulation tick.
void JonesFixedTick_BeforeTick(void);
void JonesFixedTick_AfterTick(void);

// Per drawn frame: swap the world and camera to the interpolated pose, draw, then restore the exact
// post-tick values.
void JonesFixedTick_BeginDraw(void);
void JonesFixedTick_EndDraw(void);

// Added: dev-only --draw-trace (Frame-Time.md). Opens a trace file every drawn frame writes one
// line into after the scene draw: tick, alpha, player and camera pose, three player joints and the
// active tracks. Inert unless a path was opened; works with the fixed tick off too.
bool JonesFixedTick_OpenDrawTrace(const char* pPath);
void JonesFixedTick_CloseDrawTrace(void);
void JonesFixedTick_TraceDraw(void);

// Snapshot and interpolation helpers (also used by the unit tests).
void JonesFixedTick_CaptureThing(JonesFixedTickThingState* pDst, const SithThing* pThing);
void JonesFixedTick_CaptureCamera(JonesFixedTickCameraState* pDst, const SithCamera* pCamera);
bool JonesFixedTick_BlendThing(const JonesFixedTickThingState* pPrev,
    JonesFixedTickThingState* pCur, SithThing* pThing, float alpha);
void JonesFixedTick_RestoreThing(const JonesFixedTickThingState* pCur, SithThing* pThing);
void JonesFixedTick_BlendCamera(const JonesFixedTickCameraState* pPrev,
    const JonesFixedTickCameraState* pCur, SithCamera* pCamera, float alpha);
void JonesFixedTick_RestoreCamera(const JonesFixedTickCameraState* pCur, SithCamera* pCamera);
void JonesFixedTick_LerpVector(rdVector3* pOut, const rdVector3* pA, const rdVector3* pB, float alpha);
void JonesFixedTick_LerpOrient(rdMatrix34* pOut, const rdMatrix34* pA, const rdMatrix34* pB, float alpha);

// Added: M9.22 interpolated animation helpers (also used by the unit tests).
void JonesFixedTick_CapturePuppet(JonesFixedTickPuppetState* pDst, const rdPuppet* pPuppet);
void JonesFixedTick_BlendPuppet(const JonesFixedTickPuppetState* pPrev,
    JonesFixedTickPuppetState* pCur, rdPuppet* pPuppet, float alpha);
void JonesFixedTick_RestorePuppet(const JonesFixedTickPuppetState* pCur, rdPuppet* pPuppet);

// Added: M9.24b. A track replaced, removed, appeared or looped between two ticks is a pose
// discontinuity. It is crossfaded in world space between the complete previous- and current-tick
// skeletons, so every drawn joint stays on the segment between the two tick poses. These predicates
// decide whether that is needed and possible; an impossible case snaps to the current tick.
bool JonesFixedTick_PuppetNeedsPoseCrossfade(const JonesFixedTickPuppetState* pPrev,
    const JonesFixedTickPuppetState* pCur);
bool JonesFixedTick_PuppetCanPoseCrossfade(const JonesFixedTickPuppetState* pPrev,
    const JonesFixedTickPuppetState* pCur);
void JonesFixedTick_BlendJointMatrix(rdMatrix34* pOut, const rdMatrix34* pPrev,
    const rdMatrix34* pCur, float alpha);

void JonesFixedTick_CaptureSurface(JonesFixedTickSurfaceState* pDst, const SithSurface* pSurface);
void JonesFixedTick_BlendSurface(const JonesFixedTickSurfaceState* pPrev,
    const JonesFixedTickSurfaceState* pCur, SithSurface* pSurface, float alpha);
void JonesFixedTick_RestoreSurface(const JonesFixedTickSurfaceState* pCur, SithSurface* pSurface);

void JonesFixedTick_CaptureSky(JonesFixedTickSkyState* pDst, const SithWorld* pWorld);
void JonesFixedTick_BlendSky(const JonesFixedTickSkyState* pPrev,
    const JonesFixedTickSkyState* pCur, SithWorld* pWorld, float alpha);
void JonesFixedTick_RestoreSky(const JonesFixedTickSkyState* pCur, SithWorld* pWorld);

void JonesFixedTick_CaptureSector(JonesFixedTickSectorState* pDst, const SithSector* pSector);
void JonesFixedTick_BlendSector(const JonesFixedTickSectorState* pPrev,
    const JonesFixedTickSectorState* pCur, SithSector* pSector, float alpha);
void JonesFixedTick_RestoreSector(const JonesFixedTickSectorState* pCur, SithSector* pSector);

// Added: blends a value that wraps at period, choosing the short path across a wrap.
float JonesFixedTick_LerpWrapped(float prev, float cur, float alpha, float period);

void JonesFixedTick_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESFIXEDTICK_H
