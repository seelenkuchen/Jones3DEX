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

## Done (2026-11-05)

The references were recorded (M6.1), so the removal is complete:

- `82b8f607` removed the Win32, DirectX 9 and DirectX 6 game, `Libs/std/Win95`, the DirectSound drivers, the
  Win32 wkernel and w32util, `Jones3D/Platform/Win32`, `Indy3D.rc`, DirectX61c, `cmake/mingw-i686.cmake`, the
  HLSL compile and the DX CI workflows. The DX9 rendering masks stay in `Libs/std/Tests/tv/std3D/`.
- `3af8ccae` moved the shared headers and sources to `Libs/std/General`.
- `fa592863` built the Babylon column as a 64-bit Windows program and added the `windows-babylon` MSVC x64 CI
  job.
- `cd3582ec`, `db371c6d`, `6b427d8a`, `07e77192` and `1aef12c2` made the column accept MSVC `/sdl`, MSVC x64
  and a Windows-header-free `j3d.h`; `J3D_DIRECTX6/9`, `J3D_COLORKEYSUPPORTED` and `J3D_PLATFORM_NULL` are
  gone.
- `195bb483` ported the tests of surviving code that only the Windows build ran.
- `14f1a973` and `986730b0` made the text fixtures and text-mode reads byte-identical on every checkout and
  column.

Windows configures the Babylon column as a 64-bit program by default; `JONES3D_PLATFORM_BABYLON=OFF` and
32-bit Windows are configure errors. Until M6.6 the column runs the Null renderer and shows nothing.
