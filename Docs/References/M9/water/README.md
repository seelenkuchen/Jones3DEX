# M9.17 water ripples

`engine.renderer.water.ripples` off and on, regress save, windowed build on Xvfb (lavapipe), 640 wide.

| Off | On | On, about 1 s later |
| --- | --- | --- |
| ![off](regress-off.png) | ![on 1](regress-on-1.png) | ![on 2](regress-on-2.png) |

Only the thin strip of river below Indy is water in this view, so the difference is small at this size. A view
where water fills the frame is still to be captured. Screen-space reflections are not built: translucent water does
not write depth (see Docs/Jones.cfg.md).
