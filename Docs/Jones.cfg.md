# Jones.cfg Reference

`Jones.cfg` is the JSON configuration file used by OpenJones3D.

The engine creates it automatically on first launch and keeps it auto-saved while the game is running. Missing keys are written out on demand, so the file gradually fills itself with defaults and the values chosen in the in-game dialogs.

## Location

The windowed game writes `Jones.cfg` to a per-user directory, so it runs from a read-only AppImage or a
shared install. The directory is chosen once at startup, in this order:

1. `--user-dir <dir>` on the command line.
2. The executable's directory, when its `GameData` folder holds the complete game data. This is the setup
   wizard's **Next to the game** choice.
3. The platform user directory: `~/.local/share/Jones3DEX/` (or `$XDG_DATA_HOME/Jones3DEX/`) on Linux,
   `%APPDATA%\Jones3DEX\` on Windows, and `~/Library/Application Support/Jones3DEX/` on macOS.

When the user directory has no `Jones.cfg`, the game copies the one in the working directory there.
When it has no `.nds` savegame, it copies the saves from the old save folder next to the data. The old
files are left in place. Screenshots, key sets, the log file, the error file and `datacheck.txt` are
written to the user directory.

Headless runs (`Jones3D-headless`) default to the working directory and accept `--user-dir <dir>` too,
so tests and traces keep their paths unless asked otherwise. A headless run has no dialogs: to load a
save, pass `--load <save.nds>` and set `startMode` to 1 (load game).

## Format and behavior

- The file format is JSON.
- The engine writes a top-level `"version"` key when it creates a new file.
- Many legacy settings are mapped from the old Windows Registry names into JSON keys.
- Most display, control, and gameplay settings can still be changed from the built-in dialogs.
- Some keys are engine-managed and may be rewritten automatically after you change settings in-game.

Defaults below assume the standard build with `JONES3D_QOL_IMPROVEMENTS=ON` unless stated otherwise.

## Example

```json
{
  "version": "1.0.0",
  "graphics": {
    "window": false,
    "width": 1280,
    "height": 720,
    "bpp": 32,
    "refreshRate": 60,
    "mipmapFilter": 2,
    "mipmapAutoGen": true,
    "anisotropicFilter": 8,
    "msaa": {
      "enabled": true,
      "samples": 4
    }
  },
  "engine": {
    "sim": {
      "fixedTick": 50
    },
    "renderer": {
      "hipoly": true,
      "renderScale": 1.0,
      "antiAliasing": 1,
      "bloom": 1,
      "sharpening": 0.3,
      "toneMapping": 1,
      "exposure": 1.0,
      "frameLimit": 0,
      "texturePixelated": false,
      "fog": {
        "enabled": true,
        "density": 1.0
      }
    }
  },
  "sound": {
    "volume": 1.0,
    "swmixer": {
      "falloff": 2
    }
  },
  "gameplay": {
    "defaultRun": false,
    "bloodSplatter": true,
    "quickWhip": true
  }
}
```

## Enum values

### `startMode`

| Value | Meaning |
| --- | --- |
| `0` | Start game |
| `1` | Load game |
| `2` | Developer dialog |
| `3` | Sound settings |
| `4` | Display settings |
| `5` | Main menu (windowed Babylon build) |

In the windowed Babylon build, a missing `startMode` and the value `2` (the developer dialog, which
has no screen any more) both open the main menu. `0`, `1`, `3` and `4` keep their meaning, and a
`--load` path still loads that save directly. The headless runs and the Null column keep the values
above unchanged; a headless run loads the save given with `--load` only when `startMode` is `1`, and
cancels when `--load` is absent.

### `log.mode`

| Value | Meaning |
| --- | --- |
| `0` | Disabled |
| `1` | Console |
| `2` | Log file |

### `log.level`

| Value | Meaning |
| --- | --- |
| `0` | Errors only |
| `1` | Normal |
| `2` | Verbose |

### `graphics.mipmapFilter`

| Value | Meaning |
| --- | --- |
| `0` | None |
| `1` | Bilinear |
| `2` | Trilinear |

### `graphics.anisotropicFilter`

Anisotropic filtering level for world and model textures, used by draws that request it. It never
applies to the HUD, the 2D layer or menus.

| Value | Meaning |
| --- | --- |
| `1` | Off |
| `2` | Up to 2 samples |
| `4` | Up to 4 samples |
| `8` | Up to 8 samples (Enhanced default) |
| `16` | Up to 16 samples |

An old boolean value is still accepted: `true` reads as `8` and `false` as `1`. A hand-edited value
between the steps snaps down to the next one, and the backend clamps to the device's limit.

### `engine.renderer.geometry`

| Value | Meaning |
| --- | --- |
| `0` | None |
| `1` | Vertex |
| `2` | Wireframe |
| `3` | Solid |
| `4` | Full |

### `engine.renderer.lighting`

| Value | Meaning |
| --- | --- |
| `0` | None |
| `1` | Lit |
| `2` | Diffuse |
| `3` | Gouraud |

### `engine.renderer.fov`

The player's Field of view, in horizontal degrees at 4:3, the same unit as the camera's FOV. It applies to the normal first-person and third-person gameplay cameras; cutscene cameras and any FOV a COG or the `fov` console command sets keep their authored value. The range is `60` to `110`. `0` or a missing key means the original default, `90`.

This is presentation only. The renderer keeps drawing at the chosen FOV, but the simulation's visibility marks (`SITH_TF_SEEN`, `SITH_SECTOR_SEEN` and `renderFrame`, which feed `SIGHTED` COG messages, AI wake and visibility queries) come from a separate pass at the default FOV, so changing the option does not change the game.

### `engine.renderer.renderScale`

The internal resolution of the 3D scene as a multiple of the game's video mode, clamped to `0.5`..`2.0` and
to the device's texture limit. Values above 100% supersample the scene; the final tone-map pass resolves it
to the presented size with linear filtering. The video mode, screen coordinates, the 2D/movie layer's image
and save thumbnails stay display-sized, and menus stay at the window's native resolution. Default `1.0`
(100%). Read when the display opens, so a changed value applies on the next display restart.

### `engine.renderer.antiAliasing`

`1` runs SMAA 1x on the tone-mapped 3D scene at the presented resolution; `0` presents the plain tone-mapped
image, byte-identical to before the option existed. Default `1` in Enhanced builds and `0` in Retail and
QOL-off builds. The HUD, 2D layer, menus and prompts are unchanged. Read when the display opens.

### `engine.renderer.sharpening`

One contrast-adaptive sharpening pass (AMD FidelityFX CAS, sharpen-only) on the presented-size 3D image,
after SMAA or the tone map and before the HUD layer composite, clamped to `0`..`1`. The Options row offers
Off, Low, Medium or High = `0`, `0.3`, `0.5`, `0.8`. Default `0.3` in Enhanced builds and `0` in Retail and
QOL-off builds; `0` skips the pass. Read when the display opens.

### `engine.renderer.texturePixelated`

`true` uses nearest minification and magnification for world and model textures, for the original sharp,
blocky look; the mip filter still follows `graphics.mipmapFilter`. `false` uses linear filtering where the
game asks for it. Default `false` in every build. The HUD, 2D layer and menus are unchanged. Read when the
display opens.

### `engine.renderer.bloom`

A built-in stage before the tone map: a half-size soft-knee luminance prefilter (threshold `0.6`, knee
`0.3`), a downsample chain to 1/32 of the scene, additive tent upsamples back to 1/2, and a composite that
adds the result times the strength to the scene. Off, Low, Medium or High = `0`, `1`, `2`, `3`, with
strengths `0`, `0.15`, `0.3`, `0.5`; the default is `1` (Low) in Enhanced builds and `0` in Retail and
QOL-off builds. The scene is display-referred, so the soft knee lets bright values below 1.0 glow. The HUD
layer, 2D layer, menus and prompts come after and are unchanged. `0` skips the stage. Read when the display
opens.

### `engine.renderer.toneMapping`

The curve the final pass applies after multiplying exposure: `0` clamps each channel to `0`..`1`
(byte-identical to the pre-M9.10 defaults), `1` is Khronos PBR Neutral, and `2` is the Narkowicz 2015 ACES
fitted curve. Default `1` in Enhanced builds and `0` in Retail and QOL-off builds. There is no transfer
function, so the output gamma is unchanged. Read when the display opens.

### `engine.renderer.exposure`

Multiplies the scene colour before the tone-map operator, clamped to `0.5`..`2.0`; `1.0` leaves it
unchanged and is the default in every build. Read when the display opens.

### `engine.renderer.brightness`

The calibration gamma the final tone-map pass applies after the operator:
`pow(saturate(x), 1 / brightness)`, clamped to `0.6`..`1.8`; `1.0` is the identity and skips the pow, and is
the default. On the first windowed start, when the key is missing, the brightness calibration screen runs
after the setup wizard and before the main menu; later, Options > Display > "Brightness..." opens it. Read
when the display opens, and the calibration screen also applies it to the open frame owner at once.

### `engine.renderer.colorGrading`

A subtle colour preset applied in the final tone-map pass after the operator and before brightness, from a
32^3 lookup table generated in code at startup and stored as a 1024 x 32 `Rgba8` strip of 32 slices:
Neutral, Warm, Cool, Adventure, Vivid or Muted = `0`..`5`. Neutral (`0`) skips the lookup, so the presented
image is byte-identical to before the option existed, and is the default in every build and both profiles.
The HUD, 2D layer, menus and prompts come after the pass and are unchanged. Read when the display opens.

### `engine.renderer.dof.cutscenes`, `engine.renderer.dof.gameplay` and `engine.renderer.dof.strength`

Depth of field softens the 3D scene away from the camera's focus, as a presentation stage after the post-processing chain and before bloom. Both toggles are on by default in Enhanced builds and off in Retail and QOL-off builds. `engine.renderer.dof.cutscenes` applies it to cutscene cameras, where geometry both nearer and farther than the focus softens; `engine.renderer.dof.gameplay` applies it to the normal gameplay cameras, where only the far background softens, and starts at six times the focus distance and at least 1.5 world units away (the gameplay focus, camera to Indy, is about 0.2 units), and is full after another twenty-four times the focus or at least 6 units, so Indy, the foreground and nearby level geometry stay sharp. The focus is the camera's focus thing, read each frame from the camera without touching the simulation. `strength` is `0.5`..`2.0` (default `1.0`) and scales the gather radius. Both options off, or no focus input, skip the stage and the image is byte-identical to the path without it. The HUD, 2D layer, menus and prompts are composited after and are never softened.

The circle of confusion is computed from the scene depth target with the frame's near and far planes; a half-resolution 20-tap disc gather weighted by it is composited back over the HDR scene. The renderer smooths the focus distance over about 0.2 s of game time. The values are read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.water.ripples`

Water ripples animate the water surfaces that the normal target tags with the water surface value, as a presentation stage after the post-processing chain and before depth of field. It is a toggle, on by default in Enhanced builds and off in Retail and QOL-off builds. On a water-tagged pixel the HDR scene is sampled at a small offset from two scrolling sine layers whose phase follows the frame's game time, so the water pauses when the game does; a faint view-dependent highlight is added. The offset is a constant fraction of the output height, so its pixel size follows the resolution, and a sample that lands on a non-water pixel falls back to the unoffset sample, so geometry next to the water never smears in. Every other pixel is copied unchanged. Off skips the stage, so the image is byte-identical to the path without the option. Screen-space reflections are not built: translucent water does not write depth, so the depth target at a water pixel holds the scene behind it rather than the water plane. The HUD, 2D layer, menus and prompts are composited after and are never touched. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.contactShadows`

Screen-space contact shadows ground objects against nearby geometry, as a presentation stage after the water ripples and before ambient occlusion, so ambient occlusion and every later stage read the shadowed image. It is a toggle, on by default in Enhanced builds and off in Retail and QOL-off builds. Babylon's postfx `ContactShadowEffect` (ADR-0043) draws it. One fixed overhead light, 35 degrees from vertical and 30 degrees in azimuth in the Z-up game world, is converted to the shader camera's view space from the transform `std3D_SetCameraTransform` supplies; without a camera transform the stage is skipped. A short ray is marched through the depth target toward that light, and only ordinary surfaces (the normal target's alpha carries the ordinary surface tag, class 3) darken. Sky-tagged, water-tagged, empty and the `0`..`0.1` first-person overlay pixels are returned unchanged, as are the HUD, 2D layer, menus and prompts. Off, or a failure, records no pass and the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.ssao`

Screen-space ambient occlusion darkens the creases where geometry meets itself, as a presentation stage after the contact shadows and before depth of field. It is `0` Off, `1` Low, `2` Medium or `3` High; Low is the default in Enhanced builds and Off in Retail and QOL-off builds. The classic draws write a constant facing-the-camera normal, so the stage reconstructs view-space positions from the depth target and takes the normal from the neighbouring depths, choosing the smaller-difference neighbour per axis. The depth target holds rdCamera's `sz`: `0.1` at the near plane and `1.0` at the far plane, 1/z-linear in between, because the first-person overlay uses `0`..`0.1`; overlay pixels get no occlusion. `rdCamera`'s fov is the horizontal fov at the 4:3 reference aspect, so the reconstruction derives the vertical tangent from it and the horizontal tangent from the real aspect (Hor+). At half the scene size an 8, 12 or 16 sample normal-oriented hemisphere kernel is gathered with a world-space radius (`0.020`, `0.035`, `0.05`; the game's scale, where Indy is about 0.2 world units tall), a self-occlusion bias and a range check that fades an occluder out by twice the radius so a much nearer surface does not darken the scene behind it, in a fixed 4 x 4 interleaved rotation pattern indexed by the pixel, so there is no time-based noise. A 4 x 4 depth-aware bilateral blur removes the pattern and a scene-sized composite multiplies the HDR scene colour by the occlusion term (intensity `0.65`, `0.8`, `1.0`). Pixels tagged sky or water, and pixels with nothing drawn, are left unchanged, as are the HUD, 2D layer, menus and prompts. `0` skips every pass, so the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.motionBlur`

Camera motion blur smears the 3D scene along the camera's screen-space motion, as a presentation stage after the ambient occlusion and before depth of field. It is `0` Off, `1` Low, `2` Medium or `3` High; Low is the default in Enhanced builds and Off in Retail and QOL-off builds. Each rendered frame `sithRender` passes the camera's world transform read-only to `std3D_SetCameraTransform`, and marks a camera cut when the current camera changed, the cutscene camera switched on or off, or a level or save was just loaded. The pass reconstructs each pixel's view-space position from the depth target exactly as the ambient-occlusion stage does (the same projection tangents and the game's `0.1`..`1.0` depth range) and projects it through the previous frame's camera; the screen-space difference is the velocity. Sky pixels at the far plane take part, so a camera rotation blurs the sky. Pixels with nothing drawn (normal-target alpha `0`) and the first-person overlay (`sz < 0.1`) get zero velocity. The velocity is scaled by a shutter fraction of the frame's motion and its length is clamped to a fraction of the output height. A single full-resolution gather of 8, 12 or 16 taps follows the velocity, with shutter fractions `0.5`, `0.65`, `0.8` and maximum lengths of `2%`, `3%`, `4%` of the height; a tap whose depth is much nearer than the centre's is weighted down so a background pixel does not smear over a nearer edge. The stage is skipped, with the previous transform reset, after a camera cut, when the frame's game-time step is zero (paused or a menu), when the camera moved more than 16 world units or turned more than 45 degrees in one frame, and on the first frame after open. `0` skips every pass, so the image is byte-identical to the path without the option. The previous transform is presentation state only and never reaches the simulation. The HUD, 2D layer, menus and prompts are composited after and are never blurred. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.lightShafts`

Light shafts spread the visible sky into screen-space rays, as a presentation stage after depth of field and before bloom. It is `0` Off, `1` Low, `2` Medium or `3` High, with strengths `0`, `0.25`, `0.5` and `0.8`; Low is the default in Enhanced builds and Off in Retail and QOL-off builds. The stage tags each drawn face in the normal target's alpha (ordinary `1`, sky `2/3`, water `1/3`), masks the bright sky pixels at half the scene size, reduces the mask's weighted position to a 1 x 1 light position on the GPU without a readback, and radial-blurs the mask toward that position with 40 decaying taps. The result is added to the HDR scene before bloom, at 0.15 strength on sky-tagged pixels so the clouds themselves stay crisp and at full strength over the geometry. When no sky is visible the strength fades to zero over about 0.3 s of game time. `0` skips every pass, so the image is byte-identical to the path without the option. The HUD, 2D layer, menus and prompts are composited after and are never touched. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.lensFlare`

Lens flare adds tinted ghosts and a soft halo around the visible sky, as a presentation stage after light shafts and before bloom. It is a toggle, on by default in Enhanced builds and off in Retail and QOL-off builds. The stage reuses the light-shaft stage's GPU-reduced 1 x 1 sky centroid and its smoothed visible-sky strength, so nothing is read back to the CPU; when light shafts are off, only the mask and centroid reduction needed for the flare run, not the shaft blur. The ghosts lie along the line from the light through the screen centre, tinted, and both they and the halo scale by the visible-sky strength. When no sky is visible, or the centroid is off screen, nothing is added. Off, or a failure, skips the pass and the image is byte-identical to the path without the option. The HUD, 2D layer, menus and prompts are composited after and are never touched. Read when the display opens, so a changed value applies on the next display restart like the other Display options.

### `engine.renderer.vignette`

The vignette smoothly darkens the corners of the 3D scene, as a presentation stage on the presented-size image after SMAA and sharpening and before the HUD layer composite. It is a toggle, off in every build. The radial falloff is aspect-corrected, so the corners of any window shape darken equally, and the centre is left unchanged. Off skips its work, so the image is byte-identical to the path without the option; with film grain also off the effect pass is skipped entirely. The HUD, 2D layer, menus and prompts are never darkened. Read when the display opens.

### `engine.renderer.filmGrain`

Film grain adds subtle luminance noise to the 3D scene, as a presentation stage on the presented-size image after SMAA and sharpening and before the HUD layer composite. It is a toggle, off in every build. The noise is a deterministic hash of the pixel position and a frame counter that advances on game-time frames (never wall-clock time or `rand()`), so a paused game holds the grain still and the same frame produces the same noise. The amplitude is small and weaker in the highlights, and the noise has zero mean. Off skips its work, so the image is byte-identical to the path without the option; with the vignette also off the effect pass is skipped entirely. The HUD, 2D layer, menus and prompts are never grained. Read when the display opens.

### `engine.renderer.frameLimit`

Presentation only, with the values V-Sync, 30, 60, 120, 144, 240 or Unlimited = `0`, `30`, `60`, `120`,
`144`, `240` or `-1`; `0` (V-Sync) is the default. A positive value presents immediately with V-Sync off and
waits after each present until the next frame's wall-clock deadline, so the rate is capped without affecting
game time or the simulation. `-1` turns V-Sync off with no cap. The wait is off in headless runs and while a
level loads. The Display row applies at once; any other value falls back to `0`.

### `engine.renderer.perPixelLighting`

Presentation only, with the values Classic (`false`) and Per-pixel (`true`, the default in Enhanced); the Retail
profile and QOL-off builds always use Classic. Per-pixel evaluates the camera's dynamic light list per pixel in the lit
fragment shader with N·L and the original radius ramp (full within `maxRadius`, linear to zero at `minRadius`),
and the CPU leaves exactly those lights out of the lit vertex colour so nothing counts twice. World GOURAUD stops
adding `sithRender_BuildDynamicLights` and keeps its baked light; model GOURAUD and DIFFUSE drop the camera-list
lights but keep the per-draw sector light, which is not in the uploaded list. Each lit face carries a flag in the
otherwise unused specular colour. Sprites, particles, polylines, the sky and the HUD stay classic. It needs
`engine.renderer.litFaces.enabled`; with lit faces off it behaves as Classic. The Options > Display **Lighting** row
applies at once.

### `engine.sim.fixedTick`

The fixed simulation tick: `0` (Off) keeps the measured frame time, and `50` (On) runs the simulation at
50 Hz, matching the game's 20 ms physics step. On is the default in Enhanced builds; Retail and QOL-off
builds ignore the key and always use the measured frame time. For compatibility, any other positive value
(an old `30`, `60` or `120`) maps to `50`, and a negative value keeps the build's default. On, the game runs
`sithUpdate` at the fixed step and draws extra frames by interpolating thing positions/orientations, the
camera, puppet animation and the sound listener, while the simulation-facing draw work moves onto the tick.
Options > Gameplay has an Enhanced-only **Simulation tick** toggle (Off/On at 50 Hz); the value is read at
startup, so it applies when the game restarts. A headless `--fixed-tick <hz>` overrides it for a run, and a
headless run stays off when the option is not given.

### `engine.renderer.culling.culledSectorTraversal.mode`

Controls how the renderer walks neighboring sectors outside the current camera-frustum-visible sector set, in order to collect additional things and lights that can still affect what is on screen.

| Value | Meaning | Notes |
| --- | --- | --- |
| `0` | Legacy DFS-like traversal | Original order-dependent single-path sector walk. Can miss sectors reachable through a different or shorter path. |
| `1` | BFS traversal | Level-by-level multi-path traversal. Gives more reliable thing and light collection, but in practice it should be paired with `engine.renderer.culling.culledSectorTraversal.maxThingCollectDistance = 18.0` (180 m). Lower values can cull sectors too aggressively and make sectors in front disappear, for example in the Lagoon level. |

### `sound.swmixer.falloff`

Controls how software-mixed positional sounds lose volume between their minimum and maximum radius.

| Value | Meaning | Notes |
| --- | --- | --- |
| `0` | Linear | Original falloff curve. Volume drops evenly between the minimum and maximum radius. |
| `1` | Exponential | Steeper than linear. Sounds lose volume faster as distance increases. |
| `2` | Logarithmic | Decibel-style falloff. Sounds carry a bit farther before fading to very low volume near the maximum radius. |

## Core and startup options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `version` | string | `1.0.0` | Written automatically when a new config file is created. |
| `installPath` | string | empty | Installation path. One candidate of the startup data search: `--data <dir>`, then this key, then `<user dir>/GameData`, the executable's folder, `<executable folder>/GameData`, then the working directory. The game no longer sets this key; the setup wizard installs into `GameData` instead. On Linux and macOS a Wine `Z:\` path is accepted and resolves from `/`; other drive letters are not. |
| `sourcePath` | string | empty | Source/CD path used when the game requests original data. |
| `devMode` | bool | `false` | Enables developer mode features. |
| `startMode` | int | `2` | Uses the `startMode` enum above. The windowed Babylon build treats a missing value and `2` as `5` (main menu). |
| `log.mode` | int | `0` | Uses the `log.mode` enum above. |
| `log.level` | int | `1` | Uses the `log.level` enum above. |

## Controls and gameplay options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `controls.configFile` | string | default scheme | Selected input preset name. A new install is written with the default scheme of the active control styles: `Modern keyboard and gamepad` when either style is Modern, `Keyboard and gamepad` when both are Classic. A remembered value always wins. Usually changed by the Controls tab. |
| `controls.mouse` | bool | `true` (Enhanced), `false` (Retail) | Mouse camera. A new Enhanced install is written with `true`; the Retail profile and non-QOL builds keep the original `false`. The Controls tab's **Mouse camera** row changes it. |
| `engine.input.rumble` | bool | `true` | Gamepad rumble. Presentation only, on by default in both profiles; the Controls screen's Rumble row changes it. |
| `engine.input.keyboardControlStyle` | string | `classic` | `modern` or `classic`. QOL builds only: the keyboard and mouse camera extras. Classic keeps the camera following Indy, the pre-option keyboard translator (no A/D lateral movement) and the original mouse turn/walk bindings; Modern adds A/D lateral movement and lets the mouse orbit the free camera. An unknown value falls back to classic. The Controls tab's **Keyboard & mouse style** row changes it. Non-QOL builds always use classic and neither read nor write the key. |
| `engine.input.gamepadControlStyle` | string | `modern` | `modern` or `classic`. QOL builds only: the gamepad camera. Modern orbits a free camera with the right stick; Classic keeps the original follow camera. The left stick movement translator runs in both. An unknown value falls back to modern. The Controls tab's **Gamepad style** row changes it. Non-QOL builds always use classic and neither read nor write the key. |
| `engine.input.controlStyle` | string | unset | The old single style, read once for migration and no longer written. `classic` sets both per-device styles to classic; `modern` or a missing value gives the defaults above (keyboard classic, gamepad modern). Unknown values fall back to the defaults. Startup writes the new keys only on a new install or after Options writes them, so an existing file with only this key is left alone until the player changes the style. |
| `gameplay.defaultRun` | bool | `false` | Makes running the default movement mode. |
| `gameplay.startLevel` | string | empty | Developer/startup override. |
| `gameplay.difficulty` | int | `5` | Difficulty slider value used by the game. |
| `gameplay.showText` | bool | `false` | Shows spoken subtitle text. |
| `gameplay.showHints` | bool | `false` | Enables overlay-map hints. |
| `gameplay.mapRotation` | bool | `false` | Rotates the overlay map with player facing. |
| `gameplay.lastSaveGame` | string | auto | Last save filename used by the save/load flow. Engine-managed. |
| `gameplay.bloodSplatter` | bool | `true` | Enables the hit blood-splatter effect. |
| `gameplay.quickWhip` | bool | `true` | Simulation. Enhanced only: LB (Modern gamepad) takes the whip in hand instantly anywhere it can be used, then either swings or climbs at a whip target or cracks the whip normally, and switches back to the previous weapon or empty hands when the attack ends. Neither the whip nor the previous weapon plays a draw or holster key. The whip's select key (keyboard `1`) quick-whips only at a target and otherwise selects the whip normally. On by default in Enhanced; the Retail profile is locked off. The pending state is not saved, so a save made during a quick whip loads with the whip current. |

A connected gamepad is always on and works alongside the keyboard and mouse. The old
`controls.controller` key is ignored when present and removed from the file on the next save.

## Graphics and display options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `graphics.allDevices` | bool | `false` | Show all display devices, including less-preferred ones. |
| `graphics.window` | bool | `false` | Windowed mode toggle. The windowed build also switches it at runtime with Alt+Enter. |
| `graphics.dualMonitor` | bool | `false` | Dual-monitor window mode toggle. |
| `graphics.buffering` | bool | `false` | Buffered display mode toggle. |
| `graphics.mipmapFilter` | int | `2` | Uses the `graphics.mipmapFilter` enum above. A nonzero value also generates a mip chain for world and model textures whose material brings only its top level. |
| `graphics.display` | string | auto | Selected display adapter name. Usually engine-managed. |
| `graphics.device` | string | auto | Selected 3D device name. Usually engine-managed. |
| `graphics.width` | int | desktop width | Requested render width. A new windowed install is written with the current desktop mode's width, so the first run is fullscreen at the display's resolution; headless and Null runs keep `640`. An existing value always wins. |
| `graphics.height` | int | desktop height | Requested render height. A new windowed install is written with the current desktop mode's height; headless and Null runs keep `480`. An existing value always wins. |
| `graphics.bpp` | int | `32` | Requested color depth. |
| `graphics.refreshRate` | int | `60` | Requested refresh rate. If the display doesn't offer that rate, the closest available rate at the configured size and bit depth is used. |
| `graphics.performanceLevel` | int | `4` | Performance preset used by the display dialog. |
| `graphics.mipmapAutoGen` | bool | `true` | DirectX 9-only setting. The DirectX 9 backend that read it was removed in M6.3; no current backend reads it. |
| `graphics.anisotropicFilter` | int | `8` (Enhanced), `1` (Retail and QOL-off) | Anisotropic filtering level, `1`, `2`, `4`, `8` or `16`, using the enum above (Options > Display, "Anisotropic filtering"). Presentation only; applies to world and model textures, not to the HUD, 2D layer or menus, and is read when the display opens. An old boolean value is accepted as `8`/`1`. |
| `graphics.msaa.enabled` | bool | `true` | DirectX 9-only setting. The DirectX 9 backend that read it was removed in M6.3; no current backend reads it. |
| `graphics.msaa.samples` | int | `16` | DirectX 9-only setting. The DirectX 9 backend that read it was removed in M6.3; no current backend reads it. |

## Renderer, world, and engine options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `engine.renderer.hipoly` | bool | `false` | Loads the high-poly models from `hi3do` (Options > Display, "Hi-poly models"; restart to apply). Off by default: the high-poly Indy model opens gaps at the elbows during the backward step. |
| `engine.renderer.geometry` | int | `4` | Uses the `engine.renderer.geometry` enum above. |
| `engine.renderer.lighting` | int | `3` | Uses the `engine.renderer.lighting` enum above. |
| `engine.renderer.fov` | float | `90` | Player Field of view, horizontal degrees at 4:3, `60` to `110` (Options > Display, "Field of view"). `0` or missing keeps the default. Presentation only; the simulation keeps the default visibility frustum. |
| `engine.sim.fixedTick` | int | `50` (Enhanced), `0` (Retail and QOL-off) | Fixed simulation tick: `0` Off keeps the measured frame time, `50` On runs the simulation at 50 Hz, matching the game's 20 ms physics step. Any other positive value (an old `30`/`60`/`120`) maps to `50`, and a negative value keeps the build's default. Enhanced/QOL builds only: Retail and QOL-off builds ignore the key and always use the measured frame time. On, the game runs `sithUpdate` at the fixed step, draws extra frames by interpolating thing positions/orientations and the camera, and moves the simulation-facing draw work (visibility marks, AI waypoint lasers, polyline updates) onto the tick. Options > Gameplay has an Enhanced-only **Simulation tick** toggle (Off/On at 50 Hz); the value is read at startup, so it applies when the game restarts, not on the next level load. The headless `--fixed-tick <hz>` option overrides it for a run; a headless run without it stays off. |
| `engine.renderer.frameLimit` | int | `0` | Presentation only (Options > Display, "Frame rate limit": V-Sync, 30, 60, 120, 144, 240 or Unlimited = `0`, `30`, `60`, `120`, `144`, `240` or `-1`). `0` presents with V-Sync. A positive value presents immediately (V-Sync off) and waits after each present until the next frame's wall-clock deadline, so the rate is capped without affecting game time or the simulation. `-1` turns V-Sync off with no cap. The wait is off in headless runs and while a level loads, which keeps its fast path. The Display row applies at once; any other value falls back to `0`. |
| `engine.renderer.fog.enabled` | bool | `true` | Global fog toggle. |
| `engine.renderer.fog.density` | float | `1.0` | Global fog density multiplier. |
| `engine.renderer.culling.pvs.enabled` | bool | `true` | Enables PVS sector culling. |
| `engine.renderer.culling.culledSectorTraversal.mode` | int | `1` | Default build uses BFS. Legacy/QOL-off builds use `0`. |
| `engine.renderer.culling.culledSectorTraversal.maxThingCollectDistance` | float | `18.0` | Default build value. Legacy/QOL-off builds use `8.0`. |
| `engine.renderer.culling.culledSectorTraversal.maxLightCollectDistance` | float | `18.0` | Default build value. Legacy/QOL-off builds use `8.0`. |
| `engine.renderer.litFaces.enabled` | bool | `true` | Presentation only, in every build and both profiles. On: world surfaces and models reach the renderer as lit faces, with view-space positions and normals (smoothed across level edges under 45 degrees), which fill the normal target for later per-pixel passes; the colour is the same as off. Off: the classic path, exactly as before (classic draws only). Sky, sprites, particles, polylines, shadows and the HUD are always classic. Read when the display opens. |
| `engine.renderer.perPixelLighting` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Lighting": Classic or Per-pixel); Classic is the only value in the Retail profile and QOL-off builds. Per-pixel evaluates the camera's dynamic light list per pixel in `std3d_lit.frag.hlsl` with N·L and the original `minRadius`/`maxRadius` ramp, and the CPU leaves those lights out of the lit vertex colour (world GOURAUD drops `sithRender_BuildDynamicLights`; model GOURAUD/DIFFUSE drop the camera-list lights but keep the per-draw sector light). Needs `engine.renderer.litFaces.enabled`; with lit faces off it behaves as Classic. Sprites, particles, polylines and the sky stay classic. The Options row applies at once; `JonesDisplay_Open` reads it too. |
| `engine.renderer.renderScale` | float | `1.0` | Presentation only. The internal resolution of the 3D scene as a multiple of the game's video mode, clamped to `0.5`..`2.0` and to the device's texture limit (Options > Display, "Render scale": 50, 67, 75, 100, 125, 150 or 200%). Above 100% supersamples the scene; the final tone-map pass resolves it to the presented size with linear filtering. The video mode, screen coordinates, the 2D/movie layer's image and save thumbnails stay display-sized, and menus stay at the window's native resolution; the in-world HUD is drawn with the scene. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.antiAliasing` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Anti-aliasing": Off or SMAA). `1` runs SMAA 1x on the tone-mapped 3D scene at the presented resolution, smoothing its edges; the HUD, 2D layer, menus and prompts are unchanged. `0` presents the plain tone-mapped image, byte-identical to before the option existed. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.sharpening` | float | `0.3` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Sharpening": Off, Low, Medium or High = `0`, `0.3`, `0.5`, `0.8`; clamped to `0`..`1`). One contrast-adaptive sharpening pass (AMD FidelityFX CAS, sharpen-only) on the presented-size 3D image, after SMAA or the tone map and before the HUD layer composite. It restores detail lost to a render scale below 100% and the softening of SMAA; the HUD, 2D layer, menus and prompts are unchanged. `0` skips the pass, so the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.bloom` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Bloom": Off, Low, Medium or High = `0`, `1`, `2`, `3`, with strengths `0`, `0.15`, `0.3`, `0.5`). A built-in stage before the tone map: a half-size soft-knee luminance prefilter (threshold `0.6`, knee `0.3`), a downsample chain to 1/32 of the scene, additive tent upsamples back to 1/2, and a composite that adds the result times the strength to the scene. The scene is display-referred, so the soft knee lets bright values below 1.0 glow. The HUD layer, 2D layer, menus and prompts come after and are unchanged. `0` skips the stage, so the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.lightShafts` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Light shafts": Off, Low, Medium or High = `0`, `1`, `2`, `3`, with strengths `0`, `0.25`, `0.5`, `0.8`). A built-in stage after depth of field and before bloom: the normal target's alpha tags each face (ordinary `1`, sky `2/3`, water `1/3`), a half-size mask keeps the bright sky pixels, a GPU reduce chain gives the 1 x 1 luminance-weighted light position, and a 40-tap decaying radial blur of the mask is added to the HDR scene. The strength fades over about 0.3 s of game time when no sky is visible. The HUD, 2D layer, menus and prompts come after and are unchanged. `0` skips every pass, so the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.lensFlare` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Lens flare"). A built-in stage after light shafts and before bloom: it reuses the shaft stage's 1 x 1 sky centroid and smoothed strength (the mask and centroid reduction also run when shafts are off, never the shaft blur) and adds tinted ghosts along the light-to-centre line plus a soft halo, scaled by the visible-sky strength. No sky visible or an off-screen centroid adds nothing. The HUD, 2D layer, menus and prompts are untouched. `false` skips the pass, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.vignette` | bool | `false` | Presentation only (Options > Display, "Vignette"). A presented-size stage after SMAA and sharpening and before the HUD layer composite: a smooth, aspect-corrected radial darkening of the corners, with the centre unchanged. The HUD, 2D layer, menus and prompts are untouched. `false` skips its work; with film grain off the effect pass is skipped, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.filmGrain` | bool | `false` | Presentation only (Options > Display, "Film grain"). A presented-size stage after SMAA and sharpening and before the HUD layer composite: deterministic luminance noise from a hash of the pixel and a game-time frame counter, weaker in the highlights and with zero mean. The HUD, 2D layer, menus and prompts are untouched. `false` skips its work; with the vignette off the effect pass is skipped, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.water.ripples` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Water ripples"). A built-in stage after the post-processing chain and before depth of field: on water-tagged pixels the HDR scene is sampled at a small offset from two scrolling sine layers, with a faint view-dependent highlight added; the phase follows the frame's game time, so it pauses with the game. The offset is a constant fraction of the output height; an offset sample that is not water falls back to the unoffset sample. Other pixels are copied unchanged. The HUD, 2D layer, menus and prompts are untouched. `false` skips the stage, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.contactShadows` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Contact shadows"). A built-in stage after the water ripples and before ambient occlusion, so ambient occlusion and every later stage read the shadowed image: Babylon's postfx `ContactShadowEffect` (ADR-0043) marches a short ray through the depth target toward one fixed overhead light (35 degrees from vertical, 30 degrees in azimuth in the Z-up world, converted to the shader camera's view space from the transform `std3D_SetCameraTransform` supplies; without a camera transform the stage is skipped). Only ordinary surfaces receive (the normal target's alpha is the ordinary surface tag, class 3); sky-tagged, water-tagged, empty and the `0`..`0.1` first-person overlay pixels are unchanged, as are the HUD, 2D layer, menus and prompts. `false` skips the pass, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.ssao` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Ambient occlusion": Off, Low, Medium or High = `0`, `1`, `2`, `3`). A built-in stage after the contact shadows and before depth of field: view-space positions are reconstructed from the depth target (rdCamera's `sz` is `0.1` at the near plane and `1.0` at the far plane, 1/z-linear; the `0`..`0.1` overlay range gets no occlusion) with the fov scaled from the 4:3 reference aspect to the real aspect (Hor+), and normals come from the neighbouring depths (the classic draws write a constant normal). An 8, 12 or 16 sample normal-oriented hemisphere kernel is gathered at half the scene size in a fixed 4 x 4 interleaved rotation pattern with a world-space radius of `0.020`, `0.035` or `0.05` (the game's scale, where Indy is about 0.2 world units tall), a bias and a range check that fades to zero by twice the radius, and a depth-aware 4 x 4 blur feeds a scene-sized composite that multiplies the HDR colour by the occlusion term (intensity `0.65`, `0.8`, `1.0`). Sky, water, overlay and empty pixels are left unchanged, as are the HUD, 2D layer, menus and prompts. `0` skips every pass, so the image is byte-identical to the path without the option. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.motionBlur` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Motion blur": Off, Low, Medium or High = `0`, `1`, `2`, `3`, with 8, 12 or 16 taps, shutter fractions `0.5`, `0.65`, `0.8` and maximum lengths `2%`, `3%`, `4%` of the output height). A built-in stage after ambient occlusion and before depth of field: `sithRender` passes the camera's world transform each frame with `std3D_SetCameraTransform`, the pass reconstructs view-space positions from the depth target (the same `0.1`..`1.0` range and projection tangents as ambient occlusion), reprojects them through the previous frame's camera and gathers the HDR scene along the velocity. Sky pixels take part; nothing drawn and the `0`..`0.1` first-person overlay stay still. A camera cut, a zero game-time step, a move over 16 world units or a turn over 45 degrees in one frame resets the history and skips the stage, as does the first frame after open. The HUD, 2D layer, menus and prompts are unchanged. `0` skips every pass, so the image is byte-identical to the path without the option. Read when the display opens. |
| `engine.renderer.dof.cutscenes` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Depth of field (cutscenes)"). On: cutscene cameras soften the scene both nearer and farther than the focus. The HUD, 2D layer, menus and prompts are unchanged. Off skips this mode; with both depth-of-field modes off the stage is skipped and the image is byte-identical. Read when the display opens. |
| `engine.renderer.dof.gameplay` | bool | `true` (Enhanced), `false` (Retail and QOL-off) | Presentation only (Options > Display, "Depth of field (gameplay)"). On: the gameplay cameras soften only the far background, starting at six times the focus distance and at least 1.5 world units away, so Indy, the foreground and nearby geometry stay sharp. Off skips this mode. Read when the display opens. |
| `engine.renderer.dof.strength` | float | `1.0` | Presentation only. The depth-of-field gather radius scale, clamped to `0.5`..`2.0`; `1.0` is the default. There is no Options row; the two toggles above control the stage. Read when the display opens. |
| `engine.renderer.toneMapping` | int | `1` (Enhanced), `0` (Retail and QOL-off) | Presentation only (Options > Display, "Tone mapping": Clamp, Neutral or Filmic = `0`, `1`, `2`). The curve the final pass applies after multiplying exposure: `0` clamps each channel to `0`..`1` (byte-identical to the pre-M9.10 defaults), `1` is Khronos PBR Neutral (highlights compress and desaturate, midtones stay nearly unchanged), `2` is the Narkowicz 2015 ACES fitted curve. There is no transfer function, so the output gamma is unchanged. Read when the display opens. |
| `engine.renderer.exposure` | float | `1.0` | Presentation only (Options > Display, "Exposure": `0.5`..`2.0` in steps of `0.1`; clamped). Multiplies the scene color before the tone-map operator; `1.0` leaves it unchanged. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.colorGrading` | int | `0` | Presentation only, in every build and both profiles (Options > Display, "Colour grading": Neutral, Warm, Cool, Adventure, Vivid or Muted = `0`..`5`). A subtle colour preset applied in the final tone-map pass after the operator and before brightness, from a 32^3 lookup table generated in code at startup and stored as a 1024 x 32 `Rgba8` strip of 32 slices. Neutral (`0`) skips the lookup, so the presented image is byte-identical to before the option existed. The HUD, 2D layer, menus and prompts come after the pass and are unchanged. Read when the display opens, so a changed value applies on the next display restart like the other Display options. |
| `engine.renderer.brightness` | float | `1.0` | Presentation only. The calibration gamma the final tone-map pass applies after the operator: `pow(saturate(x), 1 / brightness)`, clamped to `0.6`..`1.8`. `1.0` is the identity and skips the pow, so the presented image is byte-identical to before the option existed. On the first windowed start, when the key is missing from `Jones.cfg`, the brightness calibration screen runs after the setup wizard and before the main menu; later, Options > Display > "Brightness..." opens it. The screen writes the key on OK and on Back, like the other Options changes. Read when the display opens, so a hand-edited value applies on the next display restart; the calibration screen also applies it to the open frame owner at once. |
| `engine.renderer.texturePixelated` | bool | `false` | Presentation only (Options > Display, "Crisp pixels"). On: world and model textures use nearest minification and magnification, for the original sharp, blocky look; the mip filter is still the one `graphics.mipmapFilter` selects. Off: linear filtering where the game asks for it. The HUD, 2D layer and menus are unchanged. Read when the display opens. |
| `engine.renderer.vfx.weapon.fireFlash.enabled` | bool | `true` | Default build value. Legacy/QOL-off builds use `false`. |
| `engine.renderer.vfx.weapon.fireFlash.ambientThreshold` | float | `0.65` | Ambient-light threshold for projectile fire flash effects. |
| `engine.world.static.materials.extraCapacity` | int | `32` | Extra buffer space for static world materials. |
| `engine.world.cnd.materials.loadExternal` | bool | `true` | If enabled, the engine first tries to load a matching external material from `mat\\<name>` instead of using the material data embedded in the CND. Falls back to the CND copy if no external file is found or loading fails. |
| `engine.world.cnd.keyframes.loadExternal` | bool | `true` | If enabled, the engine first tries to load a matching external keyframe from `3do\\key\\<name>` instead of using the keyframe data embedded in the CND. Falls back to the CND copy if no external file is found or loading fails. |
| `engine.world.normal.things.extraCapacity` | int | `256` | QOL builds only. Extra buffer space for normal world things. |
| `engine.world.static.sprites.extraCapacity` | int | `128` | QOL builds only. Extra buffer space for static world sprites. |

For these CND `*.loadExternal` options, `external` means loose files on disk that override the matching data packed into the world CND. This is mainly useful for modding, testing, or replacing materials and animation keyframes without rebuilding the CND itself.

Player physics always runs at the original 50 Hz step. The obsolete `engine.physics.fixedTimestep` key is ignored if present.

## Audio options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `sound.hw` | bool | `false` | Enables hardware 3D sound when available. |
| `sound.reverse` | bool | `false` | Reverses left/right sound channels. |
| `sound.volume` | float | `1.0` | Master sound volume. |
| `sound.swmixer.falloff` | int | `2` | Default build uses logarithmic falloff. Legacy/QOL-off builds use `0`. |
| `engine.audio.outputDevice` | string | empty | Presentation only, in every build and both profiles. The opaque id of the sound output device, as listed by the device selection. Empty selects the system default. Read with the other sound settings when sound opens; a device that is missing at startup falls back to the default and is used again when it returns. The settings screen writes this key when the choice changes. |

## Reticle options

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `ui.hud.reticle.enabled` | bool | `true` | Master auto-aim reticle toggle. |
| `ui.hud.reticle.radius` | float | `50.0` | Base reticle radius. |
| `ui.hud.reticle.arrowSize` | float | `11.0` | Arrow size. |
| `ui.hud.reticle.arrowWidthScale` | float | `2.5` | Arrow width multiplier. |
| `ui.hud.reticle.lockScale` | float | `2.5` | Start scale for lock-in animation. |
| `ui.hud.reticle.lockAnimationDuration` | float | `0.3` | Lock animation duration in seconds. |
| `ui.hud.reticle.unlockScale` | float | `1.5` | End scale for unlock animation. |
| `ui.hud.reticle.unlockAnimationDuration` | float | `0.2` | Unlock animation duration in seconds. |
| `ui.hud.reticle.rotationSpeed` | float | `45.0` | Degrees per second. |
| `ui.hud.reticle.pulseMagnitude` | float | `0.45` | Pulse animation amount. |
| `ui.hud.reticle.color` | color string | engine default | Hex color in `#RRGGBBAA` format. |
| `ui.hud.reticle.distanceScaling` | bool | unused | Declared in code but currently not read. |
| `ui.hud.reticle.minDistance` | float | `0.1` | Distance where the reticle is largest. |
| `ui.hud.reticle.maxDistance` | float | `2.18` | Distance where the reticle is smallest. |
| `ui.hud.reticle.minDistanceScale` | float | `2.5` | Scale multiplier at minimum distance. |
| `ui.hud.reticle.maxDistanceScale` | float | `0.45` | Scale multiplier at maximum distance. |

## Menu and HUD options

These are mostly advanced UI tuning keys. The engine writes them out automatically when they are first touched.

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `ui.menu.item.scale` | float | `0.6` | Inventory/menu item scale. |
| `ui.menu.item.textPosY` | float | `-13.0` | Default build value. Legacy/QOL-off builds use `70.0`. |
| `ui.menu.item.textSize` | float | `11.0` | Default build value. Legacy/QOL-off builds use `14.0`. |
| `ui.menu.item.font` | string | `mat\\jonesComic Sans MS14.gcf` | Menu item font resource. |
| `ui.menu.item.moveDuration` | int | `250` | Menu item move duration in milliseconds. |
| `ui.menu.column.height` | float | `0.23` | Default build value. Legacy/QOL-off builds use about `0.195`. |
| `ui.menu.column.spacing` | float | `0.045` | Default build value. Legacy/QOL-off builds use about `0.065`. |
| `ui.menu.column.horizontalSpacing` | float | `0.065` | Horizontal spacing between menu columns. |
| `ui.menu.column.expandDuration` | int | `50` | Default build value. Legacy/QOL-off builds use `100`. |
| `ui.menu.column.collapseDuration` | int | `50` | Default build value. Legacy/QOL-off builds use `100`. |
| `ui.menu.openStartSlideDuration` | int | `100` | Menu open pre-slide duration in milliseconds. |
| `ui.menu.openSlideDuration` | int | `100` | Menu open slide duration in milliseconds. |
| `ui.menu.closeStartSlideDuration` | int | `50` | Menu close pre-slide duration in milliseconds. |
| `ui.menu.closeSlideDuration` | int | `50` | Menu close slide duration in milliseconds. |
| `ui.menu.music.enabled` | bool | `true` | Inventory/menu ambient music toggle. |
| `ui.menu.music.music` | string | `mus_gen_maploadloop.wav` | Menu music filename. Exact key name is legacy. |
| `ui.menu.music.file` | float | `0.55` | Menu music volume. Exact key name is legacy. |
| `ui.menu.music.fade` | float | `0.75` | Menu music fade duration in seconds. |
| `engine.ui.hud.systemIcons` | bool | `false` | Presentation only, both profiles. False (the default) hides the HUD menu's Statistics, Save, Load, Settings and Exit items: the pause menu on Escape or gamepad Start replaces them and still offers all of them. True brings the system items back into the HUD menu. |
| `ui.hud.interactionPrompts` | bool | `true` | Presentation only. Draws the bound key icon and **Use**, **Pick up**, **Push**, **Pull**, **Push \| Pull**, **Climb up** or **Climb** at the bottom centre when a press would interact. The icon follows the action the press reads (Activate, Jump or Forward). On by default in Enhanced; the Retail profile is locked off. A missing key is not written until the option changes in Options. |
| `ui.hud.itemChanged.showDuration` | int | `2000` | Item-change popup duration in milliseconds. |
| `ui.hud.itemChanged.slideEaseInRate` | float | `4000.0` | Slide ease-in rate. |
| `ui.hud.itemChanged.slideSpeedRate` | int | `3` | Slide speed multiplier. |
| `ui.hud.indicators.health.size` | float | `60.0` | Health indicator size. |
| `ui.hud.indicators.health.posX` | float | `0.041379310` | Health indicator X position. |
| `ui.hud.indicators.health.posY` | float | `0.942857142` | Health indicator Y position. |
| `ui.hud.indicators.health.fade` | float | `0.65` | Default build value. Legacy/QOL-off builds use `0.2`. |
| `ui.hud.indicators.health.fadeTime` | int | `3000` | Health indicator fade time in milliseconds. |
| `ui.hud.indicators.health.textures.base` | string | `hud_health_base.mat` | Base health indicator material. |
| `ui.hud.indicators.health.textures.overlay` | string | `hud_health_overlay.mat` | Health overlay material. |
| `ui.hud.indicators.health.textures.overlayHit` | string | `hud_health_overlay_hit.mat` | Hit overlay material. |
| `ui.hud.indicators.health.textures.overlayPoisoned` | string | `hud_health_overlay_skull.mat` | Poisoned overlay material. |
| `ui.hud.indicators.health.colors.full` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.health.colors.medium` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.health.colors.low` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.health.colors.critical` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.endurance.size` | float | `32.0` | Default build value. Legacy/QOL-off builds use `60.0`. |
| `ui.hud.indicators.endurance.posX` | float | `0.102` | Default build value. Legacy/QOL-off builds use about `0.9586206896551`. |
| `ui.hud.indicators.endurance.posY` | float | `0.95` | Default build value. Legacy/QOL-off builds use about `0.942857142`. |
| `ui.hud.indicators.endurance.fade` | float | `0.70` | Default build value. Legacy/QOL-off builds use `0.2`. |
| `ui.hud.indicators.endurance.fadeTime` | int | `3000` | Endurance indicator fade time in milliseconds. |
| `ui.hud.indicators.endurance.textures.overlay` | string | `hud_breath_overlay.mat` | Endurance overlay material. |
| `ui.hud.indicators.endurance.colors.oxygen` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.endurance.colors.raft` | color string | engine default | Hex RGB color serialized by the engine. |
| `ui.hud.indicators.endurance.colors.impEnergy` | color string | engine default | Hex RGB color serialized by the engine. |

## Notes

- `Jones.cfg` is the correct filename. The engine opens `Jones.cfg`, not `Jones3D.cfg`.
- Keys under `graphics.display`, `graphics.device`, resolution, refresh rate, and several UI sections are often rewritten by the in-game configuration dialogs.
- DirectX 9-only settings such as MSAA, anisotropic filtering, and automatic mipmap generation were used by the removed DirectX 9 backend. The keys stay in the file; no current backend reads them.
- A few defaults depend on whether the build was compiled with `JONES3D_QOL_IMPROVEMENTS=ON`.
