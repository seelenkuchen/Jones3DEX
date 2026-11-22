# ADR-0039: Fixed simulation tick with interpolated rendering

- **Status:** Proposed
- **Date:** 2026-11-21

## Context

M9 adds high frame rates without changing gameplay. Today `sithTime_Advance` measures wall time per frame and
everything except the detached-player physics uses that delta: animation, AI, weapons, COG timers, camera,
particles and sound timing. Physics substeps at 50 Hz only in the detached-player path (ADR-0030). The complete
map is in [Frame-Time.md](../08-crosscutting/Frame-Time.md). It shows three classes of risk:

- Simulation that reads the reciprocal of the frame delta (`sithTime_g_fps`) or a clamped delta, so its result
  changes with the step.
- Per-frame counters (`sithMain_g_frameNumber`, `SITH_ISFRAMECYCLE`, `renderFrame`), so RNG consumption and
  "every Nth frame" work changes with the frame rate.
- Simulation that reads the wall clock (`sithTime_g_clockTime`), which also breaks the determinism rule.

It also shows that drawing currently mutates simulation-visible state (thing `renderFrame`, `SITH_TF_SEEN`,
COG `SIGHTED`, AI wake, laser things created by the AI waypoint renderer) and that the camera is integrated in
the draw phase. Extra rendered frames must not repeat any of that.

## Decision

- The Enhanced profile runs `sithUpdate` on a fixed simulation tick from an accumulator. 60 Hz is proposed; the
  exact rate is a single simulation cvar.
- Extra frames above the tick rate are draw-only. Thing positions and orientations, the camera, puppet/keyframe
  poses, surface and texture animation, particles and the sound listener are interpolated between the previous
  and current tick for the drawn frame, then the interpolated state is restored.
- Simulation keeps using `sithTime_g_msecGameTime`/`secGameTime` for scheduling. In Enhanced these advance by
  the fixed step. Wall clock is no longer read from simulation: `sithTime_g_clockTime` users move to game time.
- The Retail profile keeps today's measured frame time and is unchanged. With Retail selected the retail trace
  stays identical; the Enhanced headless trace stays identical at its configured fixed `--step`.
- Draw-only frames must not run any tick logic and must not mutate simulation state. Bookkeeping that the
  simulation reads (`renderFrame`, visibility-for-COG) becomes tick-owned.
- Interpolation is presentation only. Simulation never reads the randomised numbers of an effect (grain and
  similar use their own seed, M9 rules).

## Consequences

- At 120+ Hz motion is smooth while gameplay runs at 60 Hz. Gameplay feel at the tick rate is a new tuning
  target; the `sithTime_g_fps` uses in `sithPlayerControls` and the projectile `secDeltaTime > 0.02f` branch
  need explicit retuning or removal, and the owner play-tests them.
- Retail behaviour and traces are preserved by construction; the option lives with the other Enhanced options.
- Per-frame cadence changes in Enhanced for the `SITH_ISFRAMECYCLE` sites (fidgets, gasps, awareness events,
  orientation normalisation, target search, RNG order). Each is reviewed and either moved to a game-time
  interval or accepted at the fixed tick. Enhanced trace references are re-recorded only where the owner
  approves a behavioural change.
- Physics that still integrates with the frame delta is either moved onto the tick or documented as a known
  step dependency; otherwise a 60 Hz tick changes trajectories versus today.
- Drawing becomes side-effect free for simulation. The camera update moves into the tick; the renderer keeps
  its own per-draw-frame cache tick.
- CPU cost: a drawn frame still rebuilds the CPU render caches (lighting, culling, transforms). M9.2 measures
  whether the extra frames are affordable and decides go/no-go or a lower render rate.
- Rendering still runs every rendered frame; HUD and subtitle timers that already use wall time stay wall-time
  but run on the drawn frame.

## Implementation status

M9.20 implements the first slice, behind `engine.sim.fixedTick` (int Hz, default `0` in every build;
QOL-off/Retail builds ignore it). On, `JonesMain` runs `sithUpdate` and the camera update from a
game-time accumulator at the fixed step, with a catch-up cap. Thing position/orientation and the
camera position/orientation/FOV/aspect are snapshotted before and after each tick and restored after
the interpolated draw; teleports and recreated thing slots are never blended. The simulation-facing
draw work moved onto the tick: the default-FOV visibility marks (`SITH_TF_SEEN`, `SIGHTED`, AI wake,
`renderFrame`), the AI waypoint lasers, `sithFX_UpdatePolyline` and the out-of-water
air-destroyed sprite check. `sithTime` stops reading the wall clock while the tick is on and `sithTime_g_clockTime` follows game time. The headless runner has
`--fixed-tick` and `--draw-step`, and `--frames` counts ticks. With the option off the measured-frame
loop and the v6/Retail traces are unchanged. Puppet poses, surface/sky scroll, particles and the sound
listener interpolation are M9.22; the render-rate UI is M9.23. The status stays Proposed until the
owner play-tests the cadence and decides the Enhanced default.

## What must be interpolated

- Thing position and orientation, including attached, path-moved and vehicle things.
- Camera position, orientation, FOV and aspect ratio, including the underwater wobble.
- Puppet/keyframe joint poses and blend weights; keyframe markers remain on the tick.
- Surface texture scroll, sky-horizon and ceiling scroll, and page-flip selection.
- Particles: position, size and current cel.
- Sound listener position and orientation.
