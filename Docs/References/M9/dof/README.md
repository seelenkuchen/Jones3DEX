# M9.14: depth of field

Frames from the windowed Babylon build of `m9.14-dof` on Xvfb (1920x1080) with Mesa lavapipe, a
1280x720 window, the game data in `game/native/ref/run/Resource` and the `regress.nds` and
`start_01_CANYON.nds` saves. Anti-aliasing, sharpening, bloom and colour grading are off, the tone
map is Neutral at exposure 1.0 and the render scale is 100%, so only the two depth-of-field toggles
change between a pair. Recorded 2026-11-21. Lavapipe renders on the CPU, so these show the picture,
not performance.

The gameplay pair is present 2500 of `regress.nds`: the renderer logged a gameplay focus of
`0.210261`, and the gameplay ramp's absolute floor keeps the near corridor sharp while the far end
softens. The cutscene pair is present 800 of `start_01_CANYON.nds`, during the level-start cutscene
(the first presents are the load); the renderer logged a cutscene focus of `2.03665`. The raw BMPs
contain game data and are not committed; these are 640-wide downscales.

| Mode | Off (`engine.renderer.dof.*` false) | On (default in Enhanced) | Full-frame RMSE |
| --- | --- | --- | --- |
| Cutscene (`dof.cutscenes`) | ![](cutscene-off.png) | ![](cutscene-on.png) | 0.0387 |
| Gameplay (`dof.gameplay`) | ![](gameplay-off.png) | ![](gameplay-on.png) | 0.0081 |

The cutscene blurs both sides of the focus; the gameplay pair only softens the far background, so
the difference is smaller. The HUD, menus and prompts are composited after the stage and are
unchanged in both modes.

The windowed run's log proves the stage runs:

```text
stdRenderBabylon: depth of field on, cutscenes, strength 1, focus 2.03665.
stdRenderBabylon: depth of field on, gameplay, strength 1, focus 0.210261.
```
