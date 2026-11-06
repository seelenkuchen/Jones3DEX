# ADR-0025: Babylon render interface

- **Status:** Accepted
- **Date:** 2026-11-06

## Context

M6 replaces the DirectX 9 renderer with SDL3 GPU ([ADR-0009](ADR-0009-sdl3-and-sdl3-gpu.md)) behind a Babylon
module, `render`. The game's `std3D` and `stdDisplay` adapters need pre-transformed 32-byte vertices with
16-bit indices as triangle lists, line strips and point lists, textures in RGB565, ARGB1555, ARGB4444 and
RGBA8888 with mips, blending, depth test and write, fog through uniforms, and 2D blits. Later milestones add
offscreen color, depth and normal targets and a post-processing chain
([ADR-0023](ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)), and world-space meshes must stay
possible. SDL3 GPU works in render passes and copy passes on a command buffer, uploads through transfer
buffers, acquires the swapchain image per frame and takes shaders as SPIR-V, DXIL or MSL chosen by the
device ([ADR-0022](ADR-0022-shaders-compiled-at-build-time.md)). Every interface needs a Null backend that
honors the whole contract, and contract tests that run against every backend
([ADR-0016](ADR-0016-virtual-interfaces-chosen-at-startup.md)).

## Decision

- One interface, `babylon::render::Device`, owns every texture, buffer, sampler and pipeline and hands out
  typed handles (index and generation); a destroyed handle is detected, logged and ignored.
- Descs say what a resource is in game-agnostic terms: `TextureDesc` (2D, mips, format, sampled, color
  target or depth target usage), `BufferDesc` (vertex, index, storage), `SamplerDesc` and `PipelineDesc`
  (vertex layout with several buffer slots, primitive, blend per color target, depth, cull, up to four
  color targets, shader blobs per format). Texture byte layouts are fixed in the header, the same on every
  backend.
- A frame is recorded into a `CommandList`, a concrete class shared by all backends: passes with load and
  store operations and clear values (a null target handle names the swapchain), pipeline and resource
  bindings, uniform pushes, draws and texture copies outside passes. Recording makes no virtual call; the
  list checks the frame's structure. `Device::submit` checks every command against the live resources
  once, for every backend (non-virtual interface), drops invalid commands with one warning and hands the
  rest to the backend, which presents when the frame drew to the swapchain.
- Uploads and readbacks are device calls ordered with submits; readback is synchronous, for tests and
  screenshots.
- The module depends on `log` only. The SDL GPU backend adds `platform` (for the window) and `SDL3-static`
  when it arrives, through a further change of `allowed-deps.txt`.
- The Null backend keeps texture and buffer contents in memory, applies uploads, clears and copies, does
  not rasterize, and records every pipeline creation and executed command for tests and the adapters'
  checks.

## Consequences

- The game's renderer adapters see one small interface and no SDL type; batching and pass grouping stay
  inside the backend.
- Contract tests run unchanged against Null and the SDL GPU backend; a backend that differs fails them.
- Copies inside a pass and uploads inside a frame's recording are not expressible; dynamic contents are
  uploaded before the submit that uses them.
- Uniforms are copied into the list per push, which is simple but allocates; an arena can replace it later
  without changing the interface.
- World-space meshes later need only new pipelines and buffers, not a new interface.
