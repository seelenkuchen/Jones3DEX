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
The second version did that, but still drew the whip first and holstered it before the previous weapon
returned. The owner corrected that too: the whip must be usable on the press, with no equip or unequip
animation, and the previous weapon must be back as soon as the attack ends.

## Decision

1. **Reuse the step-left row instead of adding a 38th action.** The Modern gamepad binder maps that
   row's gamepad control (LB) to `SITHCONTROL_QUICKWHIP`; the English `.kfg` format and its 37 actions
   stay unchanged. The Classic gamepad keeps LB as held step-left, and the keyboard step-left row is
   unchanged. The Button Layout's Controller screen names the row **Quick whip** in the Modern style.
2. **The keyboard uses the whip's own select row.** The WHIP action row's key (default `1`) quick-whips
   when a target is available and the option is on; otherwise it selects the whip as before. The
   interaction prompt shows **Swing** or **Climb** with the quick-whip binding of the last used device;
   in the Classic style the pad has no quick whip, so it draws no pad binding.
3. **Take the whip in hand without its draw or holster key.** The press remembers the current
   weapon and performs the same selection changes as a normal weapon change, but without the visible
   keys or waits: the previous weapon's COG receives `deselected` and the whip's COG receives
   `selected`, then the draw and holster puppet keys are cancelled, their mount waits are zeroed, and
   the keys' pull/put callback markers are run directly so the armed mode, the hand and holster
   models and the script captures match a normal change. Then:
   - at a swing or climb target, runs the same whip activation as the Activate press and switches
     back after the swing or climb;
   - anywhere else the whip can be used, presses one normal whip attack through its COG at once, and
     switches back to the remembered weapon or empty hands when the attack's fire wait is over. The
     attack's retract key is finished by hand rather than played, so the whip actor it created is
     still released and destroyed.
   The restore uses the same instant path in reverse: the whip's `deselected`/`deactivated` messages
   are sent, its retract key is finished without playing it, and the remembered weapon is selected
   with its draw key cancelled. A missing target is not an error; the 2.0 s target wait only covers a
   target move that has not started yet. The whip's own attack animation and sound still play.
4. **Enhanced only and not saved.** `gameplay.quickWhip` is a Gameplay option, on by default in
   Enhanced and locked off in Retail; Options > Gameplay writes it like its neighbours. The pending
   and restore state is not in savegames: a save made during a quick whip loads with the whip current.
5. The presentation query for the prompt stays read-only, as ADR-0037 requires, and shares the target
   finders and availability checks with the press. The prompt itself is unchanged: **Swing** and
   **Climb** still appear only at a whip point, because a normal attack has no prompt.

## Consequences

- `KeySets/` and the `.kfg` writer/reader stay compatible; an existing scheme needs no migration.
- A player who binds LB on the step-left row in the Modern style binds quick whip; the row shows the
  action under its new name and remains rebindable.
- The Retail profile keeps LB as held step-left and produces the same trace as before.
- The quick whip has no equip or unequip animation: its own attack and retract play, but the previous
  weapon's holster and draw keys do not. Normal weapon selection, cycling and changes keep theirs.
- Saves stay loadable in both directions; only the follow-up of an in-progress quick whip is lost.
- A new simulation option needs a config test, an interaction test and the UI and docs rows, which
  M8 adds.
