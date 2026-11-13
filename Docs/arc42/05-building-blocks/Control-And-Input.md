# Control And Input

This page documents how raw keyboard, mouse, and joystick state becomes gameplay intent inside Jones3D.

The gamepad is always on: a connected pad works alongside the keyboard and mouse, and
`controls.controller` is ignored and dropped ([ADR-0028](../09-decisions/ADR-0028-modern-menu-style-and-always-on-gamepad.md)).
A Modern control style adds camera-relative movement, a free camera, quick-step, target lock and
quick-select as an Enhanced option ([ADR-0029](../09-decisions/ADR-0029-modern-controls.md)).

The input architecture is intentionally layered:

1. a platform/device layer (`stdControl`)
2. a gameplay binding layer (`sithControl`)
3. gameplay consumers such as the player controller and camera logic

Primary source files:

- [`Libs/sith/Devices/sithControl.c`](../../../Libs/sith/Devices/sithControl.c)
- [`Libs/std/General/stdControl.h`](../../../Libs/std/General/stdControl.h)
- `Libs/std/Babylon/stdControlDevicesBabylon.cpp`
- `Libs/std/Null/stdControlDevicesNull.c`
- [`Libs/sith/Gameplay/sithPlayerControls.c`](../../../Libs/sith/Gameplay/sithPlayerControls.c)
- [`Libs/sith/Engine/sithCamera.c`](../../../Libs/sith/Engine/sithCamera.c)

## Architectural Split

The engine does not let gameplay code talk to the platform's input API or raw key codes directly.

Instead it uses:

- `stdControl` for device enumeration, acquisition, raw state reads, axis normalization, and idle detection
- `sithControl` for logical gameplay functions such as forward, jump, activate, fire, look, and camera mode

That split is important because it lets the gameplay layer speak in terms of "actions" and "axes" rather than hardware details.

## Gameplay Binding Layer

The binding layer is centered in [`sithControl.c`](../../../Libs/sith/Devices/sithControl.c#L1).

Its core state includes:

- a callback list for gameplay control consumers
- a function-flag table describing which logical functions are keys or axes
- a binding table that maps logical functions to one or more keys, buttons, or axes
- a hardcoded function-name list used by config/save logic and UI

The important entry points are:

- [`sithControl_Startup()`](../../../Libs/sith/Devices/sithControl.c#L132)
- [`sithControl_Open()`](../../../Libs/sith/Devices/sithControl.c#L158)
- [`sithControl_Reset()`](../../../Libs/sith/Devices/sithControl.c#L679)
- [`sithControl_RegisterControlFunctions()`](../../../Libs/sith/Devices/sithControl.c#L685)
- [`sithControl_DefaultInit()`](../../../Libs/sith/Devices/sithControl.c#L587)

### Registered Gameplay Functions

[`sithControl_RegisterControlFunctions()`](../../../Libs/sith/Devices/sithControl.c#L685) builds the engine's logical action namespace.

Examples include:

- movement and turning
- jump and crawl toggle
- weapon and item activation
- map, save, screenshot, and camera controls
- mouse-look and pitch-turn axes

This is the stable contract the rest of the gameplay code consumes.

### Default Bindings

[`sithControl_DefaultInit()`](../../../Libs/sith/Devices/sithControl.c#L587) resets the binding tables and then installs hardcoded defaults through:

- [`sithControl_RegisterKeyboardBindings()`](../../../Libs/sith/Devices/sithControl.c#L751)
- [`sithControl_RegisterJoystickBindings()`](../../../Libs/sith/Devices/sithControl.c#L815)
- [`sithControl_RegisterMouseBindings()`](../../../Libs/sith/Devices/sithControl.c#L842)

Architecturally, the defaults are still data baked into code rather than an external action-map asset. The runtime config system changes where those bindings are stored and reloaded, but the underlying action model is still the original one.

## Platform Device Layer

The platform/input backend lives in `stdControl`. Since M6.3 it has two implementations. The Babylon column's `Libs/std/Babylon/stdControlDevicesBabylon.cpp` reads the input state built from SDL3 platform events through the one shared `babylon::input::InputState`; the headless runner's `--input` script feeds the same state. The Null column's `Libs/std/Null/stdControlDevicesNull.c` reports nothing pressed unless the headless script set a key.

Both backends follow the same architecture:

- startup and open prepare the runtime control state
- per-frame reads take one device snapshot into raw key/button/axis arrays
- helper functions expose normalized axis and key state to `sithControl`

The shared entries are in `Libs/std/Null/stdControlNull.c`:

- `stdControl_Startup()`
- `stdControl_Open()`
- `stdControl_ReadControls()`
- `stdControl_EnableAxis()`
- `stdControl_SetActivation()`

The device side implements `stdControl_ReadDevices()`. It reads keyboard, mouse buttons, mouse axes and gamepads from the same `InputState`. The DirectInput backends of the removed DX6 and DX9 builds polled keyboards, joysticks and mice through the same seam. Mouse motion and the wheel are reported as the movement accumulated since the previous read, in DirectInput's units (a wheel notch is `WHEEL_DELTA`, 120) and with DirectInput's signs (y positive down), clamped to the registered axis range.

A connected gamepad becomes joystick slot 0, 1, ... in `InputState::gamepads()` order, up to `STDCONTROL_MAX_JOYSTICK_DEVICES`. Each pad registers six axes and one POV, with the ranges and signs the removed DX9 backend's `DIJOYSTATE` and XInput paths produced:

| stdControl axis | gamepad axis | state range | notes |
| --- | --- | --- | --- |
| X | LeftX | -32768..32767 | left stick dead zone |
| Y | LeftY | -32768..32767 | down positive |
| Z | LeftTrigger | -255..255 | 0..255, 30 of 255 threshold |
| RX | RightX | -32768..32767 | right stick dead zone |
| RY | RightY | -32768..32767 | down positive |
| RZ | RightTrigger | -255..255 | 0..255, 30 of 255 threshold |

| button index | gamepad button |
| --- | --- |
| 0 | South |
| 1 | East |
| 2 | West |
| 3 | North |
| 4 | LeftShoulder |
| 5 | RightShoulder |
| 6 | Back |
| 7 | Start |
| 8 | LeftStick |
| 9 | RightStick |
| 10 | Guide |

The d-pad drives POV 0 (left, up, right, down).

Rumble is output only and never changes the simulation. The game calls
`stdControl_Rumble(low, high, durationMs)` when an event should shake the pad: taking damage
(strength grows with the damage), firing a weapon, and standing inside an explosion's range. The
shared layer drops the call while rumble is off and otherwise forwards it to the column's device
side; the Babylon column calls `babylon::platform::Platform::rumbleGamepad()` for every connected
pad, and the Null column only records the call in test builds. `engine.input.rumble` (default on,
both profiles) is read in `JonesControl_Startup()` and changed by the Controls screen's Rumble row.

Enumeration happens whenever `stdControl_GetNumJoysticks()` is called, which pumps the platform queue, so a pad connected before startup is seen. The game calls it only from `JonesControl_Startup()`. A pad connected or disconnected later is reflected by the device read (the joystick slots follow `InputState::gamepads()`), but the game does not re-enumerate or rebuild its gameplay bindings at runtime: connect a pad before startup, or restart the game. `sithControl_RebindJoystick()` rebuilds the bindings but does not enable axes for newly seen pads.


## Per-Frame Read Pass

`stdControl_ReadControls()` is the raw device sampling step.

Each frame it:

1. resets idle state and just-pressed state
2. captures current read time and delta time
3. derives frame-rate-style timing helpers used by the higher binding layer
4. clears axis state accumulators
5. reads keyboard, joystick, and mouse devices that are currently enabled
6. stores the read time as the new baseline

This means the gameplay layer sees one coherent input snapshot per frame rather than each gameplay system polling devices independently.

## Axis And Key Normalization

The platform layer exposes several different views of the same raw data:

- `stdControl_ReadAxis()` returns a normalized analog-style value with deadzone handling
- `stdControl_ReadAxisRaw()` returns the raw centered delta
- `stdControl_ReadKeyAsAxis()` turns a digital key hold into an axis-like value proportional to read time
- `stdControl_ReadKey()` returns direct key/button state
- `stdControl_ControlsIdle()` exposes whether the current frame's reads were effectively idle

This is why the higher layer can represent both styles cleanly:

- digital actions such as jump or activate
- analog or pseudo-analog inputs such as turning, pitch, or wheel scrolling

## Binding Resolution In `sithControl`

Once raw input is available, the gameplay layer resolves it through binding lists:

- [`sithControl_GetKey()`](../../../Libs/sith/Devices/sithControl.c#L545)
- [`sithControl_GetAxis()`](../../../Libs/sith/Devices/sithControl.c#L512)
- [`sithControl_GetKeyAsAxis()`](../../../Libs/sith/Devices/sithControl.c#L476)
- [`sithControl_GetKeyAsAxisNormalized()`](../../../Libs/sith/Devices/sithControl.c#L421)

The binding layer can:

- merge multiple bindings into one logical function
- apply axis inversion
- apply per-binding sensitivity
- treat some button bindings as axis contributors
- suppress certain joystick reads depending on control options

This lets the engine map many hardware forms onto the same movement and camera model without rewriting the gameplay code.

The Modern control style adds a per-frame override layer on top of the bindings. `sithControl_SetKeyOverride()` makes a movement function read as pressed with an axis amount (0..1) and a one-press edge, `sithControl_SuppressKey()` makes a function without an override read as released, and `sithControl_ClearKeyOverrides()` drops both at the next frame. Overrides take precedence over suppressions, so Modern can suppress a real movement binding and drive the same function virtually. `engine.input.controlStyle` selects Modern or Classic; QOL builds default to Modern, non-QOL builds always use Classic.

Target lock is the `TARGETLOCK` key function, active only in Modern. It acquires the live
actor closest to the free camera's forward direction within 35 degrees and the track range that
is visible from the camera's eye, then keeps it while visible from the camera and in range from
Indy. Indy's own line of sight does not matter. Its defaults are `F` and the right
mouse button on keyboard and mouse, and the left trigger (`Z+`) on a gamepad. In Modern the scheme binder redirects a scheme's LT-as-Run binding to target
lock; Run comes from the left-stick magnitude instead. Classic keeps LT as Run. The lock does not
add an action to the `.kfg` format, so files written before it still load.

## Modern Control Style End To End

`engine.input.controlStyle` is read once in `JonesControl_Startup` and stored in `sithControl`, so
the movement, camera and binder code all see the same answer. QOL builds default to Modern and
write the key back; the Retail profile always uses Classic and neither reads nor writes it.

Each player frame runs `sithModernControls_Update` before the original movement code. It clears the
previous frame's virtual keys, classifies the current move status, reads the stick and the real
movement keys, and writes the frame's `FORWARD`, `BACK`, `TURNLEFT`, `TURNRIGHT` and `RUNFWD`
overrides. `sithModernControls_Compute` is the pure part: given a screen-space move vector, the
camera and player yaws and a mode, it returns the original keys to press and their amounts plus the
heading to face. The classifier returns three modes:

- **Camera-relative**: grounded and swimming states; the stick moves Indy relative to the camera,
  turning his `orient` directly toward the move heading at up to 1080 degrees per second. From
  standing, or for a change over 100 degrees, the facing snaps in one update; shorter changes ease
  below 10 degrees. The stand/walk/run and walk/run/stand transitions are skipped, so the first
  update already uses the run or walk move mode the transition would have reached. The original
  turn keys stay off.
- **Relative**: the original tank-style states (climbing, pushing, pulling, slides and vehicles);
  the real keys keep their original meaning.
- **None**: jump, fall and forced-move states; the real keys pass through untouched.

The free camera is world-anchored in `sithModernCamera`: right-stick and mouse motion orbit it, the
original camera collision stays, and a camera set by a COG script or cutscene wins while active.
Quick-step reuses the original sidestep force move. The original repeats a held step, so Modern
suppresses a held `STPLEFT`/`STPRIGHT` and lets only the press edge start exactly one step. While
the target lock is held, a sideways stick keeps the step key held, the force move plays at twice
its animation rate (same distance per step, half the time) and the original immediately starts the
next step while the stick stays sideways. Quick
select uses the scheme's D-pad bindings: up toggles the lighter, down uses the best health item
(herbs, then small health pack, then big herbs, then big health pack, as the original HEALTH
function does), and left/right remain previous and next owned weapon. Favourite weapons for
left/right are deferred.

### Default schemes

A new install picks the `JONES_STR_DFLTMODERN` ("Modern keyboard and gamepad") scheme when the style
is Modern and the `JONES_STR_DFLTPAD` ("Keyboard and gamepad") scheme when it is Classic. A
remembered active scheme is never replaced. See
[In-Game-Menus.md](In-Game-Menus.md#default-keyboard-and-gamepad-scheme) for the scheme file format
and the pad layout of the Classic default.

**Keyboard and gamepad (Classic default)**

| Input | Action |
|---|---|
| Arrow up / down | Forward / back |
| Arrow left / right | Turn left / right |
| A / S | Step left / right |
| Left / right Shift | Run |
| X, Alt | Jump |
| Left / right Ctrl | Activate and fire |
| Z | Roll |
| C | Crawl |
| Space | Toggle weapon |
| Insert / Numpad 0 | Look |
| Comma / period | Previous / next weapon |
| 0-9, P, G | Weapons, satchel, grenade |
| L / H / M / I / K | Lighter / health / map / inventory / chalk |
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
| D-pad up / down | Toggle light / health |
| D-pad left / right | Previous / next weapon |
| Back (View) | Interface (inventory) |

**Modern keyboard and gamepad (Modern default)**

| Input | Action |
|---|---|
| W / S | Forward / back, relative to the camera |
| A / D | Turn left / right, relative to the camera |
| Arrow keys | Same as W / S / A / D |
| Mouse | Free camera (with `controls.mouse` on) |
| Left Shift | Hold to run; a small stick push walks |
| Space | Jump |
| Left Ctrl / left mouse button | Activate and fire |
| Q / E | Quick-step left / right, exactly one original step per press |
| F / right mouse button | Target lock |
| C | Crawl |
| R | Draw or holster the weapon |
| L / H | Lighter / best health item |
| Tab or I | Inventory |
| 1-0, P, G | Weapons, satchel, grenade |
| M | Map |
| V / middle mouse button | Snap the camera behind Indy |
| Left stick | Omnidirectional movement relative to the camera |
| Right stick | Free camera |
| A (South) | Jump |
| X (West) | Activate (grab, pull, push, open; fires with a weapon drawn) |
| RT | Activate as well (fire) |
| B (East) | Roll |
| Y (North) | Toggle weapon |
| LB / RB | Step left / step right |
| LT (hold) | Target lock |
| L3 | Crawl |
| R3 | Snap the camera behind Indy |
| D-pad up / down | Toggle light / health |
| D-pad left / right | Previous / next weapon |
| Back (View) | Interface (inventory) |



## Callback Dispatch And Camera Side Effects

[`sithControl_Update()`](../../../Libs/sith/Devices/sithControl.c#L334) is the high-level runtime entry point called from the frame loop.

It performs two jobs.

### 1. Idle-Camera Management

Before dispatching gameplay controls, [`sithControl_Update()`](../../../Libs/sith/Devices/sithControl.c#L334) checks:

- whether a local player exists
- whether the player is stationary
- whether camera cutscene mode is inactive
- whether the player is not hanging, climbing, or in a vehicle
- whether raw controls have been idle long enough

If so, it switches the camera through:

- [`sithCamera_SetCameraFocus()`](../../../Libs/sith/Engine/sithCamera.c#L356)
- [`sithCamera_SetCurrentCamera()`](../../../Libs/sith/Engine/sithCamera.c#L288)

So the control layer is not just about reading input. It also owns one of the engine's user-experience policies: the automatic idle camera.

### 2. Gameplay Control Callback Dispatch

After camera management, [`sithControl_Update()`](../../../Libs/sith/Devices/sithControl.c#L334) reads controls once through [`sithControl_ReadControls()`](../../../Libs/sith/Devices/sithControl.c#L576), dispatches the registered callback chain, and finalizes the pass through [`sithControl_FinishRead()`](../../../Libs/sith/Devices/sithControl.c#L582).

The callback model means:

- the raw device state is sampled once per frame
- gameplay consumers all see the same snapshot
- the first callback that fully handles the frame can stop further callback processing

## Main Gameplay Consumer: Player Controls

The main gameplay consumer logic lives in [`sithPlayerControls_Process()`](../../../Libs/sith/Gameplay/sithPlayerControls.c#L471).

That function fans control input into several sub-pipelines:

- [`sithPlayerControls_ProcessLookControls()`](../../../Libs/sith/Gameplay/sithPlayerControls.c#L833)
- [`sithPlayerControls_ProcessGeneralMove()`](../../../Libs/sith/Gameplay/sithPlayerControls.c#L900)
- [`sithPlayerControls_ProcessHUDControls()`](../../../Libs/sith/Gameplay/sithPlayerControls.c#L1565)
- [`sithPlayerControls_ProcessWeaponAim()`](../../../Libs/sith/Gameplay/sithPlayerControls.c#L2410)

This is where logical actions such as forward, jump, activate, turn, and mouse turn finally become:

- thrust and angular velocity
- movement-state changes
- interaction attempts
- HUD toggles
- aim-mode changes and weapon messaging

For example, aim-mode transitions eventually emit script-visible aim messages through [`sithWeapon_SendMessageAim()`](../../../Libs/sith/World/sithWeapon.c#L1984).

## Why The Layering Matters

The input stack is easy to summarize as:

- `stdControl` owns devices
- `sithControl` owns action binding and per-frame dispatch
- `sithPlayerControls` and related gameplay systems own interpretation

That separation is one of the cleaner parts of the engine architecture. Even though the code is old-school and heavily procedural, the responsibilities are well split:

- hardware details do not leak into player movement code
- gameplay code does not need to know which device produced an action
- camera idling and control policies can sit above the raw device layer without contaminating it

## Control Scheme Files

The Babylon column restores the original `.kfg` control schemes in
[`JonesControlSchemes.c`](../../../Jones3D/Platform/Babylon/JonesControlSchemes.c), the Win32
`jonesConfig.c` scheme code without its dialogs. `Jones.cfg`'s `controls.configFile` names the
active scheme; the windowed run loads it at startup and binds it from `JonesControl_Startup` after
`sithControl` registers its functions. The headless run keeps the engine's default bindings.

The files live in the key-sets directory the original built: `<installPath>\<keysets>` when
`installPath` is set, else `..\<keysets>` relative to the working directory. A file starts with the
localized heading, the enabled marker `1`, and the `Actions`/`Keys` column header, then one quoted
action line for each of the 37 actions followed by its quoted keyboard, mouse or joystick bindings.
Line endings are CRLF, and the columns are padded to the same widths the original used.

The original format can name keyboard keys, joystick buttons, POV directions and the X, Y, Z and
RZ axes. It cannot name mouse buttons or the right-stick RX/RY axes. Those bindings go in a
trailing `EXTRAS` section after the 37 action lines, which the original reader skips because its
action loop ends there. Each extras line is `"<action>" "<control>"`, where the control is
`MOUSE1`..`MOUSE4`, `Z-`/`Z+` (the left trigger), or `RX-`/`RX+`/`RY-`/`RY+`.

The original default scheme differs from the engine defaults in three ways: activate moves off the
grave key, `I` opens the HUD menu, and pad bindings are part of the scheme rather than an option.
Joystick bindings are always applied; the binder no longer removes them when `controls.controller`
is false, and `JonesControl_Startup` drops that key from the config.

There are four built-in defaults now: the three original schemes and `JONES_STR_DFLTPAD`
("Keyboard and gamepad"), which a new install uses when `controls.configFile` names no active
scheme. A remembered active scheme stays as it is. The new scheme keeps the first default's
keyboard bindings and adds this pad layout inside the classic movement model; Start stays unbound
because the pause menu reads it directly. The triggers use the original Z and RZ axis ids: LT is
`Z+` (`JONESCONFIG_JOYAXISKID_GROUND`) and RT is `RZ+` (`JONESCONFIG_JOYAXISKID_TWISTR`).

| Pad | Action |
|---|---|
| Left stick | Walk forward/back, turn left/right |
| A (South) | Jump |
| X (West) | Activate (grab, pull, push, open; fires with a weapon drawn) |
| RT | Activate as well (fire) |
| B (East) | Roll |
| Y (North) | Toggle weapon |
| LB / RB | Step left / step right |
| LT (hold) | Run in Classic; target lock in Modern |
| L3 | Crawl |
| R3 | Look |
| D-pad up / down | Toggle light / Health |
| D-pad left / right | Previous / next weapon |
| Back (View) | Interface (inventory) |

A gamepad connected or removed while the game runs is picked up at the next control update: the
adapter's joystick change counter makes `JonesControl_ProcessControls` register the new axes and
rebind the active scheme, and a disconnect unbinds the pad while the keyboard bindings stay.

## Menu And Pause Keys

[`JonesMain_ProcessMenu()`](../../../Jones3D/Main/JonesMain.c) opens the pause menu on Escape or
gamepad Start (button 7). It does so only when the player is alive and no cutscene, load, HUD menu,
console or pause owns the frame (`JonesMain_CanOpenPauseMenu`), and it uses the same game-time and
sound pause pair as the HUD menu. The pause menu's entries run the HUD menu's system-item functions
([In-Game-Menus.md](In-Game-Menus.md)). Those system items (Statistics, Save, Load, Settings and Exit)
are off by default because the pause menu replaces them; `engine.ui.hud.systemIcons` true brings them
back into the HUD menu.

Escape no longer opens the HUD item menu. The HUD menu opens on the active control scheme's
interface action, which the default scheme binds to `I`. The original also bound it to a key and a
gamepad button through the removed Win32 controls dialog, so a loaded scheme can have no interface
binding today; Tab and gamepad Back (button 6) then stand in as a fallback while a scheme is active
(`jonesConfig_HasActiveControlScheme`). The headless run and the Null column have no scheme and keep
their old bindings. The Controls screen makes the menu binds editable again.
