# ADR-0028: Modern menu style, button prompts and an always-on gamepad

- **Status:** Accepted
- **Date:** 2026-11-08

## Context

M7 rebuilt the original dialogs as in-game screens (ADR-0026), close to the retail layout: separate settings
screens, the HUD font, a zoomed load-screen map behind the main menu and no button prompts. The owner's
play-test on 2026-11-07 found the gamepad unusable in game: with `controls.controller` false the scheme binder
removes every joystick binding, as retail did, while the menus read the pad directly. The owner wants the
menus to follow a current game's conventions, with the main menu of *Indiana Jones and the Great Circle* as
the reference for layout and behavior (not for its art or assets), and the gamepad to be a first-class device.
Decided in the owner's design session on 2026-11-08.

## Decision

- **Main menu:** CONTINUE (loads the newest save; hidden when there is none), NEW GAME, LOAD GAME, OPTIONS,
  EXIT GAME, as an uppercase list at the bottom left. The selection is brighter and marked with a bullet. No
  boxes. The background is a static load-screen image from the game's GOB files, fitted to the screen, not
  zoomed. Menu sounds are loaded at startup, so the main menu is not silent.
- **Options:** one screen with tabs (Gameplay, Controls, Display, Sound) replaces the separate settings
  screens. Tabs switch with Q/E or LB/RB. Settings are rows with `◀ value ▶`; a help column on the right
  describes the selected row (English first). The background is the menu image, darkened.
- **Pause menu:** RESUME, SAVE GAME, LOAD GAME, OPTIONS, STATISTICS, EXIT GAME, in the main menu's style over
  the dimmed game frame. Statistics gets the new style and its label fixes.
- **Controls tab:** a bind list with the button or key icon for each action, and one gamepad setting:
  Rumble (on by default). No controller diagram for now.
- **Font:** menus only; the in-game HUD keeps the original font. Cinzel for headings and items, Jost for rows
  and help text, both under the SIL Open Font License and committed with their licenses. Bolton was considered
  and dropped: nothing in its distribution allows redistribution, and the owner prefers Cinzel and Jost
  (2026-11-08).
- **Button prompts:** a prompt bar at the bottom left of every menu, using Xelu's Free Controller and Key
  Prompts (CC0), committed with their license. Sets: Xbox Series, PS5, Switch, Steam Deck and keyboard; the
  set follows the device used last, by the pad type SDL reports, with Xbox for unknown pads.
- **Gamepad always on:** a connected pad always works and cannot be disabled. Keyboard, mouse and pad are
  active together and the player switches between them at any moment. `controls.controller` is ignored on
  read, dropped on the next save and removed from the docs; the scheme binder no longer removes pad bindings.
- **Default pad layout:** the button map of ADR-0029, applied within the current movement model until the
  modern controls land.

## Consequences

- The menus no longer resemble the retail dialogs. That is presentation, which may change freely; the
  recovered `jonesConfig_*` wrappers keep their results, so scripts and savegames are unaffected.
- The repository gets its first committed runtime assets that are not game data (prompt icons, maybe the
  font); they need a data folder, install rules and license files.
- Always-on pad input changes what reaches `sithControl` when a pad is connected. Headless runs and traces
  have no pad, so the retail traces stay identical.
- Pad-only players can use every screen; the Controls screen must show pad bindings for every action.
- Supersedes the parts of ADR-0026 that describe separate settings screens and the main menu background.
