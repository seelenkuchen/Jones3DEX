# ADR-0041: Shadows

- **Status:** Proposed
- **Date:** 2026-11-24
- **Relates to:** [ADR-0023](ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md),
  [ADR-0040](ADR-0040-per-pixel-dynamic-lighting.md), [ADR-0015](ADR-0015-babylon-modules-as-cmake-targets.md)

## Context

The original game draws one shadow: `sithShadow_RenderThingShadow` puts a textured quad under the player, skipped
underwater and in the Aetherium. Everything else is baked into the level's vertex light. Most levels carry no
dynamic lights at all (the canyon none, the lagoon four, the volcano two, the temple none), so a shadow from
"the light" has nothing to come from in most places.

ADR-0023 kept geometry on the CPU: `rdroid` transforms and clips every face from the camera's view and the GPU
receives view-space positions and normals. A shadow map needs its casters seen from the light. Models are skinned
on the CPU every frame anyway, so submitting them again in world space is cheap. The level's surfaces are only
ever sent in camera space, clipped to the camera's view.

The owner chose four kinds of shadow for M11 (2026-11-24): screen-space contact shadows, character shadows from an
overhead light, the level casting onto characters, and shadows from the dynamic lights (torches, the lighter).

## Decision

Shadows are presentation. The simulation, the visibility pass and the headless traces are unchanged; the Retail
profile and QOL-off builds keep the original blob shadow.

1. **An overhead light.** Character and contact shadows use one fixed light direction (mostly down, slightly
   tilted), not a light from the level data. Its direction and strength are presentation settings; per-level
   values may follow.
2. **Contact shadows** march a short ray through the depth buffer toward the overhead light and toward each
   dynamic light. They need no new geometry and ground characters and props in every level.
3. **Character shadows** come from an overhead shadow map. The model draw also submits its triangles in world
   space as casters; the map is fitted around the visible casters. A screen-space pass reconstructs each
   pixel's position from the depth target, looks it up in the map with filtered comparisons and darkens the lit
   scene. In Enhanced this replaces the blob.
4. **The level casts onto characters** through GPU level geometry used only as a caster: the static world
   surfaces are uploaded once per level in world space and drawn into the same map. The receiving pass applies
   level casters only where the normal target's surface tag marks a character, so the shadows already baked
   into the level are not doubled. The camera's own draw stays on the CPU path.
5. **Dynamic-light shadows** use a six-face shadow map (one layer of a 2D-array depth texture per face) for each
   of the nearest dynamic lights, capped at a small number, sampled per light in the per-pixel shader so only
   that light's contribution is removed. Classic lighting keeps its unshadowed vertex lights.
6. **Babylon additions** (M11.0), game-agnostic and implemented by both the Null and the SDL GPU backend with
   contract tests: comparison samplers, 2D-array textures with a per-attachment layer, and depth bias in the
   pipeline. Depth-only pipelines and sampled depth targets exist already.

## Consequences

- A second geometry path appears for casters only (models per frame, the level per load). It never draws the
  camera's view, so ADR-0023's CPU pipeline stays the one that decides what is seen.
- The overhead light is an invented light; its direction and strength are tuned by eye and reviewed by the owner.
- Shadow quality costs GPU time per shadow map and light; the options row (Shadows: Classic, Off, On) and the
  light cap bound it.
- The blob shadow remains the Retail look and the fallback when shadows are off or unsupported.
