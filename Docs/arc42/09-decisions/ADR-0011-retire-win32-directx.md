# ADR-0011: Retire the Win32 and DirectX path after renderer parity

- **Status:** Accepted
- **Date:** 2026-11-02

## Context

The Win32 and DirectX 9 path works today, and is the only reference for what the game should look like on
a modern renderer. Porting it into Babylon would mean porting code that is about to be deleted. DirectX 6 is
already paused.

## Decision

Win32 and DirectX are not ported into Babylon. They stay on the old path as the visual reference until the
SDL3 GPU renderer reaches parity in M6, and are removed after that. Babylon starts with SDL3 and null backends.

## Consequences

- Until M6, Windows has two paths. Shared code must keep building for both.
- After M6, Windows runs through Babylon like the other platforms.

## Amendment (2026-11-04): remove it once the references are recorded

The owner decided that the Win32, DirectX 9 and DirectX 6 code goes as soon as everything needed to confirm
the new renderer is recorded: DX9 screenshots and render captures (the `std3D` and `stdDisplay` call streams)
of agreed scenes. Parity is then shown by matching draw calls plus the owner's visual confirmation against
those screenshots, not by keeping the old renderer running. Windows becomes a 64-bit build of the Babylon
column at the same time; the 32-bit Windows build goes with the old path
([ADR-0022](ADR-0022-shaders-compiled-at-build-time.md) covers the shaders).

Consequence: between the removal and the first working Babylon renderer, the Windows build runs with the
Null renderer and shows nothing, like Linux and macOS until then.

## Amendment (2026-11-04): references recorded under wine

The owner has no Windows machine. The references are recorded on Linux instead: the MinGW DX9 build runs
under wine on a virtual X display (Xvfb) with Mesa's software OpenGL (llvmpipe); wine's wined3d translates
DX9 to OpenGL. The render captures are unaffected, because they record the game's calls to its renderer,
above DX9. The screenshots show wined3d's rendering, which is close to but not pixel-identical with DX9 on a
Windows driver; the owner's visual confirmation uses them with that in mind. The recordings stay local with
the game data.
