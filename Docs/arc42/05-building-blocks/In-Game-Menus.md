# In-game menus

Status: built in the Babylon column on 2026-11-07 ([ADR-0026](../09-decisions/ADR-0026-in-game-menus.md)),
including the Controls screen. The Null column keeps all of its default-returning stubs.

Built in M7.6 ([ADR-0028](../09-decisions/ADR-0028-modern-menu-style-and-always-on-gamepad.md)): a modern
style with a new main menu, one tabbed Options screen in place of the separate settings screens and the old
Settings hub, the Cinzel and Jost menu fonts and button prompts. The gamepad is always on: a connected pad
works alongside the keyboard and mouse, and the Controls tab has no controller toggle. The Display tab drops
the Performance row; `graphics.performanceLevel` and the level stored in savegames keep working as before.

## Layers

| Part | Where | Responsibility |
|---|---|---|
| `ui` module | `Babylon/ui/` | Widgets, layout, screen stack, focus, keyboard/mouse/gamepad navigation, cvar binding, the `Painter` interface. Game-agnostic, with a Null painter |
| Painter and skin | [`JonesUiPainter.cpp`](../../../Jones3D/Platform/Babylon/JonesUiPainter.cpp), [`JonesUiSkin.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSkin.cpp) | Draws the ui through `rdFont` and the HUD's rdroid and `std3D` 2D path (ADR-0026 amendment); the HUD's menu colors and sound cues |
| Context and modal loop | [`JonesUiContext.cpp`](../../../Jones3D/Platform/Babylon/JonesUiContext.cpp), [`JonesUiModal.cpp`](../../../Jones3D/Platform/Babylon/JonesUiModal.cpp) | The column's one painter, font and `babylon::ui::Ui`, reference counted for nested screens; the blocking frame loop that pumps platform events, freezes and dims the last frame, presents the screen and handles resizes and window closes. The context holds the session background: the first loop in the session captures or supplies it and later loops reuse it |
| Screens | `Jones3D/Platform/Babylon/JonesUi*.cpp` (table below) | The Jones3D screens as declarative structs, with the Indy font and skin |
| Wrappers | the same files | The recovered `jonesConfig_*` functions: each runs the modal loop around its screen and returns the original result |

## Screens

| Screen | Opened from | Source | Notes |
|---|---|---|---|
| Main menu | Startup, unless `startMode` says otherwise | [`JonesUiMainMenu.cpp`](../../../Jones3D/Platform/Babylon/JonesUiMainMenu.cpp) | Continue, New Game, Load Game, Options, Exit Game; background is the finished load-screen map of a level, with its title. `startMode` 5; a missing value or 2 opens it in the windowed build, and 1, 3 and 4 defer their screens until display and sound are up |
| Pause menu | Escape or gamepad Start in game | [`JonesUiPauseMenu.cpp`](../../../Jones3D/Platform/Babylon/JonesUiPauseMenu.cpp) | Resume, Save, Load, Options, Statistics, Exit |
| Options | Main menu and pause menu | [`JonesUiOptions.cpp`](../../../Jones3D/Platform/Babylon/JonesUiOptions.cpp) | One tabbed screen: Gameplay, Controls, Display and Sound. Tabs switch with Q/E, LB/RB or a click; each row is a setting and a help column describes the focused one. The tabs reuse the standalone builders below |
| Save, Load | Pause menu, main menu, and HUD menu when `engine.ui.hud.systemIcons` is true | [`JonesUiSaveLoad.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSaveLoad.cpp) | Save folder list with thumbnail, level display name and date; the thumbnail row is hidden when a save has none; the default save name uses the level display name; overwrite and load warnings |
| Exit | Pause menu, and HUD menu when `engine.ui.hud.systemIcons` is true | [`JonesUiSaveLoad.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSaveLoad.cpp) | Offers to save first |
| Gameplay tab | Options | [`JonesUiGameplaySettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiGameplaySettings.cpp) | Original options, the read-only profile, the Control style row (`engine.input.controlStyle`: Modern or Classic) and the enhancements that are runtime keys |
| Controls tab | Options | [`JonesUiControls.cpp`](../../../Jones3D/Platform/Babylon/JonesUiControls.cpp) | Scheme picker, one rebind list for keys and gamepad, a Rumble toggle, reset to defaults; deeper edit and capture screens open from the tab. Writes `controls.configFile`, `controls.mouse` and `engine.input.rumble` and applies the active scheme on OK. The gamepad is always on, so there is no controller toggle |
| Display tab | Options | [`JonesUiDisplaySettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiDisplaySettings.cpp) | Resolution, window mode, renderer options (Advanced Display merged in). No Performance row; the stored `graphics.performanceLevel` is written back unchanged |
| Sound tab | Options | [`JonesUiSoundSettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSoundSettings.cpp) | Master volume, reverse stereo, output device |
| Statistics | Pause menu, and HUD menu when `engine.ui.hud.systemIcons` is true | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp) | |
| Level completed, Store | End of level | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp), [`JonesUiStore.cpp`](../../../Jones3D/Platform/Babylon/JonesUiStore.cpp) | Store is a list with quantities |
| Game over | Death | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp) | Restart, Load, Quit |
| Message | Errors and notices | [`JonesUiModalMessage.cpp`](../../../Jones3D/Platform/Babylon/JonesUiModalMessage.cpp) | Native message box only when rendering failed at startup |

Dropped: Insert CD (all data is installed; a missing file gets a message screen naming it).

## Default keyboard and gamepad scheme

New installs use the `JONES_STR_DFLTPAD` ("Keyboard and gamepad") default scheme when
`controls.configFile` names no active scheme; a remembered active scheme stays as it is. With the
Modern control style, new installs use the `JONES_STR_DFLTMODERN` ("Modern keyboard and gamepad")
scheme instead ([Control-And-Input.md](Control-And-Input.md#modern-control-style-end-to-end)). The
Classic default keeps the first default's keyboard bindings and adds this pad layout inside the
classic movement model. Start stays unbound because the pause menu reads it directly. See
[Control-And-Input.md](Control-And-Input.md#control-scheme-files) for the scheme format.

| Pad | Action |
|---|---|
| Left stick | Walk forward/back, turn left/right |
| A (South) | Jump |
| X (West) | Activate (grab, pull, push, open; fires with a weapon drawn) |
| RT | Activate as well (fire) |
| B (East) | Roll |
| Y (North) | Toggle weapon |
| LB / RB | Step left / step right |
| LT (hold) | Run |
| L3 | Crawl |
| R3 | Look |
| D-pad up / down | Toggle light / Health |
| D-pad left / right | Previous / next weapon |
| Back (View) | Interface (inventory) |

## Rules

- The simulation is paused while any screen is open; the last frame stays behind it, dimmed.
- The Options screen keeps each tab's changes when the player leaves it and applies them only on OK. Its
  background is the menu image, darkened; it has no panel box.
- Every screen opened while a `JonesUiContext` is held reuses that session's frozen frame or
  background and dims it once; a screen never captures the screen before it, nested or sequential.
- Each modal frame clears the depth buffer before redrawing the background, and the menu quads do
  not write depth, so a closed screen cannot stay visible through the depth test.
- The Save and Load rows show the save name, the level display name and the date; a level is matched
  to the level table by its header file name, and a save without a thumbnail reserves no image row.
- The wrappers keep each original function's return values and call order.
- Text comes from the `JONES_STR_*` strings; layout uses a virtual canvas 480 units high, as wide as the
  window's aspect. New labels added in M7 are English literals marked `TODO: [COMPAT]`.
- The HUD item menu keeps working with keyboard and gamepad through the normal game actions. Escape opens
  the pause menu; the HUD menu opens on the active control scheme's interface key, with Tab and gamepad Back
  as a fallback when the scheme binds none. The Controls screen makes those binds editable
  ([Control-And-Input.md](Control-And-Input.md#menu-and-pause-keys)).
- Menus play the HUD item menu's own sounds through `sithSoundMixer`: `inv_move.wav` when focus
  moves, and `inv_expand.wav` when a screen accepts or goes back. The startup main menu runs before
  the static world opens, so `JonesHud_InitializeMenuSounds` loads those two files directly; the
  later HUD open replaces the handles with the static bank's own.
