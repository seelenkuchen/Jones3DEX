#ifndef JONES3D_JONESSPIKE_H
#define JONES3D_JONESSPIKE_H

#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

J3D_EXTERN_C_START

// Added: M9.2 throwaway prototype. A fixed simulation tick with interpolated drawing, selected
// only by the environment variable J3D_SPIKE_FIXED_TICK_HZ (unset = the existing frame loop
// exactly). This module owns the accumulator, the previous/current snapshots and the
// interpolated-draw swap; JonesMain drives the tick loop with it. Not a shipped option.

// True when J3D_SPIKE_FIXED_TICK_HZ selected the prototype. Reads the environment once per run.
bool JonesSpike_IsEnabled(void);

// The fixed simulation step in milliseconds (1000 / HZ, integer); 0 when disabled.
uint32_t JonesSpike_GetTickStepMsec(void);

// Number of simulation ticks run since the last reset (state-file "frame" in headless spike runs).
size_t JonesSpike_GetTickCount(void);

// Measures the wall delta, adds it to the accumulator and returns how many ticks to run this
// rendered frame. When paused it clears the accumulator and returns 0. Alpha, the fraction of a
// step elapsed after the last tick, is exposed by JonesSpike_GetAlpha.
int JonesSpike_BeginFrame(bool bPaused);

// Snapshots the thing and camera state before and after each tick. Call BeforeTick immediately
// before a tick and AfterTick after it (and after the camera update).
void JonesSpike_BeforeTick(void);
void JonesSpike_AfterTick(void);

float JonesSpike_GetAlpha(void);

// Swaps the world and camera to the interpolated pose, draws, then restores the exact tick state.
// The swap also suppresses draw-side effects that cannot be restored (COG SIGHTED, AI wake,
// renderFrame bookkeeping, AI waypoint thing creation); see the M9.2 notes in Frame-Time.md.
void JonesSpike_BeginDraw(void);
void JonesSpike_EndDraw(void);

void JonesSpike_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESSPIKE_H
