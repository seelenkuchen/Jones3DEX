# Rendering And Visibility

This page documents the render-facing architecture: cameras, visible-sector construction, PVS, off-screen thing/light collection, dynamic lighting, render submission, and the renderer backend split. The DirectX 6.1c and DirectX 9 backends were removed in M6.3 ([ADR-0011](../09-decisions/ADR-0011-retire-win32-directx.md)). Since M6.6 the Babylon column's `std3D` draws on the Babylon render device (see [Babylon std3D Backend](#babylon-std3d-backend-m66)), and since M6.7 its `stdDisplay` draws and presents on the same device (see [Babylon stdDisplay Backend](#babylon-stddisplay-backend-m67)), so the 2D layer, HUD and console text are there too; since M6.8 the game also plays its intro movie and cutscenes through it. The Null column still accepts every call and draws nothing.

Primary source files:

- [`Libs/sith/Engine/sithCamera.c`](../../../Libs/sith/Engine/sithCamera.c)
- [`Libs/sith/Engine/sithRender.c`](../../../Libs/sith/Engine/sithRender.c)
- [`Libs/sith/Engine/sithRender.h`](../../../Libs/sith/Engine/sithRender.h)
- [`Libs/rdroid/Main/rdroid.c`](../../../Libs/rdroid/Main/rdroid.c)
- [`Libs/rdroid/Raster/rdCache.c`](../../../Libs/rdroid/Raster/rdCache.c)
- [`Libs/std/General/std3D.h`](../../../Libs/std/General/std3D.h)
- `Libs/std/Null/std3DNull.c` and `Libs/std/Null/stdDisplayNull.c`
- [`Libs/std/Babylon/std3DBabylon.cpp`](../../../Libs/std/Babylon/std3DBabylon.cpp), [`Libs/std/Babylon/stdDisplayBabylon.cpp`](../../../Libs/std/Babylon/stdDisplayBabylon.cpp) and [`Libs/std/Babylon/stdRenderBabylon.h`](../../../Libs/std/Babylon/stdRenderBabylon.h)
- [`Libs/std/Babylon/Shaders/std3d.vert.hlsl`](../../../Libs/std/Babylon/Shaders/std3d.vert.hlsl), `std3d.frag.hlsl`, `std3d_wireframe.frag.hlsl`, `present.*.hlsl`, the `smaa_*.frag.hlsl` passes, the `bloom_*.frag.hlsl` passes and `motion_blur.frag.hlsl`
- [`Jones3D/Platform/Babylon/main.cpp`](../../../Jones3D/Platform/Babylon/main.cpp) and [`mainWindowed.cpp`](../../../Jones3D/Platform/Babylon/mainWindowed.cpp)

## Layer Split

Rendering is divided across three layers:

1. `sith`

   Owns gameplay-facing scene traversal, visibility, dynamic light collection, and final high-level scene submission.

2. `rdroid`

   Owns renderer-facing primitives, cameras, materials, keyframes, meshes, and the raster cache used to batch/draw faces.

3. `std3D` / `stdDisplay`

   Own the actual graphics API backend, device state, texture allocation, projection setup, scene begin/end, and buffer presentation.

The renderer is therefore not a single module. `sith` decides what should be drawn, `rdroid` packages it into renderable primitives, and `std3D` submits it while `stdDisplay` presents it to the selected backend (Null, or the Babylon render device in the Babylon column since M6.6 and M6.7).

## Camera System

[`sithCamera.c`](../../../Libs/sith/Engine/sithCamera.c) owns the built-in camera objects and the high-level camera behaviors.

Startup creates a fixed set of system cameras, including:

- internal / first-person
- external / third-person
- cinematic
- idle
- auxiliary unknown helper camera
- orbital

Each `SithCamera` stores:

- camera type
- primary and secondary focus things
- orientation
- look position and look angles
- offset and interpolation state
- embedded `rdCamera`

The embedded `rdCamera` is what the lower render layer uses for frustum, projection, light list, and canvas state.

Architecturally, `sithCamera` is the bridge between gameplay semantics like "focus this thing" or "use external camera offsets" and the renderer-facing `rdCamera`.

For the detailed camera architecture, including built-in camera numbers, interpolation state machines, external-camera rigging, collision-limited camera placement, and the COG cutscene workflow built around camera number `2`, see [Camera-System.md](Camera-System.md).

## Per-Frame Camera Use

During [`sithDrawScene()`](../../../Libs/sith/Main/sithMain.c#L596):

1. [`sithCamera_Update(curCamera)`](../../../Libs/sith/Engine/sithCamera.c#L471) updates camera pose, interpolation, offsets, and focus logic.
2. [`sithCamera_RenderScene()`](../../../Libs/sith/Engine/sithCamera.c#L1421) makes the `rdCamera` current and calls [`sithRender_RenderScene()`](../../../Libs/sith/Engine/sithRender.c#L299).

That means camera update is part of the draw half of the frame, not the simulation half. The camera reads already-updated gameplay state and converts it into a render view for the current frame.

## Scene Render Entry

[`sithRender_RenderScene()`](../../../Libs/sith/Engine/sithRender.c#L299) performs the render-facing setup:

- update sky state
- push fog updates to `std3D` if needed
- adjust fade when paused
- call [`sithRender_Draw()`](../../../Libs/sith/Engine/sithRender.c#L332)
- flush voice subtitles and console text

The heavy work happens in [`sithRender_Draw()`](../../../Libs/sith/Engine/sithRender.c#L332).

## Main Render Pass Setup

[`sithRender_Draw()`](../../../Libs/sith/Engine/sithRender.c#L332) is the top-level render pipeline driver. At the start of that function the engine:

- sets the active lighting mode
- enables ambient camera light in the render options
- applies the underwater FOV/aspect wobble when needed
- resets all counters for visible sectors, visible thing sectors, alpha adjoins, sprites, and collected lights
- clears the active `rdCamera` light list

It then runs the frame in this order:

1. build the visible sector set
2. expand into off-screen thing/light collection
3. build per-vertex dynamic lighting
4. set the projection state
5. render opaque and sky sectors
6. render things
7. render deferred alpha adjoins

That ordering is deliberate. The visible sector pass establishes the spatial window for the rest of the frame, the off-screen pass expands that window for nearby thing/light influence, and only then does actual geometry submission begin.

### Simulation visibility and the Field of view option

The visibility pass does more than cull pixels: the sectors and things it reaches send `SIGHTED` to
COG-linked things and sectors, wake sleeping AI and stamp `renderFrame`, which `IsThingVisible` and the
destroy guards read. Widening the drawing frustum would therefore change the simulation.

The **Options > Display** Field of view option (`engine.renderer.fov`,
[Jones.cfg](../../Jones.cfg.md)) is presentation only. When the current camera uses the option and its
FOV differs from the default, [`sithRender_Draw()`](../../../Libs/sith/Engine/sithRender.c) runs
[`sithRender_MarkSimulationVisibility()`](../../../Libs/sith/Engine/sithRender.c) first: it points the
camera at `SITHCAMERA_FOVDEFAULT`, walks the visible sectors and the off-screen thing sectors, and
applies only the simulation marks through `sithRender_MarkThingSeen()`. It then advances the render
tick, restores the chosen FOV and draws normally with the marks suppressed. The simulation thus sees
exactly the default-FOV result at any option value, while the picture uses the chosen FOV. Cutscene
cameras and an authored FOV (a COG, a scripted lerp or the `fov` console command) do not take part:
`SITH_TF_SEEN` and the sector marks keep their original coupling to those cameras.

## High-Level Portal System Overview

At a high level, the visibility system is a portal renderer built on the sector/adjoin graph.

The idea is simple:

- the camera starts in one sector
- each adjoin surface is treated like a portal into a neighboring sector
- the renderer only walks through portals that are both topologically reachable and actually visible on screen
- every successful portal clip produces a tighter screen window for the next sector

That gives the engine a strong "only walk what the camera could plausibly see" property without scanning the whole world every frame.

### Mental model

```text
camera
  |
  v
[Sector A]
    |
    | portal/adjoin visible through current frustum
    v
[Sector B]
    |
    | portal/adjoin still visible after tighter clipping
    v
[Sector C]
```

If the portal from `A` to `B` is not visible, the renderer never considers `B` through that path. If `A -> B` is visible but the portal from `B` to `C` is clipped away, the traversal stops at `B`.

### Non-PVS portal path at a glance

Without PVS, the renderer discovers candidate sectors purely from the live portal graph.

High-level flow:

```text
start at camera sector
  -> inspect its visible adjoins
  -> backface-test each portal surface
  -> transform and clip the portal polygon
  -> if visible, build child clip frustum
  -> recurse into adjoining sector
  -> repeat
```

Pseudo code:

```C
BuildVisibleSectorList(sector, frustum):
    if sector already built for this frame:
        tighten sector frustum if needed
        return

    register sector as visible

    for each visible adjoin in sector:
        if portal surface is backfacing:
            continue

        transform portal surface
        clippedPortal = ClipPortalAgainstFrustum(adjoin.surface, frustum)
        if clippedPortal is empty:
            continue

        childFrustum = BuildChildFrustumFromPortal(clippedPortal, frustum)
        BuildVisibleSectorList(adjoin.neighborSector, childFrustum)
```

This path is simple and exact with respect to the current frame, but it can spend time portal-testing sectors that are only topologically reachable and never realistically visible from the source sector.

### PVS-assisted portal path at a glance

With PVS enabled, the engine still uses the portal renderer, but it first loads a precomputed table that says which adjoins are worth considering from the current source sector.

High-level flow:

```text
start at camera sector
  -> decode that sector's PVS record into an adjoin table
  -> seed visible sector set with the camera sector
  -> enqueue only PVS-approved adjoins
  -> process adjoins by PVS bucket/order
  -> still backface-test and clip portals live
  -> recurse/grow only through surviving portals
```

Pseudo code:

```C
BuildVisibleSectorListPVS(cameraSector, rootFrustum):
    adjoinTable = DecodePVS(cameraSector.pvsIdx)
    clear visible-adjoin buckets

    cameraSector.clipFrustum = rootFrustum
    BuildVisibleSectorPVS(cameraSector)

    for bucket = firstUsedBucket to lastUsedBucket:
        for each adjoin in visibleAdjoinBuckets[bucket]:
            frustum = SelectParentOrRootFrustum(adjoin, adjoinTable)

            if portal surface is backfacing:
                continue

            transform portal surface
            clippedPortal = ClipPortalAgainstFrustumPVS(adjoin.surface, frustum)
            if clippedPortal is empty:
                continue

            nextSector = adjoin.neighborSector
            if nextSector already built:
                tighten nextSector frustum if needed
            else:
                nextSector.clipFrustum = BuildOrReuseFrustum(clippedPortal, frustum)
                BuildVisibleSectorPVS(nextSector)
```

So the PVS path is still a portal renderer. The difference is that it begins with an offline-generated filter over the adjoin graph.

### Non-PVS and PVS side by side

```text
Non-PVS path
camera sector
  -> try every visible adjoin
  -> portal clip decides what survives

PVS path
camera sector
  -> decode offline PVS record
  -> try only PVS-approved adjoins
  -> portal clip still decides what survives
```

The important conclusion is:

- non-PVS = fully live portal discovery
- PVS = offline-pruned portal discovery plus the same live portal tests

That is why the two paths feel architecturally similar in code. PVS is not a second renderer; it is a prefilter for the same sector/portal traversal model.

## Visible Sector Collection

### Pass entry and mode selection

The first decision inside [`sithRender_Draw()`](../../../Libs/sith/Engine/sithRender.c#L332) is whether to use:

- [`sithRender_BuildVisibleSectorListPVS()`](../../../Libs/sith/Engine/sithRender.c#L544) when PVS culling is enabled and the world has PVS data
- [`sithRender_BuildVisibleSectorList()`](../../../Libs/sith/Engine/sithRender.c#L421) otherwise

Both paths ultimately populate the same per-frame state:

- `sithRender_aVisibleSectors`
- each sector's `pClipFrustum`
- the initial visible-thing-sector seed list
- directly visible dynamic lights

So the difference is not in the final data model. The difference is in how candidate sectors and portals are discovered.

### Portal-frustum traversal path

[`sithRender_BuildVisibleSectorList()`](../../../Libs/sith/Engine/sithRender.c#L421) is the classic portal/sector path.

For each sector it:

1. ensures the sector itself is registered as visible
2. iterates visible adjoins
3. rejects portals whose surface is backfacing relative to the camera
4. makes sure the portal surface vertices have been transformed for this render tick through [`sithRender_BuildVisibleSurface()`](../../../Libs/sith/Engine/sithRender.c#L482)
5. clips the portal polygon against the current frustum
6. either reuses the parent frustum or builds a tighter child frustum with [`sithRender_BuildClipFrustrum()`](../../../Libs/sith/Engine/sithRender.c#L505)
7. recurses into the adjoining sector

This is why the renderer is strongly topology-driven. The engine does not start from "all sectors in the world." It starts from the camera sector and walks the sector graph through visible portals.

### PVS-assisted portal path

[`sithRender_BuildVisibleSectorListPVS()`](../../../Libs/sith/Engine/sithRender.c#L544) keeps the same portal-clipping idea, but inserts a PVS filter in front of it.

The PVS path:

1. loads the PVS table for the current camera sector
2. builds per-priority visible-adjoin work lists
3. starts from a full-screen frustum
4. clips only those portals that survive the PVS lookup
5. grows each newly accepted sector through [`sithRender_BuildVisibleSectorPVS()`](../../../Libs/sith/Engine/sithRender.c#L660)

That means PVS is a coarse reachability accelerator, not a replacement for the portal-frustum tests. The engine still clips portal polygons and still propagates frusta sector by sector.

#### What "PVS" means here

In this engine, PVS is "Potentially Visible Set" data stored per world and keyed per source sector.

At render time, the current camera sector's `pvsIdx` is used to decode a packed visibility record from the world's `aPVS` blob through [`sithPVS_SetTable()`](../../../Libs/sith/World/sithPVS.c#L176).

The important architectural detail is that the decoded data is an adjoin table, not a ready-made list of visible sectors. PVS therefore accelerates the existing portal traversal rather than replacing it.

#### Packed-table decode

The first step in [`sithRender_BuildVisibleSectorListPVS()`](../../../Libs/sith/Engine/sithRender.c#L544) is:

1. decode the packed PVS record for the current camera sector into `sithRender_aAdjoinTable`
2. clear the temporary visible-adjoin work buckets in `sithRender_aVisibleAdjoins`
3. initialize a full-screen orthographic clip rectangle for the root camera sector
4. seed traversal by calling [`sithRender_BuildVisibleSectorPVS()`](../../../Libs/sith/Engine/sithRender.c#L660) on the starting sector

The decode step is handled by [`sithPVS_SetTable()`](../../../Libs/sith/World/sithPVS.c#L176), which expands the packed byte stream into one byte per world adjoin using a run-length encoding:

- high bit set: copy the following literal bytes into the adjoin table
- high bit clear: write a run of zeros

This gives the renderer a sparse per-adjoin visibility control table without storing a huge uncompressed array for every sector.

#### Per-adjoin meaning and bucketed traversal

Once decoded, each adjoin's table byte is interpreted this way:

- `0`
  Ignore this adjoin for the current PVS traversal.
- non-zero
  Consider this adjoin.

The non-zero byte then splits into:

- low `7` bits: a bucket/order value
- high bit `0x80`: a flag that changes how the working clip frustum is chosen for that adjoin

[`sithRender_BuildVisibleSectorPVS()`](../../../Libs/sith/Engine/sithRender.c#L660) walks the current sector's adjoins and, for each adjoin that is both render-visible and non-zero in the decoded PVS table, inserts it into a linked work list bucket indexed by the low `7` bits.

That is why the PVS traversal uses `sithRender_curPVSIndex` and `sithRender_lastPVSIndex` rather than plain recursion. The renderer is effectively processing adjoins in a bucketed wave order supplied by the PVS data.

#### Root sector and work-list expansion

After the starting sector is registered as visible, the renderer processes the bucketed visible-adjoin lists in increasing order:

1. take the next bucket
2. walk the linked adjoins stored in that bucket
3. clip the portal surface for each adjoin
4. if the portal survives, either tighten or reuse the clip frustum
5. if the neighboring sector has not been seen this frame, register it and enqueue its PVS-approved adjoins

This means PVS is still expanding the same sector/adjoin graph as the classic portal path. It simply prunes most adjoins before the expensive portal-clipping work begins.

#### Why some adjoins restart from the full-screen frustum

When the decoded adjoin-table byte has bit `0x80` set, [`sithRender_BuildVisibleSectorListPVS()`](../../../Libs/sith/Engine/sithRender.c#L544) does not inherit the source sector's clip frustum for that portal. Instead it restarts clipping from the full-screen frustum.

In other words, the current implementation treats bit `0x80` as "do not constrain this portal by the parent sector's tighter screen window."

That is a significant difference from the normal portal recursion path. It means the PVS data can choose between:

- ordinary portal-style propagation from an already-tight parent frustum
- a reset-to-root clip test for specific adjoins

The exact offline rule that generated those flags is not documented in the runtime code, but the runtime behavior is explicit.

#### PVS-specific portal clipping

The PVS path uses [`rdClip_ClipFacePVS()`](../../../Libs/rdroid/Engine/rdClip.c#L395) instead of the more general clip path.

The important difference is in [`rdClip_Face3WPVS()`](../../../Libs/rdroid/Engine/rdClip.c#L403): it intentionally does not clip against the frustum far plane.

That makes sense here because the PVS stage is mainly about portal acceptance and screen-space window tightening. It still needs left/right/top/bottom clipping to refine the portal window, but it does not need to use far-plane rejection as the main coarse filter.

#### Why the portal clip still matters after PVS

PVS only answers a coarse question:

- "from this source sector, which adjoins are even worth considering, and in roughly what processing order?"

It does not answer the finer question:

- "is this portal actually visible through the currently clipped chain of portals on this frame?"

That second question still requires runtime portal clipping.

This is why the PVS path still:

- checks portal-facing direction against the camera
- transforms the portal surface for the current frame
- clips the portal polygon
- builds or tightens per-sector clip frusta

Without that second stage, PVS would over-admit sectors that are only potentially visible in a broad offline sense, not actually visible from the current camera pose and portal chain.

### Sector finalization

Once a sector is accepted by either path, [`sithRender_BuildVisibleSector()`](../../../Libs/sith/Engine/sithRender.c#L687) performs the common bookkeeping.

That function:

- stamps the sector with the current render tick
- appends it to `sithRender_aVisibleSectors`
- sends `SIGHTED` to sector COGs the first time the sector is seen
- copies the current clip frustum into the per-frame sector-frustum array
- collects directly visible thing lights and actor/player headlights
- seeds the visible-thing-sector list with the visible sector itself

This is an important architectural detail: directly visible sectors are automatically valid thing-render sectors too. The off-screen thing traversal only extends that set; it does not replace it.

## Off-Screen Thing And Light Collection

### Why there is a second traversal

The visible-sector pass alone is not enough for the full frame.

A sector can fall outside the immediate camera-frustum-visible set and still matter because:

- a thing in that off-screen sector may still be partially visible on screen, for example the sun sprite in the Mero level
- an emitted light may influence geometry that is on screen
- an actor or player headlight may need to reach geometry through nearby adjoining sectors

So after visible sectors are known, [`sithRender_BuildVisibleThingSectorList()`](../../../Libs/sith/Engine/sithRender.c#L901) runs a second pass that is specifically about nearby influence, not about direct portal visibility.

### Traversal mode dispatch

[`sithRender_BuildVisibleThingSectorList()`](../../../Libs/sith/Engine/sithRender.c#L901) is just a dispatcher. It selects:

- [`sithRender_BuildVisibleThingSectorListDFS()`](../../../Libs/sith/Engine/sithRender.c#L917) for the legacy traversal
- [`sithRender_BuildVisibleThingSectorListBFS()`](../../../Libs/sith/Engine/sithRender.c#L991) for the OpenJones3D breadth-first path

Both modes write into the same structures:

- `sithRender_aVisibleThingSectors`
- `sithRender_totalVisibleThingSectors`
- the camera light list via collected thing lights

### Legacy DFS-like traversal

[`sithRender_BuildVisibleThingSectorListDFS()`](../../../Libs/sith/Engine/sithRender.c#L917) starts from the already visible sectors, walks their visible adjoins, and recursively expands through [`sithRender_CollectVisibleThingSector()`](../../../Libs/sith/Engine/sithRender.c#L949).

The legacy recursion does the following:

1. marks the sector on first encounter
2. collects thing lights if the current distance is inside the light range
3. adds the sector to the visible-thing-sector list if the current distance is inside the thing range
4. recurses into visible adjoining sectors while the accumulated path distance remains under the maximum collection range

The reason this can miss useful sectors is that marking happens on first encounter. If the sector is first reached through a worse path, later shorter paths do not get a chance to re-evaluate it.

### OpenJones3D breadth-first traversal

[`sithRender_BuildVisibleThingSectorListBFS()`](../../../Libs/sith/Engine/sithRender.c#L991) keeps the same goal but changes the traversal discipline.

The BFS path:

1. seeds a queue from the visible sectors' visible adjoins
2. stores each candidate sector together with its accumulated distance
3. processes the queue breadth-first
4. finalizes each sector only when it is popped for the first time
5. enqueues its visible adjoining sectors while staying under the configured maximum range

That gives the traversal a much more stable "nearest first" behavior. It still walks the same sector/adjoin graph and still obeys the same distance concept, but it is much less sensitive to adjoin order and unlucky early paths.

### Collection distances

The off-screen pass uses two separate limits:

- maximum thing-collection distance
- maximum light-collection distance

Legacy defaults:

- thing collection: `8.0`
- light collection: `8.0`

OpenJones3D `QOL` defaults:

- thing collection: `18.0`
- light collection: `18.0`

The broader `QOL` default exists because the BFS path is meant to gather a more complete neighboring-sector set. In larger spaces, smaller values can still under-collect forward sectors even when the traversal order itself is improved.

### Thing-light collection helper

Both traversal modes eventually rely on [`sithRender_CollectThingLights()`](../../../Libs/sith/Engine/sithRender.c#L1085) to extend the camera light list.

That helper collects two kinds of lights:

- thing-emitted dynamic lights from `SITH_TF_EMITLIGHT`
- actor/player headlights

The helper only collects active, non-disabled, non-invisible, non-destroyed emitters. That is important because the off-screen pass is specifically trying to preserve meaningful lighting influence, not just blindly gather every thing in neighboring sectors.

## Dynamic Light Build

After the visible-sector and off-screen collection passes finish, [`sithRender_BuildDynamicLights()`](../../../Libs/sith/Engine/sithRender.c#L1127) computes per-vertex dynamic lighting for the currently visible sectors.

Its work is:

1. for each visible sector, find the subset of camera lights whose radius intersects that sector's bounding sphere
2. for each sector vertex touched this frame, accumulate light contribution from the relevant lights
3. clamp the resulting RGB contribution
4. stamp the vertex with the current render tick so it is not recomputed twice

That is why the world keeps:

- `aVertDynamicLights`
- `aVertDynamicLightsRenderTicks`

The render passes that follow then consume those per-vertex light values instead of recalculating light contribution surface by surface.

## Sector Rendering

[`sithRender_RenderSectors()`](../../../Libs/sith/Engine/sithRender.c#L752) is the first real submission pass after traversal and light build.

### Opaque pass entry

The sector pass:

- starts from `sithRender_aVisibleSectors`
- prepares shared face-view pointers for transformed vertices, texture vertices, and dynamic-light arrays
- applies a global fog flag if the world has fog enabled

This pass is responsible only for world surfaces. Things and alpha adjoins are handled later.

### Surface filtering and alpha-adjoin deferral

For each visible sector and each surface inside that sector, the renderer first rejects:

- surfaces with `RD_GEOMETRY_NONE`
- surfaces whose normal is backfacing relative to the camera

If a surface has an adjoin and a translucent texture flag, the sector pass does not draw it immediately. Instead it appends the surface to `sithRender_aAlphaAdjoins` for the later alpha-adjoin pass.

That means the main sector pass is structurally an opaque-and-sky pass. Transparent adjoin surfaces are intentionally deferred.

### Sky-surface path

If a surface is marked as ceiling sky or horizon sky, the sector pass:

1. clips the face through [`rdClip_QClipFaceW()`](../../../Libs/rdroid/Engine/rdClip.c#L3970)
2. projects the clipped vertices
3. converts the clipped polygon into a sky plane through the sky helpers
4. queues it into the raster cache with [`rdCache_AddProcFace()`](../../../Libs/rdroid/Raster/rdCache.c#L191)

This is not normal world-face rendering. Sky faces are treated as inputs to sky-plane generation rather than as literal geometry submission.

### Opaque world-surface path

For non-sky surfaces, the pass:

1. ensures vertices have been transformed for this render tick
2. allocates a raster-cache entry
3. clips the face through [`rdClip_FaceToPlane()`](../../../Libs/rdroid/Engine/rdClip.c#L3985)
4. clamps the face lighting mode against the global lighting mode
5. combines surface extra light with sector extra light
6. sets material, cel, and fog-related flags
7. queues the face into the raster cache with [`rdCache_AddProcFace()`](../../../Libs/rdroid/Raster/rdCache.c#L191)

After all visible sectors have been processed, the pass flushes the opaque cache through [`rdCache_Flush()`](../../../Libs/rdroid/Raster/rdCache.c#L141).

## Thing Rendering

After world surfaces are submitted, [`sithRender_RenderThings()`](../../../Libs/sith/Engine/sithRender.c#L1189) walks `sithRender_aVisibleThingSectors`.

### Sector-local thing pass

For each visible thing sector, the function computes a sector ambient light by combining:

- `sector->ambientLight`
- `sector->extraLight`

That ambient light becomes the default ambient contribution for things rendered out of that sector.

### Per-thing filtering and frustum tests

For each thing in that sector, the pass first rejects:

- disabled things
- invisible things
- destroyed things
- the primary focus thing in non-external camera modes

It then transforms the thing position into camera space and determines a culling radius based on the render type:

- models use collision size/movesize and model size
- sprites use sprite radius
- particles use particle radius
- polylines use line length

Polylines skip the sphere-frustum test and are always considered renderable. Other thing types are tested against the sector frustum before they proceed.

### Ambient light versus flat lights

If a thing emits a flat light rather than a dynamic-radius light, the thing pass folds that flat light into the sector ambient light before drawing the thing.

That is different from the earlier dynamic light collection path. Dynamic lights are converted into per-vertex lighting. Flat lights are treated more like local ambient contribution for the current thing draw.

### Rendering the thing wrapper

Once a thing passes culling, [`sithRender_RenderThing()`](../../../Libs/sith/Engine/sithRender.c#L1319) handles render-side state around the actual draw call.

It:

- sends `SIGHTED` to a linked thing COG the first time the thing is seen
- wakes sleeping AI by clearing the sleeping mode bit
- stamps the thing with the current render frame
- temporarily copies the thing world position into `orient.dvec`
- delegates the actual draw to [`sithThing_Draw()`](../../../Libs/sith/World/sithThing.c#L393)
- optionally renders a shadow through [`sithShadow_RenderThingShadow()`](../../../Libs/sith/Engine/sithShadow.c#L37)
- accumulates opaque and alpha thing-poly counters
- restores the orientation translation field afterward

So `sithRender_RenderThing()` is a bridge wrapper. It is not the full draw implementation for every thing type.

### Final thing draw dispatch

[`sithThing_Draw()`](../../../Libs/sith/World/sithThing.c#L393) is where the engine switches from general gameplay thing state to the lower render object.

That function performs thing-type-specific preparation such as:

- setting polyline alpha
- setting model alpha color
- temporarily swapping meshes from the thing swap list

It then dispatches by thing type:

- cameras, ghosts, and hints do not draw
- sprites go through [`sithSprite_Draw()`](../../../Libs/sith/World/sithSprite.c#L106)
- polylines update their dynamic state and then draw
- most remaining renderable types go through [`rdThing_Draw()`](../../../Libs/rdroid/Engine/rdThing.c#L198)

At the end of the pass, [`sithRender_RenderThings()`](../../../Libs/sith/Engine/sithRender.c#L1189) flushes the thing geometry through [`rdCache_Flush()`](../../../Libs/rdroid/Raster/rdCache.c#L141).

## Alpha-Adjoin Rendering

Alpha adjoins are rendered last through [`sithRender_RenderAlphaAdjoins()`](../../../Libs/sith/Engine/sithRender.c#L1374).

### Deferred translucent surface queue

This pass consumes the queue that [`sithRender_RenderSectors()`](../../../Libs/sith/Engine/sithRender.c#L752) built earlier in `sithRender_aAlphaAdjoins`.

So alpha adjoins are not discovered independently. They are discovered during opaque sector rendering and then replayed in a separate translucent pass.

### Alpha-adjoin submission path

For each queued surface, the alpha pass:

1. rebuilds sector ambient light from sector ambient plus sector extra light
2. ensures the surface vertices are transformed for the current render tick
3. allocates an alpha raster-cache entry
4. clips the face through [`rdClip_FaceToPlane()`](../../../Libs/rdroid/Engine/rdClip.c#L3985)
5. fills lighting, extra light, material, cel, and fog-related flags
6. submits the face through [`rdCache_AddAlphaProcFace()`](../../../Libs/rdroid/Raster/rdCache.c#L198)

After all queued alpha adjoins are prepared, the pass flushes the alpha cache through [`rdCache_FlushAlpha()`](../../../Libs/rdroid/Raster/rdCache.c#L166).

Architecturally this is a deferred translucent surface pass, not a separate visibility system. The alpha-adjoin renderer depends completely on the earlier sector pass to discover and queue the surfaces it needs to draw.

## rdCache And rdroid

`rdroid` is the lower render layer that packages geometry for the active graphics backend.

Relevant responsibilities:

- `rdCamera` stores camera/frustum/light state
- `rdMaterial`, `rdModel3`, `rdSprite`, `rdParticle`, `rdPolyline`, `rdPuppet` manage renderable asset data
- `rdCache` accumulates processed faces and submits them in batches

The architecture here is important:

- `sithRender` decides *what* to draw
- `rdroid` decides *how* those primitives are represented and flushed
- `std3D` decides *how* the final draw reaches the graphics device

## stdDisplay And std3D

`stdDisplay` manages:

- display devices
- display modes
- primary/back buffer surfaces or swap chains
- presentation

`std3D` manages:

- 3D device enumeration/open/close
- scene begin/end
- texture formats and cache
- projection/fog state
- render-state translation
- low-level draw calls

From the rest of the engine's perspective, `std3D` is the hardware abstraction layer for rendering and `stdDisplay` for the buffers and presentation. In the Babylon column since M6.6 `std3D` draws on the Babylon render device and since M6.7 `stdDisplay` provides the video modes, the back buffer and the present on that same device, so the 2D layer is there.

## DirectX 6.1c Backend

The legacy backend lived under `Libs/std/Win95/DX6/` and was removed in M6.3. It is described here as the reference the Babylon renderer replaces.

It had these architectural characteristics:

- built around older DirectDraw/Direct3D interfaces
- explicit viewport object usage
- fixed-function style pipeline
- legacy texture-format and device enumeration constraints

This path represents the original renderer architecture most closely.

## DirectX 9 Backend

The modern backend lived under `Libs/std/Win95/DX9/` and was removed in M6.3. Its recorded screenshots and render captures are the visual and call-stream reference for the Babylon renderer (M6.1).

Its architectural extensions included:

- Direct3D 9 device/runtime management
- optional vertex and index buffer paths
- shader system support through `stdShaderDX9.c`
- wider texture-format and device-capability handling
- modern fog/shader constant handling

Fullscreen mode enumeration retains the advertised mode descriptions. Device
creation uses a separate current-mode copy, so the surface format returned by
Direct3D cannot overwrite an available mode. The current-mode and VBuffer
metadata describe the actual created surfaces. For example, a host may accept
an RGB565 fullscreen request but create a 32-bit swap chain; the enumerated
16-bit mode remains available for later selections.

The shader system owned:

- compiled shader objects
- shader parameter tables
- vertex declarations
- global shader constants such as viewport and fog data

This was not just a backend port. It was a meaningful extension of the renderer architecture while still preserving the same upper-layer scene traversal and submission model.

## Babylon std3D Backend (M6.6)

The Babylon column's `std3D` backend ([`std3DBabylon.cpp`](../../../Libs/std/Babylon/std3DBabylon.cpp)) implements the same `std3DBackend_*` functions as the removed DirectX 9 backend, ported from `std3DX9.c`, with the capture forwarders of `std3DCapture.c` still in front ([ADR-0024](../09-decisions/ADR-0024-render-capture-by-renamed-backends.md)).

### Device And Frame Owner

The render device is created by the composition root and passed in the `StdBabylonContext` ([ADR-0025](../09-decisions/ADR-0025-babylon-render-interface.md)). The headless `main.cpp` uses a Null device that records no commands by default, or the SDL GPU device without a window with `--render gpu`; `mainWindowed.cpp` uses the SDL GPU device on the game window and falls back to the Null device without a GPU.

[`stdRenderBabylon.cpp`](../../../Libs/std/Babylon/stdRenderBabylon.cpp) owns the frame: the command list being recorded and the scene targets. The color target is `Rgba16F` and also sampled (`Bgra8` where the device lacks `Rgba16F`; see [the M6.9a subsection](#render-passes-normal-target-and-post-processing-chain-m69a)), with a depth target that is `D24S8`, or `D32F` where the device lacks `D24S8`, at the display size times `engine.renderer.renderScale` (M9.4). On a device with a swapchain, submitting the frame ends with the final tone-map pass: a full-screen triangle from `present.vert.hlsl` and `tonemap.frag.hlsl` samples the scene with a nearest, clamped sampler at render scale 1 (the pre-M9.4 present) and a linear, clamped one at any other scale, so the scene resolves to the presented size; with anti-aliasing on, that pass writes an LDR image and the SMAA stage below writes the frame; with sharpening on, the LDR image passes through the CAS stage below before the frame. `copyTexture` cannot name the swapchain, so a pass is the only way the render interface offers to put an offscreen target on the screen. Through M6.6 `std3D_EndScene` submitted the frame; since M6.7 `stdDisplay_Update` does (see [Babylon stdDisplay Backend](#babylon-stddisplay-backend-m67)).

### Render Passes, Normal Target And Post-Processing Chain (M6.9a)

The frame is a sequence of passes over offscreen targets, ready for per-pixel lighting and post-processing ([ADR-0023](../09-decisions/ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)), with the classic look unchanged:

- **Scene passes** (`std3D`) write two color targets at once: the back buffer color and a **normal target**, plus the depth target. The back buffer color is `Rgba16F` on a device that can render and sample it (M9.3), else `Bgra8`; `stdRenderBabylon_GetColorFormat` reports it and every scene pipeline names it. Half floats leave room for values outside 0..1 for the final tone map, while the game still writes display-referred color as before. The normal target holds the view-space normal (x right, y up, z toward the viewer) as `n * 0.5 + 0.5`, in `R10G10B10A2` (10 bits per component in 4 bytes), or `Rgba8` where the device can't render to it. Since M9.16 its alpha is a **surface tag**: `1` ordinary, `2/3` sky (horizon or ceiling sky) and `1/3` water, values that both formats represent exactly (`2/3` = 170/255, `1/3` = 85/255). `sithRender` knows the surface flags, marks the face with `RD_FF_SKY_SURFACE` or `RD_FF_WATER_SURFACE`, and `rdCache` turns those into the read-only `STD3D_RS_SURFACE_SKY`/`_WATER` render state, which the `std3D` fragment shaders write as the tag. Things, sprites, particles and the HUD have neither bit and write ordinary; a surface that is both is sky in the current data. It belongs with the depth: it is cleared to 0 (alpha 0, nothing drawn) whenever the scene clears depth, draws that write Z replace it and draws that don't keep it (blended with `ZERO, ONE`), so it holds the normal of what the depth target holds. A translucent water face is the exception (M9.17): it is drawn without Z write, and its pipeline keeps the normal target's colour but replaces its alpha with the water tag, so the water is marked even though the depth target still holds the scene behind it. Draws of `StdVertexTL` write the constant normal facing the viewer; lit draws (`std3D_DrawRenderListLit`, see [Lit Vertices](#lit-vertices-m69b)) write their interpolated normals. The color output is computed exactly as before. The depth target is also sampled where the device can sample its format; the format itself is the one the scene always used.
- **The post-processing chain** is an ordered list of full-screen passes in `stdRenderBabylon`, empty by default. `stdRenderBabylon_AddPostPass` takes a fragment shader; each pass draws the present pass's full-screen triangle into one of two targets the chain owns, in the scene color format (`Rgba16F` when the scene is, so values outside 0..1 survive to the tone map; M9.10), reading the previous output (the back buffer color for the first pass) in fragment slot 0, the normal target in slot 1 and the depth target in slot 2, as far as its shader declares textures. It runs at every presenting submit (`stdRenderBabylon_SubmitFrame` and `_PresentFrame`), never at `_FlushFrame`.
- **Bloom** (M9.12) is a built-in stage before the tone map (`engine.renderer.bloom`: Off, Low, Medium or High = 0, 1, 2, 3; Low by default in Enhanced and off in Retail). A soft-knee luminance prefilter (threshold 0.6, knee 0.3) at half the scene size keeps the bright parts of the display-referred scene, then four four-tap box downsamples reach 1/4, 1/8, 1/16 and 1/32 of the scene and four additive tent upsamples accumulate back to 1/2. A scene-sized composite adds the result times the strength (0, 0.15, 0.3, 0.5) to the scene in the scene's format, and the tone map reads that composite. The targets are `Rgba16F` where the scene is and are recreated when the scene size or render scale changes, clamped to at least 1 x 1. The stage runs before the tone map, SMAA, CAS and the HUD composite, so the HUD, 2D layer, menus and prompts are never bloomed. Off skips every bloom pass, leaving the image byte-identical to the path without the option. The shaders are written for this project (`bloom_*.frag.hlsl`); the dual-filter/Kawase and soft-knee techniques are public.
- **Depth of field** (M9.14) is a built-in stage after the post chain and before bloom (`engine.renderer.dof.cutscenes` and `engine.renderer.dof.gameplay`, both on by default in Enhanced and off in Retail; `engine.renderer.dof.strength` 0.5..2.0, default 1.0). Each frame [`sithRender_Draw`](../../../Libs/sith/Engine/sithRender.c) tells `std3D_SetFocus` the view-space distance from the camera to its render focus thing and whether the camera is the cinematic cutscene camera (read-only: `sithCamera_GetRenderFocusThing`, the secondary focus for the cinematic camera, and `sithCamera_IsCutsceneCamera`). The renderer smooths that distance over about 0.2 s of game time, as presentation state only. A circle of confusion from the scene depth target, linearised from the game's 0.1..1.0 range with the frame's near and far planes, drives a half-resolution 20-tap disc gather with a radius of 6 half-res texels at strength 1, weighted by each sample's circle of confusion, and a full-resolution composite blends it over the HDR scene by the same weight. Gameplay mode keeps everything nearer than six times the focus distance (at least 1.5 world units) sharp and ramps the blur in over the next twenty-four times that (at least 6 units), so Indy and the foreground never blur; cutscene mode ramps from both sides of the focus, with a dead zone of a quarter of the focus distance (at least 0.1) and a span of 1.2 times the focus distance (at least 1). Both modes off, or no focus input, skip the stage and the image is byte-identical. The HUD layer, 2D layer, menus and prompts come after and are untouched. The shaders are `dof_gather.frag.hlsl` and `dof_composite.frag.hlsl`, written for this project.
- **Water ripples** (M9.17) are a built-in stage after the post-processing chain and before depth of field (`engine.renderer.water.ripples`, on by default in Enhanced and off in Retail). On a pixel whose normal target's alpha carries the water surface tag, the HDR scene is sampled at a small offset from the slope of two scrolling sine layers whose phase follows accumulated game time (`std3D_SetFrameTime`, so it pauses when the game does), and a faint view-dependent highlight is added. The offset is a constant fraction of the output height, so its pixel size follows the resolution; if the offset sample is not water (it crossed a shoreline), the unoffset sample is used, so geometry next to water never smears in. Every other pixel is copied through unchanged. The pass's single scene-sized target is recreated when the scene size or render scale changes. Off, or a failure, skips the pass and the image is byte-identical. Screen-space reflections are not built: translucent water does not write depth, so the depth target at a water pixel holds the scene behind it rather than the water plane, and a view-space ray march there would use the wrong depth. The shader is `water_ripples.frag.hlsl`, written for this project. The HUD, 2D layer, menus and prompts come after and are untouched.
- **Screen-space contact shadows** (M11.1) are a built-in stage after the water ripples and before ambient occlusion (`engine.renderer.contactShadows`, on by default in Enhanced and off in Retail), so every later stage reads the shadowed image. Babylon's `postfx::ContactShadowEffect` (ADR-0041, ADR-0043) marches a short ray through the depth target toward one fixed overhead light. The light is 35 degrees from vertical and 30 degrees in azimuth in the Z-up game world: `Lw = (sin(tilt) cos(az), sin(tilt) sin(az), cos(tilt))`. The adapter converts it to the shader camera's view space with the basis `std3D_SetCameraTransform` supplies (right, up and forward, `Lv = (dot(right, Lw), dot(up, Lw), dot(forward, Lw))`) and skips the stage while no camera transform has arrived. It passes the scene color, the depth target, the normal target, the same near and far planes and Hor+ tangents as ambient occlusion, and rdCamera's `0.1`..`1.0` depth range; the `0`..`0.1` first-person overlay range is therefore no scene surface. Only ordinary surfaces receive (`receiverClasses` `0x8`, class 3 of the normal target's surface tags), so sky-tagged, water-tagged and empty pixels are returned unchanged. Off, or a failure, records no pass and the image is byte-identical; the HUD, 2D layer, menus and prompts come after and are untouched. The effect's shader and defaults live in Babylon; the light constants and the receiver choice are adapter presentation settings.
- **Screen-space ambient occlusion** (M9.17b) is a built-in stage after the contact shadows and before depth of field (`engine.renderer.ssao`: Off, Low, Medium or High = 0, 1, 2, 3; Low by default in Enhanced and off in Retail). The classic draws write a constant facing-the-camera normal, so the pass reconstructs the view-space position from the depth target and takes the normal from the neighbouring depths, choosing the smaller-difference neighbour per axis so a depth edge does not tilt it. The depth target holds rdCamera's `sz`: 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between, because the first-person overlay uses 0..0.1; the stage leaves overlay pixels alone. `std3D_SetProjection` hands `stdRenderBabylon_SetProjection` the horizontal fov at the 4:3 reference aspect and the planes; the reconstruction derives the vertical tangent from that fov and the horizontal tangent from the real aspect, so the field stays Hor+ at 16:9 and wider. At half the scene size it gathers 8, 12 or 16 samples of a normal-oriented hemisphere with a world-space radius (0.020, 0.035, 0.05, on the game's scale where Indy is about 0.2 world units tall), a self-occlusion bias and a range check that fades an occluder out by twice the radius, so a much nearer surface (another character against the scene behind him) does not darken it; the samples use a fixed 4 x 4 interleaved rotation pattern indexed by the pixel, so the result is deterministic and carries no time-based noise. A 4 x 4 depth-aware bilateral blur removes the rotation pattern, and a scene-sized composite multiplies the HDR scene colour by the occlusion term (intensity 0.65, 0.8, 1.0). Pixels tagged sky or water, overlay pixels and pixels with nothing drawn are returned unchanged, so the stage never darkens the sky or translucent water. Off, or a failure, skips every pass and the image is byte-identical; the HUD, 2D layer, menus and prompts come after and are untouched. The shaders (`ssao`, `ssao_blur` and `ssao_composite`, all `.frag.hlsl`) are written for this project; the depth-reconstruction and hemisphere formulation is the standard screen-space ambient-occlusion technique.
- **Camera motion blur** (M9.19) is a built-in stage after the ambient-occlusion stage and before depth of field (`engine.renderer.motionBlur`: Off, Low, Medium or High = 0, 1, 2, 3; Low by default in Enhanced and off in Retail). Each frame [`sithRender_Draw`](../../../Libs/sith/Engine/sithRender.c) passes the camera's world transform read-only through the new `std3D_SetCameraTransform(aMatrix, bCut)`; `aMatrix` is the camera's `rdCamera` model matrix as 12 floats (right, forward and up basis vectors, then the world position), which the adapter permutes into the shader's x-right, y-up, z-forward view space. `sithRender` marks a cut when the current camera changed, the cutscene camera switched on or off, or a level or save was just loaded. The pass reconstructs each pixel's view-space position from the depth target exactly as the ambient-occlusion pass does (the same projection tangents and the game's 0.1..1.0 depth range), takes it to the world with the current camera and into the previous frame's camera, projects it and takes the screen-space difference. Sky pixels at the far plane take part, so a camera rotation blurs the sky; pixels with nothing drawn (normal-target alpha 0) and the first-person overlay (sz < 0.1) do not move. The velocity is scaled by a shutter fraction of the frame's motion and clamped to a fraction of the output height, and a single full-resolution gather of 8, 12 or 16 taps follows it, downweighting taps much nearer than the centre so a background pixel does not smear over a nearer edge. A cut, a zero game-time step, a move over 16 world units or a turn over 45 degrees in one frame resets the previous transform and skips the stage, and so does the first frame after open. Off, or a failure, skips the pass and the image is byte-identical; the HUD, 2D layer, menus and prompts come after and are untouched. The previous transform is presentation state only and never reaches the simulation. The shader is `motion_blur.frag.hlsl`, written for this project; the depth reprojection and directional gather are the standard screen-space motion-blur formulation.
- **Light shafts** (M9.16) are a built-in screen-space stage after depth of field and before bloom (`engine.renderer.lightShafts`: Off, Low, Medium or High = 0, 1, 2, 3, strength 0, 0.25, 0.5, 0.8; Low by default in Enhanced and off in Retail). It uses the normal target's surface tag: a half-size pass keeps the sky pixels whose scene luminance passes a soft threshold (0.4 with a 0.2 knee), writing `(w, w * u, w * v)`. A chain of four-tap averaging downsample passes reduces that to 1 x 1, so the luminance-weighted centroid is available on the GPU with no readback; the mean weight drives a strength that is smoothed over about 0.3 s of game time (the renderer receives each frame's game time through `std3D_SetFrameTime`), and fades to zero when the sky leaves the view or is too small (0.02% to 0.2% coverage). A 40-tap radial blur of the mask toward the centroid, with 0.96 decay per tap and a 0.95 ray length, is added to the scene-sized HDR target that bloom then reads, at 0.15 strength where the normal target's alpha still carries the sky tag so the clouds are not smeared, and at full strength over the geometry. Off, or a failure, skips every pass and the image is byte-identical; the HUD, 2D layer, menus and prompts come after and are untouched. The shaders (`lightshaft_mask`, `_reduce`, `_strength`, `_blur`, `_composite`, all `.frag.hlsl`) are written for this project; the radial-blur technique is the standard screen-space light-shaft formulation.
- **Lens flare** (M9.18) is a built-in stage after light shafts and before bloom (`engine.renderer.lensFlare`, on by default in Enhanced and off in Retail). It reuses the light-shaft stage's GPU-reduced 1 x 1 sky centroid and smoothed strength, so no readback is added; when light shafts are off, only the mask and centroid reduction needed for the flare run, never the shaft blur. A scene-sized HDR pass adds tinted ghosts along the line from the light through the screen centre plus a soft halo, scaled by the smoothed visible-sky strength. When no sky is visible or the centroid is off screen the shader adds nothing. Off, or a failure, skips the pass and the image is byte-identical; the HUD, 2D layer, menus and prompts come after and are untouched. The shader is `lens_flare.frag.hlsl`, written for this project.
- **The final tone-map pass** (M9.3) is built in after the chain and writes the presented frame, the swapchain or the target of `stdRenderBabylon_RenderPresentToTexture`. It draws the same full-screen triangle with `tonemap.frag.hlsl` over `present.vert.hlsl`, sampling `stdRenderBabylon_GetPresentSource` (the chain's last output, or the back buffer when the chain is empty). It multiplies `exposure` (default 1.0) in first, then applies the operator (`engine.renderer.toneMapping`): 0 clamps each channel to 0..1, 1 is [Khronos PBR Neutral](https://github.com/KhronosGroup/ToneMapping) (Apache-2.0; highlight compression with desaturation, omitting its constant shadow offset so display-referred midtones pass through), and 2 is the [Narkowicz 2015 ACES fitted curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/). After the operator it applies the colour-grading preset (`engine.renderer.colorGrading`: Neutral, Warm, Cool, Adventure, Vivid or Muted = 0..5, Neutral in every build): a 32^3 lookup table generated in code at startup and stored as a 1024 x 32 `Rgba8` strip of 32 slices, sampled in fragment texture slot 1 with manual interpolation between the blue slices; Neutral skips the lookup, so the image is byte-identical. Then it applies the brightness calibration gamma, `pow(saturate(x), 1 / engine.renderer.brightness)` (0.6..1.8, default 1.0); 1.0 skips the pow, so the default is byte-identical. There is no transfer function, exactly as the plain present pass did: the scene writes display-referred color today, so the output gamma is unchanged. `stdRenderBabylon_SetToneMap` sets the parameters; opening reads `engine.renderer.toneMapping` (Neutral by default in Enhanced, clamp in Retail/QOL-off), `engine.renderer.exposure` (0.5..2.0, default 1.0), `engine.renderer.brightness` (0.6..1.8, default 1.0) and `engine.renderer.colorGrading` (0..5, default 0 Neutral in every build). With exposure 1.0, operator 0 and brightness 1.0 it is an identity clamp, so an image test compares the presented HDR path with the pre-M9.3 `Bgra8` path at no more than 1/255 per channel. The first windowed start runs the brightness calibration screen before the main menu when the key is missing, and Options > Display > "Brightness..." opens it later; its test pattern uses the same formula, so its three scene values match the 3D view.
- **Anti-aliasing** (M9.8) is an optional SMAA 1x stage (`engine.renderer.antiAliasing`, on by default in Enhanced and off in Retail). When on, the tone-map pass writes a presented-size LDR image instead of the frame, and three passes smooth the scene's edges: luma edge detection into an `Rgba8` edges target, blending weight calculation with SMAA's precomputed area and search textures, and neighbourhood blending into the swapchain or capture target. The stage runs after the post chain and tone map and before the HUD composite, so it never touches the HUD, 2D layer, menus or prompts. Its three targets are recreated when the presented size changes. Off takes the same path as before the option existed, byte for byte. The shaders and texture data are adapted from the MIT-licensed [SMAA reference implementation](https://github.com/iryoku/smaa); the licence is at `Libs/std/Babylon/Shaders/SMAA-LICENSE.txt` and is installed in `licenses/smaa`.
- **Sharpening** (M9.9) is an optional contrast-adaptive sharpening pass (`engine.renderer.sharpening`, 0..1, 0.3 by default in Enhanced and off in Retail). It is AMD FidelityFX CAS in sharpen-only mode, adapted from the MIT-licensed [FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS); the licence is at `Libs/std/Babylon/Shaders/CAS-LICENSE.txt` and is installed in `licenses/cas`. One pass (`cas.frag.hlsl`) samples the presented-size LDR image, after SMAA when anti-aliasing is on and after the tone map when it is off, and writes the swapchain or capture target. It runs before the HUD composite, so it never touches the HUD, 2D layer, menus or prompts. The stage's source image and pipelines are recreated when the presented size changes. At 0 the pass is skipped and the image is byte-identical to the path without the option.
- **Vignette and film grain** (M9.18) are two independent toggles on the presented-size LDR image (`engine.renderer.vignette` and `engine.renderer.filmGrain`, both off in every build). They run after SMAA and CAS and before the HUD composite, so neither the edge detection nor the HUD is affected. The vignette applies a smooth, aspect-corrected radial darkening to the corners and leaves the centre unchanged. The film grain is deterministic luminance noise from a 32-bit hash of the pixel position and a frame counter that advances with the game time received through `std3D_SetFrameTime` (never wall-clock time or `rand()`), weaker in the highlights and with zero mean. The Babylon postfx effects `postfx::VignetteEffect` and `postfx::FilmGrainEffect` (ADR-0043) draw them; with both on, the vignette writes a pooled scratch target and the grain reads it. When both are off the target and passes are not recorded, so the image is byte-identical to the path without them.
- **Render scale** (M9.4) makes the scene color, depth and normal targets a multiple of the video mode (`engine.renderer.renderScale`, 0.5..2.0, default 1). The game keeps its display size: the canvas, every screen coordinate, the HUD's layout and the read-back image stay in display pixels, and `std3D`'s pre-transformed screen vertices still map over the same display-sized viewport, so a larger target supersamples and a smaller one undersamples the scene. The final pass resolves the result to the presented size, linearly when the scale is not 1. The scene source, the presentation size and the display coordinates are independent of the scale; a scaled target above the device's texture limit is clamped down with one log line.
- **The gameplay HUD layer** (M9.4b) draws after the scene's post chain and tone map. `std3D_BeginHudLayer` records the scene draws collected so far and sends the following draws to a display-sized color target with its own depth; `std3D_EndHudLayer` records that pass. The color target has the scene color format and is cleared transparent each frame; the HUD draws blend color with `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` and the alpha channel with `ONE, ONE_MINUS_SRC_ALPHA`, so the layer holds premultiplied color and coverage alpha. The presenter composites it over the tone-mapped frame with `ONE, ONE_MINUS_SRC_ALPHA` before the prompt and menu overlays. The layer never enters the scene targets, so it keeps the render scale's sharpness and no scene effect touches it; at render scale 1 with no post passes the presented image matches the old single-pass result within 1/255 per color channel. `JonesHud_Process` marks its drawing with the pair, and the Null backend accepts both as no-ops.
- **Pass order** is scene passes, the post-processing chain, the water-ripple stage when it is on, the contact-shadow stage when it is on and a camera transform is available, the ambient-occlusion stage when it is on, the motion-blur stage when it is on and the camera moved, the depth-of-field stage when it is on, the light-shaft stage when it is on and a sky is visible, the lens-flare stage when it is on, the bloom stage when it is on, the final tone-map pass (exposure, operator, colour grading when not Neutral, brightness), the SMAA anti-aliasing stage when it is on, the CAS sharpening pass when it is on, the vignette and film-grain pass when either is on, the HUD layer composite, then the interaction-prompt and menu present overlays. Earlier slice notes describe the stage they added relative to this order; this sentence is the current one.
- **What the game reads stays what the game drew.** The chain and the final pass never write the back buffer, so `stdDisplay`'s back-buffer lock, `stdRenderBabylon_ReadBackBuffer`, savegame thumbnails, `stdDisplay_SaveScreen` and the replay's frame output read the scene before post-processing. `ReadBackBuffer` and `WriteBackBuffer` translate between the scene color target and the `Bgra8` bytes the game works with (half floats to 8-bit unorm and back on an `Rgba16F` target), so the lock, thumbnails, intro movie and 2D layer keep their contract. At a render scale other than 1 they resample on the CPU between the scaled target and the display-sized image (box filtering down, bilinear up); at 1 they copy as before. The game reads and writes its back buffer as the surface it drew (the intro movie writes it, the 2D layer draws over it), and feeding a post-processed image back would apply the chain again each frame and make the replay frames depend on presentation settings. With an empty chain the presented image and the back buffer are the same up to the final pass's clamp.

### M9 Presentation Options

Every M9 presentation option, with its values, defaults and Options row, matching [Jones.cfg](../../Jones.cfg.md).
A QOL-off build uses the Retail column. Options that are not simulation settings keep the same values in both
profiles; turning one off is what makes its part of the image byte-identical to the path without it.

| Key | Values | Enhanced default | Retail default | Options row |
| --- | --- | --- | --- | --- |
| `engine.renderer.renderScale` | `0.5`..`2.0` (50, 67, 75, 100, 125, 150 or 200%) | `1.0` | `1.0` | Display: Render scale |
| `engine.renderer.antiAliasing` | `0` Off, `1` SMAA | `1` | `0` | Display: Anti-aliasing |
| `engine.renderer.sharpening` | `0`..`1` (Off, Low, Medium or High = `0`, `0.3`, `0.5`, `0.8`) | `0.3` | `0` | Display: Sharpening |
| `engine.renderer.toneMapping` | `0` Clamp, `1` Neutral, `2` Filmic | `1` | `0` | Display: Tone mapping |
| `engine.renderer.exposure` | `0.5`..`2.0`, step `0.1` | `1.0` | `1.0` | Display: Exposure |
| `engine.renderer.colorGrading` | `0`..`5` (Neutral, Warm, Cool, Adventure, Vivid, Muted) | `0` | `0` | Display: Colour grading |
| `engine.renderer.brightness` | `0.6`..`1.8`, step `0.05` | `1.0` | `1.0` | Display: Brightness... |
| `engine.renderer.bloom` | `0`..`3` (Off, Low, Medium, High; strengths `0`, `0.15`, `0.3`, `0.5`) | `1` | `0` | Display: Bloom |
| `engine.renderer.water.ripples` | bool | `true` | `false` | Display: Water ripples |
| `engine.renderer.ssao` | `0`..`3` (Off, Low, Medium, High) | `1` | `0` | Display: Ambient occlusion |
| `engine.renderer.motionBlur` | `0`..`3` (Off, Low, Medium, High) | `1` | `0` | Display: Motion blur |
| `engine.renderer.dof.cutscenes` | bool | `true` | `false` | Display: Depth of field (cutscenes) |
| `engine.renderer.dof.gameplay` | bool | `true` | `false` | Display: Depth of field (gameplay) |
| `engine.renderer.dof.strength` | `0.5`..`2.0` | `1.0` | `1.0` | none |
| `engine.renderer.lightShafts` | `0`..`3` (Off, Low, Medium, High; strengths `0`, `0.25`, `0.5`, `0.8`) | `1` | `0` | Display: Light shafts |
| `engine.renderer.lensFlare` | bool | `true` | `false` | Display: Lens flare |
| `engine.renderer.vignette` | bool | `false` | `false` | Display: Vignette |
| `engine.renderer.filmGrain` | bool | `false` | `false` | Display: Film grain |
| `engine.renderer.frameLimit` | `0` V-Sync, `30`, `60`, `120`, `144` or `240` Hz cap, `-1` Unlimited | `0` V-Sync | `0` V-Sync | Display: Frame rate limit |
| `engine.sim.fixedTick` | `0` Off or `50` On (old positive values map to `50`) | `50` On | `0` (ignored) | Gameplay: Simulation tick |
| `engine.renderer.texturePixelated` | bool | `false` | `false` | Display: Crisp pixels |
| `graphics.anisotropicFilter` | `1`, `2`, `4`, `8` or `16` | `8` | `1` | Display: Anisotropic filtering |
| `engine.renderer.fov` | `60`..`110`, horizontal degrees at 4:3 | `90` | `90` | Display: Field of view |

### Lit Vertices (M6.9b)

Option A's extended vertex ([ADR-0023](../09-decisions/ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)) reaches the GPU next to the classic one, which stays as it was:

- **`StdVertexLit`** (`Libs/std/types.h`, 56 bytes): a `StdVertexTL` at offset 0, then the view-space position (`vx, vy, vz`, offset 32) and the view-space unit normal (`nx, ny, nz`, offset 44), in rdroid's camera space (x right, y forward, z up). A zero normal means none is known.
- **`rdCache`**: `rdCache_GetLitProcEntry` and `rdCache_GetLitAlphaProcEntry` hand out entries whose `aViewVertices` and `aViewNormals` point at room next to `aVertices`; `rdCache_GetProcEntry` and `rdCache_GetAlphaProcEntry` set them to NULL. `rdCache_SendFaceListToHardware` computes every vertex's color as before and starts a new draw when a face is lit and the previous one is not (or the other way around); a lit draw goes to **`std3D_DrawRenderListLit`** with the same texture, render state and indices, a classic one to `std3D_DrawRenderList`.
- **`rdClip_FaceToPlaneLit`**: `rdClip_FaceToPlane` plus each vertex's view-space position and normal (per vertex, or the face normal for all), renormalized. `rdClip_FaceToPlane`, which world surfaces (`sithRender.c`) and models (`rdModel3.c`) use, culls faces wholly outside one frustum plane and clips none: the GPU clips the projected faces and interpolates the position and normal with the other attributes, perspective-correctly, since the clip-space position the vertex shader rebuilds is a linear function of the view-space one. So no CPU clipper interpolates normals; the sky (`rdClip_QClipFaceW`) and debug paths (`rdPrimit3_ClipFace`) that do clip stay classic.
- **Babylon `std3D`**: lit draws use their own pipelines (one per Z write) with `std3d_lit.vert.hlsl` and `std3d_lit.frag.hlsl`, and stream into their own vertex buffer, made on the first lit draw, so classic draws issue the same device calls as before. The shaders compute the color exactly as the default ones; the vertex shader maps the position and normal to the normal target's space as `(x, z, -y)`, and the fragment shader writes the normalized interpolated normal (the constant one for a zero normal).
- **Capture**: `std3D_DrawRenderListLit` records as id 35 ([RCAP](../../Formats/RCAP.md)); older captures replay unchanged.

Since M6.9b2 the game fills them, behind the presentation option `engine.renderer.litFaces.enabled` ([Jones.cfg](../../Jones.cfg.md), on by default), which `JonesDisplay_Open` hands to `rdSetLitFaces`:

- **World surfaces** (`sithRender_RenderSectors` for every non-sky surface, `sithRender_RenderAlphaAdjoins` for translucent adjoins) take lit entries and `rdClip_FaceToPlaneLit`. Levels store a face normal per surface only, so `sithRender_Open` computes **smoothed corner normals** once per world with `rdFace_CalcSmoothNormals`: a corner of a surface averages the unit normals of the surfaces sharing its vertex whose normals are less than 45 degrees from its own (`SITHRENDER_SMOOTHNORMAL_MAXANGLE`, to be tuned in M10); without such a partner it keeps the face normal, so hard corners stay hard. Sky surfaces and surfaces without geometry never smooth a neighbour. The normals live in renderer-side arrays freed by `sithRender_Close`, not in the world or savegames. Each frame a drawn surface's corner normals are rotated by the view matrix, like its vertices, and renormalized; if the arrays could not be made, lit surfaces use their rotated face normal.
- **Models** (`rdModel3_DrawMesh`, `rdModel3_DrawFace`): the mesh's vertex normals are rotated by the model-view matrix once per mesh; Gouraud faces take them by vertex, flat-shaded faces (and meshes without vertex normals) the rotated face normal, and a back face that is drawn (double-sided or culling off) gets them negated. Translucent faces go through `rdCache_GetLitAlphaProcEntry`.
- **Classic**: the sky, sprites, particles, polylines, shadows, the overlay map and the HUD (its 3D inventory icons turn lit faces off around `rdModel3_Draw`). With the option off every face is classic, exactly as before: record 14 in captures, the same frames.

`Jones3D-replay --render gpu --frames-out <dir> --normals` writes the normal target of each replayed frame next to the frame image, to look at what the lit path emits ([Testing](../../Testing.md)).

### The Frame's Light List (M6.9c)

With the same option the renderer also gets the frame's lights, for per-pixel lighting (option A L5, M10.2):

- **`sithRender_Draw`**, after the visible sector and thing lists and `sithRender_BuildDynamicLights`, calls **`rdCamera_BuildStdLights`** and **`std3D_SetLights`** once per frame. The list is the rdroid camera's: the thing lights and head lights the visibility pass collected through `rdCamera_AddLight`, in that order and up to `RDCAMERA_MAX_LIGHTS` (128, equal to `STD3D_MAXLIGHTS`), so nothing new is selected. It is read only. Not in it: the sector point light `sithRender_RenderThings` adds around each model and removes after it, and the per-sector subset `sithRender_BuildDynamicLights` picks (lights whose `minRadius` reaches the sector's bounding sphere); per-pixel lighting has to apply those rules itself.
- **`StdLight`** (`Libs/std/types.h`, 48 bytes, three groups of four 32-bit values): the view-space position (the camera's view matrix applied to the light's world position) and `minRadius`; the color (`rdLight.color`, alpha as stored); `maxRadius`, the camera's `attenuationMin` and `attenuationMax`, and flags (`STD_LIGHT_ENABLED` for `rdLight.bEnabled`). These are all the inputs of `rdLight_CalcVertexIntensities` (full intensity within `maxRadius`, linear to zero at `minRadius`, times the normal's cosine), `rdLight_CalcFaceIntensity` (plane distance times `attenuationMin` subtracted, within `minRadius`, enabled lights only) and the dynamic vertex lights (distance times `attenuationMax` subtracted, within `maxRadius`, clamped to 0..1). `rdLight.type` (always 2, a point light) and `direction` are read by none of them and are left out.
- **Babylon `std3D`** copies the list into a storage buffer (made on first use: a 16-byte header with the count, then the lights as they are) in the first scene pass after it changed, before the pass, and binds it to fragment storage slot 0 of the pass's lit draws. `std3d_lit.frag.hlsl` declares it and reads it only for vertices that ask for per-pixel lights (M10.2). A test shader in Babylon's render tests reads a storage buffer on lavapipe. The Null `std3D` ignores the call.
- **Off**: no `std3D_SetLights`, no lit draws, no light buffer; the classic device calls exactly as before.
- **Capture**: `std3D_SetLights` records as id 36 ([RCAP](../../Formats/RCAP.md)); older captures replay unchanged.

The option is presentation, not part of the Retail or Enhanced simulation profile: with Classic the colour is
the same either way, and the visibility pass, `SITH_SECTOR_SEEN` and everything else that reaches simulation state
run the same code; the load-time allocation touches no world, thing or random state, so traces are identical with
it on and off.

### Per-pixel Lighting (M10.2)

`engine.renderer.perPixelLighting` (Options > Display, **Lighting**: Classic or Per-pixel, Per-pixel by default in
Enhanced and Classic forced in the Retail profile) moves the camera's dynamic lights out of the CPU vertex colour and into
`std3d_lit.frag.hlsl`. It needs `engine.renderer.litFaces.enabled`; with lit faces off it behaves as Classic.
Classic is byte-identical to before.

One formula lights every per-pixel surface: the model GOURAUD one from `rdLight_CalcVertexIntensities`. For each
light in the uploaded list with `STD_LIGHT_ENABLED`, let `d = |lightPos - p|`; it adds nothing if `d >= minRadius`
or `N·L <= 0`, adds `color * N·L` inside `maxRadius`, and ramps linearly to zero at `minRadius` outside it.
`p` and `N` are the lit vertex's interpolated view-space position and normal; the light positions are in the same
rdroid camera space (the shader turns both into the normal target's space with `(x, z, -y)`). There is no
specular, no shadow, no per-sector subset and no new light selection: the shader iterates the frame's list, which
keeps the original order and cap. World and model surfaces therefore use the same N·L rule (the old world
distance-only formula is Classic's alone), which stops light leaking around corners at the cost of a different
look on levels lit for the old formula.

What the CPU keeps in the lit vertex colour, so nothing counts twice:

| Geometry (resolved mode) | Classic | Per-pixel base colour | Shader adds |
| --- | --- | --- | --- |
| World `GOURAUD` | baked `aIntensities` + `sithRender_BuildDynamicLights`, clamped, + `extraLight` | baked `aIntensities` + `extraLight` (`rdClip_FaceToPlaneLitBaked`) | the uploaded lights |
| World `DIFFUSE` | `extraLight` (the dynamic intensities are ignored) | same | the uploaded lights |
| World `LIT` / `NONE` | `extraLight` / none | same | nothing |
| Model `GOURAUD` | `aVertColors` + all camera lights (sector light included) + `extraLight` + camera ambient | `aVertColors` + the sector light + `extraLight` + camera ambient | the uploaded lights |
| Model `DIFFUSE` | `faceIntensity` from all camera lights + `extraLight` + camera ambient | sector-light `faceIntensity` + `extraLight` + camera ambient | the uploaded lights (with the lit face normal) |
| Model `LIT` / `NONE` | `extraLight` + camera ambient | same | nothing |
| Sprites, particles, polylines, sky, HUD | unchanged | unchanged | nothing, they never use the lit shaders |

The **model sector light** is added to the camera list only around each model draw, so it is not in the uploaded
list. `sithRender_RenderThings` records its camera-list index with `rdModel3_SetSectorLightNum`; in per-pixel mode
`rdModel3_DrawMesh`/`rdModel3_DrawFace` calculate the mesh's GOURAUD intensities and DIFFUSE face intensity from
that light alone, keeping its contribution in the base colour exactly as before. Every other camera light goes
per pixel. Models drawn outside that path (no sector light recorded) keep the classic camera-list lighting.

The shader learns per face whether to add lights from the unused specular color's blue byte: `rdCache` writes
`0xFFFFFFFF` for a resolved `GOURAUD` or `DIFFUSE` lit face and 0 for `LIT`/`NONE` in per-pixel mode, and leaves
every vertex as it was in Classic, so Classic vertices, captures and pixels are unchanged. The screen fade factor
`rdCache_SendFaceListToHardware` applies to the base colour is passed to the shader in the fragment uniform block
(`gSurfaceTag.y`, 1.0 when no fade is active) and multiplies the light sum. The fragment colour is
`texture * saturate(base.rgb + dynamic)`, then fog as before; the normal target and its surface tag are unchanged.
The `--render gpu` replay reads the same flag bytes from a per-pixel capture, so it lights the frame the same way.

The option applies at once through `rdSetPerPixelLighting` (the Display OK calls it), still gated on the current
lit-faces state; `JonesDisplay_Open` also sets it from the config. The CPU split touches only renderer-side data:
`sithRender_BuildDynamicLights` may keep running, the visibility pass and simulation state are untouched, and
headless traces are identical with the option on and off.

### Lighting terms (M10.1 audit)

This section records, for the M10 per-pixel split, every lighting term the CPU bakes into a vertex colour today, where it is computed, and which terms come from the camera's dynamic lights. It is documentation only; it changes no code. Line references are to the current tree. The short version:

- The camera light list is the thing lights and actor headlights the visibility passes collect (`sithRender_BuildVisibleSector`, `sithRender.c:955-994`, and `sithRender_CollectThingLights`, `sithRender.c:1344-1390`), capped at `RDCAMERA_MAX_LIGHTS` 128 (`rdCamera_AddLight`, `rdCamera.c:470-496`).
- World surfaces use it in the vertex colour **only in `RD_LIGHTING_GOURAUD`**, and through the separate `sithRender_BuildDynamicLights` formula, not through `rdLight_CalcVertexIntensities`.
- Models use it in `RD_LIGHTING_DIFFUSE` and `RD_LIGHTING_GOURAUD`, through `rdLight_CalcFaceIntensity` and `rdLight_CalcVertexIntensities` respectively.
- Sprites, particles, polylines and the sky never use the camera light list; they stay classic.
- `SithSector.tint` (`Libs/sith/types.h:1943`) is read only by the sector COG get/set (`sithCogFunctionSector.c:23,39`) and by serialization; no renderer path reads it today.

#### Geometry paths and the terms in their vertex colour

The resolved mode is `min(face or mesh mode, global mode)`: world surfaces clamp at `sithRender.c:1115-1119` and `1721`, models at `rdModel3.c:1793-1797` and `1902-1906`, and `rdCache_SendFaceListToHardware` clamps once more at `rdCache.c:330-334`. `sithRender_lightMode` is `RD_LIGHTING_GOURAUD` by default (`sithRender.c:244`, `JonesDisplay.c:189` and `JonesMain.c:2895`).

| Geometry path (resolved mode) | Draw | Vertex colour computed at | Terms added to the colour | Camera dynamic lights |
| --- | --- | --- | --- | --- |
| World surface, `NONE` | lit entry when `!bSky && rdGetLitFaces()` (`sithRender.c:1062-1064`), else classic | `rdCache_SendFaceListToHardware` (`rdCache.c:330-388`) | none: RGB starts at 1.0 and alpha is `aVertIntensities->alpha` (`rdCache.c:342-345`); `extraLight` and `aVertIntensities` are read but not applied for `NONE` | none |
| World surface, `LIT` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:346-352`) | `face.extraLight + sector.extraLight` (`sithRender.c:1121-1122`), no clamp | none; `aVertIntensities` RGB is ignored below `GOURAUD` (only its alpha is read, at `rdCache.c:351`) |
| World surface, `DIFFUSE` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:346-352`) | same as `LIT`; world surfaces never call `rdLight_CalcFaceIntensity` | none |
| World surface, `GOURAUD` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:354-369`) | `extraLight` plus `aVertIntensities[i]`; the latter is `clamp(aIntensities[i] + aVertDynamicLights[vertIdx])` (`rdClip.c:4003-4004`), where `aIntensities` is the level-baked static per-vertex light (`sithSurface.c:447-459`, `sithSurface.c:829-832`) | **yes**, via `sithRender_BuildDynamicLights` (`sithRender.c:1392-1452`), per-sector subset; no N·L |
| World translucent adjoin, any mode | lit alpha entry when `rdGetLitFaces()` (`sithRender.c:1713-1714`), else classic | `rdCache_SendFaceListToHardware` (`rdCache.c:330-388`) | same as the matching world-surface mode: `sithRender.c:1721-1729` and `sithRender_SurfaceFaceToPlane` (`sithRender.c:1832-1869`) | same as world `GOURAUD` only; the camera ambient set at `sithRender.c:1686-1691` is not read by `rdCache` for world faces |
| Model face, `NONE` | lit entry when `bLitMesh` (`rdModel3.c:1779`, `1889-1893`), else classic | `rdCache_SendFaceListToHardware` (`rdCache.c:342-345`) | none | none |
| Model face, `LIT` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:346-352`) | `face.extraLight` (`rdModel3.c:1945`) plus camera ambient (`rdModel3.c:1972-1975`), which `sithRender` sets to `clamp(sector.ambient + sector.extra)` (`sithRender.c:1469-1472,1571`) or that plus the thing's flat light (`sithRender.c:1557-1567`) | none (the mesh light loop runs only above `LIT`, `rdModel3.c:1682`) |
| Model face, `DIFFUSE` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:346-352`) | `face.extraLight + faceIntensity + camera ambient`; `faceIntensity` from `rdLight_CalcFaceIntensity` (`rdModel3.c:1958-1969`) over **all** camera lights (`rdModel3.c:1799-1815`) | **yes**, per face, plane-distance formula, `bEnabled` only |
| Model face, `GOURAUD` | as above | `rdCache_SendFaceListToHardware` (`rdCache.c:354-369`) | `face.extraLight + camera ambient + aLightIntensities[i]`; `aLightIntensities = aVertColors + rdLight_CalcVertexIntensities` over all camera lights (`rdModel3.c:1817-1843`) | **yes**, per vertex, radius-ramp formula |
| Sprite | classic alpha entry (`rdSprite.c:173`) | `rdSprite_Draw` (`rdSprite.c:283-310`) | `sprite.extraLight + camera ambient`, clamped to 0..1 (`rdSprite.c:289-296`); mode capped at `DIFFUSE` (`rdSprite.c:310`), so the per-vertex alpha at `rdSprite.c:289` is never used as RGB | none |
| Particle, `NONE` | classic (`rdParticle.c:388-397`) | `rdParticle_Draw` (`rdParticle.c:406-420`) | none: `lightingMode` is `NONE` for emit-light particles (`sithParticle.c:266-270`), so `extraLight` is ignored | none |
| Particle, `DIFFUSE` | classic | `rdParticle_Draw` (`rdParticle.c:406-420`) | `aExtraLights[i]` (per-particle extra light, `rdParticle.c:420`); camera ambient is put in `aVertIntensities` (`rdParticle.c:352-357,406-409`) but `rdCache` adds that only in `GOURAUD`, which particles never use | none |
| Polyline | classic alpha entry (`rdPolyline.c:226`) | `rdPolyline_DrawFace` (`rdPolyline.c:291-319`) | none: `lightingMode` is forced to `NONE` (`rdPolyline.c:319`), so both `aVertIntensities` and `extraLight` are ignored | none |
| Sky (ceiling or horizon) | classic (`sithRender.c:1062-1064,1072-1104`) | `sithRenderSky_HorizonFaceToPlane` / `_CeilingFaceToPlane` (`sithRenderSky.c:63-127`) | none: `lightingMode` is `NONE` (`sithRenderSky.c:65,92`) | none |

Flags that change a path's lighting, rather than only its presentation:

- `SITH_SURFACE_CEILINGSKY` / `SITH_SURFACE_HORIZONSKY` force `bLit = false` (`sithRender.c:1062-1063`) and `NONE` (`sithRenderSky.c:65,92`).
- `RD_FF_TEX_TRANSLUCENT` changes which colour channels are written (`rdCache.c:381-388`), not the RGB terms.
- `SITH_SURFACE_WATER` only adds the normal-target tag (`sithRender.c:1134-1138`, `1739-1742`); it does not change lighting.
- `RD_FF_FOG_ENABLED` only drives the fragment shader's fog; fog is applied after texture times diffuse (`std3d_lit.frag.hlsl`), so it must stay out of the per-pixel light sum.
- `RD_FF_DOUBLE_SIDED` and back-face-culling off only negate a model's lit normals when the back face is drawn (`rdModel3.c:1931-1938`).

One open case: `rdParticle_Draw` uses `prdParticle->lightningMode` unchanged (`rdParticle.c:416`). Sith's own clouds set it to `NONE` or `DIFFUSE` (`sithParticle.c:266-270`), but a particle loaded from a file can carry `GOURAUD` (`rdParticle.c:209`), in which case `rdCache` would add the camera ambient stored in `aVertIntensities` (`rdParticle.c:406-409`). Whether any shipped particle asset does this was not determined; the sith-created path does not.

#### Exact dynamic-light formulas

All formulas below use `rdLight.color` per channel and the camera's attenuation values. `sithCamera` sets `attenuationMin = 0.4` and `attenuationMax = 0.8` (`sithCamera.h:29-30`, applied at `sithCamera.c:335,382`); `rdCamera_NewEntry` starts at 0.2 and 0.1 before that (`rdCamera.c:77-78`). When a light arrives with zero radii, `rdCamera_AddLight` derives `minRadius = mean(color) / attenuationMin` and `maxRadius = mean(color) / attenuationMax` (`rdCamera.c:480-490`, mean at `rdLight.c:183-186`), so normally `maxRadius < minRadius`.

- **`sithRender_BuildDynamicLights`** (`sithRender.c:1392-1452`), the world-surface path:
  - per sector, select every camera light with `sector.radius + light.minRadius > |lightPos - sector.center|` (`sithRender.c:1408-1418`);
  - reset each processed vertex to 0 (`sithRender.c:1426`);
  - for each selected light, let `d = |lightPos - vertex|`. If `d < maxRadius`, add `color - d * attenuationMax` **per channel** (`sithRender.c:1435-1442`). There is no N·L, no `bEnabled` test and no `minRadius` outer term in this formula;
  - after **each** light, clamp the accumulated RGB to 0..1 (`sithRender.c:1444`); alpha stays 0;
  - cache per vertex by render tick (`sithRender.c:1448`).
- **`rdLight_CalcVertexIntensities`** (`rdLight.c:67-105`), the model `GOURAUD` path:
  - start from `aVertexColors` (`rdLight.c:71`);
  - iterate lights from last to first (`rdLight.c:69,73`); no `bEnabled` test;
  - `d = |lightPos - vertex|`; require `d < minRadius` (`rdLight.c:78-80`);
  - `L = normalize(lightPos - vertex)`, `dot = N·L`, require `dot > 0` (`rdLight.c:81-85`);
  - if `d < maxRadius`, add `color * dot`; otherwise add `color * (1 - ((d - maxRadius) / (minRadius - maxRadius))) * dot`, i.e. full inside `maxRadius`, linear to zero at `minRadius` (`rdLight.c:86-100`);
  - no clamp at this level, and no use of `attenuationMin` or `attenuationMax`.
- **`rdLight_CalcFaceIntensity`** (`rdLight.c:133-160`), the model `DIFFUSE` path:
  - only `bEnabled` lights (`rdLight.c:140`);
  - `d = (lightPos - firstFaceVertex) · faceNormal`, a **signed plane distance**, not the Euclidean distance (`rdLight.c:145`, `rdMath.h:78-81`);
  - require `d < minRadius`, then `L = normalize(lightPos - firstFaceVertex)`, `dot = faceNormal·L > 0` (`rdLight.c:146-151`);
  - add `(color - d * attenuationMin) * dot` per channel (`rdLight.c:152-155`); no `maxRadius`, no clamp.
- **Unused**: `rdLight_CalcDistVertexIntensities` (`rdLight.c:107-131`) and `rdLight_CalcDistFaceIntensity` (`rdLight.c:162-181`) have no callers in the tree. They also reuse and mutate the `attenuation` parameter across lights and the vertex variant writes `color.red` to all three channels, so they are not a specification to copy. Open: whether M10 keeps or removes them is undecided; nothing reaches the screen through them today.
- **`rdCamera_SetAttenuation`** (`rdCamera.c:450-468`) rewrites zero light radii as `attenuationMin * 40` and `attenuationMax * 50` (16 and 40). This runs when a light already exists with zero radii; in the normal startup order `rdCamera_AddLight` fills the radii first, so this path is not normally hit.

#### Light selection and caps

- The visibility passes fill the camera list before any draw: visible sectors in `sithRender_BuildVisibleSector` (`sithRender.c:955-994`) and off-screen thing sectors in `sithRender_CollectThingLights` (`sithRender.c:1344-1390`). Only things with `SITH_TF_EMITLIGHT` are considered: their light if its range is dynamic (`alpha > 0.01`, `sithRender.c:79-80,958-963`) and, for actor/player types, their headlight (`sithRender.c:976-991`). `sithRender_aThingLights` holds `SITHRENDER_MAXTHINGLIGHTS` = 64 (`sithRender.c:74,167`); `rdCamera_AddLight` caps the camera list at `RDCAMERA_MAX_LIGHTS` = 128 (`rdCamera.c:472-475`, `Libs/rdroid/types.h:9`).
- The GPU list (`rdCamera_BuildStdLights` + `std3D_SetLights`, `sithRender.c:561-566`, `rdCamera.c:501-529`) is a snapshot of that camera list taken **before** `sithRender_RenderThings`. It therefore has the thing lights and headlights but neither the per-sector subset nor the model sector light.
- World surfaces: the per-sector subset from `sithRender_BuildDynamicLights` (`sithRender.c:1408-1418`); every visible sector's vertices are stamped once per render tick, so a shared vertex keeps the first sector's subset. In `LIT`/`DIFFUSE` the dynamic result is computed but not used.
- Models: all camera lights, with **no** per-mesh or per-thing radius cull. The loops at `rdModel3.c:1799-1815` and `1817-1850` fill `aLocalLightPos`/`apMeshLights` but then pass `rdCamera_g_pCurCamera->aLights` and `->numLights` directly (the `TODO`s at `rdModel3.c:1836-1838`, `1959-1961`); `apMeshLights`/`numMeshLights` are unused.
- Model sector light: `sithRender_RenderThings` adds `pSector->light` to the camera list for the duration of each model draw and removes it afterwards (`sithRender.c:1504-1518,1580-1584`). Models therefore see it; world surfaces do not (they are drawn earlier), and it is not in the GPU list. The index `sithRender_numSectorPointLights` is never incremented (`sithRender.c:1506` TODO), so only slot 0 is used.
- Headlights are ordinary camera lights by this point (`sithRender.c:976-991,1376-1387`); they are in the GPU list.

#### Clamps and quantisation before std3D

A per-pixel sum cannot reproduce the classic result exactly because the CPU clamps at several intermediate points and then quantises to 8 bits:

1. `sithRender_BuildDynamicLights` clamps the accumulated dynamic RGB to 0..1 after every light (`sithRender.c:1444`).
2. `rdClip_FaceToPlane` adds the dynamic vector to the baked `aIntensities` and clamps the sum to 0..1 (`rdClip.c:4003-4004`) for world surfaces and alpha adjoins. Models hit the other branch and copy `aLightIntensities` without that clamp (`rdClip.c:4024`).
3. `rdCache_SendFaceListToHardware` applies the screen-fade factor to RGB (`rdCache.c:370-375`) and clamps RGB and alpha to 0..1 (`rdCache.c:376-379`).
4. The colour is written as 8-bit `D3DCOLOR` with truncating casts (`STD_COLOR_FROM_FLOATS`/`_RGB`, `std/types.h:98-99`, used at `rdCache.c:381-388`); an opaque face forces alpha 255. The lit shader then computes `texture * diffuse` from those bytes.
5. Some paths pre-clamp their ambient/extra-light terms: sprites and polylines clamp `extraLight + camera ambient` (`rdSprite.c:295`, `rdPolyline.c:305`), the sector ambient used by models is clamped (`sithRender.c:1471`) and the flat-light variant too (`sithRender.c:1564`), while the alpha-adjoin ambient set at `sithRender.c:1686-1691` is not clamped and is not read by world faces anyway.

#### Render side effects touching simulation-facing state

These paths must keep their existing side effects; the M10 split is presentation only:

- `sithRender_BuildVisibleSector` marks `SITH_SECTOR_SEEN` and sends `SIGHTED` to cog-linked sectors (`sithRender.c:941-949`); the same traversal collects the lights.
- `sithRender_RenderThing` sends `SIGHTED`/wakes AI through `sithRender_MarkThingSeen` (`sithRender.c:1631-1635`), temporarily writes `pThing->orient.dvec = pThing->pos` and clears it afterwards (`sithRender.c:1638,1649`), and updates render counters.
- `sithRender_RenderThings` writes render-side thing fields `cameraSpacePos` and `frustumCull` (`sithRender.c:1481,1544`) and changes the shared camera ambient per thing (`sithRender.c:1567,1571`).
- `sithRender_BuildDynamicLights` writes the world's renderer-side `aVertDynamicLights` and `aVertDynamicLightsRenderTicks` (`sithRender.c:1396-1448`; declared in `sith/types.h:2698-2699`); they are not serialized and never feed the simulation.
- `rdModel3_DrawMesh` overwrites each mesh asset's `aLightIntensities` (`rdModel3.c:1817-1843`); this is presentation data shared by every instance of the model.
- On the fixed tick, sprite destruction and polyline jitter are owned by the simulation mark pass (`sithRender.c:1601-1629`), not by the draw path. A lighting change must not move them.

#### Implications for M10.2

Per path, what the CPU base colour must keep and what the shader may add:

- **World `GOURAUD`**: keep `texture * (extraLight + aIntensities)`; stop adding `aVertDynamicLights` to `aVertIntensities`. The shader must add the `sithRender_BuildDynamicLights` term per pixel (distance only, no N·L, per-sector mask, clamp), **not** `rdLight_CalcVertexIntensities`. The split is not clean: classic clamps `aIntensities + dynamic` to 0..1 per vertex before `extraLight` is added (`rdClip.c:4003-4004`), so a per-pixel sum only approximates it.
- **World `LIT` / `DIFFUSE`**: classic adds no camera dynamic light. The base colour is already complete; the shader must add nothing for these faces, or the classic look changes.
- **World `NONE`, sky, `RD_FF_TEX_TRANSLUCENT` alpha handling**: no dynamic term; leave as is.
- **Model `GOURAUD`**: keep `aLightIntensities = aVertColors` only; remove the `rdLight_CalcVertexIntensities` contribution at `rdModel3.c:1835-1843`. The shader adds the same radius ramp and N·L per pixel using the whole camera list. The split is clean for this term, except for the model sector light below.
- **Model `DIFFUSE`**: remove the `faceIntensity` term (`rdModel3.c:1958-1969`); the shader must reproduce the plane-distance formula, which needs the face normal and the signed plane distance rather than the interpolated per-pixel normal. This is not equivalent to a per-pixel radial point light, so the split is not clean.
- **Model `LIT` / `NONE`**: no dynamic term; keep the base as it is.
- **Model sector light**: the shader cannot reproduce it from the current `StdLight` list, because `sithRender_RenderThings` adds it after the list is built and removes it after the model (`sithRender.c:561-566,1504-1518`). M10.2/3 must either extend the uploaded list for model draws or drop this term; either way it is a deliberate behaviour decision.
- **Camera ambient / sector ambient / flat thing light**: these stay in the base colour (`rdModel3.c:1972-1975`, `rdSprite.c:292-296`, `rdParticle.c:352-357`, `rdPolyline.c:302-306`); the shader must not treat them as dynamic lights.
- **Sprites, particles, polylines, sky**: stay classic. They have no camera-dynamic term today, so the shader must add none for them. All four draw through `rdCache_GetProcEntry`/`GetAlphaProcEntry`, never the lit entries.
- **Headlights** are already in the uploaded list, so world `GOURAUD` and model split can use them as ordinary point lights. The per-sector mask for world faces is the only selection the shader needs to reproduce.

### Textures And Cache

Five DX9 texture formats are offered, each matched to a Babylon format with the same bit layout:

- `X8R8G8B8` and `A8R8G8B8` map to `Bgra8` (opaque and alpha variants)
- `R5G6B5` maps to `B5G6R5`
- `A1R5G5B5` maps to `B5G5R5A1`
- `A4R4G4B4` maps to `B4G4R4A4`

`R8G8B8` is left out because no GPU API samples 24-bit texels. Texture allocation keeps owned system-memory copies of every mip, and the cache uploads every mip whose dimensions halve as Babylon's chain expects; a level that does not ends the chain with a warning. Pixels whose `ColorInfo` already matches the format's layout are copied unchanged, others are converted through `stdColor`, and formats without alpha get alpha 255. The texture cache follows `std3DX9.c`: frame counts, the linked cache list, and a purge of old entries when a texture does not fit the device's reported free memory.

A material that brings only its top level gets a full mip chain generated when it is uploaded, as long as `graphics.mipmapFilter` is not `0` (M9.7). Each level is box-filtered from the one above in RGBA8, then encoded into the texture's format. Alpha is handled so the transparent colour cannot bleed into visible texels: a 1-bit-alpha (colour-keyed) texture averages only its opaque texels and stays opaque when any contributor is opaque, a texture with more alpha bits is averaged premultiplied and then unpremultiplied, and a format without alpha averages every texel. The generated levels exist only in the GPU texture; the system-memory copies and `std3D_GetMipMapCount` keep the material's own count.

### Pipelines, Samplers And Draws

`std3D_GetPipeline` caches one pipeline per primitive and Z-write bit: three primitives (triangle list, line strip, point list) times Z write on or off. Triangles use `std3d.frag.hlsl`, lines and points the wireframe shader. All of them blend source alpha over inverse source alpha, test Z with `LessOrEqual`, write Z as `STD3D_RS_ZWRITE_DISABLED` says, and cull nothing, as `std3DX9.c` set the Direct3D state. Their second color target is the normal target (see above).

`std3D_GetSampler` caches samplers, indexed by bits worked out at each draw: linear filtering, linear mip filtering, clamp U, clamp V, the anisotropic level and the crisp-pixels toggle. It creates each one on first use. An anisotropic request takes the level of `graphics.anisotropicFilter` (1, 2, 4, 8 or 16; 1 keeps linear filtering, and an old boolean value reads as 8/1); the mip filter is linear for trilinear and nearest otherwise. `engine.renderer.texturePixelated` forces min and mag to nearest for world and model textures while leaving the mip filter alone, for the original look. Both options are read when the display opens, so they apply on the same display restart as the other Display options; the HUD, 2D layer and menus draw through Babylon's UI painter and never see them.

A draw streams its vertices, and for triangles its indices, into the frame's vertex and index buffers with `CommandList::updateBuffer` at growing offsets. `updateBuffer` cannot be recorded inside a pass, so the draw is streamed right away and the pass is recorded when it ends, at `std3D_EndScene` and at a `std3D_ClearZBuffer` inside a scene. The scene's Direct3D clears become the pass load operations, and the buffers start smaller than the DX9 64 MiB ones and grow when a frame needs more, destroying the old buffer after the frame's submit.

### Shaders

The game shaders (`std3d.vert.hlsl`, `std3d.frag.hlsl` and `std3d_wireframe.frag.hlsl`) are ports of the DX9 `default_vs.hlsl`, `default_ps.hlsl`, `default_wf_ps.hlsl` and `common.hlsli`, kept with their DX9 behavior; `present.*.hlsl` is the new full-screen present pass:

- The vertices are `StdVertexTL`, already transformed, with screen x and y in pixels, z in 0..1 and `rhw = 1 / w`. The vertex shader rebuilds clip space with `w = 1 / rhw`, so the GPU interpolates perspective-correctly and divides back to the same screen position and z.
- Screen position gets a half-pixel shift. Direct3D 9 puts pixel centers at integer screen coordinates, while SDL GPU (Direct3D 12, Vulkan, Metal) puts them at integer + 0.5; the shift makes the same pixels be covered and sampled.
- Colors arrive as bytes `B, G, R, A` and are swizzled with `.zyxw`, matching Direct3D 9's `D3DCOLOR`.
- The fragment color is texture times diffuse; untextured draws sample the white texture. Specular is not added, because `std3DX9.c` disabled `D3DRS_SPECULARENABLE` and its pixel shader ignored it.
- Fog is linear in `w`, as DX9 passed it: `saturate((w - start) * (1 / (end - start)))` lerps RGB to the fog color and keeps alpha. The depth, not the factor, is interpolated, so the fog is already per pixel; Direct3D 9's per-vertex fog-table fallback, which banded on large faces, was never used by this backend (checked in M9.15, which added nothing for that reason).
- The alpha test is fixed-function DX9 behavior in the shader: when `STD3D_RS_ALPHAREF_SET` is set, fragments with alpha not greater than `160 / 255` are discarded (`D3DCMP_GREATER`).
- Lines and points use the wireframe shader's constant opaque white times diffuse, with no texture and no fog.
- Both fragment shaders write the constant normal `(0, 0, 1)`, encoded, to the normal target (`SV_Target1`): `StdVertexTL` carries no normal. The lit shaders (`std3d_lit.*.hlsl`) write the interpolated one (see [Lit Vertices](#lit-vertices-m69b)).

The vertex shader's outputs are ordered so the pixel shader's inputs are a prefix of them: diffuse, texture coordinate and `w` come first and the unread specular color last. Direct3D 12 links a pixel shader only when its inputs match the vertex outputs in the same registers, and the DXIL comes through SPIR-V, which drops inputs a pixel shader never reads; the earlier specular gap made D3D12 refuse every game pipeline with `E_INVALIDARG`.

## Babylon stdDisplay Backend (M6.7)

The Babylon column's `stdDisplay` backend ([`stdDisplayBabylon.cpp`](../../../Libs/std/Babylon/stdDisplayBabylon.cpp)) implements the same `stdDisplayBackend_*` functions as the removed DirectX 9 backend, ported from `stdDisplayDX9.c`, with the capture forwarders of `stdDisplayCapture.c` still in front ([ADR-0024](../09-decisions/ADR-0024-render-capture-by-renamed-backends.md)). It drives the frame owner in [`stdRenderBabylon.cpp`](../../../Libs/std/Babylon/stdRenderBabylon.cpp) that `std3D` also draws through: `stdDisplay_Open` opens it and `stdDisplay_Close` closes it. The game-facing functions from `stdDisplay.h` keep their recovered names and answers.

### Video Modes And SetMode

One display device stands for the render device, named "Jones3D Babylon Display" with driver "stdDisplayBabylon"; its video memory is the largest texture's area times four, half of it reported free. Babylon's platform has no display-mode query, so the list is fixed. A headless run lists the default 640x480 in RGB565 (`stdColor_cfRGB565`) and X8R8G8B8 (`stdColor_cfRGB8888`) only: what `stdDisplayDX9.c` listed on a 640x480 display, such as the M6.1 DX9 recordings' Xvfb screen, so their fullscreen SetMode of mode 1 replays at the recorded size. A windowed run lists 16 fixed sizes (320x200, 320x240, 400x300, 512x384, 640x400, 640x480, 800x600, 1024x768, 1152x864, 1280x720, 1280x960, 1280x1024, 1600x900, 1600x1200, 1920x1080 and 1920x1200) in both formats, plus the window's own size when it fits the device limit, sorted by bit depth, width and height. All list 60 Hz; aspect ratio is 1 except the non-square windowed 320x200, 320x400 and 640x400 modes at 0.75, and a headless mode uses width/height. Before `stdDisplay_SetMode` the back buffer already reports 640x480 X8R8G8B8, as in the Null backend.

`stdDisplay_SetMode` takes a copy of the enumerated mode for fullscreen; a windowed mode uses the primary mode at `stdDisplay_SetDefaultResolution`'s size in X8R8G8B8. It neither switches the window to fullscreen nor resizes it: the game sizes the window to the back buffer afterwards, and the present scales the back buffer to the swapchain. The window's mode comes from the platform `Window::setFullscreen` through `wkernel_SetFullscreenStyle`/`wkernel_SetWindowedStyle`; fullscreen is desktop/borderless, so the display decides the window's pixel size and the present scales the back buffer to it, exactly as for a window resize. The mode creates the back buffer through `stdRenderBabylon_SetBackBufferSize` and makes `stdDisplay_g_frontBuffer` and `stdDisplay_g_backBuffer` hardware VBuffers of that size without CPU surfaces, fills the buffer, presents it once, and fills it again for fullscreen. `stdDisplay_ClearMode` calls the release callback and frees the back buffer.

### Back Buffer, Locks And Fills

The back buffer lives on the GPU in `stdRenderBabylon`'s color target, where `std3D` draws. Its CPU side is a copy in the video mode's pixel format that `stdDisplay_LockBackBuffer` hands out, as DX9 handed out its lockable surface. A lock reads the GPU back buffer into the copy only when the GPU changed it since the copy was last read or written, tracked by `stdRenderBabylon_GetBackBufferVersion`; the last unlock of a write lock uploads the copy after the frame recorded so far (`stdRenderBabylon_WriteBackBuffer`), and a read-only lock uploads nothing. The copy is always display-sized; at a render scale other than 1 the read resamples the scaled scene down or up into it, and the upload resamples it into the scene target (M9.4). This is what makes the 2D layer draw correctly over the 3D frame: the savegame thumbnail, which reads the frame through a read-only lock in every column, and `stdDisplay_SaveScreen` read the frame that `std3D` drew, debug lines draw over the frame, and the intro movie (`JonesMain_IntroMovieBlt*`) locks, clears the canvas to black, writes its aspect-fit destination rectangle, unlocks and presents, so it never reads back after its first frame. A read-only lock cannot be upgraded to a write lock, and a fill while the back buffer is locked fails. Fills of the whole back buffer are clear passes; fills of part of it go through the CPU copy and an upload. `stdDisplay_VBufferNew` always allocates software VBuffers because the render device has no lockable surfaces; locks of the front or back buffer lock the back buffer, other hardware VBuffers fail, and `stdDisplay_EncodeFromRGB565` encodes into the current mode's channels.

### Submit And Present

`stdDisplay_Update` ends the frame. It advances one millisecond of virtual time, as the Null backend does, so headless loops that wait on the clock around a present finish; then `stdRenderBabylon_PresentFrame` submits what `std3D` and the display recorded and, on a device with a swapchain, records the present pass and presents. Without a mode it returns 1 and does nothing. `std3D_EndScene` now leaves the frame open instead of submitting it, so submit and present sit at `stdDisplay_Update`, as `stdDisplayDX9.c` presented.

Since M9.23 the present has a frame-rate option (`engine.renderer.frameLimit`, Options > Display, "Frame rate limit"). The Babylon render device offers V-Sync, immediate and mailbox present modes, queried through `Device::presentModeSupport()` and chosen with `Device::setPresentMode()`; the SDL GPU backend maps them to `SDL_SetGPUSwapchainParameters` and the Null backend stores them, so both honor the same contract. V-Sync (`0`, the default) leaves presentation at the display's rate. A positive cap presents immediately with V-Sync off, and after the present `stdDisplay_Update` waits on `babylon::time::Clock` until the next frame deadline in a `time::FrameLimiter`: the deadlines are drift-free, most of the wait is a sleep and the last short window is a spin, and a frame more than one period late starts a fresh schedule instead of bursting. `-1` presents immediately with no cap. The wait is off on the headless virtual clock, on a device without a swapchain, and while a level loads (`stdDisplay_DisableVSync(true)` still switches the swapchain to immediate for the loading fast path). It never reads or changes game time or simulation state. The Display row applies at once through `stdDisplayBabylon_SetFrameLimit`.

The video-mode list's `refreshRate` is the fixed 60 Hz placeholder from the DX9-era list, not a value SDL reports, so the resolution row does not offer a real refresh-rate choice. A refresh-rate row waits for a reliable display-mode query.

### Reset Callbacks

`stdDisplay_RegisterDevicePreResetCallback` and `stdDisplay_RegisterDevicePostResetCallback` store their callbacks but nothing calls them, because the render device is never lost or reset; only the release callback runs, from `stdDisplay_ClearMode`. That gap is listed in [chapter 11](../11-risks-and-technical-debt.md).

## Widescreen Presentation (M9.5)

The gameplay and cutscene cameras are hor+ on a wider canvas: `rdCamera_SetFrustrum`/
`rdCamera_SetProjection` divide the planes by the canvas aspect ratio, so the vertical field of view
stays what it is and the extra width shows more scene. A 4:3 view remains fully visible at 16:9 and
21:9, which the canyon level-start cutscene confirms; nothing in the camera or simulation changes.

The HUD's edge-anchored indicators (health and endurance, drawn by
[`JonesHud.c`](../../../Jones3D/Display/JonesHud.c)) use a centered 16:9 safe area on a canvas wider
than 16:9: the safe area's left edge is added to their position, and the relative-anchor form uses the
safe area's width. At 4:3 and 16:9 the safe area is the whole canvas, so those layouts are unchanged.
Centered HUD parts (the inventory menu, reticle, interaction prompt and credits) keep their centre
anchors and need no safe area.

The intro movie (`JonesMain_IntroMovieBlt565/555/32`) keeps the bitmap's own aspect instead of
stretching to the canvas: `JonesMain_GetMovieDestination` fits the 640x480 movie, so a wider canvas
pillarboxes it with black bars and a taller one letterboxes it. Each blit clears the canvas to black
before drawing the destination rectangle. A 4:3 canvas still shows the movie full size.

## Interaction Prompt Overlay

The interaction prompt is presentation, so it does not enter the game frame or the simulation. The HUD asks
the player controls once per frame what the interaction and climb keys would do, caches the answer in
[`JonesHudPrompt.c`](../../../Jones3D/Display/JonesHudPrompt.c), and the Babylon column draws it from a second
present overlay in [`JonesHudPromptOverlay.cpp`](../../../Jones3D/Platform/Babylon/JonesHudPromptOverlay.cpp).
The overlay runs after the post chain, the present pass and the HUD layer composite, at the window's native pixel size like the menus
(ADR-0035), maps the press's control action (Activate, Jump or Forward) through the active scheme and the last
used device to its Xelu icon, and draws the icon and verb at the bottom centre. It hides while a menu overlay
is installed. Each target query is the same finding half as its press: `sithPlayerActions_Activate`, the
push/pull finder and move checks, `ClimbOn1m`/`ClimbOn2m`, the climb-wall mount split, `ClimbPullUp`, the
hanging pull-up, and the water-ledge check. The whip's **Swing** and **Climb** prompts read the whip-aim
targets through the same target choice `sithWeapon_ActivateWhip` uses. Every search runs inside
`sithCollision_BeginReadOnlySearch`/`EndReadOnlySearch`, so the prompt can never change the collision scratch
or the simulation
([ADR-0037](../09-decisions/ADR-0037-side-effect-free-presentation-queries.md)).
**Use**, **Pick up**, **Push**, **Pull** and **Push | Pull** use the Activate binding; **Climb up** uses
Activate at the top of a climbable wall, Forward while hanging and Jump for a climb-on or water ledge; **Climb**
uses Jump to mount a climbable wall. The whip's **Swing** and **Climb** use Activate and appear while the whip
is drawn. **Use** and **Pick up** are offered only when at least one COG that the
press would message (the thing's own, template or class COG, its captured COG, or a matching link) has an
`activated` handler; a COG-linked cactus without one gets no prompt. The developer log reports each change
with the kind, the surface index or thing name, the handler COG names where they apply, and the sith frame
number.

## Where OpenJones3D Extends The Original Renderer

The biggest architectural extensions were:

- DirectX 9 backend alongside the original DirectX 6.1c backend (both removed in M6.3)
- shader layer for the DX9 path (removed with it)
- VBO/IBO support in the DX9 path (removed with it)
- wider texture-format handling, including true-color textures
- breadth-first off-screen thing/light traversal
- longer default collection distances
- modern filtering options and automatic mipmap generation in the Babylon lower graphics layer (M9.7), and MSAA support in the removed DX9 layer

Crucially, those changes sit underneath or alongside the original scene-graph/sector-portal logic instead of replacing the entire renderer design. The renderer still thinks in terms of sectors, surfaces, adjoins, clip frusta, thing sectors, and per-vertex dynamic lights.
