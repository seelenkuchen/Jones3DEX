# ADR-0024: The render capture interposes on renamed backends

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

M6 replaces the DX9 renderer with a Babylon backend. The game's renderer call stream must be checkable
against the old backend without the game data in CI and without a GPU. Backends call public renderer
functions internally: Null's `std3D_AllocSystemTexture` calls `stdDisplay_VBufferNew`,
`stdDisplay_VBufferLock` and `stdDisplay_VBufferUnlock`, and `stdShaderDX9` calls
`std3D_GetCurrentDevice`. Recording at the public names would therefore record different internal
calls per backend.

## Decision

Interposition by renaming. Every renderer backend includes the private `stdRenderBackend.h` first,
which renames its 84 public functions to backend names; one forwarding layer defines the public
names, records while a capture is open, and forwards. Recording is in the game process, in a
deterministic little-endian format with handles instead of pointers. The game remembers the request
in `JonesMain_SetCapture`, opens the capture in `JonesMain_Startup` once the host services exist,
and closes it in `JonesMain_Shutdown` before they go away.

## Consequences

- Every new backend (the Babylon adapter in M6.6) gets capture by including the header. DX6 is
  excluded until it is removed in M6.3; it defines the public names itself.
- Calls inside a backend are never recorded.
- The Null column's stream differs from DX9's: Null never sets `pCachedTexture`, so `rdCache`
  re-adds textures every frame. M6.0 checks Null against Null, and M6.6 checks the Babylon adapter
  against DX9.
- Capture costs time and disk (the 1000-frame regress run: about 390 MB, 3.5 times slower) but
  nothing when off.
- The five DX9-only diagnostic functions in `std3D.h` are not part of the stream.

The file format is described in [RCAP](../../Formats/RCAP.md).
