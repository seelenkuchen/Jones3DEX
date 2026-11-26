# M11.2: character shadows (for review)

Frames from the windowed Babylon build of branch `m11.2` (after `e54a4c2a`) on Xvfb (1920x1080) with Mesa
lavapipe, the Enhanced defaults (contact shadows on in both columns), and only `engine.renderer.shadows` switched.
Recorded 2026-11-26. The raw BMPs contain game data and are not committed; these are downscales and crops.

Windowed runs are not frame-exact: game time follows the wall clock, so the camera and Indy's pose drift slightly
between the Off and On run of a scene. Compare by eye.

The light is the M11.1 overhead light (35 degrees from vertical, azimuth 30). Casters are the things that had the
original blob shadow (Indy, enemies, animals); with shadows on, the blob is not drawn for them. A 2048 x 2048
orthographic shadow map is fitted around the casters each frame and applied to ordinary surfaces with 12-tap PCF
(strength 0.55), after the contact shadows and before ambient occlusion (ADR-0041 amendment, ADR-0043).

| Scene | Off | On |
| --- | --- | --- |
| Regress (present 1200 of `regress.nds`) | ![](regress-off.png) | ![](regress-on.png) |
| Regress, Indy | ![](regress-indy-off.png) | ![](regress-indy-on.png) |
| Volcano start (present 1500 of `start_06_VOLCANO.nds`) | ![](volcano-off.png) | ![](volcano-on.png) |
| Volcano, Indy | ![](volcano-indy-off.png) | ![](volcano-indy-on.png) |
| Canyon start (present 2100 of `start_01_CANYON.nds`) | ![](canyon-off.png) | ![](canyon-on.png) |

What to look at: in the regress crop Indy casts a body-shaped shadow on the ground to his left (away from the
light) and the hat shades his face. In the volcano the floor is so dark that the shadow is hard to see; that is the
scene's lighting, not a missing shadow.

Questions for review: strength (0.55) and softness (PCF radius 1.5 texels); whether the shadow should also fall on
the walls next to Indy (it does, on ordinary surfaces, within the map); and whether to keep contact shadows on as
well (they overlap where the character shadow falls).

Known limits: the map's texel size follows the casters' bounds, so shadow edges can shimmer slightly while Indy
animates (to be quantized); level geometry does not cast yet (M11.3); torches and the lighter do not cast (M11.4).
