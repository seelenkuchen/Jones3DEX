<a name="testing"></a>

# 🧪 Testing

Jones3D EX uses Unity with Unity Fixture for C tests and CTest as the test runner.
Tests are enabled by default through `JONES3D_BUILD_TESTS`. Babylon's C++ tests
live in `Babylon/`; their framework is chosen in milestone M5. Behavior of the
whole game is checked with [headless traces](#trace-tests). Contributors and
LLM agents should follow the same workflow and coverage requirements below.

Much of this guide comes from OpenJones3D. The Win32 and DirectX build it
once described was removed in milestone M6.3
([ADR-0011](arc42/09-decisions/ADR-0011-retire-win32-directx.md)). The game now
builds in two columns: the Null column (non-Windows) and the Babylon column
(Linux, macOS and 64-bit Windows); see [Running Tests](#running-tests).

<a name="contents"></a>

## 📑 Contents

- [🔄 Testing Workflow](#testing-workflow)
- [🎯 Coverage Requirements](#coverage-requirements)
    - [🐛 Regression Proof](#regression-proof)
    - [📐 Numerical Comparisons](#numerical-comparisons)
    - [📊 Coverage Reports](#coverage-reports)
- [🗂️ Test Layout](#test-layout)
- [✍️ Test Style](#test-style)
    - [🧹 Test Isolation And Cleanup](#test-isolation-and-cleanup)
- [🧭 Test Scope](#test-scope)
    - [🎞️ Trace Tests](#trace-tests)
    - [🎥 Render Capture](#render-capture)
    - [🖼️ Renderer Reference Masks](#renderer-reference-masks)
    - [🖥️ System And Device Tests](#system-and-device-tests)
        - [🧪 Babylon Render Contract Tests](#babylon-render-contract-tests)
- [📦 Original-Engine I/O Test Vectors](#original-engine-io-test-vectors)
- [▶️ Running Tests](#running-tests)
    - [🐧 Native Unit Builds](#native-unit-builds)
    - [🪟 Windows Unit Builds](#windows-unit-builds)
    - [🤖 CI Columns](#ci-columns)
    - [🔎 Intermittent Failures](#intermittent-failures)
    - [⏱️ Timeouts And Shared Devices](#timeouts-and-shared-devices)
- [📋 Validation Report](#validation-report)
- [🚧 Planned Test Integrations](#planned-test-integrations)
    - [🛡️ AddressSanitizer In CI](#deferred-addresssanitizer-integration)

<a name="testing-workflow"></a>

## 🔄 Testing Workflow

Define the contract, establish expected results, add focused tests, build and run
them, then report the results and remaining gaps.

1. **Identify the contract.** Read the owning module and its existing tests.
   Decide whether the change preserves original behavior or introduces a fix,
   runtime guard, or QOL behavior. Check the actual CMake targets and CTest
   entries; descriptions marked **planned** do not establish that a suite exists.
2. **Choose cases and expected results.** Cover the success and failure paths
   listed in [Coverage Requirements](#coverage-requirements). For reconstructed
   I/O, follow the [original-engine capture procedure](#generation-procedure)
   and [reference-version policy](#reference-binaries-and-version-policy).
   For fixes, plan [regression proof](#regression-proof); document any numerical
   tolerance using [Numerical Comparisons](#numerical-comparisons).
3. **Implement focused tests.** Put tests and fixtures beside the owning module,
   follow [Test Style](#test-style), and register them with CTest. Comment larger,
   bulk, and specific edge cases beside the code: explain what they test and
   why they matter; see [Test Case Comments](#test-case-comments). Follow
   [Test Isolation And Cleanup](#test-isolation-and-cleanup). Keep permanent
   tests independent of the original executable and game assets.
4. **Build and run.** Build natively and [discover and run the unit suites](#native-unit-builds).
   Add the builds required by [Validation Scope](#backend-validation-scope):
   a 64-bit Windows build for code that compiles differently there, a [trace comparison](#trace-tests)
   for simulation changes, and every backend for Babylon changes. Build
   production code as well as tests, then exercise changed runtime-guard or QOL
   branches in their configuration.
5. **Check host-dependent behavior.** No [system or device tests](#system-and-device-tests)
   are built since M6.3, so behavior that needs a window, display or audio device
   is checked by hand; record what was checked. Record unavailable prerequisites
   and ignored cases; they do not count as exercised behavior. Verify the
   registration's [timeout and device scheduling](#timeouts-and-shared-devices).
6. **Review and report.** Inspect the final diff and fixture provenance. Rerun
   regenerated vectors with update modes disabled, then provide the
   [validation report](#validation-report), including any remaining gaps and
   [intermittent failures](#intermittent-failures).

Documentation-only changes require review of links, commands, and configuration
claims; they do not require rebuilding unchanged engine code.

<a name="coverage-requirements"></a>

## 🎯 Coverage Requirements

For every function or macro being tested, cover each applicable success and
failure path within its call contract. Separate preserved original behavior
from intentional fixes, malformed-input rejection, runtime guards, and QOL
expectations.

<a name="cases-and-observable-results"></a>

### ✅ Cases And Observable Results

- Exercise empty, minimal, typical, boundary-size, and relevant type/flag cases.
  Include invalid inputs where the API defines their handling.
- Check the return value and every observable output: output parameters, changed
  object/global state, generated files or buffers, resource ownership, and logs
  where they are part of the behavior being tested.
- Check function-pointer and callback behavior: call order, arguments, output
  parameters, return values, and their effect on the caller's result.
- Compare complete strings for equality, with termination checked where the API
  requires it. Compare complete serialized bytes and lengths; a prefix,
  substring, checksum alone, or successful round trip is insufficient.
- Cover allocation and I/O failures where applicable. Verify partial cleanup,
  released resources, and whether outputs remain unchanged or reset on failure.
- Use appropriate Debug/Release and guard/QOL configurations for branches that
  are compiled out elsewhere. Do not invoke unchecked invalid-input paths just
  to make a guard-disabled profile appear to cover rejection behavior.

<a name="regression-proof"></a>

### 🐛 Regression Proof

For a bug fix, demonstrate where practical that the new regression case fails
for the intended reason with the old behavior and passes with the fix. A build
failure or missing prerequisite does not establish that the assertion catches
the bug. Use an isolated checkout or temporary test build for this comparison;
keep the final production code and fixtures intact.

Include the regression test in the same change as the fix. If the old behavior
cannot be exercised reliably, record the reason and the available evidence.
Review whether the assertions would detect a recurrence of the original bug.
See [Google's test review guidance](https://google.github.io/eng-practices/review/reviewer/looking-for.html#tests).

<a name="numerical-comparisons"></a>

### 📐 Numerical Comparisons

Keep integers, strings, serialized bytes, and representation-sensitive original
behavior exact. For calculated floating-point results, use a documented absolute
or relative tolerance only when the numerical contract permits rounding
variation. Explain the tolerance's scale and purpose beside the case; preserve
exact checks wherever the original behavior or file format requires them.

Unity's `TEST_ASSERT_EQUAL_FLOAT` performs an approximate comparison.
`TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)` supplies an explicit absolute
tolerance. Use representation comparisons for bit-exact expectations and check
NaN, infinity, and signed zero explicitly when they are part of the contract.
An unexpected compatibility difference needs investigation before changing a
tolerance or expected vector.
See [Unity's assertion reference](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md#floating-point-if-enabled).

<a name="coverage-reports"></a>

### 📊 Coverage Reports

Coverage measurement is optional until suitable tooling is integrated and
validated with the supported MSVC build. When reports are available, review
uncovered lines and branches in changed code, including backend and guard/QOL
variants. Add meaningful cases or explain exclusions and remaining gaps.

Coverage records execution; review the assertions and edge cases as well. A
percentage alone does not establish correctness, and there is no universal
percentage target for this project.
See [Google's coverage guidance](https://testing.googleblog.com/2020/08/code-coverage-best-practices.html).

<a name="test-data-and-readability"></a>

### 📝 Test Data And Readability

Use independently established expected results. Reconstructed code must not
produce its own compatibility expectations. Consider attributed external corpora
for supported formats and original-engine vectors for reconstructed I/O. Keep
malformed-input regressions clearly identified and document fixture provenance.

Keep cases deterministic and focused on behavior. Use fixed local seeds and
controlled time, callbacks, and host dependencies. Tests should explain a
contract or regression rather than repeat the implementation's calculations.
Use `NULL` for null-pointer expectations in C test code.

Fixtures under any `Tests/tv/` directory are byte-exact on every checkout:
`.gitattributes` marks `**/Tests/tv/** -text`, so Git for Windows' `core.autocrlf`
does not turn text vectors into CRLF files and the parser tests read the same bytes
on every host. A test that needs CRLF input writes it in binary mode itself, as
`std` `TestFileTextModeReadsCrLfAsLf` does.

Loose text files follow the Microsoft CRT's text mode on every column: a read open
without `b` (`"r"`, `"rt"`) hands out `"\r\n"` as `"\n"`, a lone `"\r"` stays, and
Ctrl-Z ends the data until the next seek. Positions stay byte offsets; binary opens
stay byte-exact. The `std` tests `TestFileTextModeReadsCrLfAsLf` and the Babylon
file suite's `TextModeReadsCrLfAsLf` pin this contract (see
[Platform-Layer.md](arc42/05-building-blocks/Platform-Layer.md#file-services)).

<a name="test-case-comments"></a>

### 💬 Test Case Comments

Place comments beside the case or immediately before a related block of cases.
For larger tests, bulk/table-driven cases, and specific edge cases, explain both
**what behavior is being tested** and **why the case matters**. Describe the
boundary, preserved quirk, regression, or chosen data that makes the case useful.
For a loop or case table, explain the shared purpose and comment individual
entries where a particular edge case needs its own explanation.

Small, straightforward cases can omit comments when the name, setup, and
assertions immediately show what they test. Comments should explain intent,
not repeat obvious assignments or assertions.

<a name="test-layout"></a>

## 🗂️ Test Layout

Place tests next to the module or subsystem they cover, using a local `Tests` directory. The project convention is plural `Tests`, with a capital `T`.

Examples:

- `Libs/j3dcore/Tests`
- `Libs/std/Tests`
- `Libs/std/General/Tests`
- `Libs/rdroid/Primitives/Tests`
- `Libs/sith/World/Tests`
- `Libs/sith/Cog/Tests`
- `Libs/sound/Tests`
- `Libs/smush/Tests`
- `Jones3D/Main/Tests`

Prefer the narrowest source-area owner that makes sense. For example, tests for `stdFnames` belong under `Libs/std/General/Tests`, while tests for voice playback state belong under `Libs/sith/World/Tests`.

Subareas can keep their own colocated test files and fixtures while still sharing the parent module test executable. For example, `Libs/std/General/Tests/stdGobTest.c` is owned by `std/General` and runs with the other `std/General` suites inside the `stdTests` executable (and `stdTestsBabylon` in the Babylon column).

Avoid collecting all tests in one repository-level directory. Keeping tests beside the reconstructed module helps preserve the original engine boundaries and makes ownership clearer.

<a name="test-style"></a>

## ✍️ Test Style

<a name="naming-and-ownership"></a>

### 🏷️ Naming And Ownership

Use Unity Fixture groups for module-level test files. Name test groups after the subsystem or source file under test, such as `stdFnames`, `stdConffile`, `rdClip`, `sithVoice`, or `AudioLib`.

Name test source files after the module followed by `Test.c`, matching the reconstructed codebase's C naming style. For example, use `stdMathTest.c` for `stdMath.c`.

When a module has different backend implementations, split backend-specific unit tests by backend instead of hiding `#ifdef` branches inside a broad test file. For example, `Libs/std/Tests/stdCvarNullTest.c` covers the Null column's simulation cvar seam (`stdCvarNull.c`), and `Libs/std/Babylon/Tests/stdCvarBabylonTest.cpp` covers its Babylon replacement.

Name system test source files after the exact module or backend they exercise, followed by `SystemTest.c`.

<a name="test-executables-and-registration"></a>

### 🧩 Test Executables And Registration

Prefer one executable per source area or module boundary, with shared fixture
startup and a stable module-level CTest entry.

| Executable | CTest entry | Coverage | Integration |
| --- | --- | --- | --- |
| `j3dcoreTests` | `j3dcore.Macros` | Core macros | Available |
| `j3dCrtTests` | `j3dcore.Crt` | The `j3dcrt` Microsoft CRT shim | Available |
| `rdroidTests` | `rdroid.Math` | Vector and matrix debug-source numerical fidelity | Available |
| `stdTests` | `std` | `std/General` services and the Null backends; not on Windows (POSIX file utilities) | Available |
| `stdTestsBabylon` | `std.OnBabylon` | The same suites through the Babylon file adapter; Babylon column only | Available |
| `stdBabylonTests` | `std.Babylon` | The std adapter onto Babylon: time, files, control devices, events, cvars; Babylon column only | Available |
| `wkernelTests` | `wkernel` | The Babylon wkernel event loop and window callbacks; Babylon column only | Available |
| `jonesCaptureTests` | `Jones3D.Capture` | Render capture command line and console command | Available |
| `jones3dTests` | `Jones3D.Main` | `JonesMain` video mode lookup | Available |
| `soundTests` | `sound` | Deterministic lip-sync generation and lookup, AudioLib codec and header vectors | Available |
| `smushTests` | `smush` | INSANE movie player: core, bitmap, codec, sound, stream areas and original-engine assembly vectors | Available |
| `sithTests` | `sith` | World thing/template I/O, voice lip-sync and the cogstatus console command bounds | Available |
| `sithThingQuetzVecTests` | `sith.ThingQuetz` | Original-engine vectors of the Quetz tail functions (world-state harness, `Libs/sith/Tests/Vec`) | Available |
| `sithAIInstinctVecTests` | `sith.AIInstinct` | Original-engine vectors of the instinct registration (world-state harness) | Available |
| `sithAIInstinctFnVecTests` | `sith.AIInstinctFn` | Original-engine vectors of the instinct functions (world-state harness) | Available |
| `sithAIMoveVecTests` | `sith.AIMove` | Original-engine vectors of AI movement and turning (world-state harness) | Available |
| `sithAIUtilVecTests` | `sith.AIUtil` | Original-engine vectors of the AI utility functions: waypoints, sight, firing, path checks (world-state harness) | Available |
| `sithDSSTests` | `sith.DSS` | Original-engine vectors of the DSS and DSS thing save/network serialization functions | Available |
| `sithMultiTests` | `sith.Multi` | Original-engine vectors of multiplayer state and synchronization | Available |

Name CTest entries after the owning module when the executable covers that whole module, such as `std`. When an executable covers only a subarea, use the owning module first and a short area name second, separated by a dot, such as `j3dcore.Macros`. Use labels that match the owning module, such as `j3dcore`, `std`, `rdroid`, `sith`, `sound`, or `Jones3D`.

The column is chosen at configure time, so a suite that only one column builds keeps its module labels; `ctest -N -L unit` shows which suites a build tree has.

<a name="fixture-structure"></a>

### 🏗️ Fixture Structure

Test files should normally provide:

- one `TEST_GROUP`
- one `TEST_SETUP`
- one `TEST_TEAR_DOWN`
- one `TEST_GROUP_RUNNER`
- focused `TEST` cases for the behavior under test

Include Unity headers as external headers, for example `#include <unity_fixture.h>`.

Follow the owning module's coding conventions, including local declarations
near first use and aligned assignment initializers in consecutive declaration blocks.

<a name="test-isolation-and-cleanup"></a>

### 🧹 Test Isolation And Cleanup

Every test must work individually and in any order. Initialize its own state;
never depend on a previous test leaving globals, files, or resources behind.
A scenario that needs several ordered operations belongs in one test with its
own setup and assertions for those operations.

Restore globals, callback bindings, allocation-failure controls, clocks, random
state, environment/current-directory changes, and host state modified by the
case. Give temporary files/directories unique ownership and remove them during
cleanup. Release allocations, handles, devices, windows, and replaced hooks in
teardown, including after failed assertions or partial setup.

Shared fixture helpers may reduce duplication, but each case must establish its
own preconditions. Unit tests should use controlled dependencies and local
fixtures; real devices and external services belong in labelled system tests.
See [Google's test isolation guidance](https://testing.googleblog.com/2010/12/test-sizes.html).

<a name="shared-test-support-and-macro-assertions"></a>

### 🔧 Shared Test Support And Macro Assertions

Tests that need host-service assertion capture should link `j3dTestSupport` and include `#include <j3dcore/Tests/j3dTest.h>`. Use `J3DTest_Startup()` and `J3DTest_BindHostServicesGlobal((uintptr_t)&<module>_g_pHS)` in `TEST_SETUP`, then `J3DTest_Shutdown()` in `TEST_TEAR_DOWN`. Some older tests pass a `<module>_g_pHS_ADDR` macro that their test header defines as that address; the retail addresses these names once stood for are gone ([ADR-0004](arc42/09-decisions/ADR-0004-drop-binary-compatibility.md)).

For header and macro tests, prefer stable behavior checks over release-specific value checks. For example, `j3d.h` macro tests should verify string/memory helpers, runtime guard behavior, platform flags, logging/assertion macros, and compile-time shape, but should not pin exact `J3D_VERSION_STRING` or `J3D_VERSION_FULL` values because those change with release numbers and suffixes.

<a name="test-scope"></a>

## 🧭 Test Scope

<a name="backend-validation-scope"></a>

### 🖥️ Validation Scope

Choose validation from the changed production paths and test dependencies, not
only the module name or whether a test file was edited.

| Changed code or tests | Required validation |
| --- | --- |
| Any engine code | Native build (Clang), unit suites in Debug |
| Code that compiles differently on Windows (paths, CRT calls, `_WIN32` branches) | Also a [64-bit Windows build](#windows-unit-builds) (MinGW x86_64 under wine, or MSVC x64) with its unit suites |
| Simulation behavior (physics, collision, AI, COG, game time, savegames) | Also a [trace comparison](#trace-tests) against the retail profile, and against the Enhanced profile when an enhancement changed |
| Memory handling, file or savegame parsing, 64-bit portability | Also a sanitizer build (`-DJONES3D_SANITIZE=ON`) |
| Babylon interfaces or backends | Babylon's own tests against every backend, including Null, and its standalone build |

Trace the affected dependencies and explain the selected validation scope in the report.

<a name="trace-tests"></a>

### 🎞️ Trace Tests

A trace test runs the whole game headless: it loads a savegame, feeds scripted
input for a fixed number of frames with fixed game time, and writes the
simulation state. Comparing state files shows whether a change altered the
simulation. See [ADR-0003](arc42/09-decisions/ADR-0003-fidelity-split.md) and
[chapter 8](arc42/08-crosscutting/README.md).

```bash
cd <game>/Resource
<build>/Jones3D/Jones3D-headless --load ../SaveGames/<save>.nds --frames 1000 --state state.txt
```

Every column builds `Jones3D-headless`: the Null column, the Babylon column
(`-DJONES3D_PLATFORM_BABYLON=ON`) on Linux and macOS, and the Babylon column on 64-bit
Windows. In the Babylon column `--render null|gpu` picks what `std3D` draws with: `null`,
the default, is a Null device that keeps no command record, so a trace run needs no GPU;
`gpu` is the SDL GPU device without a window (set `SDL_VIDEO_DRIVER=offscreen` where there
is no display). The option does not change the state trace. The same run must give the same
state file, byte for byte, in all of them; the
runner writes the state file in binary mode so Windows line endings do not differ. On
Linux the Windows build runs under wine (`wine <build>/Jones3D/Jones3D-headless.exe ...`).
`--input <file>` feeds a scripted-input file of `frame <n>: cmd <command>` and
`frame <n>: key <name|number> down|up` lines; scripted keys take effect at the next
`stdControl_ReadControls`, as device input does.

The state file starts with one `cvar <name> <value>` line per simulation cvar (today
`cvar game.profile enhanced` or `retail`), followed by the frame, game time, player state and one line per
active thing.

Since M5 native builds use Microsoft's `rand` generator (the `j3dcrt` shim), as the original does, so native
traces follow the retail random sequence ([research](Research/Time-Sources.md)).

Rules:

- The reference for the retail profile is a build of the tagged baseline with
  all simulation options set to retail. The Enhanced profile (the default) is compared
  against the same baseline with its enhancements on.
- Identical state files are the expected result. Any difference is investigated
  before anything else changes; reference traces are never edited to make a
  change pass.
- The same run must give identical state files on 32-bit, 64-bit and sanitizer
  builds, and on the Null, Babylon and 64-bit Windows columns.
- Traces need game data, so they are not permanent unit tests. Record the
  savegame, input script, frame count and build in the validation report.

**Status:** the headless runner, scripted input and state files exist and are
deterministic across 32-bit, 64-bit and sanitizer builds. Selecting the retail
or Enhanced profile at run time waits for the QOL options to become runtime
options; until then, the profile is the build's `JONES3D_QOL_IMPROVEMENTS`
setting.

<a name="render-capture"></a>

### 🎥 Render Capture

A render capture records every call the game makes to the 84 renderer functions,
with its arguments, return value and recorded out-parameters, into an RCAP file
([RCAP](Formats/RCAP.md)). The stream is what the replay tool (M6.0c) replays
against a new renderer backend; the design is in
[ADR-0024](arc42/09-decisions/ADR-0024-render-capture-by-renamed-backends.md).

```bash
cd <game>/Resource
<build>/Jones3D/Jones3D-headless --load ../SaveGames/<save>.nds --frames 1000 --capture capture.rcap
```

Like the traces, the run needs game data, so a capture is not a permanent unit
test. Record the savegame, frame count and build with it.

A frame is large: in the regress scene the file grows by roughly 390 KB per
recorded frame.

The M6.1 references were recorded with the DX9 Windows build, which M6.3 removed:
`Jones3D.exe -capture <file>` (with `-captureframes` to record from startup) and
the console command `capture frames on|off`, partly under wine with Xvfb and
`xdotool` ([ADR-0011](arc42/09-decisions/ADR-0011-retire-win32-directx.md),
[M6.1 references](References/M6.1/README.md)). A capture holds the calls of the
backend that recorded it, and replay needs a matching backend (M6.6).

The console command `capture` stays: it prints the state, for example
`Render capture: open, frames on`, and `capture frames on|off` sets the frame
window. With no capture open it reports that the game must be started with
`-capture <file>`. The windowed `Jones3D` of the Babylon column does not parse
`-capture` yet (`JonesCapture_ParseCommandLine` has no caller since M6.3), so
today only `Jones3D-headless --capture` opens a capture.

Rules:

- Two runs of the same command give identical files.
- Capturing does not change the state trace of the run.
- The Null and Babylon columns record the same stream; only the column name in
  the header differs.
- The 1000-frame regression run costs about 390 MB and about 3.5 times the time
  of the same run without `--capture`.

Replay a capture and optionally record the replayed calls into a second file:

```bash
<build>/Libs/std/Jones3D-replay capture.rcap [--capture replay.rcap]
```

The Babylon column's `Jones3D-replay` also takes `--render null|gpu` and, with `gpu`,
`--frames-out <dir> [--every <n>]`: after each replayed `stdDisplay_Update` it reads the
back buffer and writes `frame_<NNNNN>.bmp` (every nth frame with `--every`), so an
M6.1-style capture renders to image frames for visual comparison. The captures themselves
contain game data and stay local; they are not committed.

The replay exits `0` when it finishes, `1` for a malformed or inconsistent
input file (or a file that cannot be opened), and `2` for wrong arguments. A
replay with `--capture` produces the same bytes as the original unless the
input contains calls the replay does not reproduce or a call returns different
values.

**Status:** the writer, the forwarding layers, the headless switch, the console
command and the replay tool exist, with unit tests beside them.

<a name="renderer-reference-masks"></a>

### 🖼️ Renderer Reference Masks

The DirectX 9 rendering masks of the removed `std3D` system tests are kept in
`Libs/std/Tests/tv/std3D/` as references for the tests of the new renderer.
Nothing builds or audits them today; their scenes, sizes and provenance are in
[that directory's README](../Libs/std/Tests/tv/README.md). Magenta pixels in a
mask are ignored by a comparison; all other pixels are expected output.

<a name="deterministic-unit-and-regression-tests"></a>

### 🔬 Deterministic Unit And Regression Tests

Start with deterministic unit and regression tests for parser, file, math, resource, and state-machine behavior.
Integration tests can still live near the module that owns the scenario, but should use CTest labels such as `integration`, `network`, `slow`, or `gui` when they require extra orchestration.

For parsers and file formats, prefer deterministic fuzz-style corpora in the normal unit tests before adding a separate fuzzing harness. Good examples are generated malformed JSON strings, exhaustive small truncation sweeps for binary containers, boundary-size argument lists, and generated round-trip vectors. These tests should be reproducible, fast enough for every CTest run, and should not use process-wide random seeds.

When a production contract necessarily terminates the process, test it in a child process: re-enter the same test executable in a private temporary directory and check the child's result. Keep child-only modes out of the normal Unity runner and suppress host error-reporting dialogs so CI cannot block on them. (The `stdPlatform` minidump death test did this until M6.3 removed the Win32 exception filter it tested.)

`stdBmpTest.c` and its vectors (generated images plus selected public-domain images from BMP Suite 2.8, recorded in `Libs/std/General/Tests/tv/stdBmp/README.md`) are kept but not built since M6.3: `stdBmp_WriteVBuffer` and `stdBmp_Load` lived in the removed Win32 backend. Prefer extending that attributed corpus over copying unknown BMP files from the web when the BMP code returns.

<a name="system-and-device-tests"></a>

### 🖥️ System And Device Tests

System tests are integration-style tests that touch host or device services such as windows, input devices, the GPU, audio devices, or filesystem locations outside small test vectors. The Babylon render module (M6.4) brings the first ones, its contract tests; the Win32 and DirectX system tests went with M6.3. Keep them in separate executables from deterministic unit tests and label them with `system`; add more specific labels such as `device`, `audio`, or `gui` as appropriate.

System/device tests may use Unity ignore results when the host cannot provide the needed service, for example a headless CI worker without a render device. They should still fail normally when the service was available and the engine behavior is wrong.

When multiple system tests share host setup, keep that setup in a small local support file and keep the test cases themselves in the owning module files.

Tests that can change fullscreen display modes, reset or recreate graphics devices, or disturb desktop input focus are disruptive: keep them in separate source files named `*DisruptiveSystemTest.c`, label them `disruptive`, keep them out of default builds, and make host capability failures explicit with Unity ignore messages.

<a name="babylon-render-contract-tests"></a>

#### 🧪 Babylon Render Contract Tests

`Babylon/render/tests/RenderContractTest.cpp` runs the same `Device` contract against the Null device
(also one without B4G4R4A4 support) and the headless SDL GPU device. `NullDeviceTest.cpp` covers the Null
backend's record and `SdlGpuDeviceTest.cpp` the SDL GPU device's driver, formats and swapchain; CTest runs
them as `babylon.render`. The SDL GPU cases need a Vulkan device: on Linux CI that is Mesa's lavapipe from
the `mesa-vulkan-drivers` package, and `SDL_VIDEO_DRIVER=offscreen` (set by the test properties) lets SDL
video start without a display. On a host without a GPU device the SDL cases print a skip message and count as
passed, so they validate nothing there; setting `BABYLON_REQUIRE_GPU=1`, as the Linux CI columns do, turns that skip into a failure. Windows and the
macOS column leave the variable unset because they have no lavapipe (WARP comes later), so their SDL GPU
cases skip.

`ShaderDrawTest.cpp` covers the shader path with the test shaders in `Babylon/render/tests/shaders/`,
compiled by `babylon_add_shaders()`. It draws a vertex-colored triangle, a textured indexed quad with a
pushed tint, and two overlapping quads with a `LESS` depth test. On the SDL GPU device it reads a 64x64
Rgba8 target back and compares pixels: exact where the quad edges fall on pixel boundaries, with a small
tolerance where rounding is possible, and only pixels well inside or outside the triangle. The Null device
does not rasterize, so the same scenes check that it accepts the same pipelines and records exactly the
same command stream, and that the target keeps its clear color. A draw whose shader declares a texture or
uniform slot that was not filled earlier in the pass is dropped with one warning on every device. The SDL
image checks skip on a host without a GPU device, as above; the Null cases always run.

The first configure fetches SDL_shadercross at its pinned commit and builds it with the pinned DXC release,
which takes about 50 seconds; later configures reuse the cache in `${BABYLON_CACHE_DIR}` (`.cache/`, or
`Babylon/.cache` in the standalone build). An offline configure needs that cache already populated or
`BABYLON_SHADERCROSS` set to an existing tool. macOS and arm64 hosts have no prebuilt DXC: they set
`BABYLON_DXC_ROOT` to a DXC they built, or configure with `BABYLON_BUILD_SHADERS=OFF` and leave out what
needs shaders.

<a name="std3d-on-babylon-tests"></a>

#### 🧪 std3D On Babylon Tests

`std.std3DOnBabylon` builds `std3DBabylonTests` from `Libs/std/Babylon/Tests/std3DBabylonTest.cpp`
and the C replay suite `stdRenderReplayTest.c`, against `std3DBabylon.cpp`, `stdRenderBabylon.cpp`
and `stdRenderReplayBabylon.cpp`. The Null group drives the public `std3D_*` calls and the capture
forwarders on a `babylon::render::NullDevice` and checks the recorded command stream and the texture
contents byte for byte against values derived from the DX9 backend's rules. The GPU group repeats the
texture readback and renders synthetic scenes on the SDL GPU device, comparing back-buffer pixels for
formats, alpha test and blending, Z, fog, lines, points and perspective; it also checks the windowed
present and that the replay writes BMP frames. Like the render contract tests, the GPU cases need a
Vulkan device (Mesa's lavapipe in Linux CI), skip without one, and fail instead when
`BABYLON_REQUIRE_GPU=1` is set. CTest runs the suite as `std.std3DOnBabylon` with
`SDL_VIDEO_DRIVER=offscreen` (`dummy` on macOS).

<a name="original-engine-io-test-vectors"></a>

## 📦 Original-Engine I/O Test Vectors

> **Historical procedure.** The capture harness below runs inside the original
> `Indy3D.exe` and relies on the hook and RTI machinery that Jones3D EX removes
> ([ADR-0004](arc42/09-decisions/ADR-0004-drop-binary-compatibility.md)). Existing
> vectors keep their recorded provenance and stay valid. New vectors come from
> the original game (for example savegames and traces) or the byte-exact
> decompilation, with recorded provenance. The rules on provenance, failure
> inputs and review below still apply.

For reconstructed readers, writers, codecs, and other behavior-sensitive I/O,
expected compatibility vectors must come from the selected original `Indy3D.exe`
function when an original implementation exists. Generate the same cases with
OpenJones3D and compare the results. A reconstructed writer/reader round trip
checks internal consistency, but does not by itself establish original-engine
compatibility.
A renamed copy of decompiled code is useful for investigation; record that source
separately from vectors captured by executing the original binary.

Keep original-behavior vectors separate from synthetic malformed-input,
runtime-guard, bug-fix, and QOL expectations. New modules and behavior without a
retail equivalent need independently specified expectations and clear provenance.
For example, BMP masks regenerated by an OpenJones3D backend remain rendering
regression baselines unless they were also independently checked against retail.

<a name="reference-binaries-and-version-policy"></a>

### 🔖 Reference Binaries And Version Policy

The primary references are the original debug build and retail v1.0 of
`Indy3D.exe`. Use these for engine behavior analysis and compatibility vectors.
Identify the exact executable by SHA-256 before invoking it:

| Reference binary | Use | SHA-256 |
| --- | --- | --- |
| Original debug build (`indy3d-dbg-1999-10-28.exe`) | Primary reference | `65f9165283b2e00a78baa93476073b3b302f5d561c240f2e0ae304fd98d028cb` |
| Retail v1.0 (`Indy3D_v1.0.exe`) | Primary reference | `3fbaf8cd401b4af80967cbe42e3420fb803288b336ebbe72a9a01b6dfd661a53` |
| Retail v1.2 (`Indy3D_v1.2.exe`) | Reference only for an implemented v1.2 fix | `55fb00c0a2cb30793ff451dc6fd17681c5128aa32419aa711015fd5117d608bf` |

For functions shared by the debug build and v1.0, compare the same cases with
both where practical. Record debug-only assertions, diagnostics, and other
behavior differences explicitly; do not assume debug results are identical to
retail v1.0 results.

Use v1.2 only when the tested function has a fix that is absent from both primary
reference versions and OpenJones3D implements that specific fix. Record the
function, the version difference that establishes the fix, and the corresponding
OpenJones3D implementation. Keep those fixtures identified as v1.2 fix vectors;
preserve the debug/v1.0 baselines and do not use v1.2 as the general oracle for
other behavior.

<a name="capture-harness"></a>

### 🛠️ Capture Harness

Use a small, temporary MSVC Win32 C or C++ capture harness running inside the
original engine process. Native code can reuse the project's ABI declarations,
construct large engine structs, manage their referenced resources, and call the
original functions with the declared calling conventions. Verify original struct
sizes, field offsets, packing, and pointer widths; QOL-expanded layouts must not
be passed to original functions.

Python may define case lists and seeds, launch the original process with the
capture harness, enforce timeouts, collect output files, and calculate hashes.
Keep the engine struct construction and function calls in the native harness,
so nested pointers, callbacks, and ABI layouts have one checked definition.
Loading an EXE with `ctypes`/`LoadLibrary` is not a replacement for running the
initialized engine: Windows does not resolve an EXE's static imports that way.
See [Microsoft's LoadLibrary documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibrarya).

The RTI `<function>_ADDR` and `<function>_TYPE` declarations of the reconstruction (removed in Jones3D EX,
still in the `upstream-compatible-final` tag) described retail v1.0 entry points and prototypes. Resolve and verify the matching addresses
and layouts separately for the debug build or an eligible v1.2 fix; do not reuse
v1.0 addresses for a different binary. `J3D_CALLFUNCFAR` and
`J3D_TRAMPOLINE_CALL` call addresses directly; they do not preserve an unpatched
copy of a function.
Capture from unmodified original entry points and dependencies. Normal
`Jones3D.dll` startup installs replacement hooks, so use a dedicated capture
module and avoid installing those hooks for an original-engine reference run.
`j3dTestSupport` maps globals for standalone tests; it does not load original code.

<a name="generation-procedure"></a>

### ⚙️ Generation Procedure

1. Select the reference binary according to the version policy above and verify
   its SHA-256. Resolve its matching function addresses and ABI declarations in
   the loaded image, then initialize the services needed by the target function.
2. Construct deterministic cases in native code. Initialize every serialized
   field and relevant engine global; provide valid object lifetimes, resource
   references, allocators, file handles, and host callbacks. Cover empty,
   minimal, typical, boundary-size, and each relevant type/flag combination.
   Control seeds, time, locale, floating-point settings, and callback behavior.
3. Invoke the selected original writer or codec and capture the complete emitted
   bytes, return values, output parameters, and callback calls. Record callback
   order, arguments, and outputs using stable object IDs. Generate each case twice
   from freshly initialized state and require identical results before freezing
   it as a reference vector.
4. Run the reconstructed writer or codec with the same semantic inputs. Compare
   the complete file/buffer and its length with the original output, and compare
   all returned and callback-produced outputs. For strings, check full equality,
   including the terminator where it is part of the API's output contract.
5. Feed original-generated files to both readers. Compare all meaningful parsed
   fields, resource references, return values, and callback results. When the
   format permits it, serialize both parsed states again with the original
   writer and require identical output. Also check fields and side effects that
   the writer does not serialize; reserialization alone cannot cover them.
6. Commit the portable input/output fixtures and standalone Unity assertions
   under the owning module's `Tests/tv` directory. Normal CTest runs must consume
   the saved fixtures without launching, injecting into, or depending on
   `Indy3D.exe`. Once equivalent standalone coverage is available, remove
   temporary oracle hooks, decompiled comparison copies, and capture-only call
   seams from permanent engine/test paths.

<a name="failure-inputs"></a>

### 🧯 Failure Inputs

Keep unsafe or unsupported original-engine inputs out of the compatibility corpus.
Exercise malformed data and allocation/I/O failures in separately identified
regression cases. Capture original failure behavior only where the original
function accepts that contract; label intentional fixes and hardened rejection
behavior explicitly.

<a name="provenance-and-review"></a>

### 🔎 Provenance And Review

For each corpus, record the original EXE hash/version, function name and
entry-point address, ABI/layout checks, input cases and seeds, setup and host
callback behavior, relevant environment settings, capture-harness source/version,
regeneration procedure, and SHA-256 of each output. Record whether the two
original-engine runs and reconstructed results matched, plus any preserved quirk
or intentional difference. Keep game assets, the original executable, and
temporary injected oracle code out of the committed fixture set.

Store semantic inputs and stable identifiers rather than raw process addresses
or dumps of pointer-bearing structs. Preserve the original serializer's actual
output bytes. Any format-defined nondeterministic field or permitted comparison
normalization must be documented explicitly; do not silently rewrite a captured
file to make the comparison pass. Review regenerated fixtures as a behavior
change and rerun the permanent tests with vector-update modes disabled.

<a name="running-tests"></a>

## ▶️ Running Tests

<a name="build-options"></a>

### ⚙️ Build Options

| CMake option | Default | Effect |
| --- | --- | --- |
| `JONES3D_BUILD_TESTS` | ON | Builds Unity test executables and enables CTest |
| `JONES3D_PLATFORM_BABYLON` | OFF, ON on Windows | Builds the Babylon column instead of the Null column; Windows accepts only ON |
| `JONES3D_QOL_IMPROVEMENTS` | ON | The Enhanced profile's QOL changes |
| `JONES3D_RUNTIME_GUARDS` | OFF | Enables opt-in release safety checks and their rejection paths |
| `JONES3D_SANITIZE` | OFF | AddressSanitizer and UndefinedBehaviorSanitizer; non-Windows only |

Use separate build trees for column and guard/QOL settings so an existing CMake
cache does not silently select a different profile.

<a name="native-unit-builds"></a>

### 🐧 Native Unit Builds

The default build on Linux and macOS. Clang is the tested compiler; GCC needs
extra flags for now (`-D__cdecl= -D__stdcall= -D__fastcall=` and, for GCC 14+,
`-Wno-error=incompatible-pointer-types`). On non-Windows hosts CMake selects the
Null platform and the standalone build automatically.

```bash
cmake -S . -B build/native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native
ctest --test-dir build/native -N -L unit
ctest --test-dir build/native --output-on-failure -L unit
```

Sanitizer build (GCC; Clang needs its sanitizer runtime installed):

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug -DJONES3D_SANITIZE=ON
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure -L unit
```

<a name="windows-unit-builds"></a>

### 🪟 Windows Unit Builds

Windows builds the Babylon column only, as a 64-bit program. Configuring with
`-DJONES3D_PLATFORM_BABYLON=OFF` or for 32-bit Windows is a configure error.
The column uses the real Microsoft CRT and the Windows SMUSH timer
(`TimerWin32.c`). Until M6.6 it runs the Null renderer: the windowed `Jones3D.exe`
shows nothing.

With MinGW-w64 (x86_64) from Linux, the tests run under wine; the toolchain file
sets wine as `CMAKE_CROSSCOMPILING_EMULATOR`, so CTest starts every test
executable through it:

```bash
cmake -S . -B build/win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/win64
ctest --test-dir build/win64 -N -L unit
ctest --test-dir build/win64 --output-on-failure -L unit
```

`stdTests` (the `std` entry) is not built on Windows; `std.OnBabylon` runs the
same `std/General` suites there. Two known differences under wine:

- `std.OnBabylon` fails one case, `stdConffile`
  `TestReadAndScanLineFormatsAndErrors`, because Wine's `vsscanf_s` lacks `%a`.
  It passes natively; report it as a known Wine failure, not as passed.
- `ctest -L check` does not work: CMake prefixes the `python3` command of the
  Babylon boundary checks with the emulator. Run the checks in a native tree.

With Visual Studio (MSVC x64), from a Developer Command Prompt; the Visual
Studio 2026 generator needs CMake 4.2 or newer. CI runs the same in its
`windows-babylon` column:

```bat
cmake -S . -B build\win64-msvc -G "Visual Studio 18 2026" -A x64 -DJONES3D_PLATFORM_BABYLON=ON
cmake --build build\win64-msvc --config Debug
ctest --test-dir build\win64-msvc -C Debug -N -L unit
ctest --test-dir build\win64-msvc -C Debug --output-on-failure -L unit
```

MSVC builds with `/sdl`, which rejects the unbounded CRT functions (`strcpy`,
`fopen`, `getenv` and others, warning C4996); use the bounded forms, which
`j3dcrt` shims on the other hosts.

<a name="guard-and-legacy-profiles"></a>

### 🛡️ Guard And Legacy Profiles

Runtime-guard rejection tests require `JONES3D_RUNTIME_GUARDS=ON`. Default builds
preserve the original unchecked contracts while retaining `STD_ASSERTREL`.
When changing guard behavior, exercise the guard-enabled branch in a separate
legacy-hardened tree:

```bash
cmake -S . -B build/legacy-hardened -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DJONES3D_QOL_IMPROVEMENTS=OFF -DJONES3D_RUNTIME_GUARDS=ON
cmake --build build/legacy-hardened
ctest --test-dir build/legacy-hardened -N -L unit
ctest --test-dir build/legacy-hardened --output-on-failure -L unit
```

When changing QOL or preserved legacy behavior, also run with
`JONES3D_QOL_IMPROVEMENTS=OFF` and `JONES3D_RUNTIME_GUARDS=OFF` in a separate
legacy tree, in each column the change affects.

<a name="ci-columns"></a>

### 🤖 CI Columns

The DirectX build and test workflows inherited from OpenJones3D were removed in
M6.3. Two workflows remain:

- `Babylon` (`.github/workflows/babylon.yml`) builds `Babylon/` on its own, runs its
  tests and checks its module graph against `Babylon/allowed-deps.txt`.
- `Game (native)` (`.github/workflows/game-native.yml`) builds the game and runs its
  tests in Debug columns with `fail-fast: false`: the Null column (`linux-null`) and the
  Babylon column (`linux-babylon`, `-DJONES3D_PLATFORM_BABYLON=ON`) on `ubuntu-24.04`
  with Clang and Ninja, and the Babylon column as a 64-bit Windows program
  (`windows-babylon`, MSVC x64 with the `Visual Studio 18 2026` generator on
  `windows-2025-vs2026`). SDL3 is built from source in CI, so the Linux columns install
  the same dependency packages as `babylon.yml`.

The macOS columns of both workflows (`macos-null` and `macos-babylon` on
`macos-15-intel` with the system Apple clang) are commented out for now because the
macOS runners are flaky (jobs not acquired, timing failures). Uncomment them to turn
macOS back on.

- Unit suites run as `ctest -N -L unit` followed by `ctest --output-on-failure -L unit`, so the
  discovery list is logged before the results.
- The Babylon boundary checks (`babylon.check.boundary`, `babylon.check.selftest`) carry the
  labels `babylon;check`, not `unit`, because they inspect the source tree instead of running
  engine code. A second labelled call, `ctest -L check`, runs them in every column.
- `python3 Scripts/checkboundary.py --check` (the Windows/DirectX include boundary) runs once per
  push, in the `linux-null` column.
- The address-pinned sith vector suites (`sith.DSS`, `sith.Multi`, `sith.ThingQuetz`,
  `sith.AIInstinct`, `sith.AIInstinctFn`, `sith.AIMove`, `sith.AIUtil`) are not built on Apple
  hosts: they need GNU ld's `-no-pie -Wl,-Ttext-segment=0x20000000` to put the synthetic arena in
  the window that `sithVecTest_AssertArenaPlacement` requires, and ld64 offers no equivalent.
  The Linux columns still run them.

Traces need game data, so CI runs none; compare them locally on the Null, Babylon
and 64-bit Windows columns (see [Trace Tests](#trace-tests)).

<a name="intermittent-failures"></a>

### 🔎 Intermittent Failures

Preserve the initial failure, logs, input seed/fixture identity, build settings,
and relevant host/device details. Use bounded reruns to investigate whether the
cause is test state, timing, a host prerequisite, or production behavior. Report
the original failure and subsequent outcomes; a later pass does not resolve an
unexplained failure.

If a fix is deferred, record the outstanding problem and follow-up. A Unity
ignore is appropriate for an unavailable prerequisite, as defined under
[System And Device Tests](#system-and-device-tests). Keep behavior failures
visible when the prerequisite was available. Investigate unexpected outputs
before changing assertions, tolerances, or fixture expectations.
See [Google's experience with flaky tests](https://testing.googleblog.com/2016/05/flaky-tests-at-google-and-how-we.html).

<a name="timeouts-and-shared-devices"></a>

### ⏱️ Timeouts And Shared Devices

Set a finite CTest `TIMEOUT` for process and device tests when adding or changing
their registration. Choose a documented limit from expected runtime with margin
for slower hosts. Verify the configured timeout and failure-path cleanup;
CTest can terminate a timed-out process before its teardown runs.

Within one CTest run, use a shared `RESOURCE_LOCK` for tests that compete for the
desktop, display mode, input focus, or the same device. Use `RUN_SERIAL` when a
test must run without any other test in that invocation. These properties
coordinate one CTest scheduler; serialize conflicting device test runs across
build directories and separate invocations as well.

A capability skip still needs a clear reason, and a timeout is a failure to
investigate. Verify the actual registration instead of assuming that these
properties are already configured for an existing suite.
See CMake's [TIMEOUT](https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html),
[RESOURCE_LOCK](https://cmake.org/cmake/help/latest/prop_test/RESOURCE_LOCK.html),
and [RUN_SERIAL](https://cmake.org/cmake/help/latest/prop_test/RUN_SERIAL.html) documentation.

<a name="validation-report"></a>

## 📋 Validation Report

Include the following in the change description or review reply:

- Functions or macros covered, important cases, and any known coverage gaps.
- Regression proof for fixes, or the reason it was unavailable, plus numerical
  tolerance rationale and coverage findings when collected.
- Build trees (Null, Babylon, 64-bit Windows, sanitizer), compiler, Debug/Release
  configuration, guard/QOL settings and scope rationale, with the commands
  actually run.
- For simulation changes: the trace runs (savegame, input script, frame count,
  profile, reference build) and whether the state files were identical.
- Build results and discovered CTest suites. Report CTest executable totals and
  Unity case totals where available; distinguish passes, failures, ignores, and
  tests not run.
- Fixture provenance for compatibility vectors,
  plus reproducibility results and documented intentional differences.
- Host limitations and unresolved device/disruptive failures. Include initial
  intermittent failures, diagnostic reruns, timeouts, and remaining follow-up.
  Do not describe skipped or unavailable cases as validated behavior.

<a name="planned-test-integrations"></a>

## 🚧 Planned Test Integrations

This section describes pending integrations. Verify the current CMake targets
and workflow files before using these commands or claiming CI coverage.

<a name="deferred-addresssanitizer-integration"></a>

### 🛡️ AddressSanitizer In CI

Native sanitizer builds work locally (see [Native Unit Builds](#native-unit-builds)).
Making them a required CI check is planned with the three-platform CI in M5.

<a name="additional-module-commands"></a>

### 🧩 Additional Module Commands

The `rdroidTests`, `soundTests`, `smushTests`, `sithTests`, and `sithDSSTests` targets are available and
included in a full build. Use the matching target and CTest entry for a focused rebuild and run:

```bash
cmake --build build/native --target rdroidTests
ctest --test-dir build/native --output-on-failure -R "^rdroid\.Math$"
cmake --build build/native --target soundTests
ctest --test-dir build/native --output-on-failure -R "^sound$"
cmake --build build/native --target smushTests
ctest --test-dir build/native --output-on-failure -R "^smush$"
cmake --build build/native --target sithTests
ctest --test-dir build/native --output-on-failure -R "^sith$"
cmake --build build/native --target sithDSSTests
ctest --test-dir build/native --output-on-failure -R "^sith\.DSS$"
```

These focused commands supplement the validation selected under
[Validation Scope](#backend-validation-scope).

The `rdroid.Math` suite compares vector normalization, reciprocal scaling,
axis-angle construction, and angle extraction against source-level arithmetic
oracles transcribed from `Jones3DRE/v1.0-dbg/indy3d-dbg.exe.c`. It checks exact
float representations (and NaN classification), including rounding-sensitive
inputs, zero/underflow/overflow behavior, singularities, range clamps, and the
existing near-axis guards. These oracles validate debug-source reconstruction,
not execution of the original x87 binary or bitwise parity with optimized retail
builds. They do not require game assets or invoke the original executable.

The `stdMath` group inside `stdTests` checks all sine/tangent table entries
against standalone binary fixtures, third-quadrant tangent interpolation,
finite-angle normalization across exponent ranges, inverse-trig polynomial
rounding, negative-power subnormals and signed zero, and damping with extreme
finite endpoints. Its inverse-trig cases pin binary32 C arithmetic, not x87
runtime parity. Reference provenance belongs in the
[math fixture documentation](../Libs/std/General/Tests/tv/stdMath/README.md),
not in test names or binary-address comments. Normal runs do not require the
original executable.

The fixture descriptions below cover the available `soundTests`, `smushTests`,
`sithTests`, and `sithDSSTests` targets.

<a name="audiolib-fixtures"></a>

### 🎙️ AudioLib Fixtures

The `AudioLib` tests cover packed mouth-position lookup boundaries, invalid
headers and times, the preserved final-timestamp and zero-entry lookup bugs, PCM format
rejection, exact silence and deterministic-waveform output blocks, independent
X/Y quantization, cross-sample PCM decoding, multiple analysis/sample rates,
terminal timestamps, checked input/output capacities, arithmetic limits, and
generated-block round trips. Generated timelines are queried at every
millisecond through and beyond their terminal entry. During
reconstruction validation, a temporary renamed copy of the decompiled original
was compared against the production implementation for 49,158 lookups and 72
generation combinations in both Debug and Release. The oracle was removed after
the results matched byte-for-byte; the permanent suite keeps independent golden
blocks and behavior assertions.

The ADPCM and WVSM codec functions and `AudioLib_ParseWaveFileHeader` are
covered by original-engine vectors captured from retail v1.0 `Indy3D.exe` and
stored in `Libs/sound/Tests/tv/AudioLib` (provenance, case lists, preserved
quirks and the inputs kept out of the corpus are in that directory's
`README.md`). The `TestOriginal*` cases compare complete outputs, return values
and compressor state, including chained block calls.

<a name="smush-fixtures"></a>

### 🎞️ Smush Fixtures

The `smush` suite (`smushTests`, CTest entry `smush`, labels `unit;smush`) links the smush library and runs
every area against the real functions of the other areas; the only mocks are the LEC services (share table, allocator,
timer), an in-memory file system and the executable's zlib `gz*` functions (through the `Zfilio_TestSetGz` seam). The pure
C `IDirectSound` mock went with the DirectSound driver `Dsnd.c` in M6.3. Each area has its own test file and CMake list
under `Libs/smush/Tests`: `SmushCoreTest.c` (SmushPlay, LecShare, Timer, Memory, List, Bitmap, Blit), `SmushSoundTest.c`
(Engine, Mixer, DsndNull), `SmushCodecTest.c` (D_bloc16,
D_wave, Decode), `SmushStreamTest.c` (Cdstream, Framestream, Stdfile, Stdio, Zfilio, Readauto, Frame, Pcplay) and
`SmushAsmTest.c`.

`SmushAsmTest.c` covers the six assembly files that were translated to C (`Bloc16Asm`, `DecodeAsm`, `MixerAsm`,
`BlitAsm`, `MemCopy`, `MemFill`) with original-engine vectors: inputs and complete results of every procedure captured by
executing the retail v1.0 `Indy3D.exe`, stored in `Libs/smush/Tests/tv` (format, reference executable hash, procedure
addresses and case counts in `Libs/smush/Tests/tv/README.md`). The test requires every byte of every buffer to match and
never launches the executable. The vectors include the assembly's quirks (zero counts that run the loop once, 16-bit
pitch multiplies, index carry-over in the three-byte table lookups), which carry `TODO: [BUG]` notes in the code.

Not covered by unit tests: the intro movie playback itself (needs the game data and a window; checked by running the
game), and the exact output of `mixer_Setup`'s sine and clip tables, which are computed with the DLL's CRT instead of the
executable's.

<a name="sound-fixtures"></a>

### 🔊 Sound Fixtures

The `Sound` fixture exercises `Sound_GenerateLipSync()` without opening an audio
device. Test-only controls replace channel position and decompression while the
production cache, allocation, generation, and mouth-position paths remain in
use. Coverage includes uncompressed and compressed input, lazy cache reuse,
blocks larger than the original 8192-byte temporary buffer, allocation cleanup,
playback window boundaries, time offsets, generated-data lookup, and every
seven-bit input to `SOUND_LIPSYNC_GETMOUTHLEVEL()`. Malformed
sound-bank metadata belongs to the `Sound_Load()` and `Sound_ImportBank()` test
fixtures because those functions own admission of data into the bank.

<a name="siththing-fixtures"></a>

### 📦 sithThing Fixtures

The `sithThing` fixture validates the reconstructed binary codec with standalone
retail-generated golden vectors covering every thing type, movement and control
block, resource name, sector reference, bounded name, inherited weapon data,
allocation failure, short I/O, and malformed block counts. Synthetic static-world
text sections additionally exercise text placement parsing, skip behavior,
performance filtering, wrapper serialization, binary restoration, runtime ID
assignment, and wrapper failure propagation without loading game assets.
Recalculated Euler angles use an absolute tolerance of `0.00001` degrees;
all other serialized fields remain byte-exact against the unchanged vectors.
The math suites separately validate the numerical evaluation and constants.

<a name="sithtemplate-fixtures"></a>

### 🧬 sithTemplate Fixtures

The `sithTemplate` fixture starts the thing parser and template cache together.
Synthetic text vectors cover blank and inherited templates, missing bases,
discarded type-less entries, duplicate names, capacity exhaustion, missing end
markers, static indices, and allocation failure. Binary tests then verify exact
round trips, ordered cache reconstruction, empty lists, short I/O, truncation,
and cleanup after partial deserialization.

<a name="sithvoice-fixtures"></a>

### 🗣️ sithVoice Fixtures

The `sithVoice` fixture exercises the gameplay mapping without opening an audio
device or loading actor assets. Test-only seams provide deterministic lip-sync
coordinates, game time, and swap operations while production builds retain the
original calls. Coverage includes every 4 x 4 lookup-table cell, the shipped
Y-only table layout, repeated-head variation, missing and existing M-sound
heads, targeted swap cleanup, dying speakers, and the original global timing
and cross-speaker state behavior.

<a name="sithdss-fixtures"></a>

### 📨 sithDSS Fixtures

The `sithDSS` and `sithDSSThing` fixtures compare the save/network message writers and readers of `sithDSS.c` and `sithDSSThing.c` with
original-engine vectors captured from retail v1.0 `Indy3D.exe` and stored in
`Libs/sith/Dss/Tests/tv/sithDSS` (provenance, regeneration, case lists and the
documented differences are in that directory's `README.md`). The writers and readers
run on a synthetic world (sectors, surfaces, things, AI blocks, puppets, cameras and
the gameplay globals) that is built by the same source code in the capture harness and
in the tests. The engine functions the DSS code calls with side effects are replaced by
shared stubs that record their arguments, so a case result holds the return value, the
complete message handed to `sithMessage_SendMessage`, the engine calls made and every
byte of the state that changed. Each case is run with two different fills of the unused
parts of the world and of the message buffers to reject accesses outside the data the
function is given. The engine globals of the original executable are redirected to
static memory in this suite, and the suite links its image at a fixed high base so the
normalized addresses of the synthetic state cannot collide with generated values.
The one expected difference (an intentional playback speed fix in
`sithDSS_ProcessPuppetStatus`) makes the matching test report as ignored.
