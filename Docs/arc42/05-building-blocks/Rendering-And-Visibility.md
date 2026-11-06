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
- [`Libs/std/Babylon/Shaders/std3d.vert.hlsl`](../../../Libs/std/Babylon/Shaders/std3d.vert.hlsl), `std3d.frag.hlsl`, `std3d_wireframe.frag.hlsl` and `present.*.hlsl`
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

[`stdRenderBabylon.cpp`](../../../Libs/std/Babylon/stdRenderBabylon.cpp) owns the frame: the command list being recorded and the back buffer. The back buffer is a `Bgra8` color target that is also sampled, plus a depth target that is `D24S8`, or `D32F` where the device lacks `D24S8`, at the display size. On a device with a swapchain, submitting the frame first records a present pass: a full-screen triangle from `present.*.hlsl` samples the color target with a nearest, clamped sampler. `copyTexture` cannot name the swapchain, so a pass is the only way the render interface offers to put an offscreen target on the screen. Through M6.6 `std3D_EndScene` submitted the frame; since M6.7 `stdDisplay_Update` does (see [Babylon stdDisplay Backend](#babylon-stddisplay-backend-m67)).

### Render Passes, Normal Target And Post-Processing Chain (M6.9a)

The frame is a sequence of passes over offscreen targets, ready for per-pixel lighting, SSAO and post-processing ([ADR-0023](../09-decisions/ADR-0023-advanced-graphics-on-the-cpu-geometry-path.md)), with the classic look unchanged:

- **Scene passes** (`std3D`) write two color targets at once: the back buffer color and a **normal target**, plus the depth target. The normal target holds the view-space normal (x right, y up, z toward the viewer) as `n * 0.5 + 0.5` with alpha 1, in `R10G10B10A2` (10 bits per component in 4 bytes), or `Rgba8` where the device can't render to it. It belongs with the depth: it is cleared to 0 (alpha 0, nothing drawn) whenever the scene clears depth, draws that write Z replace it and draws that don't keep it (blended with `ZERO, ONE`), so it holds the normal of what the depth target holds. Draws of `StdVertexTL` write the constant normal facing the viewer; lit draws (`std3D_DrawRenderListLit`, see [Lit Vertices](#lit-vertices-m69b)) write their interpolated normals. The color output is computed exactly as before. The depth target is also sampled where the device can sample its format; the format itself is the one the scene always used.
- **The post-processing chain** is an ordered list of full-screen passes in `stdRenderBabylon`, empty by default. `stdRenderBabylon_AddPostPass` takes a fragment shader; each pass draws the present pass's full-screen triangle into one of two `Bgra8` targets the chain owns, reading the previous output (the back buffer color for the first pass) in fragment slot 0, the normal target in slot 1 and the depth target in slot 2, as far as its shader declares textures. It runs at every presenting submit (`stdRenderBabylon_SubmitFrame` and `_PresentFrame`), never at `_FlushFrame`. The present pass then samples the chain's last output (`stdRenderBabylon_GetPresentSource`); with an empty chain it samples the back buffer, as before. Later passes (bloom and depth of field in M9, SSAO in M10) add their shaders here; uniforms and their own intermediate targets come with their first user.
- **What the game reads stays what the game drew.** The chain never writes the back buffer, so `stdDisplay`'s back-buffer lock, `stdRenderBabylon_ReadBackBuffer`, savegame thumbnails, `stdDisplay_SaveScreen` and the replay's frame output read the scene before post-processing. The game reads and writes its back buffer as the surface it drew (the intro movie writes it, the 2D layer draws over it), and feeding a post-processed image back would apply the chain again each frame and make the replay frames depend on presentation settings. With an empty chain the presented image and the back buffer are the same.

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

With the same option the renderer also gets the frame's lights, for per-pixel lighting (option A L5, M10), which nothing reads yet:

- **`sithRender_Draw`**, after the visible sector and thing lists and `sithRender_BuildDynamicLights`, calls **`rdCamera_BuildStdLights`** and **`std3D_SetLights`** once per frame. The list is the rdroid camera's: the thing lights and head lights the visibility pass collected through `rdCamera_AddLight`, in that order and up to `RDCAMERA_MAX_LIGHTS` (128, equal to `STD3D_MAXLIGHTS`), so nothing new is selected. It is read only. Not in it: the sector point light `sithRender_RenderThings` adds around each model and removes after it, and the per-sector subset `sithRender_BuildDynamicLights` picks (lights whose `minRadius` reaches the sector's bounding sphere); per-pixel lighting has to apply those rules itself.
- **`StdLight`** (`Libs/std/types.h`, 48 bytes, three groups of four 32-bit values): the view-space position (the camera's view matrix applied to the light's world position) and `minRadius`; the color (`rdLight.color`, alpha as stored); `maxRadius`, the camera's `attenuationMin` and `attenuationMax`, and flags (`STD_LIGHT_ENABLED` for `rdLight.bEnabled`). These are all the inputs of `rdLight_CalcVertexIntensities` (full intensity within `maxRadius`, linear to zero at `minRadius`, times the normal's cosine), `rdLight_CalcFaceIntensity` (plane distance times `attenuationMin` subtracted, within `minRadius`, enabled lights only) and the dynamic vertex lights (distance times `attenuationMax` subtracted, within `maxRadius`, clamped to 0..1). `rdLight.type` (always 2, a point light) and `direction` are read by none of them and are left out.
- **Babylon `std3D`** copies the list into a storage buffer (made on first use: a 16-byte header with the count, then the lights as they are) in the first scene pass after it changed, before the pass, and binds it to fragment storage slot 0 of the pass's lit draws. `std3d_lit.frag.hlsl` declares no buffer yet (its compiler drops one nothing reads), so its compiled code and colour are unchanged; SDL GPU keeps the binding without passing it on. A test shader in Babylon's render tests reads a storage buffer on lavapipe. The Null `std3D` ignores the call.
- **Off**: no `std3D_SetLights`, no lit draws, no light buffer; the classic device calls exactly as before.
- **Capture**: `std3D_SetLights` records as id 36 ([RCAP](../../Formats/RCAP.md)); older captures replay unchanged.

The option is presentation, not part of the Retail or Enhanced simulation profile: the colour is the same either way, and the visibility pass, `SITH_SECTOR_SEEN` and everything else that reaches simulation state run the same code; the load-time allocation touches no world, thing or random state, so traces are identical with it on and off.

### Textures And Cache

Five DX9 texture formats are offered, each matched to a Babylon format with the same bit layout:

- `X8R8G8B8` and `A8R8G8B8` map to `Bgra8` (opaque and alpha variants)
- `R5G6B5` maps to `B5G6R5`
- `A1R5G5B5` maps to `B5G5R5A1`
- `A4R4G4B4` maps to `B4G4R4A4`

`R8G8B8` is left out because no GPU API samples 24-bit texels. Texture allocation keeps owned system-memory copies of every mip, and the cache uploads every mip whose dimensions halve as Babylon's chain expects; a level that does not ends the chain with a warning. Pixels whose `ColorInfo` already matches the format's layout are copied unchanged, others are converted through `stdColor`, and formats without alpha get alpha 255. The texture cache follows `std3DX9.c`: frame counts, the linked cache list, and a purge of old entries when a texture does not fit the device's reported free memory.

### Pipelines, Samplers And Draws

`std3D_GetPipeline` caches one pipeline per primitive and Z-write bit: three primitives (triangle list, line strip, point list) times Z write on or off. Triangles use `std3d.frag.hlsl`, lines and points the wireframe shader. All of them blend source alpha over inverse source alpha, test Z with `LessOrEqual`, write Z as `STD3D_RS_ZWRITE_DISABLED` says, and cull nothing, as `std3DX9.c` set the Direct3D state. Their second color target is the normal target (see above).

`std3D_GetSampler` caches 16 samplers, indexed by four bits worked out at each draw: linear filtering, linear mip filtering, clamp U and clamp V. It creates each one on first use. Anisotropic requests filter linearly because no device here filters anisotropically, as in `std3DX9.c`; the mip filter is linear for trilinear and nearest otherwise.

A draw streams its vertices, and for triangles its indices, into the frame's vertex and index buffers with `CommandList::updateBuffer` at growing offsets. `updateBuffer` cannot be recorded inside a pass, so the draw is streamed right away and the pass is recorded when it ends, at `std3D_EndScene` and at a `std3D_ClearZBuffer` inside a scene. The scene's Direct3D clears become the pass load operations, and the buffers start smaller than the DX9 64 MiB ones and grow when a frame needs more, destroying the old buffer after the frame's submit.

### Shaders

The game shaders (`std3d.vert.hlsl`, `std3d.frag.hlsl` and `std3d_wireframe.frag.hlsl`) are ports of the DX9 `default_vs.hlsl`, `default_ps.hlsl`, `default_wf_ps.hlsl` and `common.hlsli`, kept with their DX9 behavior; `present.*.hlsl` is the new full-screen present pass:

- The vertices are `StdVertexTL`, already transformed, with screen x and y in pixels, z in 0..1 and `rhw = 1 / w`. The vertex shader rebuilds clip space with `w = 1 / rhw`, so the GPU interpolates perspective-correctly and divides back to the same screen position and z.
- Screen position gets a half-pixel shift. Direct3D 9 puts pixel centers at integer screen coordinates, while SDL GPU (Direct3D 12, Vulkan, Metal) puts them at integer + 0.5; the shift makes the same pixels be covered and sampled.
- Colors arrive as bytes `B, G, R, A` and are swizzled with `.zyxw`, matching Direct3D 9's `D3DCOLOR`.
- The fragment color is texture times diffuse; untextured draws sample the white texture. Specular is not added, because `std3DX9.c` disabled `D3DRS_SPECULARENABLE` and its pixel shader ignored it.
- Fog is linear in `w`, as DX9 passed it: `saturate((w - start) * (1 / (end - start)))` lerps RGB to the fog color and keeps alpha.
- The alpha test is fixed-function DX9 behavior in the shader: when `STD3D_RS_ALPHAREF_SET` is set, fragments with alpha not greater than `160 / 255` are discarded (`D3DCMP_GREATER`).
- Lines and points use the wireframe shader's constant opaque white times diffuse, with no texture and no fog.
- Both fragment shaders write the constant normal `(0, 0, 1)`, encoded, to the normal target (`SV_Target1`): `StdVertexTL` carries no normal. The lit shaders (`std3d_lit.*.hlsl`) write the interpolated one (see [Lit Vertices](#lit-vertices-m69b)).

The vertex shader's outputs are ordered so the pixel shader's inputs are a prefix of them: diffuse, texture coordinate and `w` come first and the unread specular color last. Direct3D 12 links a pixel shader only when its inputs match the vertex outputs in the same registers, and the DXIL comes through SPIR-V, which drops inputs a pixel shader never reads; the earlier specular gap made D3D12 refuse every game pipeline with `E_INVALIDARG`.

## Babylon stdDisplay Backend (M6.7)

The Babylon column's `stdDisplay` backend ([`stdDisplayBabylon.cpp`](../../../Libs/std/Babylon/stdDisplayBabylon.cpp)) implements the same `stdDisplayBackend_*` functions as the removed DirectX 9 backend, ported from `stdDisplayDX9.c`, with the capture forwarders of `stdDisplayCapture.c` still in front ([ADR-0024](../09-decisions/ADR-0024-render-capture-by-renamed-backends.md)). It drives the frame owner in [`stdRenderBabylon.cpp`](../../../Libs/std/Babylon/stdRenderBabylon.cpp) that `std3D` also draws through: `stdDisplay_Open` opens it and `stdDisplay_Close` closes it. The game-facing functions from `stdDisplay.h` keep their recovered names and answers.

### Video Modes And SetMode

One display device stands for the render device, named "Jones3D Babylon Display" with driver "stdDisplayBabylon"; its video memory is the largest texture's area times four, half of it reported free. Babylon's platform has no display-mode query, so the list is fixed. A headless run lists the default 640x480 in RGB565 (`stdColor_cfRGB565`) and X8R8G8B8 (`stdColor_cfRGB8888`) only: what `stdDisplayDX9.c` listed on a 640x480 display, such as the M6.1 DX9 recordings' Xvfb screen, so their fullscreen SetMode of mode 1 replays at the recorded size. A windowed run lists 16 fixed sizes (320x200, 320x240, 400x300, 512x384, 640x400, 640x480, 800x600, 1024x768, 1152x864, 1280x720, 1280x960, 1280x1024, 1600x900, 1600x1200, 1920x1080 and 1920x1200) in both formats, plus the window's own size when it fits the device limit, sorted by bit depth, width and height. All list 60 Hz; aspect ratio is 1 except the non-square windowed 320x200, 320x400 and 640x400 modes at 0.75, and a headless mode uses width/height. Before `stdDisplay_SetMode` the back buffer already reports 640x480 X8R8G8B8, as in the Null backend.

`stdDisplay_SetMode` takes a copy of the enumerated mode for fullscreen; a windowed mode uses the primary mode at `stdDisplay_SetDefaultResolution`'s size in X8R8G8B8. It neither switches the window to fullscreen nor resizes it: the game sizes the window to the back buffer afterwards, and the present scales the back buffer to the swapchain. The mode creates the back buffer through `stdRenderBabylon_SetBackBufferSize` and makes `stdDisplay_g_frontBuffer` and `stdDisplay_g_backBuffer` hardware VBuffers of that size without CPU surfaces, fills the buffer, presents it once, and fills it again for fullscreen. `stdDisplay_ClearMode` calls the release callback and frees the back buffer.

### Back Buffer, Locks And Fills

The back buffer lives on the GPU in `stdRenderBabylon`'s color target, where `std3D` draws. Its CPU side is a copy in the video mode's pixel format that `stdDisplay_LockBackBuffer` hands out, as DX9 handed out its lockable surface. A lock reads the GPU back buffer into the copy only when the GPU changed it since the copy was last read or written, tracked by `stdRenderBabylon_GetBackBufferVersion`; the last unlock of a write lock uploads the copy after the frame recorded so far (`stdRenderBabylon_WriteBackBuffer`), and a read-only lock uploads nothing. This is what makes the 2D layer draw correctly over the 3D frame: savegame thumbnails (whose GDI surface is still missing in this column, so saving is refused; see [chapter 11](../11-risks-and-technical-debt.md)) and `stdDisplay_SaveScreen` read the frame that `std3D` drew, debug lines draw over the frame, and the intro movie (`JonesMain_IntroMovieBlt*`) locks, writes every pixel, unlocks and presents, so it never reads back after its first frame. A read-only lock cannot be upgraded to a write lock, and a fill while the back buffer is locked fails. Fills of the whole back buffer are clear passes; fills of part of it go through the CPU copy and an upload. `stdDisplay_VBufferNew` always allocates software VBuffers because the render device has no lockable surfaces; locks of the front or back buffer lock the back buffer, other hardware VBuffers fail, and `stdDisplay_EncodeFromRGB565` encodes into the current mode's channels.

### Submit And Present

`stdDisplay_Update` ends the frame. It advances one millisecond of virtual time, as the Null backend does, so headless loops that wait on the clock around a present finish; then `stdRenderBabylon_PresentFrame` submits what `std3D` and the display recorded and, on a device with a swapchain, records the present pass and presents. Without a mode it returns 1 and does nothing. `std3D_EndScene` now leaves the frame open instead of submitting it, so submit and present sit at `stdDisplay_Update`, as `stdDisplayDX9.c` presented.

### Reset Callbacks

`stdDisplay_RegisterDevicePreResetCallback` and `stdDisplay_RegisterDevicePostResetCallback` store their callbacks but nothing calls them, because the render device is never lost or reset; only the release callback runs, from `stdDisplay_ClearMode`. That gap is listed in [chapter 11](../11-risks-and-technical-debt.md).

## Where OpenJones3D Extends The Original Renderer

The biggest architectural extensions were:

- DirectX 9 backend alongside the original DirectX 6.1c backend (both removed in M6.3)
- shader layer for the DX9 path (removed with it)
- VBO/IBO support in the DX9 path (removed with it)
- wider texture-format handling, including true-color textures
- breadth-first off-screen thing/light traversal
- longer default collection distances
- modern filtering options, automatic mipmap generation, and MSAA support in the lower graphics layer (removed with DX9)

Crucially, those changes sit underneath or alongside the original scene-graph/sector-portal logic instead of replacing the entire renderer design. The renderer still thinks in terms of sectors, surfaces, adjoins, clip frusta, thing sectors, and per-vertex dynamic lights.
