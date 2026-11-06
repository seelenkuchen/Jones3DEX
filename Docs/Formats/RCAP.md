# RCAP File Format

This document describes the binary `.rcap` render-capture format.

In the current codebase the writer is [`stdRenderCapture.c`](../../Libs/std/General/stdRenderCapture.c)
(header, records, blobs, globals, handles) and the two forwarding layers
[`std3DCapture.c`](../../Libs/std/General/std3DCapture.c) and
[`stdDisplayCapture.c`](../../Libs/std/General/stdDisplayCapture.c) build the record payloads. The
record ids live in [`stdRenderCaptureIds.h`](../../Libs/std/General/stdRenderCaptureIds.h), the
open/close control in [`stdDisplayCapture.h`](../../Libs/std/General/stdDisplayCapture.h).

## Purpose

A render capture records every call the game makes to the 84 renderer functions (`std3D_*` and
`stdDisplay_*`): the arguments, the return value and the recorded out-parameters. A later replay
tool reads the file and drives a renderer backend with the same calls, so a new backend can be
checked against the old one call by call.

This is a tool format, not a game asset. Nothing in the game reads it.

## Who Writes And Reads

- Writer: `Jones3D-headless --capture <file>`
  ([`JonesHeadless.c`](../../Jones3D/Platform/Null/JonesHeadless.c)); the Babylon column writes the
  `babylon` column name and the Null column writes `null`. The windowed `Jones3D` does not parse
  `-capture` yet, so nothing writes the `dx9` column name that the removed DX9 build used. The
  console command `capture frames on|off` ([`JonesConsole.c`](../../Jones3D/Display/JonesConsole.c))
  still switches the frame window.
- Reader: the replay tool (M6.0c).

The game remembers the request in `JonesMain_SetCapture()`
([`JonesMain.c`](../../Jones3D/Main/JonesMain.c)) and opens the capture in `JonesMain_Startup()`,
after the host services exist and before the first renderer call. It closes the capture in
`JonesMain_Shutdown()`, before the host services go away.

The header carries the name of the column that wrote the file: `null` or `babylon`. Captures written by
the removed DirectX 9 build carry `dx9`.

## Replay

The replay tool is `Jones3D-replay` (plain Null column). It reads a capture and makes the recorded
calls again through the public renderer functions, so a capture written during the replay has the
same bytes as the original:

```bash
<build>/Libs/std/Jones3D-replay capture.rcap [--capture replay.rcap]
```

It exits `0` when the replay finishes, `1` for a malformed or inconsistent input file (or a file
that cannot be opened), and `2` for wrong arguments. Records the replay does not reproduce (the
skipped calls and unknown record ids) are counted as skipped and make the output differ; a call
that returns different values is counted as a mismatch.

## Header

16 bytes, then the column name without a NUL terminator:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 8 | signature `J3DRCAP\0` (`4A 33 44 52 43 41 50 00`) |
| 8 | 4 | version, u32 = 1 |
| 12 | 4 | column length in bytes, u32 (1 to 64) |
| 16 | column length | column name bytes |

The file is created or truncated when the capture opens. All integers are little-endian and are
packed byte by byte; the writer never writes a struct.

## Records

Every record starts with an 8-byte header:

| Size | Value |
| --- | --- |
| 2 | record id, u16 |
| 2 | reserved, always 0 |
| 4 | payload size in bytes, u32 |

The payload follows immediately. A payload of size 0 is legal: the record is then only its header.

Game records use ids 1 to 34 (`std3D_*`) and 101 to 150 (`stdDisplay_*`). Four ids are reserved
by the writer:

| Id | Record | Payload |
| --- | --- | --- |
| `0xFFF0` | BLOB | u32 blob id, u64 size, then `size` bytes of content |
| `0xFFF1` | GLOBAL | u32 global id, u32 value bits |
| `0xFFF2` | FRAMES | 1 byte, 0 or 1 |
| `0xFFF3` | VBSTATE | u32 VBuffer handle, then the full tRasterInfo: u32 width, u32 height, u64 size, u64 rowSize, u64 rowWidth, ColorInfo |

A reader skips a record id it does not know by using the payload size in the header. Ids are
never renumbered; a new renderer function is added at the end of its range.

## Handles

Pointers are not written to the file. The writer gives every live object a handle:

| Kind | What it identifies |
| --- | --- |
| VBuffer | `tVBuffer` objects |
| System texture | `tSystemTexture` objects |

Rules:

- Per kind the ids start at 1, count up in call order and are never reused inside a capture, even
  after a release. They reset on every open.
- 0 means a NULL pointer.
- VBuffer handle 1 is the back buffer. `stdDisplayCapture_Open()` registers `&stdDisplay_g_backBuffer`
  as handle 1 before anything else; if it does not become handle 1 the open fails.
- A pointer first seen in a call gets the next free number of its kind. An unknown VBuffer is a
  warning once per capture.
- `std3D_ClearSystemTexture` and `stdDisplay_VBufferFree` release their handle after the record, so
  a later call with the same pointer gets a new number.
- `stdDisplay_VBufferConvertColorFormat` releases the source handle before recording the result
  when it returns a different, non-NULL buffer: the backend frees the source, so the source's
  handle ends and the result gets a new number.
- The `texHandle` of `std3D_DrawRenderList` is the handle of the live system texture whose
  `pCachedTexture` equals the passed pointer. An unknown pointer is recorded as `0xFFFFFFFF`,
  warned once per capture.

## VBuffer State

Game code can change a VBuffer's `rasterInfo` directly between renderer calls, without telling the
renderer. `sithMaterial.c` does this when it reuses a converted mip buffer for the next cel: it
writes a new `colorInfo`, `rowSize` and `size` into the buffer, then locks, fills and converts it
again. A call's arguments alone would not reproduce that state on replay.

Before every recorded call that takes an existing VBuffer argument (`stdDisplay_VBufferLock`,
`stdDisplay_VBufferLockReadOnly`, `stdDisplay_VBufferUnlock`, `stdDisplay_VBufferFill`,
`stdDisplay_VBufferConvertColorFormat`'s source, and each mip of
`std3D_AllocSystemTexture`), the forwarder writes a VBSTATE record when the buffer's current
`rasterInfo` differs from the last VBSTATE written for that handle in this capture, or when none has
been written yet. The comparison covers every encoded `tRasterInfo` field: width, height, size,
rowSize, rowWidth and the 14 `ColorInfo` fields. The back buffer follows the same rule.
`stdDisplay_VBufferFree` is not included, and VBSTATE records are written before the call's blobs.

On replay, VBSTATE sets the referenced buffer's `rasterInfo` to the recorded values. For each
replayed VBuffer the replay remembers the pixel allocation size it was created with (`height *
rowSize` after `stdDisplay_VBufferNew` or a different result from
`stdDisplay_VBufferConvertColorFormat`; for the back buffer, its `rasterInfo.size` when first seen).
A VBSTATE whose `height * rowSize` exceeds that allocation is malformed and fails the replay.

## Blobs

Pixel content is written once as a BLOB record and referenced by its id. Blob ids start at 1,
count up, are never reused and reset on every open.

Content is deduplicated by size and two independent 64-bit hashes: FNV-1a and CRC-64/XZ (reflected
polynomial `0xC96C5795D7870F42`, all-ones init and final xor). When the size and both hashes match,
the existing blob id is returned and nothing is written; the capture is never read back. A collision
of both hashes for different bytes of the same size is accepted as practically impossible.

Pixel data is recorded at:

- `stdDisplay_VBufferUnlock`, when the unlock takes the buffer from locked (`lockRefCount` 1) to
  unlocked: `height * rowSize` bytes.
- `stdDisplay_VBufferConvertColorFormat`: the source buffer, before the backend call.
- `std3D_AllocSystemTexture`: one blob per mip level VBuffer, before the backend call.
- `stdDisplay_UnlockBackBuffer`: the pixels of the remembered back-buffer lock, `height * pitch`.

A VBuffer without `pPixels` is read through a read-only backend lock; that lock itself is not
recorded. A non-hardware buffer without pixels, a failed lock, a size overflow or a back-buffer
lock with no positive pitch gives blob 0.

`stdDisplay_LockBackBuffer` and `stdDisplay_LockBackBufferReadOnly` remember the pixels they hand
out even when nothing records, so `stdDisplay_UnlockBackBuffer` can capture them.

## Globals

A GLOBAL record carries a value that many calls depend on, written once when it changes.

- Global ids are 1 to 8. Today only 1 is used: the fog density (`std3D_g_fogDensity`) as the bits
  of an f32.
- A value is written only when it differs from the last value written for that id in this capture.
  The first value for an id always writes. An equal value writes nothing.
- GLOBAL 1 is written before every recorded `std3D_*` call, so it carries the density the backend
  sees before that call. `stdDisplay_*` calls write no global.

## The Frame Window

The frames flag decides whether the 13 frame-class functions record. It defaults to on and
survives open and close.

- While it is on, the frame-class functions record; while it is off they forward without recording.
  All other functions record whenever a capture is open.
- A FRAMES record follows every change of the flag while a capture is open. When the flag is off
  when the capture opens, a FRAMES record with value 0 follows the header.
- The 13 frame-class functions are: `std3D_StartScene`, `std3D_EndScene`, `std3D_DrawRenderList`,
  `std3D_DrawLineStrip`, `std3D_DrawPointList`, `std3D_UpdateFrameCount`, `std3D_ClearZBuffer`,
  `stdDisplay_Update`, `stdDisplay_GetBackBufferSize`, `stdDisplay_BackBufferFill`,
  `stdDisplay_LockBackBuffer`, `stdDisplay_LockBackBufferReadOnly` and
  `stdDisplay_UnlockBackBuffer`.

The headless runner leaves the flag on, so it records from start to end. The Windows entry
turns it off before opening the capture, and the player switches it on for the scene to record
with `capture frames on`, then off again with `capture frames off`.

## Recording Order

For each call:

1. A `std3D_*` call writes GLOBAL 1 first, when the fog density changed.
2. VBSTATE records for the VBuffer arguments whose raster state changed come next.
3. Handles are resolved and blobs are written, while the caller and the backend still hold the data.
4. The backend function runs.
5. The record is built and written with the arguments, the return value and the out-parameters.

Handles released by the call (`std3D_ClearSystemTexture`, `stdDisplay_VBufferFree`) are released
after the record. `stdDisplay_UnlockBackBuffer` writes its blob before the backend call and
forgets the remembered lock whether or not it records.

A failed write, seek or read logs an error and closes the capture. The rest of the run records
nothing.

## Determinism

The bytes depend only on the sequence of calls and their data. Handles replace pointers, blobs are
deduplicated by content and globals by value, and nothing writes wall-clock time, thread timing,
uninitialized memory or pointer values. Two runs of the same game data in the same column give
identical files.

## Argument Encodings

Payload fields are written with these helpers:

| Helper | Encoding |
| --- | --- |
| `u8` | 1 byte |
| `bool` | 1 byte, 0 or 1 |
| `u16` | 2 bytes, little-endian |
| `u32` | 4 bytes, little-endian |
| `i32` | 4 bytes, little-endian two's complement |
| `u64` | 8 bytes, little-endian |
| `f32` | the IEEE-754 binary32 bits of the float as a u32, little-endian |
| `str` | u32 length, then the bytes without a NUL; a NULL pointer is only the u32 `0xFFFFFFFF` |

Composite fields, as built by [`stdDisplayCapture.c`](../../Libs/std/General/stdDisplayCapture.c):

| Name | Encoding |
| --- | --- |
| `opt(X)` | u8 0 for a NULL pointer (nothing follows) or u8 1 followed by X |
| `colorInfo` | i32 colorMode, u32 bpp, u32 redBPP, u32 greenBPP, u32 blueBPP, i32 redPosShift, i32 greenPosShift, i32 bluePosShift, i32 redPosShiftRight, i32 greenPosShiftRight, i32 bluePosShiftRight, u32 alphaBPP, i32 alphaPosShift, i32 alphaPosShiftRight |
| `rasterIn` | u32 width, u32 height, colorInfo. size, rowSize and rowWidth are left out: the game leaves them uninitialized |
| `rasterOut` | u32 width, u32 height, u64 size, u64 rowSize, u64 rowWidth, colorInfo |
| `rect` | i32 left, i32 top, i32 right, i32 bottom |
| `colorKey` | u32 dwColorSpaceLowValue, u32 dwColorSpaceHighValue |
| `videoMode` | f32 aspectRatio, rasterOut, u32 refreshRate |
| `verts` | u32 count, then per vertex: f32 sx, f32 sy, f32 sz, f32 rhw, u32 color, u32 specular, f32 tu, f32 tv |
| `indices` | u32 count, then count times u16 |

A NULL array or a count above `UINT32_MAX` records count 0.

## Record Ids

The payload is listed in the order it is written: arguments, return value, recorded
out-parameters. `Frame` marks the frame-class functions. `Replay` marks the calls the replay
tool does not reproduce: `skip` for the calls it drops, `pair` for the two calls it replays
together.

| Id | Function | Payload (in order) | Frame | Replay |
| --- | --- | --- | --- | --- |
| 1 | `std3D_Startup` | i32 result | | |
| 2 | `std3D_Shutdown` | | | |
| 3 | `std3D_GetCurrentDevice` | u8 result != NULL | | |
| 4 | `std3D_GetNumDevices` | u64 result | | |
| 5 | `std3D_GetAllDevices` | u8 result != NULL | | |
| 6 | `std3D_SetFindAllDevices` | i32 bFindAll | | |
| 7 | `std3D_Open` | u64 deviceNum, i32 result | | |
| 8 | `std3D_Close` | | | |
| 9 | `std3D_GetTextureFormat` | i32 type | | |
| 10 | `std3D_GetColorFormat` | opt(colorInfo) pCi, i32 result | | |
| 11 | `std3D_GetNumTextureFormats` | u64 result | | |
| 12 | `std3D_StartScene` | i32 result | yes | |
| 13 | `std3D_EndScene` | | yes | |
| 14 | `std3D_DrawRenderList` | u32 texHandle, i32 rdflags, verts, indices | yes | |
| 15 | `std3D_DrawLineStrip` | verts | yes | |
| 16 | `std3D_DrawPointList` | verts | yes | |
| 17 | `std3D_SetRenderState` | i32 rdflags | | |
| 18 | `std3D_SetWireframeRenderState` | | | |
| 19 | `std3D_AllocSystemTexture` | u32 textureHandle, u32 num, num times (u32 vbufferHandle, u32 blobId), i32 formatType | | |
| 20 | `std3D_GetValidDimensions` | u32 width, u32 height, u32 outWidth, u32 outHeight | | |
| 21 | `std3D_ClearSystemTexture` | u32 textureHandle | | |
| 22 | `std3D_AddToTextureCache` | u32 textureHandle, i32 format | | |
| 23 | `std3D_ResetTextureCache` | | | |
| 24 | `std3D_GetMipMapCount` | u32 textureHandle, u64 result | | |
| 25 | `std3D_UpdateFrameCount` | u32 textureHandle | yes | |
| 26 | `std3D_FindClosestFormat` | opt(colorInfo) pMatch, u64 result | | |
| 27 | `std3D_SetMipmapFilter` | i32 mode, i32 result | | |
| 28 | `std3D_SetProjection` | f32 fov, f32 nearPlane, f32 farPlane, i32 result | | |
| 29 | `std3D_EnableFog` | i32 bEnabled, f32 density | | |
| 30 | `std3D_SetFog` | f32 red, f32 green, f32 blue, f32 startDepth, f32 endDepth | | |
| 31 | `std3D_ClearZBuffer` | | yes | |
| 32 | `std3D_BuildDisplayEnvironment` | u8 result != NULL | | pair |
| 33 | `std3D_FreeDisplayEnvironment` | u8 pEnv != NULL | | pair |
| 34 | `std3D_ResetGlobals` | | | |
| 101 | `stdDisplay_Startup` | i32 result | | |
| 102 | `stdDisplay_Shutdown` | | | |
| 103 | `stdDisplay_Open` | u64 deviceNum, i32 result | | |
| 104 | `stdDisplay_IsOpen` | bool result | | |
| 105 | `stdDisplay_Close` | | | |
| 106 | `stdDisplay_GetAdapterNum` | u64 deviceNum, u64 result | | |
| 107 | `stdDisplay_GetSystemDevice` | u8 result != NULL | | |
| 108 | `stdDisplay_CreateZBuffer` | u8 pPixelFormat != NULL, i32 bSystemMemory, i32 result | | |
| 109 | `stdDisplay_DisableVSync` | bool bDisable | | |
| 110 | `stdDisplay_Update` | i32 result | yes | |
| 111 | `stdDisplay_Refresh` | i32 bReload | | |
| 112 | `stdDisplay_RegisterDevicePreResetCallback` | u8 pCallback != NULL | | skip |
| 113 | `stdDisplay_RegisterDevicePostResetCallback` | u8 pCallback != NULL | | skip |
| 114 | `stdDisplay_RegisterDeviceReleaseCallback` | u8 pCallback != NULL | | skip |
| 115 | `stdDisplay_GetNumDevices` | u64 result | | |
| 116 | `stdDisplay_GetDevice` | u64 deviceNum, u8 pDest != NULL, i32 result | | |
| 117 | `stdDisplay_GetCurrentDevice` | u8 pDevice != NULL, i32 result | | |
| 118 | `stdDisplay_GetAllDevices` | u8 result != NULL | | |
| 119 | `stdDisplay_SetMode` | u64 modeNum, i32 bFullscreen, u64 numBackBuffers, i32 result | | |
| 120 | `stdDisplay_ClearMode` | | | |
| 121 | `stdDisplay_GetNumVideoModes` | u64 result | | |
| 122 | `stdDisplay_GetVideoMode` | u64 modeNum, i32 result, opt(videoMode) pDestMode | | |
| 123 | `stdDisplay_GetCurrentVideoMode` | i32 result, opt(videoMode) pDisplayMode | | |
| 124 | `stdDisplay_VBufferNew` | opt(rasterIn) pRasterInfo, i32 bUseVSurface, i32 bUseVideoMemory, u32 handle | | |
| 125 | `stdDisplay_VBufferFree` | u32 handle | | |
| 126 | `stdDisplay_VBufferLock` | u32 handle, i32 result | | |
| 127 | `stdDisplay_VBufferLockReadOnly` | u32 handle, i32 result | | |
| 128 | `stdDisplay_VBufferUnlock` | u32 handle, u32 blobId, i32 result | | |
| 129 | `stdDisplay_VBufferFill` | u32 handle, u32 dwFillColor, opt(rect) pRect, i32 result | | |
| 130 | `stdDisplay_VBufferConvertColorFormat` | opt(colorInfo) pDesiredColorFormat, u32 srcHandle, u32 srcBlobId, i32 bColorKey, opt(colorKey) pColorKey, u32 resultHandle; a result different from the source ends the source handle (the backend frees it) | | |
| 131 | `stdDisplay_GetTextureMemory` | i32 result | | |
| 132 | `stdDisplay_GetTotalMemory` | i32 result | | |
| 133 | `stdDisplay_GetBackBufferSize` | u32 width, u32 height | yes | |
| 134 | `stdDisplay_BackBufferFill` | u32 color, opt(rect) lpRect, i32 result | yes | |
| 135 | `stdDisplay_SaveScreen` | str pFilename, i32 result | | skip |
| 136 | `stdDisplay_SetDefaultResolution` | u32 width, u32 height | | |
| 137 | `stdDisplay_GetFrontBufferDC` | u8 result != NULL | | skip |
| 138 | `stdDisplay_ReleaseFrontBufferDC` | u8 hdc != NULL | | skip |
| 139 | `stdDisplay_GetBackBufferDC` | u8 result != NULL | | skip |
| 140 | `stdDisplay_ReleaseBackBufferDC` | u8 hdc != NULL | | skip |
| 141 | `stdDisplay_FlipToGDISurface` | i32 result | | |
| 142 | `stdDisplay_SetBufferClipper` | i32 bFrontBuffer, i32 result | | |
| 143 | `stdDisplay_RemoveBufferClipper` | i32 bFrontBuffer, i32 result | | |
| 144 | `stdDisplay_CanRenderWindowed` | i32 result | | |
| 145 | `stdDisplay_IsFullscreen` | i32 result | | |
| 146 | `stdDisplay_LockBackBuffer` | i32 result, u32 width, u32 height, i32 pitch | yes | |
| 147 | `stdDisplay_LockBackBufferReadOnly` | i32 result, u32 width, u32 height, i32 pitch | yes | |
| 148 | `stdDisplay_UnlockBackBuffer` | u32 blobId | yes | |
| 149 | `stdDisplay_EncodeFromRGB565` | u16 pixel, u32 result | | |
| 150 | `stdDisplay_ResetGlobals` | | | |

Out-parameters are recorded only where the payload lists them. `std3D_GetTextureFormat`,
`stdDisplay_GetTextureMemory` and `stdDisplay_GetTotalMemory` record no out-parameters.
`std3D_AllocSystemTexture` records num 0 when `apVBuffers` is NULL or `numMipLevels` is above
`UINT32_MAX`.

The four diagnostic functions still declared in `std3D.h` (`std3D_IsShaderSystemActive`,
`std3D_IsAnisotropicFilteringSupported`, `std3D_IsMipmapAutoGenSupported`, `std3D_IsMSAASupported`)
are not part of the stream; no backend defines them since M6.3.
