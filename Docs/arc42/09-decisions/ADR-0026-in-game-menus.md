# ADR-0026: In-game menus replace the Win32 dialogs

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

The retail game showed its settings, save, load, store and end-of-level screens as modal Win32 dialogs
(`jonesConfig.c`, 9,690 lines, removed in M6.3), and was started from a separate launcher that chose a
`startMode`. Since M6.3 the Babylon column has only Null stubs behind the 15 `jonesConfig_*` functions the
game calls. M7 has to make the game playable with keyboard, mouse and gamepad on every desktop platform, with
no native dialogs and no launcher.

PowerslaveEX (Kex 3), the model for this port, builds each menu as a C++ class that places widgets at fixed
positions and binds them to cvars. It has no menu data format and registers menus through static
constructors, which Babylon forbids (ADR-0015).

## Decision

1. **A Babylon `ui` module**, game-agnostic C++: widgets (button, toggle, slider, choice, list, key and
   gamepad binding capture, text entry, file list with thumbnail), automatic layout, a screen stack, focus,
   and navigation by keyboard, mouse and gamepad. Widgets can bind to cvars (ADR-0013). It draws through
   `render` with a font and skin the game supplies, and has a Null backend.
2. **Screens are defined in code.** Each game declares its screens as small declarative structs against the
   `ui` widgets. A data file format may be added later on top of the same structs, when a user needs one.
3. **The blocking contract stays.** Each `jonesConfig_ShowX()` runs its own frame loop until the screen
   closes and returns what the original dialog returned. Callers, call order and the Retail trace don't
   change. The simulation is paused while a screen is open; the last frame stays behind it, dimmed.
4. **The dialog set:** Insert CD is dropped, because all game data is installed (ADR-0021); a missing file
   shows an error screen that names it. Advanced Display merges into Display. The other 14 dialogs become
   screens. The store becomes a list with quantities instead of drag and drop.
5. **Two new menus.** A main menu (New Game, Load, Settings, Quit) replaces the launcher; its background is a
   level load-screen map. A pause menu (Resume, Save, Load, Settings, Statistics, Exit) opens on Escape or
   Start. The HUD item menu stays for inventory and weapons; its system icons stay and open the new screens,
   and a cvar can hide them. `startMode` stays for automation and headless runs.
6. **Look:** the game's fonts, colors and HUD sounds, and its localized `JONES_STR_*` strings. Layout uses a
   virtual canvas 480 units high and as wide as the window's aspect.
7. **Saves and controls keep their files.** Save and Load list the save folder with thumbnails; a new save
   gets a default name (level, date, time) that can be confirmed with a gamepad. The `.nds` format is
   unchanged. Control schemes keep their files: gamepad bindings use the original joystick ids, and what
   those can't express goes in a new section that older readers ignore.
8. If rendering fails at startup, errors fall back to the platform's native message box.

## Consequences

- One UI for keyboard, mouse and gamepad; no platform dialog code remains in the game.
- Babylon gains a `ui` module that other games can use; it must not name Jones3D concepts (ADR-0006).
- Menus can't run while the simulation runs; that would need non-blocking callers, which no current feature
  needs.
- The menu sounds use the M7 audio path, so the UI work follows the sound work.
- A data-driven menu format remains possible but costs a parser, validation and an action registry when it
  comes.

## Amendment (2026-11-07)

The `ui` module's `Painter` is implemented by the game, not by a Babylon render painter: Jones3D draws text
with its own `rdFont` fonts (the HUD's `.gcf` materials) and rectangles and images through the same rdroid and
`std3D` path the HUD uses, so the menus look like the HUD without a second font system. Babylon keeps only the
`Painter` interface and the `NullPainter`; a render-backed painter can be added when a second game needs one.
The painter and the screens live in the Babylon column of the game (`Jones3D/Platform/Babylon/`, C++); the
Null column keeps its default-returning dialog stubs.
