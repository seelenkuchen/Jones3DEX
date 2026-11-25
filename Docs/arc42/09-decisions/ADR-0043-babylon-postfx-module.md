# ADR-0043: The Babylon post-processing module

- **Status:** Proposed
- **Date:** 2026-11-25
- **Relates to:** [ADR-0015](ADR-0015-babylon-modules-as-cmake-targets.md), [ADR-0041](ADR-0041-shadows.md),
  [ADR-0042](ADR-0042-babylon-second-game.md)

## Context

ADR-0042 item 3 asks for a post-processing module in Babylon, written fresh, chain first. The owner decided on
2026-11-25 that the chain comes before the M11.1 contact shadows, so the shadow receive pass can be a chain
stage. Today every effect lives in the AGPL adapter (`Libs/std/Babylon/stdRenderBabylon*.cpp`); none of that
code may move into Babylon.

Babylon's render module is device level: textures, samplers, pipelines and a recorded `CommandList` per frame,
with a Null and an SDL GPU backend. `uirender` already shows how a Babylon module owns pipelines and shaders on top
of it (`babylon_add_shaders`).

## Decision

1. **New module `postfx`** (`Babylon/postfx/`, namespace `babylon::postfx`), depending on `render` and `log`
   (`allowed-deps.txt`: `postfx: render log`). It owns its shaders through `babylon_add_shaders`.
2. **An effect is an object with one job:** `postfx::Effect` records its passes from one input texture into one
   output texture, given a per-frame `FrameInputs` (scene colour, optional depth and normals, size, camera
   projection, time, frame index) and a `TargetPool` for scratch targets. Each effect states its stage
   (`Hdr` before the tone map, `Resolve` for the tone map itself, `Ldr` after it) and what inputs it needs.
3. **`postfx::Chain`** runs the enabled effects in order: the `Hdr` effects ping-pong between two scene-sized HDR
   targets, the single `Resolve` effect (or a plain copy when none is set) writes LDR, then the `Ldr` effects
   ping-pong in LDR into the caller's output (a texture or the swapchain). A disabled or failed effect is skipped
   and its input passes through, with one warning per failure, so a broken effect never blanks the frame.
4. **Effects can also be used without the chain.** A game that keeps its own pass order (Jones3D EX while it
   switches effect by effect) calls an effect's `record()` directly with its own input and output.
5. **Game-specific stages implement the same interface in the adapter.** Water ripples, light shafts and the
   lit path stay AGPL in the adapter (ADR-0042 item 4); once Jones3D EX uses the chain they are adapter-side
   `Effect`s placed in it. Babylon never names them.
6. **Clean room.** Babylon effects are written from public algorithm descriptions in a worktree without
   `Libs/` and `Jones3D/`; SMAA and AMD CAS may come from their MIT upstream sources. The adapter's constants may
   be quoted as parameter values; its code and shaders may not.
7. **Deterministic inputs.** Effects that animate (grain, flicker) take time and the frame index from
   `FrameInputs`, never the wall clock.
8. **Tests:** contract tests on the Null device (recorded passes, bindings, target sizes, pass-through on
   failure) and pixel tests on SDL GPU under the offscreen driver, as in `uirender`.

## Consequences

- The chain is proven by its first effect (vignette and film grain) and the first switch in Jones3D EX before
  more effects are written; the M11.1 contact shadows are then a Babylon `Hdr` effect that needs depth.
- An effect's look may differ slightly from the adapter's version; the switch happens only after the
  screenshot comparison of ADR-0042 item 4.
- Until all generic effects have moved, the adapter keeps its own pass order and calls Babylon effects one by
  one (item 4); adopting the whole chain in the adapter is a later step.
