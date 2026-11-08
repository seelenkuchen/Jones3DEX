# ADR-0029: Modern controls as an Enhanced option

- **Status:** Accepted
- **Date:** 2026-11-08

## Context

Retail uses tank controls on PC (turn and run, a "Look" button, sidesteps) and the N64 port added a slow
strafe. Both are awkward on a current gamepad. The hard part is to combine fluid, omnidirectional running with
the precision that block pushing and platforming need. These changes reach the simulation (movement, camera,
targeting, inventory use), so under the fidelity rules they are Enhanced options that the Retail profile
turns off. Decided in the owner's design session on 2026-11-08.

## Decision

A **Control style** option with the values Modern (the default) and Classic. Classic keeps the original
controls. The Retail profile forces Classic, and the retail trace stays identical.

Modern, on a gamepad:

| Input | Action |
|---|---|
| Left stick | Camera-relative, omnidirectional movement; a small push walks, a full push runs |
| Right stick | Free camera (replaces the "Look" button) |
| R3 | Snap the camera behind Indy |
| L3 | Toggle crouch or crawl |
| LB / RB | Quick-step: one tap moves Indy exactly one original sidestep left or right, independent of the stick |
| LT (hold) | Target lock on the nearest visible hostile in front of Indy, the same pick the game's auto-aim makes; with no target it centers the camera forward. While held, the left stick strafes and backpedals. Release ends the lock |
| RT | Fire the weapon, whip or punch |
| A | Jump, swim up |
| X | Context action: grab, pull, push, open |
| B | Roll or dodge |
| Y | Holster or draw the weapon |
| D-pad Up | Toggle the lighter |
| D-pad Down | Use the best health item carried |
| D-pad Left / Right | Cycle through four favorite weapons, chosen in the inventory |
| View | Open the full inventory |
| Menu | Pause |

Keyboard and mouse get the same model: WASD moves relative to the camera, the mouse drives the free camera,
and quick-step, target lock and quick-select have keys.

The free camera keeps the game's camera collision. Any camera set by a COG script or a cutscene, including
fixed puzzle views, overrides the free camera while it is active.

## Consequences

- A separate milestone before M8, with its own design and tests; it reworks parts of the movement, camera
  and inventory logic in `sith` and `Jones3D`, each change marked `// Added:` or `// Altered:`.
- Quick-step reuses the original sidestep, so alignment for block pushing and jumps matches retail.
- New tests need recorded inputs with expected Enhanced traces; the Retail traces must not change.
- Speedrun and savegame behavior under Classic is unchanged; under Modern, movement differs by design.
