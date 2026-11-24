# ADR-0040: Per-pixel dynamic lighting

- **Status:** Accepted
- **Date:** 2026-11-24
- **Relates to:** [ADR-0023](ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)

## Context

M6 put the camera's dynamic lights into a GPU list and the lit vertex into a shader
(`std3D_DrawRenderListLit`), and M10.1 recorded every place a light reaches a vertex colour. The CPU bakes all of
it: world GOURAUD adds `sithRender_BuildDynamicLights` (distance only, no N·L), model GOURAUD adds
`rdLight_CalcVertexIntensities` over the camera list, and model DIFFUSE adds `rdLight_CalcFaceIntensity`. A
per-pixel shader must add the dynamic part itself, so the vertex must not carry it too.

The owner decided the shape in the M10 plan:

- One formula for every lit surface: the model GOURAUD one from `rdLight_CalcVertexIntensities` (N·L plus the
  `minRadius`/`maxRadius` ramp), including world surfaces. It must not reproduce the old world distance-only
  formula, which is Classic's alone.
- Keep the original light list, order and cap. No specular, no shadows, no new light selection or per-sector
  subset.
- The model sector light is not in the uploaded list (it is added only during each model draw), so the CPU keeps
  its contribution in the model base colour and only uploaded lights go per pixel.
- Default Classic at first; the owner switched Enhanced to Per-pixel after the play-test (2026-11-24). The
  Retail profile forces Classic. Per-pixel needs lit faces; without them it behaves
  as Classic.

## Decision

Add the presentation option `engine.renderer.perPixelLighting` (Options > Display, Lighting: Classic or
Per-pixel), read by `JonesDisplay_Open` and applied at once through `rdSetPerPixelLighting`, and gate it on
`rdGetLitFaces()`.

When it is on:

- **World GOURAUD** keeps baked `aIntensities + extraLight` and does not add `aVertDynamicLights`
  (`rdClip_FaceToPlaneLitBaked`). World DIFFUSE adds nothing on the CPU as before; LIT and NONE add nothing.
- **Model GOURAUD** keeps `aVertColors + extraLight + camera ambient` and only the sector light's
  `rdLight_CalcVertexIntensities` term. **Model DIFFUSE** keeps extra light and camera ambient and only the
  sector light's `rdLight_CalcFaceIntensity` term. The sector light's camera-list index is recorded by
  `sithRender_RenderThings` with `rdModel3_SetSectorLightNum`; models drawn without one keep the classic list.
- **The flag** telling the shader to add lights is the unused specular colour's blue byte: `rdCache` writes
  `0xFFFFFFFF` for a resolved GOURAUD/DIFFUSE lit face and 0 for LIT/NONE, only in per-pixel mode. Classic
  vertices are not touched.
- **The shader** (`std3d_lit.frag.hlsl`) reads the `std3D_SetLights` storage buffer, iterates the enabled lights,
  and adds the N·L/ramp sum to the base colour, multiplied by the screen fade factor that `rdCache` applies to the
  base colour (passed in an unused uniform component). It computes in view space and converts the light positions
  and the lit vertex to the normal target's space. The result is `texture * saturate(base + dynamic)`, then fog;
  the normal target and surface tag are unchanged.
- Sprites, particles, polylines and the sky keep the classic shaders and light no differently.

Classic remains byte-identical. The CPU split changes renderer-side data only; traces are identical with the
option on and off. `sithRender_BuildDynamicLights` may keep running.

## Consequences

- World surfaces look different from Classic where levels were lit for the old distance-only formula: light no
  longer leaks around corners, and faces turned away from a light go dark. That is the point of the option, and
  Classic stays available.
- The split is approximate where Classic clamps intermediate sums (world `aIntensities + dynamic`, then the final
  colour), because a per-pixel sum clamps once. With N·L the surfaces differ anyway.
- Every lit face carries one extra flag in memory it already had; no vertex format, capture format or savegame
  changes. A per-pixel capture replays through the same shader because the flag is recorded with the vertex.
- Performance: up to `RDCAMERA_MAX_LIGHTS` (128) lights may be summed per pixel. The existing list and cap are
  unchanged; a per-sector mask may come with M10.3 if profiling asks for it.
- The model sector light keeps working without uploading it per draw. Uploading it would need an extra slot and a
  per-draw list update; the CPU term is simpler and exactly the original contribution.
