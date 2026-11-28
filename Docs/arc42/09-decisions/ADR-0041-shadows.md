# ADR-0041: Shadows

- **Status:** Proposed
- **Date:** 2026-11-24
- **Relates to:** [ADR-0023](ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md),
  [ADR-0040](ADR-0040-per-pixel-dynamic-lighting.md), [ADR-0015](ADR-0015-babylon-modules-as-cmake-targets.md),
  [ADR-0042](ADR-0042-babylon-second-game.md)

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
7. **Where the code lives** (ADR-0042): the shadow framework (shadow-map allocation and fitting, the receive
   pass, contact shadows, the dynamic-light shadow maps) is Babylon code, reusable by a second game. The
   Jones3D adapter supplies only the casters (models from the model draw, the level geometry) and the light
   choice.

## Consequences

- A second geometry path appears for casters only (models per frame, the level per load). It never draws the
  camera's view, so ADR-0023's CPU pipeline stays the one that decides what is seen.
- The overhead light is an invented light; its direction and strength are tuned by eye and reviewed by the owner.
- Shadow quality costs GPU time per shadow map and light; the options row (Shadows: Classic, Off, On) and the
  light cap bound it.
- The blob shadow remains the Retail look and the fallback when shadows are off or unsupported.

## Amendment (2026-11-26, M11.1 and M11.2)

- **Light:** 35 degrees from vertical at azimuth 30 degrees in the Z-up world, converted to view space from the
  camera basis each frame; reviewed by the owner with the M11.1 screenshots.
- **Character shadows work in view space, not world space** (item 3). Model vertices already reach rdroid in
  the camera's space: `rdModel3_DrawMesh` passes every face of a shadow-casting model in camera space through
  `std3D_AddShadowCasterPolygon`, including faces turned away from the camera, before back-face culling and
  clipping. The adapter fans each polygon into triangles and converts it to the postfx view convention, and the
  receive pass reconstructs view positions from depth. So the overhead map is an orthographic light view fitted
  around the casters' view-space bounds, and no world-space submission is needed for characters. Level geometry
  (item 4) still needs a world-space upload in M11.3.
- **Casters** are the things that get the original blob shadow (`SITH_TF_SHADOW`): rdroid marks their faces with
  `RD_FF_SHADOW_CASTER`, and while `engine.renderer.shadows` is on `rdModel3_DrawMesh` sends all of the model's
  faces, not only the camera-visible ones, through `std3D_AddShadowCasterPolygon`. Things outside the view frustum
  do not cast yet. `STD3D_RS_SHADOW_CASTER` stays for M11.3's receiver mask, and `std3D_DrawsThingShadows` tells
  sithRender to skip the blob; Retail and QOL-off keep the blob.
- **Where the code lives:** `babylon::shadow::DirectionalShadowMap` and `babylon::postfx::ContactShadowEffect` /
  `ShadowReceiveEffect` (ADR-0043); the adapter collects casters and orders the stages (water, contact shadows,
  character shadows, SSAO).
- **Characters cast but do not receive (M11.2g).** After the scene pass and before the receive pass, the adapter
  re-draws the frame's scene draws marked `STD3D_RS_SHADOW_CASTER` with their own vertex and index buffers,
  ranges and vertex shaders against the scene depth target, read-only, into a scene-sized mask cleared to 0. A
  fragment that passes the depth test writes 1, so the mask marks a caster's own visible surface. The receive
  pass samples it as `ShadowMapInput::excludeMask`: masked pixels keep their exact input colour, so the hat's
  shadow stays off the face below it while the character still casts onto the scene. The stage runs only when
  the frame's map was recorded and a caster draw was collected; otherwise the receive pass gets no mask.
- **M11.3 reuses the mask.** The same re-draw against the scene depth also identifies the characters for the
  level-shadow receive. The final shape is the include mode in the M11.3 amendment below; the mask need not
  be inverted.
- **M11.6b gentler character and contact shadows.** The owner's play-test found the shadows too strong, razor
  sharp and reaching too far. The character receive passes strength `0.4`, a `0.002`-view-unit minimum penumbra
  that grows by `0.03` per unit behind the caster, a `0.3`..`1.0`-unit fade behind the map, a `3`..`6`-view-unit
  distance fade and a `0.03`..`0.25` luminance gate; contact shadows pass strength `0.5` and a `2`..`4`-view-unit
  distance fade. These are presentation constants in the adapter, tuned at the game's scale, where Indy is about
  `0.2` units tall.

## Amendment (2026-11-26, M11.3)

- **Two maps, one light.** The character map is unchanged. A second `shadow::DirectionalShadowMap` (2048,
  `staticReach` 1.5 view units: the game's scale is about 0.2 units per character height) is fitted around
  the same characters through a `CasterBatch::fitOnly` batch and draws only the level's static set. Both
  maps use the same fixed overhead light and are recorded in the same frame.
- **The reach is a hard limit (M11.6f).** The level map opens with `clampCastersToNearPlane` off, so a
  caster nearer the light than the fitted near plane is clipped rather than clamped to depth 0: only
  geometry close above the characters counts. A moving character caster keeps the clamp so it never pops.
- **The level fit is anchored to the world (M11.6f).** The level map gets `setViewToWorld()` every frame
  from the same camera matrix as its world-to-view batch transforms, so a camera rotation about its own
  position leaves a fixed caster's light fit unchanged instead of turning the texel grid.
- **The level receive is gentle (M11.6f).** Strength `0.3`, a `0.002`-view-unit minimum penumbra that
  widens by `0.03` per unit behind the map, a `3`..`6`-view-unit distance fade and a `0.03`..`0.25`
  luminance gate. There is no distance-behind fade: the receivers are the characters themselves, possibly
  far below a ledge.
- **Level casters are uploaded at level open.** `sithRender_Open` walks `sithWorld_g_pCurrentWorld` and keeps
  the surfaces the renderer draws as solid level geometry (`RD_GEOMETRY_NONE` dropped, translucent adjoins
  dropped, sky and water dropped), fan-triangulates each into world-space triangles and passes the one list
  through `std3D_SetStaticShadowCasters`; `sithRender_Close` clears it. The adapter copies the list and
  uploads it once to a `shadow::StaticCasterSet` on the first frame that needs it. Nothing in the simulation
  changes. Only up-facing level surfaces cast: the light is a fixed overhead light and a room's ceiling faces
  down into the room, so letting it cast would put every indoor character in full shadow, while the top of a
  ledge or overhang faces up and still casts.
- **Include mask.** The level receive is a second `postfx::ShadowReceiveEffect` with
  `MaskMode::Include` and the same caster mask, so only the characters (not the world, whose shadows are
  already baked) receive the level map. It runs before the character receive, which reads its output as the
  scene source; the two passes touch disjoint pixels.
- **Order and freeing.** The level map is recorded after the caster mask and before the contact shadows; the
  level receive runs just before the character receive. The level target, map, set and effect are freed with
  the character-shadow ones on close and on a scene-size change; turning the option off stops every pass.
- **Contact ray (M11.7c).** The contact-shadow ray is narrowed to `0.04` view units with a `0.02`-unit
  sample thickness, down from Babylon's `0.2` and `0.04` defaults, which reached a whole character height and
  made large, view-dependent patches.
