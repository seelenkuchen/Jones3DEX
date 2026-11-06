# ADR-0022: Shaders are compiled at build time, with a local cache

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

The SDL3 GPU API (ADR-0009) takes shaders as SPIR-V (Vulkan), MSL (Metal) or DXIL (Direct3D 12) and picks the
format the device accepts at run time. These are portable intermediate forms: the graphics driver compiles
them to the GPU's own code when a pipeline is created, so nothing in them is specific to one GPU. They still
have to be produced from the shader source by a compiler: SDL_shadercross (zlib), which brings DXC and
SPIRV-Cross. The choices were to commit the generated files, to compile during the build, or to compile when
the game starts.

## Decision

Shaders are compiled during the build, from source, into all three formats. Nothing generated is committed.
The outputs are also kept in a gitignored cache directory in the checkout, named by a hash of the shader
source, the compiler version and the options, so a new build tree reuses them and only changed shaders are
recompiled. Building the game needs the shader compiler; there is no build from the cache alone. CI builds
the compiler or restores it with its cache.

## Consequences

- No generated shader files in the repository, so none can drift from their source.
- Every build machine needs SDL_shadercross and what it brings; the first build takes longer.
- Babylon's dependency list (ADR-0017) gains SDL_shadercross, DXC and SPIRV-Cross as build tools; their
  licenses must stay permissive (ADR-0008).

## Amendment (2026-11-06)

The toolchain route is now fixed:

- SDL_shadercross is built from source at pinned commit `1ff05bec573988a98ef9e0260b4da44f512b8367`, with
  the SPIRV-Cross submodule it pins (`1a6169566c73d3da552748fc372fe2bbb856e46e`).
- DXC is the official prebuilt release, version 1.9.2609, with its SHA-256 pinned, for Linux x86_64 and
  Windows x64. Other hosts set `BABYLON_DXC_ROOT` to a DXC they built (in the layout of the release
  packages), set `BABYLON_SHADERCROSS` to an existing tool, or turn `BABYLON_BUILD_SHADERS` off.
- The package's dxil validator library (`libdxil.so`, `dxil.dll`) is deleted unused: its Microsoft licence
  (`LICENSE-MS.txt`) limits it to Windows, and DXC 1.9 validates and signs DXIL itself, so `dxcompiler`
  alone produces the same signed DXIL.
- DXC's licence is NCSA (BSD-style). It is accepted as a build-tool licence, which widens ADR-0008's list
  for build tools.
- The tool is built for the host, never the target, so a cross-compiling configure still gets a tool that
  runs on the build machine.
- The cache layout is `.cache/shadercross/<toolchain id>/` for the tool with its `dxcompiler`, and
  `.cache/shaders/<key>/` for the compiled shaders. `<key>` is the SHA-256 of the toolchain id, the options
  and the source bytes. CI caches the `.cache/` directory (the standalone build's `Babylon/.cache`) before
  configure, keyed by `runner.os`, `runner.arch` and a hash of the shader CMake files; there is no restore
  key, because a stale toolchain must not be reused.
