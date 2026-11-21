# ADR-0037: Presentation queries are side-effect free

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The interaction prompt shows what the activation key would do. It must agree with the press exactly: the same
repeat guard, the same weapon and AI filters, the same collision search along the look vector and the same
item search. The collision search writes global scratch: the stack level, per-level collision arrays and the
searched-sector marks. A HUD query that ran the search every frame would otherwise change what the next
simulation step or a COG handler sees, and the headless traces could differ.

The owner's requirement is also that the prompt stays sharp at the window's native resolution (ADR-0035),
while the HUD is drawn into the game frame at the render resolution.

## Decision

1. **Split finding from acting.** `sithPlayerActions_Activate` runs the search through a shared walker that
   offers each collision to a callback. Activate's callback sends the COG messages in place, exactly where the
   original loop sent them. The query's callback records the first target and stops. A helper decides which
   collisions are eligible, so the prompt and the press read the same filters.
2. **Queries save and restore scratch.** `sithCollision_BeginReadOnlySearch` saves the stack level and the
   scratch level a search will use, and `sithCollision_EndReadOnlySearch` puts every array and mark back. The
   thing intersection also restores the orientation translation it sets while testing a face collision, because
   the real search leaves it zero. The activate-target and item queries pair the scope around their searches;
   nested calls have their own save slot.
3. **The query changes no game state.** It does not set the 500 ms repeat time, send a COG message, move a
   thing or write a HUD field. The HUD caches the answer and the present overlay reads the cache.
4. **Native-resolution drawing uses an overlay.** `stdRenderBabylon` gains a second present overlay for the
   gameplay HUD. It runs before the menu present overlay, so a menu and its dim layer cover it, and the prompt
   hides while a menu is open. The overlay maps `JONESCONTROL_ACTION_ACTIVATE` through the active scheme and
   the last used device to its Xelu icon.

## Consequences

- A new presentation query must follow the same rule: no simulation or collision state, or an explicit
  saved/restored scope, and no calls from inside a simulation step that would change ordering.
- The prompt and the press cannot drift apart without both call sites changing; the interaction tests prove
  they agree for surfaces, things, items and the excluded cases, and that repeated queries leave the
  collision scratch checksum unchanged.
- The simulation stays deterministic and both profiles keep their traces with the prompt on. Retail keeps
  the prompt off and locked, like the other Enhanced-only rows.
- The prompt draws at the window's pixel size, independent of the game render resolution.
- A screenshot taken while the HUD overlay is installed uses the present capture path, so it includes the
  prompt and is window-sized, like a menu screenshot.

## Amendment: Push/Pull, climb-up and climb prompts (2026-11-15)

The prompt now also covers grabbing a movable object, climbing on or pulling up, and mounting a climbable
wall. The same rule applies to each kind:

- **Finding is split from acting.** `sithPlayerControls_FindPushPullThing` is the standing press's grab
  search; `sithPlayerControls_CheckPullObject`/`CheckPushObject` are its move checks, and the press keeps
  only the voice, timer and grab-state side effects it had. `sithPlayerControls_FindClimbWall` and
  `MountClimbWall` split `TryMountClimbWall`; `CheckClimbPullUp` is shared by the Activate and Forward
  branches of `ProcessClimbMove`; `CheckWaterLedge` takes a `bMoveToLedge` flag so the query gets the same
  answer without the grab-position move. The query calls the same helpers, so the label and the press
  cannot disagree.
- **The query changes no state.** Every new search runs inside the read-only collision scope. The query also
  never snaps the player's orientation, clears a puppet mode, plays a voice line or moves a thing, which
  the press does only after its decision.
- **The prompt shows the binding the press reads.** The query reports the Sith control function
  (Activate for Use, Pick up and push/pull; Jump for climb-on, wall mount and water ledge; Forward for the
  hanging pull-up). The overlay maps that to the active scheme instead of always using Activate.
- Labels are **Push**, **Pull** or **Push | Pull** from the two move checks, **Climb up** for climb-on and
  pull-up, and **Climb** for a climbable-wall mount. Priority follows the press: items before Activate,
  Activate before push/pull, and 2m climb-on before 1m climb-on before wall mount.
