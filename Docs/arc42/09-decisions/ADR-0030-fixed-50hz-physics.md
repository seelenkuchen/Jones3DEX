# ADR-0030: Physics always integrates at the original 50 Hz step

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

OpenJones3D's QOL changes made the player physics step configurable (`engine.physics.fixedTimestep`, 150 Hz by
default), to make movement smoother at high frame rates. A smaller step changes the integrated falling velocity
at a given time, and `sithPlayerAction`'s ledge-grab checks compare that velocity with fixed thresholds tuned for
the original 20 ms step, so ledge grabs fail or succeed differently. Upstream (smlu/OpenJones3D, 85ac23e1) went
back to the fixed 20 ms step. The owner decided on 2026-11-13 to do the same.

## Decision

- Both profiles integrate physics at the original fixed 20 ms (50 Hz) step. The step is a constant.
- `engine.physics.fixedTimestep` is no longer read or written; a config that still has it behaves the same as
  one without it.
- Smooth motion above 50 frames per second comes from presentation-side interpolation (M9), not from a smaller
  simulation step.

## Consequences

- Retail traces are unchanged (Retail already ran at 50 Hz). Enhanced traces change wherever the 150 Hz step
  was used, so the Enhanced trace references are re-recorded (v5) with this change.
- Ledge grabs and every other velocity threshold behave as in retail in both profiles.
- Until M9 interpolation, moving objects advance in 20 ms steps on high-refresh displays.
