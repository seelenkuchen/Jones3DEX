# Roadmap

Where Jones3D EX stands and what comes next. The reasons behind the order are in
[chapter 4 of the architecture docs](arc42/04-solution-strategy.md).

| Milestone | Result | Status |
|---|---|---|
| M0. Toolchain | The engine builds and its tests run outside MSVC | Done |
| M1. Running baseline | A standalone executable runs the game; regression runs against the original | Done |
| M2. Layer design | Platform code separated from engine code, design approved | Done |
| M3. Subsystems behind the layer | Window, input, display, rendering, sound and dialogs behind interfaces; Win32 and DirectX 9 as one backend | Done |
| M4. Null backends and native 64-bit | The game runs headless on 64-bit Linux, deterministic across 32-bit, 64-bit and sanitizer builds | Done |
| Direction | Hard fork, Jones3D EX on the Babylon Engine, these docs | Done |
| M5. Babylon foundation | `Babylon/` with tests, SDL3 window, input, timer and files, Null backends, cvars, adapters. CI on Windows, Linux and macOS | Done |
| M6.0 Render capture | The renderer forwarding layer, the `.rcap` writer and replay tool, and their tests ([ADR-0024](arc42/09-decisions/ADR-0024-render-capture-by-renamed-backends.md)) | Done |
| M6.1 Reference captures | DX9 render captures and screenshots of the agreed scenes, recorded under wine on Linux | Done |
| M6.2 Windows Babylon column | The Babylon column builds as a 64-bit Windows program: MSVC x64 CI job, `cmake/mingw-x86_64.cmake`, drive-rooted paths, binary state files and the CRT forms `/sdl` accepts | Done (2026-11-05) |
| M6.3 Remove Win32 and DirectX | The Win32, DX9 and DX6 game, the DirectSound drivers, the Win32 wkernel and w32util, DirectX61c and the DX CI workflows are gone; Windows is 64-bit only, and the DX9 rendering masks stay as references | Done (2026-11-05) |
| M6.4 to M6.6 Babylon renderer | SDL3 GPU renderer at parity with DirectX 9, built to carry the later graphics: an extended vertex with position and normal, a light buffer, render passes and a post-processing chain ([ADR-0023](arc42/09-decisions/ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)). The Null renderer runs until M6.6 | In progress: M6.4 interface, Null and SDL GPU backends, CI on lavapipe done (2026-11-06); M6.6 std3D adapter on the Babylon device done (2026-11-06) |
| M6.5 Shaders | Build-time shader compilation with SDL_shadercross and a pinned DXC, a local toolchain and shader cache, and the first pipelines and draws on SDL GPU ([ADR-0022](arc42/09-decisions/ADR-0022-shaders-compiled-at-build-time.md)) | Done (2026-11-06) |
| M6.7 stdDisplay adapter | The video modes, back buffer, locks, fills and present on the Babylon render device in `stdDisplayBabylon.cpp`, so the 2D layer, HUD text and the intro movie draw on the Babylon column | Done (2026-11-06) |
| M7. Playable | miniaudio sound, the original dialogs as in-game screens, playable on Windows, Linux and macOS | |
| M8. Release | Packages (Linux as AppImage or Flatpak, so nothing is installed into the system), game data installation, macOS signing, first Jones3D EX release | |
| M9. Presentation | Widescreen and high-resolution output; bloom and depth of field as post-processing; high frame rates, which first needs an answer to how much of the simulation depends on frame time | |
| M10. Modern lighting | Per-pixel dynamic lighting and screen-space ambient occlusion, with the original vertex lighting kept as the classic mode | |
| M11 and later. Babylon extras | Visible console; modding (loose-file overrides, mod folders, COG); threading last. Open idea: shadows and geometry on the GPU | |

The enhancements planned so far, and what each needs from Babylon, are listed in [Enhancements.md](Enhancements.md).

Each milestone ends with a check that can be seen, such as a run of the game or a passing CI job, before the
next one starts.
