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
// the tick rate in Hz; 0 keeps the measured frame time. Allowed rates are 0, 30, 50, 60 and 120,
// and any other value is clamped to the nearest. QOL-off/Retail builds ignore the option.
#define JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK "engine.sim.fixedTick"

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

// Added: one active thing's previous or current tick state. renderFrame and flags are included
// because the draw used to write them; the draw now only reads them, but restoring them keeps a
// drawn frame from leaking any future draw-side change into the simulation.
typedef struct sJonesFixedTickThingState
{
    bool          valid;
    SithThingType type;
    uint32_t      signature;
    SithThingFlag flags;
    size_t        renderFrame;
    rdVector3     pos;
    rdMatrix34    orient;
} JonesFixedTickThingState;

typedef struct sJonesFixedTickCameraState
{
    bool       valid;
    rdVector3  pos;
    rdMatrix34 orient;
    float      fov;
    float      aspectRatio;
} JonesFixedTickCameraState;

// Configuration and lifecycle.
int JonesFixedTick_ClampRate(int rateHz);         // 0, 30, 50, 60 or 120, nearest to rateHz
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
size_t JonesFixedTick_GetTickCount(void);

// Per simulation tick.
void JonesFixedTick_BeforeTick(void);
void JonesFixedTick_AfterTick(void);

// Per drawn frame: swap the world and camera to the interpolated pose, draw, then restore the exact
// post-tick values.
void JonesFixedTick_BeginDraw(void);
void JonesFixedTick_EndDraw(void);

// Snapshot and interpolation helpers (also used by the unit tests).
void JonesFixedTick_CaptureThing(JonesFixedTickThingState* pDst, const SithThing* pThing);
void JonesFixedTick_CaptureCamera(JonesFixedTickCameraState* pDst, const SithCamera* pCamera);
bool JonesFixedTick_BlendThing(const JonesFixedTickThingState* pPrev,
    const JonesFixedTickThingState* pCur, SithThing* pThing, float alpha);
void JonesFixedTick_RestoreThing(const JonesFixedTickThingState* pCur, SithThing* pThing);
void JonesFixedTick_BlendCamera(const JonesFixedTickCameraState* pPrev,
    const JonesFixedTickCameraState* pCur, SithCamera* pCamera, float alpha);
void JonesFixedTick_RestoreCamera(const JonesFixedTickCameraState* pCur, SithCamera* pCamera);
void JonesFixedTick_LerpVector(rdVector3* pOut, const rdVector3* pA, const rdVector3* pB, float alpha);
void JonesFixedTick_LerpOrient(rdMatrix34* pOut, const rdMatrix34* pA, const rdMatrix34* pB, float alpha);

void JonesFixedTick_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESFIXEDTICK_H
