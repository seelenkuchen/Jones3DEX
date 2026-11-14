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
| LB / RB | Quick-step: hold RB and push a direction to step left, right, forward or back, independent of the stick magnitude |
| LT (hold) | Target lock on the visible hostile closest to the free camera's forward direction within 35 degrees; with no target it centers the camera forward. While held, the left stick sidesteps sideways and walks or backpedals forward and back. Release ends the lock |
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

## Implementation (2026-11-13)

The style is read once at startup into `sithControl`, where `engine.input.controlStyle` selects Modern or
Classic; non-QOL builds always stay Classic. The movement layer never edits the original movement code. It
runs once per frame before `sithPlayerControls_Process` reads its functions and writes virtual key
overrides: `FORWARD` and `BACK` carry the translated direction and amount, `TURNLEFT` and `TURNRIGHT`
keep their meaning only in the relative states, `RUNFWD` carries walk/run, and quick-step suppresses a
held `STPLEFT`/`STPRIGHT` so only the press edge reaches the original sidestep force move. In the
camera-relative states the layer instead turns `orient` directly toward the move heading, about
the world up axis at up to 1080 degrees per second with the last few degrees eased. From standing,
or for a change over 100 degrees (including reversing), the facing snaps in one update. The
stand-to-walk/run and walk/run-to-stand transitions are skipped, reaching their walk or run move
mode and animation in the same update, so a push from standing moves Indy at once. The original
turn keys stay off. `sithControl_ClearKeyOverrides` drops the overrides before every player frame,
so a cutscene or a transition cannot inherit them.

One move-status classifier (`sithModernControls_GetMode`) is shared by movement and the free camera. It
returns the original tank-style states (climbing, pushing, vehicles, slides) as relative, leaves jump and
forced-move states to the real keys, makes surface swimming and every other grounded state camera-relative,
and makes underwater swimming camera-relative in 3D. The
free camera is world-anchored: its yaw and pitch live in the camera module rather than in Indy's
orientation, right-stick and mouse input orbit it, and the original camera collision is kept. A camera set
by a COG script or a cutscene overrides it for the duration.

Target lock is acquired on the press edge of `TARGETLOCK` from living hostiles, within
35 degrees of the free camera's forward direction and inside the track range, that are visible
from the camera's eye position rather than from Indy; the smallest angle to the camera direction
wins, with distance as the tie-break. The held lock's visibility check uses the same camera eye,
while the range stays measured from Indy. While held, Indy turns toward the target with the
same direct rotation. A sideways stick holds the original sidestep keys, so the original animation
and distance repeat and quick-step's press-edge rule stays off; forward and back walk and
backpedal. The locked step's force move plays at twice its animation rate, which also halves its
duration and doubles its velocity, keeping the distance per step; the held key starts the next
step as soon as the previous one ends. Without a target the camera is centered forward for the
press and the stick uses Indy's facing. The gamepad binder redirects a scheme's left-trigger Run
binding to target lock in Modern, while Classic keeps it as Run.

The first owner play-test replaced the slow turn-in-place with the direct facing rotation and the
continuous strafe thrust with the original repeated sidesteps. The second replaced the remaining
turn-and-pause when starting or reversing with the snap and transition skip above, doubled the
locked sidestep rate, and moved the lock's line of sight to the camera. The third turns the
horizontal momentum with the facing and shortens the start to the original walk or run speed in
about 0.15 s, both only on Modern ground moves; release still stops at once. Deferred: favourite
weapons for D-pad left/right (they remain previous and next owned weapon, with a TODO at the
scheme). The fourth steers underwater swimming through the free camera's yaw and pitch: the stick's
direction becomes a 3D target, the facing turns toward it at 360 degrees per second in yaw and 180 in
pitch (eased near the target, pitch limited to the original 75 degrees), and the original swim thrust
runs from an override of its JUMP key. On the surface the original camera-relative thrust and ledge
grab stay as they were. The fifth makes quick-step a held modifier, RB or Left Alt, plus a
direction: the camera-relative push is resolved to the nearest of Indy's four directions and plays
one original sidestep, one back hop, or a short forced walk equal to the sidestep's 0.1-unit travel.
While target-locked, a backward stick chains the back hop at the locked sidestep rate instead of
backpedaling.
