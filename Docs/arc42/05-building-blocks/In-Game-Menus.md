# In-game menus

Status: built in the Babylon column on 2026-11-07 ([ADR-0026](../09-decisions/ADR-0026-in-game-menus.md)).
The Controls screen is in progress (M7.17b); until it lands, the column's `jonesConfig_ShowControlOptions`
is still the Null stub. The Null column keeps all of its default-returning stubs.

## Layers

| Part | Where | Responsibility |
|---|---|---|
| `ui` module | `Babylon/ui/` | Widgets, layout, screen stack, focus, keyboard/mouse/gamepad navigation, cvar binding, the `Painter` interface. Game-agnostic, with a Null painter |
| Painter and skin | [`JonesUiPainter.cpp`](../../../Jones3D/Platform/Babylon/JonesUiPainter.cpp), [`JonesUiSkin.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSkin.cpp) | Draws the ui through `rdFont` and the HUD's rdroid and `std3D` 2D path (ADR-0026 amendment); the HUD's menu colors and sound cues |
| Context and modal loop | [`JonesUiContext.cpp`](../../../Jones3D/Platform/Babylon/JonesUiContext.cpp), [`JonesUiModal.cpp`](../../../Jones3D/Platform/Babylon/JonesUiModal.cpp) | The column's one painter, font and `babylon::ui::Ui`, reference counted for nested screens; the blocking frame loop that pumps platform events, freezes and dims the last frame, presents the screen and handles resizes and window closes |
| Screens | `Jones3D/Platform/Babylon/JonesUi*.cpp` (table below) | The Jones3D screens as declarative structs, with the Indy font and skin |
| Wrappers | the same files | The recovered `jonesConfig_*` functions: each runs the modal loop around its screen and returns the original result |

## Screens

| Screen | Opened from | Source | Notes |
|---|---|---|---|
| Main menu | Startup, unless `startMode` says otherwise | [`JonesUiMainMenu.cpp`](../../../Jones3D/Platform/Babylon/JonesUiMainMenu.cpp) | New Game, Load, Settings, Quit; background is a level load-screen map. `startMode` 5; a missing value or 2 opens it in the windowed build, and 1, 3 and 4 defer their screens until display and sound are up |
| Pause menu | Escape or gamepad Start in game | [`JonesUiPauseMenu.cpp`](../../../Jones3D/Platform/Babylon/JonesUiPauseMenu.cpp) | Resume, Save, Load, Settings, Statistics, Exit |
| Save, Load | Pause menu, HUD menu, main menu | [`JonesUiSaveLoad.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSaveLoad.cpp) | Save folder list with thumbnail, level and date; default save name; overwrite and load warnings |
| Exit | Pause menu, HUD menu | [`JonesUiSaveLoad.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSaveLoad.cpp) | Offers to save first |
| Gameplay settings | Settings | [`JonesUiGameplaySettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiGameplaySettings.cpp) | Original options, the read-only profile, the enhancements that are runtime keys |
| Controls | Settings | M7.17b | Coming; the Null stub answers now. Scheme picker, one rebind list for keys and gamepad, reset to defaults |
| Display | Settings | [`JonesUiDisplaySettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiDisplaySettings.cpp) | Resolution, window mode, renderer options (Advanced Display merged in) |
| Sound | Settings | [`JonesUiSoundSettings.cpp`](../../../Jones3D/Platform/Babylon/JonesUiSoundSettings.cpp) | Master volume, reverse stereo, output device |
| Statistics | Pause menu, HUD menu | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp) | |
| Level completed, Store | End of level | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp), [`JonesUiStore.cpp`](../../../Jones3D/Platform/Babylon/JonesUiStore.cpp) | Store is a list with quantities |
| Game over | Death | [`JonesUiEndScreens.cpp`](../../../Jones3D/Platform/Babylon/JonesUiEndScreens.cpp) | Restart, Load, Quit |
| Message | Errors and notices | [`JonesUiModalMessage.cpp`](../../../Jones3D/Platform/Babylon/JonesUiModalMessage.cpp) | Native message box only when rendering failed at startup |

Dropped: Insert CD (all data is installed; a missing file gets a message screen naming it).

## Rules

- The simulation is paused while any screen is open; the last frame stays behind it, dimmed.
- The wrappers keep each original function's return values and call order.
- Text comes from the `JONES_STR_*` strings; layout uses a virtual canvas 480 units high, as wide as the
  window's aspect. New labels added in M7 are English literals marked `TODO: [COMPAT]`.
- The HUD item menu keeps working with keyboard and gamepad through the normal game actions. Escape opens
  the pause menu; the HUD menu opens on the active control scheme's interface key, with Tab and gamepad Back
  as a fallback when the scheme binds none. The Controls screen will make those binds editable
  ([Control-And-Input.md](Control-And-Input.md#menu-and-pause-keys)).
- Menus play the HUD item menu's own sounds through `sithSoundMixer`: `inv_move.wav` when focus
  moves, and `inv_expand.wav` when a screen accepts or goes back. The HUD loads those sounds with
  its menu, so the startup main menu, which runs before a world opens, is silent.
