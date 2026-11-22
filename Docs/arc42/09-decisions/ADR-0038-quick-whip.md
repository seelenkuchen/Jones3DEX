# ADR-0038: Quick whip reuses the Modern step-left row

- **Status:** Accepted
- **Date:** 2026-11-21

## Context

At a locked whip target the owner wants one press to whip and then switch back, without drawing the
whip first and selecting it again afterwards (W10d). The whip has a select key, and on the Modern
gamepad the left shoulder was still unbound while the right shoulder already carries the quick-step
modifier. The key set files are an original format shipped in `KeySets/`: each lists exactly the 37
actions, so a new action row would break them. The new behavior also changes the simulation, so under
the fidelity split it is an Enhanced option that the Retail profile turns off.

The first version (2026-11-21) only quick-whipped at a swing or climb target; elsewhere the pad did
nothing. The owner corrected that: the pad should crack the whip anywhere Indy could use it normally.

## Decision

1. **Reuse the step-left row instead of adding a 38th action.** The Modern gamepad binder maps that
   row's gamepad control (LB) to `SITHCONTROL_QUICKWHIP`; the English `.kfg` format and its 37 actions
   stay unchanged. The Classic gamepad keeps LB as held step-left, and the keyboard step-left row is
   unchanged. The Button Layout's Controller screen names the row **Quick whip** in the Modern style.
2. **The keyboard uses the whip's own select row.** The WHIP action row's key (default `1`) quick-whips
   when a target is available and the option is on; otherwise it selects the whip as before. The
   interaction prompt shows **Swing** or **Climb** with the quick-whip binding of the last used device;
   in the Classic style the pad has no quick whip, so it draws no pad binding.
3. **Draw through the normal selection.** The press remembers the current weapon, selects the whip
   through `sithWeapon_SelectWeapon` so the draw runs through its COG and waits, and then:
   - at a swing or climb target, runs the same whip activation as the Activate press and switches
     back after the swing or climb, as unchanged from the first version;
   - anywhere else the whip can be used, presses fire once on the drawn whip through the normal fire
     path (the same COG, damage, sounds and animation a player gets by firing a drawn whip), and
     switches back to the remembered weapon or empty hands when the weapon's fire wait is over.
   A missing target is not an error: only losing the conditions before the attack or activation runs
   cancels and restores. The 2.0 s wait for a target stays for the target case; a normal attack gets
   the longer draw wait it needs when a weapon change from a gun holsters and draws first.
4. **Enhanced only and not saved.** `gameplay.quickWhip` is a Gameplay option, on by default in
   Enhanced and locked off in Retail; Options > Gameplay writes it like its neighbours. The pending
   and restore state is not in savegames: a save made during a quick whip loads with the whip drawn.
5. The presentation query for the prompt stays read-only, as ADR-0037 requires, and shares the target
   finders and availability checks with the press. The prompt itself is unchanged: **Swing** and
   **Climb** still appear only at a whip point, because a normal attack has no prompt.

## Consequences

- `KeySets/` and the `.kfg` writer/reader stay compatible; an existing scheme needs no migration.
- A player who binds LB on the step-left row in the Modern style binds quick whip; the row shows the
  action under its new name and remains rebindable.
- The Retail profile keeps LB as held step-left and produces the same trace as before.
- Saves stay loadable in both directions; only the follow-up of an in-progress quick whip is lost.
- A new simulation option needs a config test, an interaction test and the UI and docs rows, which
  M8 adds.
