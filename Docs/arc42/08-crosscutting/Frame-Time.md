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

## M9.2 spike results

A throwaway branch (`m9.2-interp-spike`, not merged) prototyped the fixed tick and interpolated draw
described in [ADR-0039](../09-decisions/ADR-0039-fixed-tick-interpolated-rendering.md), to decide Phase 5.
The prototype is off unless `J3D_SPIKE_FIXED_TICK_HZ` is set; with it unset the frame loop is the old one.
It answers the five questions from the M9.2 brief. Measurements are from the Debug native build on this
sandbox, game files under `game/native/ref`, save `../SaveGames/regress.nds`.

### What the spike does

- **Tick.** `JonesMain_ProcessGame` (`Jones3D/Main/JonesMain.c`) keeps a wall-time accumulator and calls
  `sithTime_SetFrameTime(1000 / HZ)` then `sithUpdate()` zero or more times per rendered frame. The per-step
  work is `sithUpdate` plus `sithCamera_Update`, which today runs in the draw phase; nothing else in the
  frame loop moved. Menu handling, the quicksave timer, save-state transitions, the HUD and the overlay map
  stay on the rendered frame. `sithTime_Advance` is bypassed while the fixed step is active and
  `sithTime_g_clockTime` follows game time, so the two or three simulation throttles that still read it see
  the same value at the same tick regardless of how many draw frames lie between ticks.
- **Interpolation.** Before every tick the spike snapshots every active thing's `pos`, `orient`, `flags`,
  `renderFrame` and `signature`, plus the camera's `pos`/`orient`/`fov`/aspect; after the tick it snapshots
  the new values. A drawn frame swaps the world and camera to `prev + alpha * (cur - prev)` with
  `alpha = accumulator / step`, draws, then restores the exact post-tick values. Orientation is a row-wise
  blend of `rvec`/`lvec`/`dvec` with `rdMatrix_Normalize34` re-orthonormalising (not slerp); that degenerates
  only for a turn of 180 degrees or more between two ticks.
- **Draw side effects.** The spike removes, rather than restores, the mutations that cannot be undone:
  `sithRender_RenderThing` skips `SITH_TF_SEEN`, `renderFrame`, AI wake and the COG `SIGHTED` message; the AI
  waypoint renderer (which creates laser things) is skipped; `sithFX_UpdatePolyline` (lightning, which consumes
  `SITH_RAND` and can destroy things) is skipped. The final design must move all of these to the tick.
- **Render tick.** `sithMain_g_frameNumber` and the per-thing `renderFrame`/`flags` are restored. The render
  tick is deliberately not restored: it is a monotonic presentation cache namespace, the simulation never
  reads it (`sithAIUtil_GetThingsInView` advances it itself before use), and restoring it makes the next draw
  hit caches keyed by an old value. The `renderTick` caches in the audit are therefore render-only.
- **Headless.** `J3D_SPIKE_DRAW_MS` makes the headless virtual clock advance by the draw step instead of
  `--step`, so draw-only frames occur headless. In spike mode `--frames` and the state-file `frame` line
  count simulation ticks (the `spike: N rendered frames for M ticks` line in the run log gives the rendered
  count), which makes the two runs below directly comparable.

### a. Switch unset: v6 traces identical

```bash
cd /home/kai/development/OpenJones3D
REFV=v6 .claude/tools/trace-check.sh <build/native>
```

Result: `native regress: identical`, `native wander: identical`.

### b. HZ = 30, step 33 ms vs. draw step 8 ms: state identical

Run from `game/native/ref/run/Resource` with `<exe>` = `build/native/Jones3D/Jones3D-headless`:

```bash
J3D_SPIKE_FIXED_TICK_HZ=30 <exe> --load ../SaveGames/regress.nds \
    --frames 1000 --step 33 --state /tmp/spike-33.txt
J3D_SPIKE_FIXED_TICK_HZ=30 J3D_SPIKE_DRAW_MS=8 <exe> --load ../SaveGames/regress.nds \
    --frames 1000 --step 33 --state /tmp/spike-8.txt
cmp /tmp/spike-33.txt /tmp/spike-8.txt
```

Result: identical (`cmp` exit 0). The first run rendered 972 frames for 1000 ticks, the second 3668 frames
for 1000 ticks; both stopped at the same game time and every thing position, sector, health and count matched.
The same comparison with the wander input (`--input .claude/tools/in-wander.txt` from the main repo) also
matched: 972 and 3668 rendered frames for 1000 ticks, identical state. Draw-only frames therefore changed
nothing observable in the simulation.

### c. Still only per tick, and would stutter at high fps

Checked in the code, from the audit list:

- **Puppet/keyframe poses.** `rdPuppet_Advance` advances `curFrame` on the tick
  ([`rdPuppet.c:273`](../../../Libs/rdroid/Engine/rdPuppet.c)); the draw samples that pose in
  `rdPuppet_BuildJointMatrices` ([`rdPuppet.c:442`](../../../Libs/rdroid/Engine/rdPuppet.c)). Joint matrices
  still pop at the tick rate; the final design needs a two-pose blend or sampled-matrix blend.
- **Surface and sky animation.** `sithAnimate_Update`, called from `sithUpdate`, advances surface texture
  offsets, sky/horizon offsets, page-flip cels and light animation. The draw only reads them, so these step
  once per tick. Interpolate offsets/cels at draw time.
- **Particles.** `sithParticle_Update` runs through `sithThing_Update` on the tick; draw samples the current
  position, radius and cel. They will move in tick-sized jumps.
- **Sound listener.** `sithSoundMixer_Update` is inside `sithUpdate` ([`sithMain.c:542`](../../../Libs/sith/Main/sithMain.c)),
  so the listener updates once per tick, not per drawn frame. Fast camera motion spatialises at the tick rate.
- **Camera.** Moving `sithCamera_Update` to the tick and interpolating `pos`/`orient` makes damping smooth,
  but FOV/aspect changes and the focus-fade state (`pThing->alpha`) still start only on a tick. The underwater
  FOV/aspect wobble in `sithRender_Draw` reads `sithTime_g_secGameTime`, so it also steps at the tick.
- **HUD and subtitles.** `JonesHud_Process` already runs per drawn frame on wall time, so its own animation is
  smooth, but the simulation values it displays (health, ammo, interaction prompt) only change on a tick.
  Subtitles are wall-time and per frame.
- **Effects that need a home.** Lightning (`sithFX_UpdatePolyline`) and the AI waypoint laser creation run in
  the draw today and were disabled by the spike. They need a tick-owned update or a render-only RNG.
- **Per-frame counters.** `SITH_ISFRAMECYCLE` sites (fidgets, gasps, awareness, normalise, RNG cadence) now
  run at 30 Hz, and physics that integrated the measured delta now integrates 33 ms. Both are expected
  tick-change effects from section 6, not draw-frame artifacts.

### d. CPU cost of a draw-only frame vs. a full frame

Measured with the headless Null render device (the default column), 640x480, 230 active things in this save.
It runs the engine's CPU side (culling, transform, geometry build) and records draws without rasterising, so
this is the CPU geometry part; present/raster is additional. The GPU (lavapipe) headless path was unstable in
this environment (segfaults at low frame counts, also with the unmodified engine), so present was not measured.

Using the same 1000-tick runs at 100 and 1000 ticks to cancel startup and tick cost:

| run | rendered frames | wall (s) |
|---|---:|---:|
| HZ 30, step 33, 1000 ticks | 972 | 8.25 |
| HZ 30, draw 8, 1000 ticks | 3668 | 20.26 |
| HZ 30, step 33, 100 ticks | 99 | 4.52 |
| HZ 30, draw 8, 100 ticks | 368 | 5.70 |

The difference between the two modes gives a draw-only frame of about **4.46 ms**; a full frame (one tick plus
one draw) is about the same, so the tick is below the noise floor (upper bound roughly 0.2 ms on this idle
save). Startup is about 3.9 s. In other words the drawn frame, not the simulation step, dominates: a 60 Hz
tick rendered at 120 Hz costs roughly 8.9 ms of CPU geometry per tick, before any present, so on this machine
120 Hz is marginal and a higher render rate is not free.

### e. Recommendation for Phase 5

**Go**, but not as an uncapped "draw every displayed frame" change:

1. Keep the fixed tick and interpolated draw. The spike proves draw-only frames can leave the simulation
   byte-identical, and that the two tick rates reach the same state.
2. Add an explicit render-rate cap (or budget) as part of Phase 5. At about 4.4 ms of CPU geometry per draw
   here, 60 Hz is comfortable, 120 Hz is at the edge, and lavapipe present makes it worse. Do not promise
   uncapped high fps before the draw cost is reduced.
3. Before enabling it, move the draw side effects onto the tick (COG `SIGHTED`, AI wake, `renderFrame`
   visibility, AI waypoint creation, polyline RNG). The spike had to disable them, which is not shippable.
4. Implement the remaining interpolation from section 11 (puppet poses, surface/sky scroll, particles, sound
   listener, render-time game time for the underwater wobble). Without them the fixed tick makes those visibly
   coarser at high fps.
5. Retune the `sithTime_g_fps` users in `sithPlayerControls`/`sithControl`, decide the physics step policy, and
   review the `secDeltaTime > 0.02f` projectile branch, as the audit already calls out.
6. Ship it as an Enhanced option with a cvar for the rate, off in the Retail profile; re-record Enhanced traces
   only where the owner approves the tick-change effects.

## M9.20 implementation

M9.20 ships the first slice behind `engine.sim.fixedTick` (int Hz, default `J3D_QOL_VALUE(50, 0)`: on at
50 Hz in Enhanced, off in Retail and QOL-off builds, which ignore it and keep the measured frame time).
Valid values are `0` (Off) and `50`; any other positive value, such as an old 30/60/120 from an earlier test
build, maps to 50, and a negative value keeps the build's default. With `0` the frame loop is the old one and
the v6/Retail traces are identical. With the tick on (50 Hz; the headless dev override also accepts 30, 60 and
120 for comparisons) the following move. The owner chose the default on 2026-11-22, because ADR-0030 makes the
game's physics step 20 ms, so only 50 Hz gives every tick a physics step; at 60 Hz every sixth tick has none and
airborne motion stutters.

**Tick loop.** `JonesMain_ProcessGame` feeds a game-time accumulator from the measured `stdPlatform_GetTimeMsec`
delta, clamped to `sithTime`'s 1..200 ms, and runs zero or more ticks, at most 5 catch-up ticks per drawn
frame (a longer stall drops the backlog). Each tick calls `sithTime_SetFrameTime(1000 / Hz)` then `sithUpdate`,
the camera update (`sithCamera_Update`, which the draw used to own), the tick-owned draw stage below, and the
interpolation snapshots. Menus, pause, quicksave, save/load transitions, the HUD and the overlay map stay on
the drawn frame. Paused drawn frames still run the `sithUpdate` pause branch (sound mixer and control update)
and never tick. `sithTime_Advance` is a no-op while the tick is on and `sithTime_g_clockTime` follows game
time, so the simulation does not read the wall clock.

**Interpolation.** Before and after every tick `JonesFixedTick` snapshots each active thing (`pos`, `orient`,
`flags`, `renderFrame`, `signature`) and the camera (`pos`, `orient`, `fov`, `aspectRatio`). A drawn frame sets
`prev + alpha * (cur - prev)` with `alpha = accumulator / step`, draws, then restores the exact post-tick
values. Orientations are blended row-wise and re-orthonormalised with `rdMatrix_Normalize34` (not slerp).
A thing created between ticks, a changed `signature`, or a move beyond the teleport threshold is drawn at its
current state, so nothing is blended across two different things or across a teleport. The camera FOV/aspect
are applied to its `rdCamera` for the draw.

**Tick-owned draw work.** The mark-only pass runs once per tick instead of once per drawn frame: `SITH_TF_SEEN`,
`SIGHTED` COG messages, AI wake and `renderFrame` come from the default-FOV visibility pass. The AI waypoint
renderer's laser things, `sithFX_UpdatePolyline` (lightning; consumes `SITH_RAND` and can destroy things)
and the draw's out-of-water air-destroyed sprite check also run in that tick stage. The draw suppresses
the marks, the waypoint creation, the polyline update and the sprite destruction on a fixed tick, so a
draw-only frame cannot change the simulation or the RNG. The render cache tick keeps advancing per draw;
it is presentation only. With the option off all of these stay exactly where they are today.

**Headless.** A headless run stays off unless `--fixed-tick <hz>` is given, whatever the cfg default, so the
trace references and existing headless scripts keep the measured-frame loop; `--fixed-tick` overrides the cfg
and `--draw-step <ms>` (default `--step`, except that an input-script run clamps it to the tick step; see
M9.24) makes the virtual clock advance by the draw step, so draw-only frames happen without a display. With a
fixed tick `--frames` and the state file's `frame` line count ticks and the run log ends with
`fixed tick: N rendered frames for M ticks`. From `game/native/ref/run/Resource`, `--step 33 --fixed-tick 50`
against the same run with `--draw-step 8`, `16` or `5` produced identical state files, with and without the
wander input.

## M9.22 implementation

M9.22 interpolates the remaining presentation state on a fixed tick. Everything below is applied in
`JonesFixedTick_BeginDraw` after the thing/camera blend and restored exactly in `JonesFixedTick_EndDraw`;
with the option off these paths do not run and the measured-frame loop is unchanged.

- **Puppet poses.** Before and after each tick every thing's puppet tracks are snapshotted (`curFrame`,
  `blendWeight`, keyframe pointer). A drawn frame samples each track at
  `prevFrame + alpha * (curFrame - prevFrame)` and blends the fade weight. `rdPuppet_SetTrackDrawSample`
  resets the keyframe node cache so `rdPuppet_BuildJointMatrices` finds the right segment for the sampled
  frame. Markers, callbacks, status changes and loop resets stay on the tick. A loop (curFrame behind the
  previous tick), a replaced track, or a recreated thing snaps to the current tick instead of blending.
- **Surface and sky scroll.** Every surface's `texVertOffset`/`extraLight`, the world horizon/ceiling sky
  offsets and every sector's `extraLight` are snapshotted per tick. Scrolling offsets and sky offsets blend
  with a 1024-unit wrap that takes the short path across a boundary; light ramps blend linearly. Page-flip
  cels, cel animation and `matCelNum` changes stay on the tick because a cel cannot be blended.
- **Particles.** Position and orientation come from the thing blend; the particle cloud radius (growth) is
  blended too. A particle created between ticks is drawn at its current size. Vertex cels and the vertex
  count stay on the tick.
- **Sound listener.** `sithSoundMixer_UpdateListener` now runs on each drawn frame after the camera blend and
  calls the new `Sound_SetListener` path, which moves the driver listener and refreshes only the
  listener-relative volume/pan/pitch of active 3D channels. It never runs fades, advances play clocks,
  reads thing positions, stops/restarts/releases channels or changes channel flags or handles. The full
  `Sound_Update` and the sector/ambient switch stay in `sithSoundMixer_Update` on the tick, so sound state
  cannot feed the simulation on a draw-only frame.
- **Underwater wobble.** `sithRender_Draw` reads `sithTime_GetRenderGameTime()`, which is
  `prevSecGameTime + alpha * step` during a fixed-tick draw and `sithTime_g_secGameTime` otherwise.

Unit tests cover puppet sampling at alpha 0/1 and restore, a replaced puppet track, surface and sky offset
blend/restore, the short-path scroll wrap, sector and surface light blends, and particle size blend/restore
including a particle created between ticks. The headless key proof (`--fixed-tick 60 --step 33` against
`--draw-step 8`, `16` and `5`, with and without the wander input) produces identical state files, and the
v6/Retail traces are unchanged. `sithTime_g_fps` retuning and the physics-step policy from section 6 are
still open.

## M9.24 jump/fall jitter findings

The owner still saw jitter in jump and fall animation on a fixed tick. A dev-only headless trace measured it:
`--draw-trace <file>` writes one line per drawn frame after the scene draw, while the interpolated values are
still in place. A line carries the tick count, alpha, the player position and left vector, the camera position,
`lookPos`, and the `sithCamera_g_camSpot`/`camLookSpot` globals, the world positions of Indy's root node (0),
head, left hand and left shoe (`inhead`, `inlhand`, `inlshoe` in `gen_in_shirt.3do`, nodes 8, 12 and 25, found
by name so the other costumes work), the player model's joint frame stamp with the current `rdCache` frame and
skip flag, and every active puppet track's keyframe name, frame and blend weight. The option is
compiled into every build and is inert unless the file is opened; it works with the fixed tick on or off.
Trace, `--input` and analysis scripts are run artifacts, not part of the repository.

The measurement used `regress.nds` and a script that walks, jumps forward three times and finishes with a
controlled fall, at `--fixed-tick 60 --step 33 --draw-step 8`. The analyser took each point's per-draw
displacement, compared it with the local median and flagged a frame when the displacement was more than twice
the median. Before the fix it flagged 104 of 713 drawn frames: 95 at tick boundaries, 6 at puppet track
changes, and 3 camera/joint mixed. Two causes were real presentation snaps:

- **Camera look/focus.** `JonesFixedTick` blended the camera's `pos` and `orient` but not `lookPos`, `focusPos`
  or the camera-spot globals. The scene draw reads `lookPos` for the sky, backface culling and the depth-of-field
  focus distance, so those moved in tick-sized steps. `JonesFixedTickCameraState` now captures all three and
  `BlendCamera`/`RestoreCamera` blend and restore them with the pose.
- **Replaced, looped and removed tracks.** A track slot replaced in place (stand to walk, walk to jump
  anticipation, jump forward to fall, fall to land), a loop reset, or a track appearing or disappearing was
  drawn at the new tick's keyframe, so the pose snapped between two drawn frames. The first fix crossfaded
  the old keyframe into the new track's local node transforms. It removed the take-off foot jump but could
  still sample an old keyframe with a different node mapping and left joints outside the segment between the
  two tick poses; the owner now saw wrong frames. It was replaced by a complete-pose crossfade, described in
  M9.24b below.

The camera/look/focus fix remains. The first track fix flagged 15 of 713 drawn frames, including the
take-off foot jump's disappearance; M9.24b supersedes its track part and leaves only the real landing and
camera accelerations. With the fixed tick off at `--step 8` and the same script scaled to 8 ms frames, the
draw path takes every frame's tick state directly and has no track or look/focus snap. The off path is
byte-identical: `--draw-step 8`, `16` and `5` produce identical state files to `--step 33`, plain and with
the wander input.

## M9.24b complete-pose crossfade and the matching 50 Hz tick

The local-keyframe crossfade could put a joint outside the segment between its previous-tick and
current-tick positions. For example, at the fall-to-land change the left hand was 1.13 cm off the segment,
because the old keyframe's node mapping can differ and because interpolating local bone transforms does not
interpolate world joint positions linearly. It also carried draw-only crossfade state in `rdPuppetTrack`,
which could outlive a tick.

The replacement builds the complete previous- and current-tick skeletons, then linearly blends their world
joint matrices by the draw alpha. The translation is linear, so every drawn joint lies exactly on the tick
segment; the rotation is re-orthonormalised after the row blend. It is used only on a discontinuity, when a
track was replaced, appeared, was removed, or looped (`curFrame` went backwards). The normal same-track frame
interpolation is unchanged. If the old frame is unknown or out of range, the model changed between ticks, the
node count exceeds the puppet cache, or the model skips building joints, the case snaps to the current tick
pose instead. The world-space crossfade touches presentation only: simulation state, callbacks, markers and
loop bookkeeping stay on the tick, and the live puppet is copied for the two builds.

The trace was extended with the previous- and current-tick positions of the three sample joints, so the
analyser can check each drawn sample joint against the segment. "Wrong frame" means a joint farther than a
small tolerance from that segment. With 1 mm tolerance the jump/fall script had 11 joint-draws on 10 frames before
the fix (maximum 11.3 mm) and has 2 joint-draws on 2 frames after it (maximum 1.57 mm). Those two are ordinary
same-track frame interpolation during a fast foot swing, not a track change; with 2 mm tolerance the count is
5 frames before and 0 after. The displacement table below uses the same per-draw median test as M9.24, and a
track change is categorised per tick rather than per draw, so both draws of a transition tick are attributed
to the transition:

| fixed tick | drawn frames | flagged | track change | tick boundary | camera only | mixed | joint only | sample joints >2 mm off |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 60 Hz | 713 | 15 | 8 | 4 | 2 | 1 | 0 | 0 |
| 50 Hz | 890 | 11 | 6 | 3 | 2 | 0 | 0 | 0 |
| off, `--step 8` | 800 | 92 | 0 | 0 | 1 | 85 | 6 | n/a |

The 60 Hz and 50 Hz runs used `--step 33 --draw-step 8` and the tick-scaled jump script; the off run used
`--step 8` and the 8 ms-scaled script. The 85 mixed off-path flags are the physics substep beat, not
animation: player physics integrates in fixed 20 ms (50 Hz) substeps, so an 8 ms or 60 Hz frame sometimes has
no substep and the player stalls for one frame. The fixed-tick interpolation cannot remove that, because the
tick pose itself is the stalled one. At a 50 Hz tick the substep is exactly one tick, the beat disappears,
and the table drops from 92 flagged frames to 11; the remaining flags are track changes and real camera
accelerations. That makes 50 Hz the tick rate that matches the simulation's physics step. Key proof: at both
60 and 50 Hz, `--step 33` against `--draw-step 8`, `16` and `5` produces identical state files, plain and with
the wander input.

One runner detail surfaced while checking the key proof: with `--step` larger than the tick step, one headless
process call runs several ticks, but `--input` was sampled once per call. A jump key held for a few ticks
therefore behaved differently at 33 ms frames than at 8 ms frames. When a fixed-tick run has an input script
and no explicit `--draw-step`, the runner now clamps the implicit draw step to the tick step, so every tick
samples the script at its own frame and the state files match across draw steps. An explicit `--draw-step`
still wins, and runs without an input script keep the old default.

## M9.24c drawn joints rebuilt from the interpolated placement

The owner saw Indy drawn briefly at another place during jumps and upward moves with the fixed tick on.
`rdModel3` keeps a thing's world-space joint matrices and rebuilds them only when
`rdThing.rdFrameNum != rdCache_GetFrameNum()` (`rdModel3.c`). The simulation also builds them at the tick
placement for path movement and force moves (`sithPuppet_UpdatePuppet`, `sithPuppet_FinishForceMove`), and a
path/force-move tick leaves `bSkipBuildingJoints` set. A drawn frame between two ticks then either reused a
stamp the tick had written or, with the skip flag, never rebuilt at all: the mesh stayed at the tick pose while
the camera and every other thing were interpolated.

The dev-only `--draw-trace` hook had hidden this. It built the tick endpoint skeletons for the trace and then
set `pPlayer->renderData.rdFrameNum = -1` to force the scene draw to rebuild, so every trace run drew at the
interpolated placement regardless of what a run without the trace did. The hook now saves the model's joint
matrices and stamp around its own builds and puts both back, so the trace leaves the drawn state exactly as it
was and the analyser compares the real scene draw with the alpha blend of the two tick skeletons.

`JonesFixedTick_BeginDraw` now keeps, for every active model thing, the joint matrices, `rdFrameNum` and
`bSkipBuildingJoints` the tick left. After the pose blend, unless a whole-pose crossfade already built the
interpolated skeleton for this draw (the crossfade reports it explicitly; the stamp alone cannot tell, because
a tick build in the same cache frame matches it too), a normal model thing's stamp is invalidated so the scene
draw rebuilds its joints from the interpolated placement and the sampled pose. A thing with
`bSkipBuildingJoints` set is different: the path/force-move build placed the joints at the origin and then
shifted the root joint onto the thing position, consuming the animation's root motion, so clearing the flag
and rebuilding with the thing position as the placement would add that root offset again and draw Indy
displaced by it. Its skip flag stays set for the draw and the stored world matrices are moved from the tick
placement to the drawn one instead: rotated about the tick root by `R_draw * inverse(R_tick)` and translated
so the root lands on the interpolated position. `JonesFixedTick_EndDraw` puts the matrices, stamp and flag back
unchanged, because the next tick may read them (path movement, attached meshes, the internal camera and weapon
fire all do). Unit tests build a thing's joints at the tick placement in the draw's cache frame, and a
force-move-style variant with a root animation offset, then check the drawn rebuild and the moved skipped pose;
they fail with the old clear-skip handling. The jump/fall harness used for the earlier pose work does not hit
the path/force-move skip path in the recorded save, so it reports no displaced sample joints with or without
this change; the climb/pull-up moves are the case to replay on a real display.
