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
| M6.0 Render capture | The renderer forwarding layer, the `.rcap` writer and replay tool, and their tests ([ADR-0024](arc42/09-decisions/ADR-0024-render-capture-by-renamed-backends.md)) | Done (2026-11-04) |
| M6.1 Reference captures | DX9 render captures and screenshots of the agreed scenes, recorded under wine on Linux | Done (2026-11-04) |
| M6.2 Windows Babylon column | The Babylon column builds as a 64-bit Windows program: MSVC x64 CI job, `cmake/mingw-x86_64.cmake`, drive-rooted paths, binary state files and the CRT forms `/sdl` accepts | Done (2026-11-05) |
| M6.3 Remove Win32 and DirectX | The Win32, DX9 and DX6 game, the DirectSound drivers, the Win32 wkernel and w32util, DirectX61c and the DX CI workflows are gone; Windows is 64-bit only, and the DX9 rendering masks stay as references | Done (2026-11-05) |
| M6.4 Babylon render module | The Babylon `render` module: the device interface, the Null backend and the SDL3 GPU backend, with the contract tests on both and lavapipe in Linux CI ([ADR-0025](arc42/09-decisions/ADR-0025-babylon-render-interface.md)) | Done (2026-11-06) |
| M6.5 Shaders | Build-time shader compilation with SDL_shadercross and a pinned DXC, a local toolchain and shader cache, and the first pipelines and draws on SDL GPU ([ADR-0022](arc42/09-decisions/ADR-0022-shaders-compiled-at-build-time.md)) | Done (2026-11-06) |
| M6.6 std3D adapter | `std3D` on the Babylon render device: textures and cache, render states, draw lists, fog and Z, so the 3D scene draws on the Babylon column | Done (2026-11-06) |
| M6.7 stdDisplay adapter | The video modes, back buffer, locks, fills and present on the Babylon render device in `stdDisplayBabylon.cpp`, so the 2D layer, HUD text and the intro movie draw on the Babylon column | Done (2026-11-06) |
| M6.8 Movies | The intro movie and cutscenes decode and draw through the Babylon column's `stdDisplay`; their sound came in M7 | Done (2026-11-06) |
| M6.9 Later-graphics foundation and parity | Render passes over offscreen color, depth and normal targets with an empty post-processing chain, view-space normals in lit vertices from world surfaces and models, and the frame's light list as a storage buffer ([ADR-0023](arc42/09-decisions/ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)); the owner reviewed the M6.6 to M6.9b screenshot pages in the parity review | Done (2026-11-06) |
| M6.10 Final docs pass | The M6 documentation made consistent: roadmap, rendering building block, testing, risks, ADRs and changelog | In progress (2026-11-06) |
| M7.1 Sound | The Babylon audio interface with its Null backend, the miniaudio backend, the game's sound and cutscene sound through Babylon, the output device choice ([ADR-0027](arc42/09-decisions/ADR-0027-sound-completion-on-game-time.md)) | Done (2026-11-07) |
| M7.2 Saving | Portable savegame thumbnails and the Save, Load and Exit screens, so the Babylon builds save and load with thumbnails | Done (2026-11-07) |
| M7.3 Input | Mouse axes, gamepads, text input, and the player's control schemes in the windowed Babylon build | Done (2026-11-07) |
| M7.4 Menus | The in-game screens in place of the Win32 dialogs: the main menu and the windowed start-mode rules, pause, save/load/exit, settings (including the Controls screen), statistics, level completed, store and game over ([ADR-0026](arc42/09-decisions/ADR-0026-in-game-menus.md)) | Done (2026-11-07) |
| M7.5 Polish | Menu sounds, the Display screen's window mode, fullscreen and Alt+Enter | Done (2026-11-07) |
| M7. Playable | The Babylon build plays with sound, saving and in-game menus; the owner's play-test on real hardware remains | In progress |
| M8. Release | Packages (Linux as AppImage or Flatpak, so nothing is installed into the system), game data installation, macOS signing, first Jones3D EX release | |
| M9. Presentation | Widescreen and high-resolution output; bloom and depth of field as post-processing; high frame rates, which first needs an answer to how much of the simulation depends on frame time | |
| M10. Modern lighting | Per-pixel dynamic lighting and screen-space ambient occlusion, with the original vertex lighting kept as the classic mode | |
| M11 and later. Babylon extras | Visible console; modding (loose-file overrides, mod folders, COG); threading last. Open idea: shadows and geometry on the GPU | |

The enhancements planned so far, and what each needs from Babylon, are listed in [Enhancements.md](Enhancements.md).

Each milestone ends with a check that can be seen, such as a run of the game or a passing CI job, before the
next one starts.
