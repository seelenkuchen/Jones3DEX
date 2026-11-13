# Babylon Engine

Babylon is the game-agnostic platform framework that the restored Jones3D EX game logic
runs on, in the model of the Kex engine. It is C++20, licensed under the Mozilla Public
License 2.0 ([LICENSE](LICENSE)), and builds both alone and as a subdirectory of the
Jones3D EX repository.

## The boundary

Nothing in `Babylon/` includes or names anything outside `Babylon/`: no Jones3D or Sith
concepts reach it. All dependencies are permissive (zlib, MIT, BSD, Apache-2.0, public
domain) and recorded in [third_party/README.md](third_party/README.md). Decisions:
ADR-0006, ADR-0008 and ADR-0015 to ADR-0017 in `../Docs/arc42/09-decisions/`.

## Layout

Each module hides one platform decision and is one CMake static-library target
(ADR-0015):

    <module>/
        include/babylon/<module>/   public headers, the only thing other modules see
        src/                        private implementation
        backends/<backend>/         per-backend code
        tests/                      doctest unit tests

`core` (version, assert) is the first module, `log` (the global logging API with
pluggable sinks) the second. `files` hides the host file system: file access,
directory listing, base path and per-user writable path (ADR-0019), with an
SDL3 backend and an in-memory Null backend. `platform` hides the operating
system layer: windows and one polled queue of OS events (ADR-0009), with the
same two backends. `input` has no backend of its own: `InputState` folds the
polled events into the state a game reads per frame, and `InputScript` replays
recorded events so headless runs see exactly what a device would (ADR-0018).
`cvars` holds named, typed settings with defaults, ranges, flags and change
callbacks (ADR-0013, ADR-0020); a `Registry` is an ordinary object owned by the
composition root, never a global. `font` loads a TrueType file from bytes and
bakes the glyphs a caller asks for into an 8-bit coverage atlas with metrics
and kerning; `image` decodes PNG bytes into RGBA8. Both are CPU-only and read no
files themselves (ADR-0031).

`ui` is the game-agnostic menu toolkit (ADR-0026): a screen stack, declarative
widgets (labels, images, buttons, toggles, sliders, choices, lists, binding
capture and text entry), automatic layout on a canvas 480 units high and
480 * aspect units wide, keyboard/mouse/gamepad focus, cvar-bound values and
one polled event queue. It draws through the `Painter` interface, so the
module never depends on `render`; `NullPainter` records draw calls for tests,
and the game's render-backed painter lives in `Jones3D/Platform/Babylon/`
(M7.9).

`render` hides the GPU (ADR-0025): a `Device` owns textures, buffers, samplers
and pipelines behind typed handles, a frame is recorded into a `CommandList`
(passes, bindings, draws, copies) and submitted once, and `Device::submit`
checks every command against the live resources the same way for every
backend. The Null backend keeps contents in memory, so uploads, clears, copies
and readback are real, and records what it executed for tests. The SDL GPU
backend (`createSdlGpuDevice`) runs on Vulkan, Direct3D 12 or Metal, headless
or on a window of the SDL platform, whose `SDL_Window` it gets through
`platform::sdlWindowOf`; the contract tests run against it too, on Mesa's
lavapipe where there is no GPU, and report it as skipped without a driver.

`audio` hides the sound output (ADR-0010): a `Device` lists and opens output
devices, owns immutable sounds and typed voices behind handles, applies gain,
pan, pitch, per-voice 3D position, distance attenuation and a listener, and
feeds streaming PCM blocks from the caller. Device loss is a polled event, and
sounds, voices and their settings survive closing, reopening and switching
outputs. The Null backend keeps the whole state without a device and advances
playback only through a test helper. The miniaudio backend
(`createMiniaudioDevice`, M7.2) mixes the voices itself on a low-level
`ma_device`; an option runs it on miniaudio's own null output, so the contract
tests run every case on both backends without a sound card. The null backend
has no controllable clock, so the cases that step a voice forward report a
skip there; `backends/miniaudio/tests/` covers play-to-end and stream
underrun on the host clock instead.

## Declaring a module

    babylon_add_module(<name>
        SOURCES <files...>
        [DEPS <babylon modules...>]          # private dependencies
        [PUBLIC_DEPS <babylon modules...>]   # dependencies in public headers
        [LIBS <external targets...>])        # non-Babylon targets, linked PRIVATE

Creates the library `babylon_<name>` (alias `babylon::<name>`) with C++20, Babylon's
warning flags and, with `BABYLON_SANITIZE=ON`, address and undefined behavior sanitizer
flags. Dependencies must name modules added before this one, so the target graph stays
acyclic. `LIBS` names targets outside Babylon, such as `${BABYLON_SDL3_TARGET}` from
`cmake/BabylonSdl3.cmake`; an unknown target is a configure error.

    babylon_add_module_tests(<name>
        SOURCES <files...>
        [DEPS <babylon modules...>])        # modules the tests use directly

Links `babylon_<name>` against the shared doctest main and registers the test
`babylon.<name>` with the labels `babylon;unit`. An optional `DEPS` module is
checked like a module dependency and linked PRIVATE to the test executable,
for modules the tests call that the tested module keeps private, such as
`log` when a test attaches a `CaptureSink`; such a link adds no edge to the
module graph.

## Shaders

    babylon_add_shaders(<target>
        SOURCES <name>.vert.hlsl | <name>.frag.hlsl ...
        [NAMESPACE <c++ namespace>]          # default babylon::shaders
        [PUBLIC_HEADER])

It compiles HLSL at build time through SDL_shadercross into
SPIR-V, DXIL and MSL ([ADR-0022](../Docs/arc42/09-decisions/ADR-0022-shaders-compiled-at-build-time.md)).
Each file holds one stage and is named `<name>.vert.hlsl` or `<name>.frag.hlsl`; its entry point is
`main`. The generated header, `#include <babylon/shaders/<target>.h>`, declares one function per file
(named in camel case, so `my_tint.frag.hlsl` becomes `myTintFrag()`) that returns a
`const babylon::render::ShaderDesc&`. Textures and samplers use the register spaces SDL GPU expects:
vertex textures in space0 and vertex uniform blocks in space1, fragment textures in space2 and fragment
uniform blocks in space3, slots from 0. Shaders must not `#include` other files: the cache key covers
only the file itself.

It can be called from any directory of a project that added Babylon. The generated header directory is
PRIVATE to `<target>`; `PUBLIC_HEADER` makes it PUBLIC, for a library of shaders that other targets link
and include.

`BABYLON_BUILD_SHADERS` (default ON) builds the toolchain and the shaders; OFF skips both, and
`babylon_add_shaders()` becomes a configure error. `BABYLON_CACHE_DIR` (default `.cache/` under the
top-level source directory) holds the toolchain under `shadercross/` and the compiled shaders under
`shaders/`. `BABYLON_SHADERCROSS` names an existing executable to use instead of building one, and
`BABYLON_DXC_ROOT` a DXC install in the layout of its release packages for hosts without a prebuilt one.

## Build and test alone

    cmake -S Babylon -B build/babylon -G Ninja
    cmake --build build/babylon
    ctest --test-dir build/babylon --output-on-failure

In the repository build, `JONES3D_BUILD_BABYLON` gates the subdirectory and
`JONES3D_BUILD_TESTS` and `JONES3D_SANITIZE` drive the Babylon equivalents.

## Checks

`tools/check_boundary.py` enforces the boundary: every `#include` resolves to
Babylon, a C++20 standard header or a third-party header at an allowed
location, public headers stay babylon/standard, and no game name appears in
code or CMake files. It also checks that `third_party/README.md` lists every
vendored directory and FetchContent dependency under a permissive license
(ADR-0008). `tools/check_graph.py` compares a `--graphviz` configure
of the standalone build against `allowed-deps.txt`: each module's direct
dependencies must match exactly and the graph must be acyclic. Both run as
ctest tests labelled `check` (plus a self-test of the checks) and in
`.github/workflows/babylon.yml`. Changing `allowed-deps.txt` is an
architecture change and needs an ADR (ADR-0015).

Design rules (naming, interfaces, patterns):
`../Docs/arc42/08-crosscutting/Babylon-Design-Rules.md`.
