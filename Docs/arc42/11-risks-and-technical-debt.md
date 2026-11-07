# 11. Risks and technical debt

## 11.1 Risks

| Risk | Effect | Mitigation |
|---|---|---|
| The retail reference is incomplete | Traces prove sameness with a build, not with retail, wherever the reference build already differs | Keep the decompilation as tie-breaker; record each known difference here |
| Simulation code depends on frame rate or wall-clock time | Traces differ between machines, or a high frame rate changes gameplay | Fixed game time in headless runs; time sources are behind the Babylon clock (M5) |
| A fixed simulation step for high frame rates (M9) changes gameplay | Only player physics uses fixed slices today; the rest advances by the measured frame time, so interpolated rendering as in Doom ports isn't presentation-only | Investigate in M9 which systems depend on frame time; if adopted, an option (on by default, off in the Retail profile) with the retail trace unchanged ([research](../Research/Engine-Patterns.md)) |
| The renderer changes game state | `sithRender` marks every sector it draws as seen (`SITH_SECTOR_SEEN`); a new render path that skips the original visibility pass changes the simulation | Keep the original visibility pass in every render path; list render side effects and cover them by trace checks in M6 and before M10 |
| SDL GPU backend differences | The same frame can render differently on Vulkan, Direct3D 12 and Metal; against the DX9 screenshots the canyon frames differ in under 0.8% of pixels, along edges ([M6.6 references](../References/M6.6/README.md)) | D3D12 is checked in CI (the Windows runner has a D3D12 device); Metal is untested; the M6.9 parity review accepted the M6.6 to M6.9b screenshot pages as a match |
| Threading (M11 and later) breaks determinism | Traces become flaky | Threading comes last, behind traces for everything else |
| AGPL code leaks into Babylon | Babylon's MPL-2.0 license is no longer valid | Include and license checks in CI; Babylon is written fresh |
| Copyrightability of AI-assisted code is legally unsettled | Weakens every license claim, Babylon's included | Human review and authorship of design; keep records of decisions |
| Small team | Long milestones stall | Thin slices, each one built, tested and committed |
| The startup main menu is silent | The HUD loads the menu sounds with a level, so the main menu before a world opens plays no focus or accept cues; the pause and in-game menus play them | Load the cues with the UI skin, or start them independently of the HUD |
| Escape now opens the pause menu | The HUD item menu moved to the scheme's interface key, or Tab and gamepad Back while a scheme binds none; players used to Escape opening the HUD menu see a different screen | The Controls screen makes the scheme's menu binds editable and documents the change |
| Real audio, gamepad and display behavior is untested in CI | CI has no speakers, gamepad or real display, so the owner play-test is the first real check of sound output, device switching, pad input, fullscreen, Alt+Enter and high-density windows | Owner play-test on real hardware before the release |

## 11.2 Technical debt

| Debt | Where | Plan |
|---|---|---|
| QOL behavior as one compile-time profile | `J3D_QOL_IMPROVEMENTS`, `J3D_SPEEDRUN_BUILD`; only `engine.renderer.vfx.weapon.fireFlash.enabled`, `engine.world.cnd.materials.loadExternal` and `engine.world.cnd.keyframes.loadExternal` are runtime options today | Make the rest individual runtime options ([ADR-0003](09-decisions/ADR-0003-fidelity-split.md)) |
| New UI labels are English literals | The M7 screens in `Jones3D/Platform/Babylon/JonesUi*.cpp`, marked `TODO: [COMPAT]` | Replace them with localized `JONES_STR_*` strings |
| DirectDraw type names in shared headers | `LPDDCOLORKEY` in `std3D.h`, `stdColor.h`, `stdDisplay.h` and `stdRenderBackendApi.h` (the Direct3D vertex types are gone since M6.3) | Rename to a neutral colour-key type |
| The Controls screen is still a Null stub | `jonesConfig_ShowControlOptions` in `Jones3D/Platform/Null/JonesPlatformNull.c`, used by the Babylon column before M7.17b | Replace it with the scheme picker and rebind screen ([ADR-0026](09-decisions/ADR-0026-in-game-menus.md)) |
| Sound parser can read before its input buffer | `AudioLib_ParseWaveFileHeader` | Fix with a regression test; a malformed sound file triggers it |
| Lip-sync pointer restored from sound banks | `Sound.c` | Force to NULL on load; a wild pointer on 64-bit |
| Cutscene blitter relies on 32-bit pointer wrap | `Libs/smush/insane/Bitmap/BlitAsm.c` | Not reached by the game: its only caller (`mixer_Blit` through `blit_Wrapper`) passes row 0 and clip-bounded rows, and SmushPlay's movies blit through the game's callback instead (M6.8). Only the captured vectors hit it |
| Sound bank scalars read as 4 bytes into `size_t` | `Sound.c` | Read through `uint32_t` |
| GCC native builds need manual flags | `-D__cdecl= -D__stdcall= -D__fastcall=` and `-Wno-error=incompatible-pointer-types` | Move into `j3d.h.in` and CMake |
| Multiplayer sends `wchar_t` arrays | `sithMulti.c` | Out of scope while multiplayer has no backend |
| SMUSH frame pacing rounds to 60 Hz ticks | `TimerPolled.c` keeps the original's 60 Hz tick: a 15 fps frame takes 3 to 6 ticks and the average runs about 2% slow, as in the original | The M7 cutscene stream keeps the same timer pacing; revisit if the stream's drift needs correcting |
| `ctest -L check` does not run under the wine toolchain | CMake prefixes the `python3` boundary checks with the emulator | Run the boundary checks in a native build tree; the unit and Babylon suites run under wine |
| SDL GPU limits are fixed constants, not queried | `SdlGpuDevice.cpp` reports 8192 texels and 128 MiB per buffer on every driver because SDL GPU has no limit queries | Revisit when SDL exposes limits |
| No source-build fallback for DXC | The shader toolchain relies on a prebuilt DXC for Linux x86_64 and Windows x64 | Build DXC and set `BABYLON_DXC_ROOT`, set `BABYLON_SHADERCROSS` to an existing tool, or turn `BABYLON_BUILD_SHADERS` off |
| macOS and arm64 have no prebuilt DXC | The first configure fails on those hosts unless DXC is supplied | Set `BABYLON_DXC_ROOT` to a locally built DXC, or `BABYLON_BUILD_SHADERS=OFF` |
| Lavapipe leak workaround in tests | `KeepGpuDriverLoaded.h` keeps Mesa's lavapipe loaded because it leaks memory under `pthread_once` | Tests only; remove when lavapipe or the Vulkan loader frees that memory |
| `rdCache` lit buffers are static arrays | `Libs/rdroid/Raster/rdCache.c`: the view vertices and normals of the classic and alpha lists, plus the lit hardware vertices, add about 200 MB of static memory | Size them for one frame or share one buffer between the classic and lit paths |
| Headless `startMode` 0 quits after loading | `Jones3D/Platform/Null/JonesHeadless.c`, `Jones3D/Main/JonesMain.c`; predates M6 and is not investigated | Reproduce and fix when it blocks a headless check |
| A back-buffer lock in the middle of a scene | `stdRenderBabylon_WriteBackBuffer` submits the frame recorded so far and uploads the CPU pixels, so that scene's later draws land over them instead of under | Debug drawing is the only mid-scene lock today; keep locks at frame boundaries or layer the 2D pass explicitly |
| Display reset callbacks stored but never called | `stdDisplayBabylon.cpp` keeps the pre- and post-reset callbacks; only the release callback runs, from `stdDisplay_ClearMode` | The render device is never lost or reset; remove the registration or call them if a reset path arrives |
