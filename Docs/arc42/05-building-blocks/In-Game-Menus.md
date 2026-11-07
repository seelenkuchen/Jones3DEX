# In-game menus

Status: decided 2026-11-07 for M7 ([ADR-0026](../09-decisions/ADR-0026-in-game-menus.md)); not built yet.

## Layers

| Part | Where | Responsibility |
|---|---|---|
| `ui` module | `Babylon/ui/` | Widgets, layout, screen stack, focus, keyboard/mouse/gamepad navigation, cvar binding. Game-agnostic, with a Null backend |
| Screens | `Jones3D/` | The Jones3D screens as declarative structs, the Indy font and skin, the HUD sounds |
| Wrappers | `Jones3D/` | The recovered `jonesConfig_*` functions: each runs a frame loop around its screen and returns the original result |

## Screens

| Screen | Opened from | Notes |
|---|---|---|
| Main menu | Startup, unless `startMode` says otherwise | New Game, Load, Settings, Quit; background is a level load-screen map |
| Pause menu | Escape or Start in game | Resume, Save, Load, Settings, Statistics, Exit |
| Save, Load | Pause menu, HUD menu, main menu | Save folder list with thumbnail, level and date; default save name; overwrite and load warnings |
| Exit | Pause menu, HUD menu | Offers to save first |
| Gameplay settings | Settings | Original options, the Enhanced/Retail profile, each enhancement |
| Controls | Settings | Scheme picker, one rebind list for keys and gamepad, reset to defaults |
| Display | Settings | Resolution, window mode, vsync, renderer options (Advanced Display merged in) |
| Sound | Settings | Volumes, output device |
| Statistics | Pause menu, HUD menu | |
| Level completed, Store | End of level | Store is a list with quantities |
| Game over | Death | |
| Message | Errors and notices | Native message box only when rendering failed at startup |

Dropped: Insert CD (all data is installed; a missing file gets a message screen naming it).

## Rules

- The simulation is paused while any screen is open; the last frame stays behind it, dimmed.
- The wrappers keep each original function's return values and call order.
- Text comes from the `JONES_STR_*` strings; layout uses a virtual canvas 480 units high, as wide as the
  window's aspect.
- The HUD item menu keeps working with keyboard and gamepad through the normal game actions.
