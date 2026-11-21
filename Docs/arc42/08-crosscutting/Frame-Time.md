# Frame time

This page maps everything that depends on frame time or wall time today, as input for M9's fixed
simulation tick with interpolated rendering. It is an audit, not a design: the decision is in
[ADR-0039](../09-decisions/ADR-0039-fixed-tick-interpolated-rendering.md). Retail keeps the timing it has;
only the Enhanced profile changes.

## 1. Current model

`sithTime_Advance` ([`Libs/sith/Gameplay/sithTime.c:31`](../../../Libs/sith/Gameplay/sithTime.c)) measures the
wall clock once per game frame through `stdPlatform_GetTimeMsec`, clamps the delta to 1..200 ms, and publishes:

| Value | Unit | Meaning |
|---|---|---|
| `sithTime_g_frameTime` | ms | The clamped frame delta; `0` while paused |
| `sithTime_g_frameTimeFlex` | s | `frameTime * 0.001`, minimum 0.0001 |
| `sithTime_g_fps` | Hz | `1 / frameTimeFlex`, i.e. the reciprocal of the delta |
| `sithTime_g_msecGameTime` | ms | Accumulated game time; paused frames add nothing |
| `sithTime_g_secGameTime` | s | `msecGameTime * 0.001` |
| `sithTime_g_clockTime` | ms | Raw wall clock of the previous `sithTime_Advance` |

`sithUpdate` ([`Libs/sith/Main/sithMain.c:542`](../../../Libs/sith/Main/sithMain.c)) increments
`sithMain_g_frameNumber`, calls `sithAdvanceRenderTick`, then passes `frameTimeFlex`/`frameTime` to animation,
controls, things and multiplayer. `sithDrawScene` ([`Libs/sith/Main/sithMain.c:595`](../../../Libs/sith/Main/sithMain.c))
advances the render tick a second time, updates the camera and draws. Physics substeps at a fixed 50 Hz only in
the detached-player path ([`sithPhysics.c:1118`](../../../Libs/sith/Engine/sithPhysics.c), ADR-0030); every
other physics path integrates with the frame delta.

`sithTime_SetFrameTime` is only reached from tests; the headless runner instead latches a virtual clock
(`stdPlatformNull_UseVirtualClock`) and advances it after every `JonesMain_Process` by `--step` ms (default 33)
plus the 1 ms that a present adds in `stdDisplay_Update`. The headless trace prints `sithTime_g_secGameTime`
([`JonesHeadless.c:528`](../../../Jones3D/Platform/Null/JonesHeadless.c)), so the trace encodes game time.

Three independent per-frame counters exist:

| Counter | Incremented at | Used for |
|---|---|---|
| `sithMain_g_frameNumber` | [`sithMain.c:565`](../../../Libs/sith/Main/sithMain.c) | Frame cycles, "rendered this frame" checks |
| `sithMain_g_curRenderTick` | [`sithMain.c:762`](../../../Libs/sith/Main/sithMain.c), also at `sithMain.c:567`, `sithMain.c:600`, `sithOverlayMap.c:307`, `sithAIUtil.c:2677` | Sector/surface/vertex render caches |
| `rdCache_frameNum` | [`rdCache.c:59`](../../../Libs/rdroid/Raster/rdCache.c) | Model joint-matrix cache per drawn frame |

## 2. Classes

Each site below is exactly one of:

- **tick** - simulation logic that is correct on a fixed tick unchanged; it already scales by, or is scheduled
  in, game time.
- **tick-change** - simulation logic whose result changes with a different but fixed step: per-frame counters,
  frame-number cycles, reciprocal-of-dt use, non-linear delta use, clamps, or wall clock read from the simulation.
- **render** - presentation that must run every rendered frame and needs interpolated or render-time input.
- **wall** - real-time use (menus, loading, intro skip, sound driver, movie timer) that the tick does not affect.

## 3. Reproducing the audit

Run from the repository root:

```bash
rg -n 'sithTime_g_frameTime|sithTime_g_frameTimeFlex|sithTime_g_fps|secDeltaTime|frameTime|stdPlatform_GetTimeMsec|SoundDriver_GetTimeMsec|sithTime_g_msecGameTime|sithTime_g_secGameTime|sithMain_g_frameNumber|SITH_ISFRAMECYCLE' \
    Libs/sith Libs/rdroid Libs/sound Libs/smush Jones3D Libs/std Libs/wkernel
```

The command returns 1146 lines over 116 files: 1006 in production code and 140 in tests and recorded test
vectors. Per-pattern counts: `secDeltaTime` 657, `sithTime_g_msecGameTime` 187, `sithTime_g_secGameTime` 84,
`frameTime` 63, `stdPlatform_GetTimeMsec` 66, `sithTime_g_frameTime` 55, `sithMain_g_frameNumber` 44,
`sithTime_g_frameTimeFlex` 40, `sithTime_g_fps` 27, `SITH_ISFRAMECYCLE` 25, `SoundDriver_GetTimeMsec` 7.

The per-file inventory in section 9 covers every hit. Test and generated-vector hits are grouped there. The
additional grep for wall clock used by simulation is:

```bash
rg -n 'sithTime_g_clockTime' Libs/sith Jones3D Libs/rdroid Libs/sound -g '!**/Tests/**'
```

## 4. Simulation and rendering interleaved today

The tick and the draw are already entangled; extra draw-only frames must not repeat any of this:

1. **Draw mutates simulation-visible state.** `sithRender_RenderThing`
   ([`sithRender.c:1362`](../../../Libs/sith/Engine/sithRender.c)) sets `pThing->renderFrame`, sets
   `SITH_TF_SEEN`, wakes a sleeping AI, and sends the COG `SIGHTED` message. `sithThing_Draw`
   ([`sithThing.c:1488`](../../../Libs/sith/World/sithThing.c)) temporarily writes `orient.dvec` and swaps
   meshes back afterwards.
2. **The camera is updated while drawing.** `sithDrawScene` calls `sithCamera_Update`
   ([`sithMain.c:601`](../../../Libs/sith/Main/sithMain.c)) before `sithCamera_RenderScene`. That update
   integrates interpolation state with frame time, can start fade animations and can write `pThing1->alpha`.
3. **Draw creates things.** `sithRender_RenderScene` calls `sithAIUtil_RenderAIWaypoints`
   ([`sithRender.c:442`](../../../Libs/sith/Engine/sithRender.c)), which calls `sithFX_CreateLaserThing` and
   therefore allocates and links things during the draw.
4. **Render caches live in world state.** `sithRender_BuildVisibleSectorList` and neighbours key sectors,
   surfaces and vertices by `sithMain_g_curRenderTick`, and `sithOverlayMap_DrawSectors` reuses the same field.
   The render tick is advanced in `sithUpdate`, again in `sithDrawScene`, and again by the overlay map and the
   AI view query, so it is not "one tick per frame" today.
5. **Puppet joint matrices are built both in update and draw.** For path-moved things,
   `sithPuppet_UpdatePuppet` builds joint matrices during update ([`sithPuppet.c:371`](../../../Libs/sith/Engine/sithPuppet.c));
   otherwise the draw calls `rdPuppet_BuildJointMatrices`.
6. **The HUD and overlay run in the process phase.** `JonesMain_ProcessGame` calls `sithOverlayMap_Draw` and
   `JonesHud_Process` after `sithDrawScene` ([`JonesMain.c:1254`](../../../Jones3D/Main/JonesMain.c)); both
   mutate presentation state and the overlay advances the render tick.
7. **The headless present is inside the timed loop.** `stdDisplay_Update` advances the virtual clock by 1 ms
   ([`stdDisplayNull.c:858`](../../../Libs/std/Null/stdDisplayNull.c)), so the drawn frame contributes to the
   next measured delta.

## 5. tick

Simulation logic that a fixed tick can run unchanged. Game-time scheduling is tick, not wall, because it pauses
with the game.

| File:line | Function | Effect | Proposed handling |
|---|---|---|---|
| `Libs/sith/Gameplay/sithTime.c:31-124` | `sithTime_Advance`, `_Pause`, `_Resume`, `_Reset`, `_SetGameTime`, `_SetFrameTime` | Produces every frame-time value; pause skips game time | Fixed tick writes the constant step into `frameTime`/`frameTimeFlex`; keep `msecGameTime` accumulation |
| `Libs/sith/Main/sithMain.c:542-624` | `sithUpdate`, `sithDrawScene`, `sithAdvanceRenderTick` | Update/draw order and render tick | Split tick from draw; one tick per accumulated step, one draw per rendered frame |
| `Libs/sith/Gameplay/sithEvent.c:85-150` | `sithEvent_CreateEvent`, `_RegisterTask`, `_Process` | Queued events and interval tasks on `msecGameTime` | No change; fires on tick boundaries |
| `Libs/sith/Cog/sithCog.c:307-313,1844-1861` | `sithCog_UpdateThingTimer`, `sithCog_ProcessCogs` | Thing and cog pulse/timer checks | No change |
| `Libs/sith/Cog/sithCogFunction.c:131,395,420,677,682,743` | COG wait/pulse/timer/time functions | Schedules and reads game time | No change |
| `Libs/sith/Cog/sithCogFunctionAI.c:639` | AI attack-start setter | Game-time stamp | No change |
| `Libs/sith/Cog/sithCogFunctionThing.c:1694,1719` | Thing pulse/timer setters | Game-time scheduling | No change |
| `Libs/sith/AI/sithAI.c:196,227,237-249,304` | `sithAI_Process`, state change, instinct scheduler | AI update and pause times in ms | No change |
| `Libs/sith/AI/sithAIMove.c:447,2035` | Pause-move checks | Game-time thresholds | No change |
| `Libs/sith/AI/sithAIInstinct.c` (game-time hits throughout) | Instinct state machine | `msecNextUpdate`, attack/hunt/flee/talk timestamps | No change |
| `Libs/sith/AI/sithAIUtil.c:186,197,257,268,446,608` | Attack/goal/fire/pause stamps | Game-time thresholds | No change |
| `Libs/sith/World/sithWeapon.c` (game-time hits throughout) | Weapon timers, activation, rapid fire | Game-time thresholds | No change |
| `Libs/sith/World/sithExplosion.c:39-274` | `sithExplosion_Update` and setup | Baby/expand/fade schedule in game time | No change |
| `Libs/sith/World/sithItem.c:41,43` | Touch cooldown | Game-time threshold | No change |
| `Libs/sith/Engine/sithAnimate.c:379,421,461` | Animation setup | First `msecNextFrameTime` | No change |
| `Libs/sith/Devices/sithComm.c:92,120,201` | Message/comm stamps | Game-time | No change |
| `Libs/sith/Dss/sithMulti.c:107-261,558,602,627,989,992` | Timeouts, ping, quit delay, sync stamp | Game-time networking | Keep on game time; a fixed tick makes timeouts stable |
| `Libs/sith/Dss/sithGamesave.c:739` | Save header | Writes `msecGameTime` | No change; trace/save meaning preserved |
| `Libs/sith/Dss/sithDSS.c:1211` | Event time delta serialization | Relative game time | No change |
| `Libs/sith/World/sithVoice.c:118,375,426` | Head-swap timing via `SITHVOICE_SEC_GAME_TIME` | Game-time lip-sync head swap | No change (presentation timing already game time) |
| `Libs/sith/World/sithSprite.c:63-65` | `sithSprite_Update` | Delta unused | No change |
| `Libs/sith/Gameplay/sithWhip.c:239-241` | `ProcessWhipClimbMove` | Delta unused | No change |
| `Jones3D/Platform/Null/JonesPlatformNull.c:42,52` | Quick-save HUD timer | Game-time timer | No change |
| `Jones3D/Display/JonesHud.c:3627,3651` | Statistics update | Reads game time | No change |
| `Jones3D/Play/jonesInventory.c:237,355` | Statistics | Reads game time | No change |
| `Jones3D/Platform/Null/JonesHeadless.c:528,570` | Trace output | Prints game time | No change; Enhanced headless must keep its fixed `--step` trace |

## 6. tick-change

These change value or cadence when the step changes. Most are harmless but must be reviewed for trace and feel.

| File:line | Function | Effect | Proposed handling |
|---|---|---|---|
| `Libs/sith/Main/sithMain.h:51` + `sithPuppet.c:985-1116`, `sithActor.c:129-446`, `sithCollision.c:681`, `sithPhysics.c:623,632,961,1106,1214`, `sithAnimate.c:1436,1457,1467`, `sithFX.c:1136`, `sithWeapon.c:371`, `sithPlayerControls.c:3525` | `SITH_ISFRAMECYCLE` users | "Every 2nd/4th/8th/16th frame": fidgets, gasps, awareness events, orientation normalize, page-flip checks, cel choice, target search. Also consumes random numbers at a frame-rate-dependent cadence | Derive from game time (interval accumulators) or keep on the fixed tick and accept the new cadence; verify RNG order in traces |
| `Libs/sith/AI/sithAIMove.c:691` | `sithAIMove_CheckWalkMove` | `(thingIdx + frameNumber) & 1` alternates a movement branch | Keep on fixed tick; it becomes deterministic at 60 Hz |
| `Libs/sith/Engine/sithRender.c:1382` | `sithRender_RenderThing` | Writes `renderFrame` during draw | Move "seen this tick" bookkeeping out of draw, or make it tick-owned |
| `Libs/sith/World/sithItem.c:110` | `sithItem_DestroyItem` | Keeps an item alive if `renderFrame + 1 == frameNumber` | Simulation reads rendering; replace with a tick-owned visibility flag |
| `Libs/sith/World/sithActor.c:807` | `sithActor_DestroyCorpse` | Same pattern for corpses | Same |
| `Libs/sith/Cog/sithCogFunctionThing.c:3234` | `IsThingVisible` | COG result from `renderFrame + 1 >= frameNumber` | Define "visible" on the tick, not the draw |
| `Libs/sith/AI/sithAIUtil.c:131,136,224,229` | `UpdateTargetSight`, `UpdateGoalSight` | Caches sight once per `frameNumber` | Keep on tick; extra render frames must not invalidate |
| `Libs/sith/Gameplay/sithPlayerControls.h:147-149`, `sithPlayerControls.c:2152,2164,2301,2305` | Thrust/angular-velocity helpers | Mixes `sithTime_g_fps` (1/dt) with constant terms, then clamps | Replace with a constant tuned for the fixed rate, or scale by dt correctly; this is the largest feel risk |
| `Libs/sith/Devices/sithControl.c:614` | Axis binding | Multiplies axis by `sithTime_g_fps` | Same as above |
| `Libs/sith/World/sithThing.c:1328` | Attached-thing velocity | Uses `sithTime_g_fps` | Review; move to dt-scaled form |
| `Libs/sith/Gameplay/sithPlayerControls.c:3073-3135` | Swim pitch/yaw | `angleDelta = rate * frameTimeFlex` with angle clamps and early returns | Linear in dt but clamped per step; verify at 60 Hz |
| `Libs/sith/Engine/sithCollision.c:986,1616,1630,1795,1938` | Impact speed | `frameTimeFlex` folded into impact damage/speed | Step-dependent; compute from velocity, not delta |
| `Libs/sith/AI/sithAIUtil.c:2526` | Flee point prediction | Extrapolates by one `frameTimeFlex` | Use velocity and a defined look-ahead, not the frame delta |
| `Libs/sith/Engine/sithPhysics.c:590-5180` (excluding the fixed-substep block) | All physics updaters | Integrate with the frame delta; drag, gravity, clamps and per-8th-frame normalize change with the step | The fixed 50 Hz substep exists only for the detached player (`sithPhysics.c:1118-1174`); move every path onto the tick step or keep the variable delta and accept the trace change |
| `Libs/sith/World/sithThing.c:1674-1844` | `sithThing_Update`, `UpdateMove` | Per-frame lifetime (`msecLifeLeft`), path scroll, dispatch to every updater | Runs once per tick; extra draw frames must not call it |
| `Libs/sith/Gameplay/sithVehicleControls.c` (all delta hits) | Jeep/minecar/raft control | Thrust, turn and state timers scaled by delta | Review at the new step; timers already accumulate |
| `Libs/sith/Gameplay/sithModernControls.c` (all delta hits) | Modern movement/lock | Turn and acceleration steps scaled by delta | Review at the new step |
| `Libs/sith/Engine/sithModernCamera.c` (all delta hits) | Camera input | Yaw/pitch step scaled by delta | Keep on tick with the camera |
| `Libs/sith/Gameplay/sithPlayerActions.c:1920-1984` | `UpdateTimedStep` | Distributes a fixed move over remaining time | Step-dependent; review |
| `Libs/sith/World/sithWeapon.c:761` | Projectile spawn | Runs an extra physics step when `secDeltaTime > 0.02f` | A fixed 16.7 ms step disables this branch; verify projectiles |
| `Libs/sith/Engine/sithPuppet.c:336-646`, `Libs/rdroid/Engine/rdPuppet.c:365-410`, `Libs/rdroid/Primitives/rdModel3.c:2023-2068` | Puppet/move tracks, joint damping | Advances animation clocks and smooth-damps joints with dt; keyframe markers fire gameplay callbacks | Keep advancement on the tick; interpolate pose only at draw |
| `Libs/sith/Engine/sithAnimate.c:952-1375` | Quick turn, move-to-pos, fade, sprite size, light | Thing-pose and light animation driven by dt | Decide per slot: pose presentation can interpolate; things that move gameplay objects stay on the tick |
| `Libs/sith/Gameplay/sithFX.c:279-1136` | Fairy dust, paddle ripples, cel choice | Spawns things and timers from game time and frame cycles | Spawns on the tick; visuals interpolate |
| `Libs/sith/Dss/sithDSSThing.c:890-893,1792` | Network fire and frame number | Sends a delta and the frame number to peers | Send the tick number instead, or keep the fixed tick identical on peers |
| `Libs/sith/Dss/sithMulti.c:115-117,319,616-670` | Score sync, welcome accumulator | Wall clock and a delta accumulator | Move score sync to game time; accumulator already step-safe |
| `Libs/sith/Gameplay/sithTime.c:38-50,81` | `sithTime_Advance`, `_Resume` | Publishes `sithTime_g_clockTime` (raw wall clock) that simulation later reads | Enhanced stops exposing wall time to simulation; keep it for pause/resume only |
| `Libs/sith/Engine/sithCollision.c:1426,1428` | Surface `COGLINKED` touch throttle | Reads `sithTime_g_clockTime` (wall) for a gameplay message | Move to `msecGameTime` so the simulation stops reading wall time |
| `Libs/sith/Gameplay/sithPlayerActions.c:113,416` | `CanActivate`, `Activate` throttle | Reads `sithTime_g_clockTime` (wall) | Move to game time |
| `Jones3D/Display/JonesHud.c:1202` | Interaction prompt log | Logs `frameNumber` | Debug only |

## 7. render

Presentation that must run for every rendered frame. "Interpolate" means previous/current state blending at
draw time.

| File:line | Function | Effect | Proposed handling |
|---|---|---|---|
| `Libs/sith/Engine/sithCamera.c:474-1259` | `sithCamera_Update` | Integrates camera pose, focus, offsets, shake and free camera with frame time; currently called in the draw phase | Move the integration to the tick; keep previous/current camera transform and interpolate position/orientation for the draw |
| `Libs/rdroid/Engine/rdPuppet.c:442-655` | `rdPuppet_BuildJointMatrices` | Samples keyframe tracks at `curFrame`, writes joint matrices and `rdFrameNum` | Sample two tick poses (previous/current) and blend, or interpolate the sampled matrices; do not advance tracks here |
| `Libs/rdroid/Primitives/rdModel3.c:1494,1676` | `rdModel3` draw cache | Skips joint rebuild when `rdFrameNum` matches the cache frame | Give every drawn frame its own cache key without touching simulation state |
| `Libs/sith/Engine/sithRender.c:313-442` | `sithRender_RenderScene` | Sector/thing/light collection and draw; calls the AI waypoint renderer that creates laser things | Draw-only pass; move `sithAIUtil_RenderAIWaypoints` creation to the tick or gate it |
| `Libs/sith/Engine/sithRender.c:346-378` | `sithRender_Draw` | Underwater FOV/aspect wobble from `secGameTime` | Use render-time (interpolated) game time; no simulation state |
| `Libs/sith/Engine/sithRender.c:442-1457` | Render caches | `renderTick`, `aVertexRenderTickIds`, surface `renderTick`, vertex lights | Per-draw-frame tick for caches; never advance the simulation tick |
| `Libs/sith/Engine/sithRender.c:1362-1415` | `sithRender_RenderThing` | `renderFrame`, `SITH_TF_SEEN`, AI wake, COG sighted | Take the side effects out of the draw and run them once per tick |
| `Libs/sith/Engine/sithAnimate.c:1428-1487` | Surface scroll, sky slide, page flip | Mutates surface UV offsets, sky offsets and cel numbers | Keep the tick value; interpolate offsets for extra frames instead of advancing twice |
| `Libs/sith/Engine/sithParticle.c:178-239` | `sithParticle_Update` | Particle elapsed/growth/cel state | Tick state; interpolate position/size/cel at draw |
| `Libs/sith/Gameplay/sithFX.c:762-891` | Paddle ripple | Ripple placement from game time | Render-time visual; no simulation state |
| `Libs/sith/Devices/sithSoundMixer.c:231-285` | `sithSoundMixer_Update` | Listener position/orientation from the camera | Run every rendered frame with the interpolated camera |
| `Jones3D/Display/JonesHud.c:1266-5000` | `JonesHud_Process` | HUD timers, menus, reticle and credits animated from wall-clock deltas | Run every rendered frame; already wall-time, so extra frames are safe; do not let HUD state feed the tick |
| `Libs/sith/World/sithVoice.c:824-900` | `sithVoice_Draw` | Subtitle fade from wall clock | Render/wall; no change |
| `Libs/sith/Gameplay/sithOverlayMap.c:301-630` | `sithOverlayMap_Draw` | Map render caches and icon blink | Per-render tick; icon blink already wall time |
| `Libs/sith/World/sithVoice.c:118` (visual use) | Voice head swap | Visual model swap on game time | Interpolate or accept tick cadence |

## 8. wall

Real-time uses outside the simulation. They stay on the wall clock.

| File:line | Function | Effect |
|---|---|---|
| `Libs/std/General/stdPlatform.c:37`; `Libs/std/Null/stdPlatformTimeNull.c:34`; `Libs/std/Babylon/stdPlatformTimeBabylon.cpp:39` | `stdPlatform_GetTimeMsec` | Clock provider; virtual clock in headless |
| `Libs/sith/Gameplay/sithTime.c:68-84` | `sithTime_Pause`, `_Resume` | Pause bookkeeping in wall time |
| `Libs/sith/World/sithVoice.c:599-831` | Subtitle timing | Wall-clock start/end times |
| `Libs/sound/Sound.c:185-3196`; `Libs/sound/Driver.h:62`; `Libs/sound/DriverNull.c:355`; `Libs/sound/Babylon/DriverBabylon.cpp:803-1047` | Sound play position, fades, device reopen | Audio device time (ADR-0027) |
| `Libs/smush/insane/Timer/TimerPolled.c:28-108`; `TimerSys.h:34` | Movie timer | Movie pacing |
| `Jones3D/Display/JonesDisplay.c:490-527` | Loading screen | Wall-clock wait between progress updates |
| `Jones3D/Main/JonesMain.c:1458-1485` | `JonesMain_PrintFramerate` | Wall-clock FPS counter |
| `Jones3D/Display/JonesConsole.c:262,748` | Console expiry and scroll | Wall clock |
| `Jones3D/Display/JonesConsole.c:458` | Console game-time print | Debug output (reads game time) |
| `Jones3D/Platform/Babylon/JonesUiModal.cpp:134-179` | Modal menu loop | Wall-clock UI animation |
| `Libs/sith/World/sithWorld.c:513-1581` | World load timing | Wall-clock load measurement; calls at 1106/1114 are **unclear** (bare `stdPlatform_GetTimeMsec()` with `// ??`, likely timing probes) |
| `Jones3D/Platform/Null/JonesHeadless.c:746-852` | Virtual clock start/advance | Runner, not the simulation |

## 9. Per-file hit inventory

Every grep hit is accounted for here. Production files list the count of matching lines; test and
generated-vector files are grouped at the end. A file may appear in more than one class table above; this
inventory only proves coverage.

| File | Hits | Covered by |
|---|---:|---|
| `Libs/sith/Gameplay/sithTime.c` | 44 | Producer, tick, wall (pause) |
| `Libs/sith/Gameplay/sithTime.h` | 10 | Declarations |
| `Libs/sith/Engine/sithPhysics.c` | 165 | tick-change (fixed substep at 1118 tick) |
| `Libs/sith/Gameplay/sithPlayerControls.c` | 152 | tick-change |
| `Libs/sith/AI/sithAIInstinct.c` | 57 | tick |
| `Libs/sith/AI/sithAIMove.c` | 52 | tick + tick-change |
| `Libs/sith/World/sithWeapon.c` | 48 | tick + tick-change |
| `Libs/sith/Engine/sithAnimate.c` | 36 | tick + tick-change + render |
| `Libs/sith/World/sithExplosion.c` | 27 | tick |
| `Jones3D/Display/JonesHud.c` | 26 | render/wall |
| `Libs/sith/Gameplay/sithVehicleControls.c` | 24 | tick-change |
| `Libs/sith/World/sithActor.c` | 19 | tick-change |
| `Libs/sith/Gameplay/sithModernControls.c` | 19 | tick-change |
| `Libs/sith/Engine/sithPuppet.c` | 19 | tick-change |
| `Libs/sith/World/sithThing.c` | 18 | tick-change |
| `Libs/sith/Gameplay/sithFX.c` | 15 | render + tick-change |
| `Libs/sith/Engine/sithPhysics.h` | 15 | Declarations |
| `Libs/sith/Dss/sithMulti.c` | 15 | tick + tick-change + wall |
| `Libs/sith/AI/sithAIMove.h` | 15 | Declarations |
| `Libs/sith/Engine/sithModernCamera.c` | 13 | tick-change |
| `Libs/sith/AI/sithAIUtil.c` | 13 | tick + tick-change + render |
| `Libs/rdroid/Primitives/rdModel3.c` | 12 | tick-change |
| `Libs/sith/Main/sithMain.c` | 11 | tick |
| `Libs/sith/Gameplay/sithPlayerControls.h` | 9 | Declarations/tick-change |
| `Libs/sith/World/sithWorld.c` | 8 | wall |
| `Libs/sith/Cog/sithCog.c` | 8 | tick |
| `Libs/sith/World/sithVoice.c` | 7 | wall + tick-change |
| `Libs/rdroid/Primitives/rdModel3.h` | 7 | Declarations |
| `Libs/sith/Gameplay/sithEvent.c` | 6 | tick |
| `Libs/sith/Cog/sithCogFunction.c` | 6 | tick |
| `Libs/sith/AI/sithAI.c` | 6 | tick |
| `Libs/sith/Engine/sithCollision.c` | 6 | tick-change |
| `Jones3D/Main/JonesMain.c` | 6 | wall |
| `Libs/sith/Gameplay/sithPlayerActions.c` | 5 | tick-change + wall-in-sim |
| `Libs/sith/Engine/sithCamera.c` | 5 | tick-change (called from draw) |
| `Libs/sound/Babylon/DriverBabylon.cpp` | 4 | wall |
| `Libs/sith/World/sithWeapon.h` | 4 | Declarations |
| `Libs/sith/Main/sithMain.h` | 4 | Declarations/macros |
| `Libs/sith/Engine/sithParticle.c` | 4 | render |
| `Libs/sith/Devices/sithControl.c` | 4 | tick + tick-change |
| `Libs/rdroid/Engine/rdPuppet.c` | 4 | tick-change |
| `Libs/sith/World/sithItem.c` | 3 | tick + tick-change |
| `Libs/sith/Gameplay/sithVehicleControls.h` | 3 | Declarations |
| `Libs/sith/Engine/sithRender.c` | 3 | render + tick-change |
| `Libs/sith/Engine/sithModernCamera.h` | 3 | Declarations |
| `Libs/sith/Dss/sithDSSThing.c` | 3 | tick-change |
| `Libs/sith/Devices/sithComm.c` | 3 | tick |
| `Libs/sith/Cog/sithCogFunctionThing.c` | 3 | tick + tick-change |
| `Jones3D/Display/JonesConsole.c` | 3 | wall + render/debug |
| `Libs/smush/insane/Timer/TimerPolled.c` | 3 | wall |
| `Jones3D/Display/JonesDisplay.c` | 2 | wall |
| `Jones3D/Platform/Babylon/JonesUiModal.cpp` | 2 | wall |
| `Jones3D/Platform/Null/JonesHeadless.c` | 2 | tick (trace) |
| `Jones3D/Platform/Null/JonesPlatformNull.c` | 2 | tick |
| `Jones3D/Play/JonesControl.c` | 2 | tick (delta unused) |
| `Jones3D/Play/jonesInventory.c` | 2 | tick |
| `Libs/std/Null/stdPlatformTimeNull.c` | 2 | wall provider |
| `Libs/std/Babylon/stdPlatformTimeBabylon.cpp` | 2 | wall provider |
| `Libs/sound/DriverNull.c` | 2 | wall |
| `Libs/sith/World/sithSprite.c` | 2 | tick |
| `Libs/sith/World/sithActor.h` | 2 | Declarations |
| `Libs/sith/Gameplay/sithWhip.c` | 2 | tick |
| `Libs/sith/Gameplay/sithOverlayMap.c` | 2 | render |
| `Libs/sith/Gameplay/sithModernControls.h` | 2 | Declarations |
| `Libs/sith/Engine/sithPuppet.h` | 2 | Declarations |
| `Libs/std/General/stdPlatform.c` | 1 | wall provider |
| `Libs/std/General/stdPlatform.h` | 1 | Declaration |
| `Libs/std/Null/stdPlatformNull.h` | 1 | Declaration |
| `Libs/std/Null/stdControlNull.c` | 1 | wall provider |
| `Libs/sound/Sound.c` | 1 | wall |
| `Libs/sound/Driver.h` | 1 | Declaration |
| `Libs/smush/insane/Timer/TimerSys.h` | 1 | wall |
| `Libs/sith/types.h` | 1 | `renderFrame` field |
| `Libs/sith/World/sithThing.h` | 1 | Declaration |
| `Libs/sith/World/sithSprite.h` | 1 | Declaration |
| `Libs/sith/World/sithExplosion.h` | 1 | Declaration |
| `Libs/sith/Gameplay/sithWhip.h` | 1 | Declaration |
| `Libs/sith/Gameplay/sithPlayerActions.h` | 1 | Declaration |
| `Libs/sith/Engine/sithParticle.h` | 1 | Declaration |
| `Libs/sith/Dss/sithMulti.h` | 1 | Declaration |
| `Libs/sith/Dss/sithGamesave.c` | 1 | tick |
| `Libs/sith/Dss/sithDSS.c` | 1 | tick |
| `Libs/sith/Cog/sithCogFunctionAI.c` | 1 | tick |
| `Libs/sith/Devices/sithControl.h` | 1 | Declaration |
| `Libs/rdroid/Engine/rdPuppet.h` | 1 | Declaration |
| `Jones3D/Play/JonesControl.h` | 1 | Declaration |
| **Tests and generated vectors (grouped)** | 140 | |
| `Jones3D/Display/Tests/JonesHudTest.c`, `Jones3D/Main/Tests/JonesMainTest.c` | 4 | Test group |
| `Libs/sith/AI/Tests/sithAIInstinct*`, `sithAIMove*`, `sithAIUtil*` (`.h`, `Addrs.h`, `Test.c`, `tv/README.md`) | 68 | Generated vectors / test fixtures |
| `Libs/sith/Dss/Tests/sithDSS*`, `sithMulti*` (`.h`, `.def`, `Test.c`, `tv/README.md`) | 17 | Generated vectors / test fixtures |
| `Libs/sith/Tests/sithInteractionTest.c`, `sithModernControlsTest.c`, `Vec/sithVecTestAddrs.h`, `Engine/Tests/sithCameraTest.c` | 29 | Test group |
| `Libs/sith/World/Tests/sithThingQuetz*`, `sithThingVecQuetz.h`, `tv/README.md` | 7 | Generated vectors / test fixtures |
| `Libs/sound/Tests/SoundTest.c`, `Libs/std/Babylon/Tests/stdPlatformTimeBabylonTest.cpp`, `Libs/std/General/Tests/stdControlTest.c`, `stdPlatformTest.c` | 15 | Test group |

The per-file counts sum to 1006 production hits; the grouped test files sum to 140, matching the 1146 lines
from the command in section 3.

## 10. Genuinely unclear

| Site | Why unclear |
|---|---|
| `Libs/sith/World/sithWorld.c:1106,1114` | Bare `stdPlatform_GetTimeMsec();` calls with `// ??`; they may be timing probes or leftover scheduling. No value is used |
| `Libs/sith/Dss/sithDSSThing.c:1792` | The frame number is sent to peers; it is not clear whether they use it for prediction, replay or just logging |
| `Libs/sith/AI/sithAIInstinct.c` mixed ms/s fields | Some state stores seconds (`secGameTime`), others `msecNextUpdate`, in the same state machine; unit intent is not documented |
| `Libs/sith/Dss/sithMulti.c:319` | `sithMulti_msecSyncTime` uses the wall clock while the surrounding timeouts use game time; the reason is not recorded |

## 11. What must be interpolated (input for ADR-0039)

- Thing position and orientation (all moving things, including attached and path-moved ones).
- Camera position, orientation, FOV and aspect (including the underwater wobble).
- Puppet/keyframe joint poses and blend weights; keyframe markers stay on the tick.
- Surface texture scroll, sky scroll and page-flip selection.
- Particles: position, size and cel.
- Sound listener position/orientation.
- HUD and subtitle timers may keep using wall time, but they must run on the rendered frame, not only on the tick.
