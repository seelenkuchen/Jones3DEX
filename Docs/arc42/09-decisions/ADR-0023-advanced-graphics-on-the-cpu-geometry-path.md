# ADR-0023: Advanced graphics start on the CPU geometry path

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

The new renderer (M6) has to be able to carry the graphics planned for later: per-pixel dynamic lighting and
screen-space ambient occlusion (M10), post-processing such as bloom and depth of field (M9), maybe shadows.
Today `rdroid` transforms, lights, clips and sorts everything on the CPU and hands `std3D` pre-transformed,
pre-lit vertices (`StdVertexTL`: screen position, color, texture coordinates). Two ways lead to the
features: keep the CPU geometry path and give its vertices the extra data a shader needs (option A), or submit
world-space meshes, materials and lights and let the GPU transform and light them (option B). The renderer
also changes game state: `sithRender` marks sectors as seen (`SITH_SECTOR_SEEN`), so visibility must keep
running exactly as it does.

## Decision

Option A first. `rdroid` keeps transform, clipping, sorting and visibility on the CPU. M6 adds what the later
features need without changing the classic look: an extended vertex with view-space position and normal
(carried by `rdCache`, interpolated by `rdClip`, filled by `sithRender` and `rdModel3`), the per-frame light
list as a GPU buffer, render passes with offscreen color, depth and normal targets, and a post-processing
chain that is empty by default. GPU geometry (option B) may follow later; the Babylon render interfaces must
not rule it out.

## Consequences

- `rdroid` and `sith` rendering code changes in M6. It is presentation, so it may change freely, but the
  visibility pass stays untouched and the headless traces must stay identical.
- Per-pixel lighting and SSAO in M10 become shader work on data that already reaches the GPU.
- Shadows need extra CPU passes from each light's view, or option B.

## Amendment (2026-11-06): the GPU interpolates the extended vertex

The extended vertex is `StdVertexLit` (a `StdVertexTL` plus view-space position and normal), drawn through
`std3D_DrawRenderListLit` and recorded in captures as a new record id. "Interpolated by `rdClip`" turned out
to need no interpolation: world surfaces and models reach `rdCache` through `rdClip_FaceToPlane`, which culls
whole faces and clips none. The GPU clips the projected faces, and since the clip-space position the vertex
shader rebuilds is a linear function of the view-space position, it interpolates the position and normal
perspective-correctly, as a CPU clipper would. `rdClip_FaceToPlaneLit` therefore passes each vertex's
position and normal through, renormalizing the normal, and the fragment shader renormalizes the interpolated
one. The clipping variants (`rdClip_Face3*`, `rdQClip_*`) serve the sky and debug paths, which stay on the
classic vertex.

## Amendment (2026-11-06): the light list is the camera's, per frame, in a storage buffer

The lights reach the GPU as one list per frame, `std3D_SetLights` with `StdLight` records (view-space
position, `minRadius`, colour, `maxRadius`, the camera's two attenuation factors, an enabled flag; 48 bytes),
recorded in captures as a new record id. The list is the rdroid camera's after the visibility pass, read only,
in its order and with its cap (`RDCAMERA_MAX_LIGHTS`), so no light is selected that the CPU lighting doesn't
see. The per-sector subset and the per-model sector light stay rules the per-pixel shader applies (option A
L5), not separate lists. The Babylon backend keeps the list in a read-only storage buffer bound to the lit
draws; Babylon's render interface gained storage buffer bindings for it (`CommandList::bindStorageBuffer`,
`ShaderDesc::storageBufferCount`).
