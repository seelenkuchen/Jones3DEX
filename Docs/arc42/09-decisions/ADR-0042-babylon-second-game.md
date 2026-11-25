# ADR-0042: Babylon serves a second game

- **Status:** Accepted
- **Date:** 2026-11-24
- **Relates to:** [ADR-0006](ADR-0006-babylon-engine-in-repo.md), [ADR-0007](ADR-0007-adapter-between-game-and-babylon.md),
  [ADR-0008](ADR-0008-babylon-license.md), [ADR-0041](ADR-0041-shadows.md)

## Context

ADR-0006 kept Babylon in this repository until a second user exists. The owner decided on 2026-11-24 that a
second game will be ported onto Babylon (the candidate is an idTech3-based single-player port).

Babylon today offers platform, input, audio, files, cvars, UI and a device-level render module. The
post-processing chain and every effect of M9 and M10 (tone mapping and colour grading, bloom, SMAA, sharpening,
SSAO, depth of field, light shafts, water ripples, motion blur, lens flare, vignette, film grain, per-pixel
lighting) live in the adapter (`Libs/std/Babylon/`), under this project's AGPL licence. The owner will not
relicense that code, and ADR-0008 forbids copying AGPL or GPL code into Babylon.

Shared engines in the Kex model are a shared core plus per-game adaptation on both sides: each game gets an
adapter, and features useful to more than one game move into the core.

## Decision

1. **Babylon grows reusable features as new Babylon code.** Nothing moves from the adapter into Babylon; a
   feature Babylon needs is written fresh from public algorithm descriptions (clean room). Third-party sources
   under permissive licences (SMAA, AMD CAS: MIT) may be used as they are.
2. **The M11 shadow framework is Babylon code from the start** (ADR-0041): shadow-map allocation, the
   receive pass and contact shadows. The Jones3D adapter only supplies the casters (its models and level
   geometry).
3. **A Babylon post-processing module is written in parallel:** the chain first (pass ordering, intermediate
   and HDR targets, render scale, the final present), then the generic effects one at a time, taking colour,
   depth and normal targets as neutral inputs.
4. **Jones3D EX switches effect by effect.** An effect moves from the adapter to the Babylon version only after
   a screenshot comparison of the agreed scenes shows no visible difference the owner objects to; the adapter
   copy is deleted with the switch. Effects that depend on Jones3D data stay in the adapter: water ripples
   (surface tag), light shafts (sky and sector logic) and the per-pixel lit path (rdroid's lit vertices).
5. **Babylon moves into its own repository** with its own versioning before the second game uses it, as
   ADR-0006 foresaw; Jones3D EX then consumes it as a dependency.

The order of the shadow slices, the post-processing module and the repository split is planned with the owner.

## Consequences

- Each game keeps its own adapter (GPL or AGPL is fine there); only the core is shared, under MPL-2.0.
- For a while some effects exist twice (adapter and Babylon); every switch removes one copy, and the
  screenshot check guards the look of Jones3D EX.
- Babylon's interfaces get their first real test from a second user; changes they need are made in Babylon
  with tests on both backends, not worked around in an adapter.
