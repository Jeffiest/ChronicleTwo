# PC port

`PLATFORM=PC` builds the PAL game's code (`SCES_511.90`) as a native x64 program, compiled by clang as C++26, on SDL3 and Vulkan 1.4. ChronicleTwo ships only the PAL release, so PAL is the only region the port builds for. `docs/PC_PORT_PLAN.md` is the plan the port follows and records its phases.

The port has Phases 4-6 in: rendering, audio and movies. The executable loads its configuration, opens a window, indexes the game data, places the game's memory in arenas above 4 GiB, and runs the retail main loop on the shared renderer and mixer: the language select, the Level-5 logo movie (`L5LOGO.PSS`) and the copyright screens, the title over its 3D map with the title music, and the title's attract demo (the opening event with skinned characters). Every game frame is a recorded display list rendered once canonically, with interpolated display frames between ticks. Pressing START at the title starts the next mode, after which the run stops advancing frames (Phase 7). This document describes what is built.

## Building and running

### Linux

```sh
./build.sh linux-x64
tools/kazarov/bin/kazarov shell ctest --test-dir port/build/pc --output-on-failure -j4
```

`./build.sh linux-x64` builds in the Kazarov container, from the image in `tools/kazarov/image` (built locally the first time), with the Debug preset into `port/build/pc`. `linux-x64-release` builds Release into `port/build/pc-release`. `CLEAN=1` discards the build directory first; `JOBS=N` sets the parallel job count.

Without the container, `cmake --preset linux-x64`, `cmake --build --preset linux-x64` and `ctest --preset linux-x64` do the same work. The host needs what the image provides: clang 20 or newer with lld, CMake 3.25 or newer (the presets need it), Ninja, `glslangValidator`, SDL3 3.4, the Vulkan 1.4 headers and loader, and Python 3.

The renderer's cases need a Vulkan device with the features `tools/kazarov/port/gfx/README.md` lists under "Device". The container's Mesa lavapipe provides them, so no GPU is needed there.

`./run.sh linux-x64` builds, then runs `port/build/pc/darkchronicle` on a Linux host. It first extracts `data/` from `rom/pal/Dark Chronicle (PAL).iso` (or `rom/pal/extracted/iso`) with `dc2data`, under `.kazarov/locks/port-data.lock`, and skips the extraction when `data/.kazarov-data` matches the source and `dc2data`. The game holds the lock shared while it runs.

### Windows

```bat
.\build.bat windows-x64
.\run.bat windows-x64
```

`build.bat` and `run.bat` pass the configuration through Kazarov's host launcher to `tools/windows/kazarov.ps1`. That script builds into `port/build/<configuration>`. The first run fetches the toolchain into `.kazarov/windows`, through `tools/windows/acquire.ps1`: llvm-mingw 20260922 (clang and lld), Ninja 1.13.2, SDL3 3.4.18 (MinGW), glslang 16.6.0 and the Vulkan 1.4.363.0 headers. Each archive is checked against its SHA-256 before it is extracted. `windows-x64` is a Debug build; `windows-x64-release` (and `windows`) is RelWithDebInfo.

`run.bat` extracts `data\` from `rom/pal/Dark Chronicle (PAL).iso` (or `rom/pal/extracted/iso`) with `dc2data.exe` before launching, under the `port-data.lock` lock, and skips it when `data\.kazarov-data` matches the source and `dc2data.exe`, as the Linux launcher does. The game holds the lock shared while it runs.

The scripts need PowerShell 7, 7-Zip, CMake and Python 3, found on `PATH` or at their default install locations.

The test build is separate. After a `build.bat` run has fetched the toolchain:

```powershell
./tools/windows/configure-tests.ps1 -Root .kazarov/windows -BuildDirectory port/build/windows-x64-tests
./tools/windows/build.ps1 -Root .kazarov/windows -BuildDirectory port/build/windows-x64-tests -Tests
ctest --test-dir port/build/windows-x64-tests --output-on-failure -j4 -E '^(GfxMisc|TexDecode)\.'
```

`configure-tests.ps1` links the Vulkan loader from `System32`. CI, whose runner may have no loader there, calls `configure.ps1 -Tests -VulkanLibrary` with the runtime it downloads. The `-E` filter drops the two renderer cases that need a Vulkan device, which the CI runner lacks. The executables need `vulkan-1.dll` beside them; CI copies it into the build directory.

### macOS

```sh
./build.sh macos-arm64
ctest --preset macos-arm64
```

`macos-arm64` is a Debug build with the Homebrew preset in `CMakePresets.json`, into `port/build/macos-arm64`. `macos-arm64-release` is the Release build. The preset expects Homebrew's LLVM under `/opt/homebrew/opt/llvm`, plus `sdl3`, `vulkan-headers`, `vulkan-loader`, `glslang`, `molten-vk`, `googletest` and `nlohmann-json`. To run the game, SDL needs the loader by its full path, and the loader needs the MoltenVK driver:

```sh
export SDL_VULKAN_LIBRARY="$(brew --prefix vulkan-loader)/lib/libvulkan.1.dylib"
export VK_DRIVER_FILES="$(brew --prefix)/etc/vulkan/icd.d/MoltenVK_icd.json"
```

### The PS2 build

```sh
./build.sh ps2-pal
BUILD_DIR=build/pal python3 scripts/build/verify.py -c
```

`./build.sh ps2-pal` builds `build/pal` in the container. `verify.py -c` checks the rebuilt executable against retail. Every change to `ps2/` must leave that check passing (see "Keeping the PS2 build matching").

### darkchronicle

```
darkchronicle [--data DIR] [--save DIR] [--headless] [--offscreen]
```

| Option | Meaning |
|---|---|
| `--data DIR` | the extracted game data; `--data=DIR` also works. Default: `DC2_DATA`, then `./data`, then `data/` beside the executable, then the installed location (`%LOCALAPPDATA%\chronicletwo\data` on Windows, `$XDG_DATA_HOME/chronicletwo/data` or `~/.local/share/chronicletwo/data` elsewhere) |
| `--save DIR` | saves and the pipeline cache; `--save=DIR` also works. Default: `DC2_SAVE`, then `save/` if it exists beside the working directory or the executable, then `save/` beside a local `data/`, then the installed save directory. Created on first use |
| `--headless` | SDL's offscreen video driver with a Vulkan headless surface (`VK_EXT_headless_surface`). Without that extension it behaves as `--offscreen` |
| `--offscreen` | `--headless` without a Vulkan surface: frames are rendered to an image only |

Any other argument is a usage error (status 2).

Environment, with the `DC2` prefix that `GameSetIdentity` sets:

- `DC2_DATA`, `DC2_SAVE`: as above.
- `DC2_AUDIO=off`: no audio device.
- `DC2_AUDIO_WAV=PATH`: write the mix to a WAV file in place of the device.
- `DC2_AUDIO_TRACE`: any value other than empty or `0` traces the audio calls.

Debug builds ask for the Khronos validation layer (`gfx::RendererConfig::validation`, on unless `NDEBUG` is defined). Release builds do not. Without the layer installed, the renderer prints `validation requested but VK_LAYER_KHRONOS_validation is not installed` and goes on. There is no environment switch for it.

Exit statuses (`port/src/exitcodes.hpp`): 0 for a normal exit, 2 for a usage error, 4 for a game assertion (retail's `__assert`). A `PS2_UNIMPLEMENTED` stub aborts instead of exiting, so a debugger or a core dump stops at it: SIGABRT on Linux (status 134 from a shell) and `0xC0000409` on Windows.

The test `Smoke.HeadlessFramesScreenshot` (`port/src/tests/CMakeLists.txt`) runs `darkchronicle --headless --offscreen --frames 3 --screenshot` on an empty data directory. It passes when the run exits 0 after a `dataread: indexed` line and writes a PNG. It runs under a generated CMake script, because ctest counts a process killed by `abort()` as failed even when `PASS_REGULAR_EXPRESSION` matches its output; the script checks the exit status and the file.

## What Phase 1 does

1. `main` (`port/src/main.cpp`) sets the paths and the window identity (`port/src/identity.cpp`), parses the options and opens the window, or the offscreen surface with `--headless`. It then initialises input and the renderer with a logical size of 512x480 at a 4:3 display aspect. The renderer builds every pipeline during `RendererInit`, uses `save/pipeline_cache.bin` as its pipeline cache, and prints `compiling shaders n/total` as it goes.
2. The game's static constructors run before `main`. The three VU0 helpers they need, `mgUnitMatrix`, `mgZeroMatrix` and `mgZeroVectorW` (`port/src/mg_math.cpp`), are real C++. On the PS2 the constructors run after the IOP set-up inside `mwInit`; on the host `mwInit` is empty (`port/src/runtime.cpp`). An SIF RPC client that no game code has bound yet gets an answer from `sceSifCheckStatRpc` and `sceSifCallRpc` instead of an abort, because the constructors reach the RPC layer before the game binds its clients (`tools/kazarov/port/sce/sifrpc.cpp`).
3. `main` calls `MainLoop()` (`ps2/src/mainloop.cpp`), which runs the game's own set-up and calls `mgInit`.
4. In Phase 1 `mgInit` (`port/src/mglib.cpp`) was a `PS2_UNIMPLEMENTED` stub, so the run aborted there. It is implemented now; see "What Phase 2 does" and "Rendering".

## What Phase 2 does

`darkchronicle --headless [--offscreen] --data data` loads `config.json`, creates the window and renderer (504 pipelines precompiled), sets the clock to `game.tick_rate` (default 50), and indexes `data/`, printing `dataread: indexed N files under <dir>`. The disc's `DATA.DAT` holds 8751 files. It then runs the retail `MainLoop`, which calls `mgInit` on the shared renderer (see "Rendering"). The first unported call is `mgCDrawPrim::Begin2`, which aborts with:

```
port/src/mg_drawprim.cpp:6: Begin2: PlayStation 2 code is not implemented on PC
```

### Config

The configuration is `<save root>/config.json`, with the sections `game`, `video`, `audio` and `input`. They fill Kazarov's `HostSettings`. An invalid value keeps its default and is reported on stderr. A missing file is written with the defaults. A file that exists but cannot be read is left untouched and is never written in that session.

`SettingsChange` stores the new settings, saves them, calls `DisplayChanged`, and calls a change hook. Through that hook `main` applies `ClockSetTickRate` and `InputApplyConfig`.

### Memory

`mgCMemory` is replaced whole (`port/src/mg_memory.cpp`). Its pointers are host pointers, and the `mgMEMORY_BLOCK` header is two quadwords on the host. Blocks are 64-byte aligned through `uintptr_t`. The arenas themselves are described in "Arenas" below.

Each arena's high-water mark is tracked, and `MainLoop` logs `mg_memory: <name> peak P of C quadwords (F free)` after every loop exit. C includes the headroom, so `main_buffer`'s is four times retail's 0x1A0000 quadwords. The loops cannot run yet, because `mgCDrawPrim::Begin2` aborts first. Once they run, this line is how the headroom for 8-byte pointers is measured.

### MainLoop

`port/src/mainloop.cpp` is the retail sequence. It differs in three ways: pointer alignment goes through `uintptr_t`; the static `InitPadTable` and `VSyncCallBack` are reached through renamed stubs; and file paths are passed as writable arrays. `InitCDFile` runs from `main` before `MainLoop`, after the main-thread priority is set, as retail does.

### Decision 1: long64 is kept

The PS2's `long` is 64-bit, and on Windows (LLP64) it is not. `scripts/port/long64.py` rewrites the build-tree copies of `ps2/src` and the `ps2/include` mirror, on every host, so the port keeps the PS2's width:

- `long` becomes `long long`, `L` and `UL` integer suffixes become `LL` and `ULL`, and `%l` integer conversions become `%ll`.
- It tokenizes, so comments, string and character literals (other than printf conversions), `long double`, `long long`, identifiers containing `long`, and the MIPS `%lo(` relocation operator are untouched. Line numbers are preserved.
- Over the real sources it changes 87 tokens in 21 files: 80 keywords, 6 hex `UL` suffixes, and 1 format (`password.cpp`'s `"err %lu\n"`). It changes no line count and no string otherwise.

Proof is by test. `scripts/port/test_long64.py` has 91 cases. The `Long64` GoogleTest suite (`port/src/tests/long64_test.cpp`) runs retail `mgSetPkTEX0`, where both forms write full 64-bit TEX1, TEX0 and TEXA words, and retail `password.cpp`, where `ConvertBinToTxt` and `ConvertTxtToBin` round-trip 64-bit groups up to `0xFFFFFFFFFFFFFFFF`, and `EncodePassword` and `DecodePassword` round-trip and reject tampering. Both run on Linux and Windows.

This closes the Phase 1 open items on `u_long`, `mgSetPkTEX0` and `password.cpp`. No per-function `u64` overrides are needed.

### Decision 2: threads

Kernel threads are cooperative fibers. See "Threads and timing".

## Arenas

The blocks come from `ArenaBlock` in Kazarov's `tools/kazarov/port/platform/arena.hpp`, the same code Dark Cloud 1 uses. Each block is zeroed, 64-byte aligned and followed by a guard page, and holds `kArenaHeadroom` (4) times the quadwords retail asked for. A debug build checks that a block lies above 4 GiB, so a mapping that drifted low cannot hide a pointer the game truncates (see "Pointers and 32-bit integers"). `port/src/tests/arena_test.cpp` covers roots, children, the guard page and placement growth.

Retail buffers that lie inside no mapped block become blocks. `MainLoop` maps `main_buffer` (`MainBuffer`), `InfoBuff` (`InfoStack`) and `SystemSeBuff` (`SystemSeStack`) explicitly through `ArenaRoot` (`port/src/arena_root.cpp`). `mgCMemory::stSetBuffer` and `SetHeapMem` treat any other buffer that lies outside every block the same way: retail statics, function-local arrays and member arrays (`editdata.cpp`, `gamedata.cpp`, `inventmn.cpp`, `character.cpp`, `mg_visual.cpp`, `menuaqua.cpp`, `scenesnd.cpp`, `event_func.cpp` and others) are roots. A root is keyed by the retail buffer's address, takes a copy of the retail bytes (`gamedata.cpp` memsets its buffer before handing it over), and gets four times the quadwords. Each mapping is logged once, as `arena: <name> <bytes> bytes at <address> (above 4 GiB)`.

A buffer inside a block is a child view, and it stays exactly where retail carves it. Dark Chronicle carves sub-arenas from a parent's top (`stGetTop`, `stGetRest`), often through local `mgCMemory` objects whose allocations outlive them, and re-carves overlapping regions by design (`menuchr`, `menusys`, `inventmn`, `dng_main`, `editloop`). Per-object blocks, as in Dark Cloud 1, would break that. An audit of every `stSetBuffer` and `SetHeapMem` site found more than 20 that depend on it. Headroom reaches a child through its parent, since most children take the parent's rest.

`stAlloc` and `Alloc` past an arena's end abort, naming `mgCMemory::name` or, when it has none, the arena's address, where retail printed and returned NULL. `stAllocTest` keeps retail's probe.

Retail places objects in runs with hand-written quadword counts. The placement operators in `port/src/mg_memory.cpp` grow the run to the host object's size while it is still its arena's last, as Dark Cloud 1 does. `arena_placement_test.cpp` lists the 51 literal-count sites whose host object outgrows its run, such as `CCharacter2` (2496 bytes in 0x68 quadwords), and the 8 cast-not-placed runs that nothing can grow: the script stacks `RS_STACKDATA` and `RS_CALLDATA` (`actscript.cpp`, `effscript.cpp`, `event.cpp`, `runscript_opcodes.cpp`) and `mgSORT_PACKET` (`mg_drawprim.cpp`). Each of those overruns its run on the host until `ps2/src` sizes it with `sizeof`.

On Windows the executable is linked with `--dynamicbase` and `--high-entropy-va`, lld's MinGW defaults pinned in `port/cmake/arena.cmake`. The image base is 0x140000000, relocated by high-entropy ASLR, and the image is `LARGE_ADDRESS_AWARE`, so the image and the `VirtualAlloc` mappings lie above 4 GiB, and a truncated pointer faults at once. Linux stays `-pie -z separate-code`. Dark Cloud 1 links Windows with the same defaults.

## Threads and timing

PS2 kernel threads run as cooperative fibers on the one OS thread: ucontext on Linux, Win32 fibers on Windows. `CreateThread`, `StartThread`, `RotateThreadReadyQueue` and the semaphores are real.

Switches happen only in `StartThread`, `RotateThreadReadyQueue`, a blocking `WaitSema`, `SignalSema` preemption, and thread exit. A switch attempted inside `ClockPump` (its callback or hooks) aborts; death tests on both OSes cover this. An uncontended semaphore pair never yields. A `WaitSema` with nothing else ready aborts as a deadlock.

`WaitVSync` waits, then pumps every tick with the rotate. `sceGsSyncV` goes through `ClockSyncV`. `mgNowFrameRate` comes from the wall clock via `mgTimeFrameFields()`, as seconds times `ClockTickRate`, the same unit as retail's `T0/262`.

The reason for this design: the retail thread loops (`GamePadStep`, `NowLoadingLoop`, fishing's `StepDataLoading`, the movie threads) run unchanged, and host state stays on the main thread.

## Rendering

`port/src/mglib.cpp` replaces retail's `mglib`. Its `PORT_OVERRIDE` definitions are the only mglib bodies the port links. `port/src/mglib_port.hpp` is the seam the Phase-4 units use. It is frozen: changes to it are additive only. Its groups:

- Frame: renderer state, the frame count, and `--frames` exit.
- Projection: the eye, clip and view-to-clip mapping.
- GS state: the context-1 registers (TEST, ZBUF, ALPHA, TEXA, CLAMP, TEX1), scissor and fog colour.
- Textures: the lookups from `mgCTexture` and TEX0 to gfx textures, and the bindings.
- Render targets: the main target, the render targets and the current target.
- Submission: 2D and mesh draws, and the VU program id.
- Readback: colour of a target and the depth queries.

### Frame

`mgBeginFrame` opens a display-list recording (`gfx::BeginRecording`), not an immediate frame. It sets the main target, CLAMP repeat and the default frame buffer, and clears the colour to `mgBackColor` and the depth to far. The game's draws between `mgBeginFrame` and `mgEndFrame` are recorded into the tick's list. Draws keep going through `mgPortDraw2D`, `mgPortDrawMesh` and `mgPortDrawMeshImmediate`.

`mgEndFrame` queues the four `mgPickZBuff` 8x8 reads. A pick within 4 pixels of the screen edge gets `z` of -1, as retail does. It seals the list and renders it once, canonically. That render ends the gfx frame and fills `mgPickZBuff[].z` in GS units. `--screenshot`, `mgStoreImage`, `mgPortCanonicalReadColor`, `StoreImage`, `mgPickZBuff` and the depth queries (`mgPortQueueDepthRead`) all read that canonical render, never a display frame. It then waits for the next game frame (see "Ticks and display frames"). It sets `mgNowFrameRate` from `mgTimeFrameFields()`, with retail's upper clamp of `1 + mgFrameRate`. When the frame count reaches the `--frames` limit, it writes the screenshot and exits before the wait.

### Ticks and display frames

- One game frame is one pass of the game's loop, and one recorded list. It spans `mgFrameRate` clock ticks: 2 on the 50 Hz tick, so 25 game frames a second. `mgFrameRate` keeps its meaning.
- After the canonical render, `mgEndFrame` calls `PresentWaitFrame`. It waits the same tick count `WaitVSync` waited for; the rotating-thread and pump behaviour is unchanged. Meanwhile `present.cpp` renders display frames from the newest list and the one before it. Each display frame's alpha is the elapsed fraction of the game frame: the ticks since the frame began plus the fraction of the current tick, over `mgFrameRate`. An alpha from the clock tick alone would run 0 to 1 twice per game frame at `mgFrameRate` 2. Display frames are presented as fast as `video.present_mode` and `video.max_fps` allow (`max_fps` 0 is unlimited). A game frame whose wait presented nothing presents its canonical image afterwards (`PresentTickEnd`).
- Interpolated: the models of tagged mesh draws, and the camera (`tools/kazarov/port/gfx/README.md`, "Display lists"). A draw is tagged with `mgPortInterpScope(object, teleport_distance, blend_vertices, no_interpolation)`, keyed by the `mgCFrame` or, outside a frame, the visual's record. `kMgPortFrameTeleportDistance` is 200 units.
- Not interpolated: 2D without depth (the HUD is tick-exact); 3D sprites' own motion; objects and cameras past the teleport thresholds; the first tick after `mgInit` and after `mgPortCutInterpolation()`, which is a mode change (the call from `MainLoop` is pending in `mainloop.cpp`); skinned poses, until W4 interpolates the bone palette.
- `video.interpolation = false` presents each canonical image once.
- Headless, the clock is unbounded: one canonical render and one present per game frame. `--display-per-tick N` also renders N display frames at k/N and discards them. Screenshots are byte-identical with it (`PresentIdentity.DisplayPerTickScreenshot`).
- Frame grabs: `mgPortRenderTarget(..., frame_target = true)` makes a gfx frame target, which a display render grabs again from its own image.
- The FPS counter: `video.show_fps` turns it on (F3 and the `fps_toggle` host action flip it; headless only with `--show-fps`). It is drawn with the host's 5x7 font through `OverlayRecord`, as a display render's overlay, so no canonical image or screenshot holds it. Its text is set by `video.fps_detail`; with all fields it reads `FPS 143.9  TICK 50.0/50  DRAWS 412`. TICK counts clock ticks per second, so it reads the tick rate whatever `mgFrameRate` is. DRAWS is the newest list's mesh plus 2D draws. `--log-fps` prints the same text on stderr once a second.

### Packets

No packet is built or sent, so there are no packet buffers. `mgInitVif1Packet` and `mgSetPacketBuffer` ignore their buffers. `mgVif1Packet` points at one empty sentinel, because `mgCDrawPrim` disables itself when it is null. `mgDrawDirect` passes every draw the same non-null scratch tag, which selects the immediate path and is never written, and returns the sizes the draws report (0 on PC). The data buffers (`mgSetDataBuffer`, `mgGetDataBuffer`) stay: the draw manager's sort lists and the visuals' per-draw records are allocated from them. `SendDMA` is empty. `mgSendVuProg` only records the program id, which `mgPortVuProg` returns. VU programs are never uploaded: the `linknames.cpp` symbols are placeholders.

### Initial GS state

`mgPortResetGs` sets the state `My_dma_start0` leaves the GS in (`docs/vu1/dma_start0.md`, 8.1): TEST `0x5000B`, ALPHA `0x44`, TEXA `0x0000008000008080`, TEX1 `0x61`, fog colour 200 on each channel, and SCISSOR 0..639 by 0..223. `mgSetPkFrameBuffer` then resets the scissor to the target's size. `mgInit`'s C-side caches keep retail's values (for example `mgTEXA_1` is `0x100400000`), because only CPU readers see them.

### Projection

`mgRENDER_INFO::SetRenderInfo` still fills the 2048-centred matrices, for CPU readers. `mgPortEyeToClip` maps view space to Vulkan clip, so a point lands on the same logical pixel that retail's screen matrix gives it. It uses `eye`, `aspect_y` and the target's logical mapping. The output is reverse-Z, with near at 1 and far at 0. `mgPortWorldToClip` and `mgPortMeshTransform` are built on it.

GS Z is `depth * 1.67e7`, or `depth * 65000` for 16-bit Z (`mgPortDepth` and `mgPortGsDepth`).

### Render targets

`mgSetPkFrameBuffer` on the draw buffer's FBP selects the main target. Any other FBP, and any texture passed by pointer, gets a gfx render target keyed by TBP0. A texture's pointer is also recorded against its TBP0. The target is created on first use and recreated on a size or format change. Because it is keyed by TBP0, a texture never holds a destroyed handle.

`mgPortFindTexture(tex0)` resolves the draw buffer's TBP0 to the main target and the display buffer's to the previous frame. Other TBP0 values are looked up as render targets, then by the registry. The texture registry installs its lookups with `mgPortSetTextureLookup`.

### Images

`mgSetPkMoveImage` copies with `gfx::CopyTexture` when given a destination offset, and stretches with `gfx::BlitTexture` (linear) when given a destination rect.

`mgStoreImage` reads the source back and writes the texture's PSM layout linearly, sampling the source at the texture's size. Inside a frame it reads the copy cached at the previous canonical render. 16-bit formats keep one alpha bit.

`mgStoreZBuffImage` fills the rect, rounded down to 8x8, with one depth value. That value is the answer to the previous call's query for the same rect, or 0 when there is none. It is an approximation.

### Shadows

`mgBeginDrawShadow` draws into a `share_main_depth` target, registered under the shadow's frame-buffer address before `mgSetPkFrameBuffer` runs. `mgEndDrawShadow` composites it onto the main target as a quad, with ALPHA `(0 - Cd) * As + Cd`, FIX 64, TEXA AEM set with TA0 48, and an alpha test of `>= 1`.

### Font and headless

`mgInitFont` returns 0 and `mgCloseFont` does nothing. The port keeps no console, because retail never draws it: `font_draw_flag` is never set.

`--frames N` exits after frame N. `--screenshot PATH` writes that frame as a PNG first; a failed write exits with status 1. `--screenshot` without `--frames` is a usage error. `--input SCRIPT` loads the script at start-up. The pump applies it on each tick with `mgPortFrameCount()`, so pad input follows the game's frame count.

Draws are recorded into display lists (see "Ticks and display frames").

### Window aspect

- Logical space stays 512x480 (`mgScreenWidth`/`mgScreenHeight`); `mgScreenOffx`/`mgScreenOffy` are the 2048-centred offsets. Nothing in `port/src` outside `platform/` and `mglib.cpp` reads the window's pixel size. The grep gate `grep -rnE "SDL_GetWindowSize|SDL_GetWindowSizeInPixels|\bWindowSize\(|WindowCurrentMode|DisplayShownMode|SDL_GetCurrentDisplayMode" port/src --include=*.cpp --include=*.hpp | grep -vE "^port/src/(platform/|mglib\.cpp)"` prints nothing.
- `video.aspect` `auto` is `gfx::AspectMode::Fill`. `mgPortEyeToClip` maps retail's projection through the target's logical mapping, so the frame's 480 rows fill the window's height at retail's vertical field of view. A wider window shows more at the sides; a narrower one keeps the width and shows more above and below. `4:3` is `gfx::AspectMode::Letterbox`, byte for byte the letterboxed frame. `video.ui_scale` (0.25 to 4) scales depthless 2D about the centre. HUD anchoring is W4's.
- Culls: `mgPortVisibleExtent()` (`gfx::VisibleLogicalRect` of the current target, in screen units, with centred half extents and GS coordinates) replaces retail's 256/240 half extents in `mgCFrame::GetDrawRect` (`mg_frame_asm.cpp`). It also replaces them in `mgRENDER_INFO::screen_box_min`/`max`, which retail's `SetRenderInfo` sets from the screen size. `mgSetRenderInfo` is overridden to widen them, and `mgBeginFrame` refreshes them every game frame, since the window can change shape between scenes. `mgCFrame::Draw`'s `mgClipBoxW` reads them. A mesh at logical x = -40 is drawn at 16:9 and culled at 4:3 (`MglibAspect.*`). The GS guard band (2048) is unchanged.
- A layout change (`video.aspect`, `video.ui_scale`) is applied from the pump outside a recording.
- `--window WxH` sets the window size (headless: the offscreen image's).
- Config keys: `video.interpolation` (`true`), `video.max_fps` (`0`), `video.present_mode` (`fifo`, `mailbox` or `immediate`), `video.aspect` (`auto` or `4:3`), `video.ui_scale` (`1`), `video.show_fps`, `video.fps_detail`.

## Audio

The sound driver runs on Kazarov's shared audio stack. `CSound` in `port/src/sound.cpp`, with `snd_mngr.cpp`, `ezmidi.cpp` and `ezbgm.cpp`, plays through `audio::DefaultMixer()` (`tools/kazarov/port/audio/mixer.hpp`). The port has no IOP, no SIF RPC and no SPU2 path. The mixer's 48-voice synth stands in for the SPU2.

`AudioInit()` and `AudioShutdown()` (`port/src/audio_init.cpp`) start and stop the output. `DC2_AUDIO=off` starts none, and `DC2_AUDIO_WAV` writes the mix to a file in place of the device (see "darkchronicle").

### Banks and sequences

`sndLoadSound` (`port/src/snd_mngr.cpp`) passes each pack's `hd` and `bd` to `SoundLoadHdBd` and `SoundLoadHdBdAdd`, and each `sq` to `SoundLoadSeq`. They take pointers (`port/src/sound_port.hpp`).

The retail `CSound::LoadHdBd`, `LoadHdBd2`, `LoadHdBdAdd`, `LoadSeq` and `TransHdBd` take addresses in `int`. The port overrides them to refuse and print a line to stderr. The port does not round-trip 32-bit pointers.

Banks are parsed and decoded once, by `audio::Bank::Create`. A port holds up to 16 banks (`MIDI_PORT_BANK_MAX`). Bank 0 is bound with `Mixer::BindBank(port, bank)`. Added banks are bound with `BindBank(port, n, bank)`, and a channel selects one with the bank-select controller that the SE calls send. The port layout (linked ports, dependent ports and `ezmidi_param` attributes) is the table that retail `CSound::Init` loads, kept in `sound.cpp` as `kLayout`. `TransBdState` always reports the transfer complete.

### Sound effects

`SE_Play`, `SE_SetVol`, `SE_SetPan`, `SE_Stop` and `SE_SetPitch` queue the same short and HS messages that retail queued for `libmodmsin`. `CSound::Step` delivers them through `Mixer::ShortMessage` and `Mixer::HsMessage`. The HS pitch message (`FD 02`, 14-bit, centre `0x2000`) bends the effect voice over its split's bend range.

Retail `SE_Play` builds a pitch message and overwrites it before sending. The port sends no pitch message at all.

### Master volume and reverb

`SetMasterVol(core, volume)` clamps `volume` to 0 to `0x3FFF` and sets the core's gain through `Mixer::SetCoreVolume`. `SetReverb` and `set_spu` go through `Mixer::SetReverb`. Retail also resets the core's master volume to full there, and the port does the same. `SndInReverb` has nothing to switch, because stream channels do not enter the effect units.

`sndWaitSema` and `sndSignalSema` are empty. The game's threads are fibers on one OS thread, and nothing in those sections yields (see "Threads and timing").

### Streams

`ezBgm` drives 16 `audio::StreamChannel`s, attached to the mixer on first use. An open reads the SOUND.DAT entry extracted to `data/sound/<name>` (see [SOUND_DAT.md](SOUND_DAT.md)). Names with a device prefix, such as `cdrom0:\VOICE\...WV;1`, go through the data index and fail. Voice packs (`StreamOpenFromFPLFast`) fail too, because those files are not on the PAL disc. Opens are synchronous, so `StreamOpenState` is always 0.

| Command | Effect |
|---|---|
| `0x10` | quit: nothing |
| `0x30` | close the channel |
| `0x40` | preload: nothing |
| `0x50` | play |
| `0x60` | stop, keeping the position (used for pause) |
| `0x70` | seek to the start |
| `0x80` | volume, `(left << 16) \| right` |
| `0x8000` | init: nothing |
| `0x80B0` | state |
| `0x80C0` | mode |
| `0x80D0` | info: bit 0 is stereo |
| `0x80E0` | level |
| `0x8020`, `0x80F0`, `0x8A00` | opens, only through `EzBgmOpen` and `EzBgmOpenFromPack`, because their argument is an address |

The state word holds the mode in its low byte, `0x1000` while playing, and `0x8000` when the data runs out. The driver clears `0x8000` once its buffers drain. The port holds `0x8000` for one second of output and then reads 0. A failed open leaves the channel unopened, with state 0, as on retail.

A non-zero low nibble in the mode turns looping on. A looping stream restarts at the start of the data, as described in [SOUND_DAT.md](SOUND_DAT.md).

The level is the raw 16-bit sample pair at the play position, left in the low half and right in the high half. `_STREAM_SILENT_CHECK` (`ps2/src/event_func.cpp`) reads it and treats both halves within 10 of zero as silent.

Without an output (`DC2_AUDIO=off` or no device), nothing renders the mixer. `CSound::Step` then advances the channels by one tick of samples (`EzBgmStep`), so streams still end.

### ezMidi

The retail `CSound::Stop`, `SetVol`, `SetStereoMode` and the stream methods stay as they are. `ezMidi` serves the scalar commands: `0x00` play, `0x20` stop, `0x30` rewind, `0xA0` attribute, `0xB0` volume, `0xC0` stereo (1) or mono (0), `0x8090` is playing and `0x80E0` volume. The commands that carry addresses have no host equivalent. `ezMidi` traces them and returns 0.

### Open

The title is reached headless. `DC2_AUDIO_WAV=title.wav darkchronicle --headless --input <script> --frames 2500` writes 60 s of 48 kHz stereo: silent through the language select, then the logo and title music (RMS about 750 at the title, peaks near 24,000). The capture is closed cleanly on a `--frames` exit: `main` registers `AudioShutdown` with `atexit`, so the output stops before the mixer's statics are destroyed. The real-data tests `SoundRealData.*` (`port/src/tests/sound_test.cpp`) load `snd2/SY_000.snd`, every `snd2/bgm` pack and a SOUND.DAT entry through the driver.

`sndCSeSeqData::LoadSMF` (`ps2/src/snd_seseq.cpp`) adds the SMF header size to a `uptr` since W2.u.

## Movies

`port/src/movie.cpp` overrides `CMovie::Load` (the seven-argument overload; the other two forward to it), `Play`, `SwitchThread`, `Term`, `EndCheck` and `IsStarted`. Nothing else reaches the retail player, so its three threads, the VBlank and DMAC channel 2 handlers, the IPU and DMAC channel 3/4 register writes, `setImageTag`'s GIF chains and the IOP transfers have no caller and are dropped by `--gc-sections`, and so are the `libmpeg` stubs. One movie plays at a time, as the retail file-scope statics did.

`MoviePortPath` maps the game's name to `data/movie/<name>` lower-cased, with a device prefix, a leading separator and a `;1` version removed (`L5LOGO.PSS` -> `data/movie/l5logo.pss`). `movie/` is outside the DATA index, so it does not go through `DataReadFind`. A missing or non-MPEG-2 file logs `movie: <name> not found or not an MPEG-2 PSS (<path>)`, `IsStarted` still returns 1 so the callers' busy-waits end, and `EndCheck` returns 1 at once (the PAL disc has no `TUTO`/`TUTO2` manual movies).

The pipeline is in `port/src/video/`. `pss.*` demuxes packs into video (stream 0xE0) and PCM (private stream 1, substream 0xA0) chunks with their PTS. `mpeg2_decoder.*` wraps libavcodec's `mpegvideo` parser and `mpeg2video` decoder, single-threaded, YUV 4:2:0, pictures in display order with 90 kHz PTS (extrapolated by the frame duration where a picture has none), and rejects a stream whose sequence header has no MPEG-2 sequence extension. `yuv.*` converts BT.601 limited range to RGBA with nearest chroma per 2x2 block, as the IPU's colour space conversion did, alpha 0xFF (the renderer's opaque, GS 0x80). `pcm.*` reads the soundtrack. `player.*` ties them together.

There are no threads. The game calls `SwitchThread` once per game frame and `EndCheck` beside it; both advance the player to the movie time elapsed since `Play`, `(ClockTickCount() - tick at Play) * 90000 / ClockTickRate()`. The newest picture whose PTS is due is shown; pictures overtaken after a stall are decoded but not converted. On the default 50 Hz tick with `mgFrameRate` 2 a 25 fps movie shows one picture per game frame; headless runs on the unbounded clock are deterministic.

The picture is uploaded into the texture the game names in `Play` (`"moviework"`), found with `mgTexManager.GetTexture(name, -1)` and `mgPortFindTexture`, with `gfx::UpdateTexture` (recorded into the tick's display list when one is open). That is what the PS2 did: `setImageTag` uploaded into the same texture and the game draws it itself as a 2D quad (the copyright screen's `L5LOGO.PSS`, `RUSH.PSS`, the movie viewer, the event script's `_LOAD_MOVIE`, the manual), so its fades, stretch and letterbox stay the game's, tick-exact.

There is no deinterlacing; fields are woven, as the IPU output them. `L5LOGO.PSS` codes all 150 pictures with `progressive_frame = 0`, every other disc movie with 1. Measured on the luma of the first 1,500 pictures of each of the nine movies, the mean difference between adjacent lines (opposite fields) is at most 0.83 of that between lines two apart (the same field) on `L5LOGO.PSS` and at most 1.008 anywhere: there is no comb, so a blend would only blur.

The soundtrack is 16-bit PCM in private stream 1, substream 0xA0, behind a 4-byte sub-header: `SShd` (type 1, 48,000 Hz, 2 channels, 0x200 bytes per channel block, interleaved left block then right block) then `SSbd` and the body. `Load` reads the whole track before the first picture (`L5LOGO.PSS`: 1,154,048 bytes, 6.01 s), aligns it by the first PCM PTS against the first video PTS, and `Play` hands it to the shared mixer as an `audio::PcmTrack` with `Mixer::SetStream` on port 5 at full port volume (`kMoviePortAudioPort`: `GetCSndPortNo` never maps a game port there). `Term` stops and clears it. The track is read whole because the mixer's existing API plays complete tracks; a streaming channel would replace that. `init_sound` (which reset the PS2 sound driver), the memory pools and the width and height arguments are ignored: the host allocates and the picture size comes from the stream.

The game links only libavcodec and libavutil (plan section 3.9; `port/cmake/movies.cmake`); no libavformat, libswscale or libswresample symbol is referenced. On Windows `darkchronicle.exe` imports `avcodec-61.dll` and `avutil-59.dll` only; `swresample-5.dll` ships beside them because BtbN's `avcodec-61.dll` depends on it.

Tests:

- `video_mpeg2_test`: MPEG-1 and header-less streams are rejected; `L5LOGO.PSS`'s first picture is a 512x416 I-picture at PTS 4758, the black frame the logo fades in from; all 150 pictures decode in increasing PTS order.
- `video_yuv_test`: hand-computed pixels, limited range, chroma siting, strides.
- `movie_test`: PCM layout, the player against the clock, `CMovie` through the glue, and a 30-frame play of `L5LOGO.PSS` through the `"moviework"` texture to a PNG compared with the decoded picture.

The `L5LOGO.PSS` cases skip without `data/`, so on Linux they run only in the main checkout.

## Layout

The root `CMakeLists.txt` selects the platform: `PS2` builds `ps2/`, and `PC` enables testing and builds `port/`.

- `ps2/src` and `ps2/include` hold the game as the PS2 build compiles it. `ps2/CMakeLists.txt` and `ps2/cmake/` are that build. Port code does not live there, except the `#ifndef PORT` guards and the edits listed in "Keeping the PS2 build matching".
- `port/src` holds code only the port compiles. `main.cpp` starts the game. `identity.cpp` sets the game's name and directories. `<unit>.cpp` replaces functions of `ps2/src/<unit>.cpp`. `*_asm.cpp` defines the functions `ps2/src` writes in MWCC assembly. `linknames.cpp` supplies link-time names (below). `tests/` holds the GoogleTest cases, one process per case.
- `port/include` holds `port.h`, included ahead of every unit the port compiles, and `stubs/`, the per-unit headers (below).
- `tools/kazarov` is the Kazarov submodule (`TheMoonPeople/Kazarov`, branch `main`). `tools/kazarov/port` holds the code both games share: `gfx`, `audio`, `platform`, `sce` (the SDK replacements) and `tests`. `cmake/KazarovPort.cmake` gives the port its targets:
  - `kz_port_host_library(ct_host)` builds `gfx`, `audio` and `platform`.
  - `kz_port_sce_sources(... LIBS libvu0 libpad libmc sifdev sifrpc eekernel libcdvd libgraph libdma libpkt eekernel_threads libdev libmpeg libsdr modmsin sifdma)` selects the SDK families.
  - `kz_port_tests(darkchronicle_tests)` adds the shared test cases.
- `tools/windows` holds the Windows scripts. `scripts/port/header_fixups.py` builds the header mirror (below). `scripts/build/verify.py` checks the PS2 build.

### The shared-code pin

The submodule is pinned at Kazarov `main` `a7674f4`, which adds the fiber thread family as the only `eekernel_threads` family, `ClockPumping()`, `PathsReset`, `sceMcSeek`/`sceMcEnd`, and the `Vu_prog0` mesh pipelines behind `RendererConfig::prog0_meshes`. Chronicle pins the same commit. Changes to the shared code go to Kazarov's `main` first. ChronicleTwo, like Chronicle, must build and test green on that commit before either game pins it. A game never edits a submodule checkout in place.

## How `port/src` takes precedence

A definition in `port/src` that replaces one of `ps2/src`'s is tagged `PORT_OVERRIDE`, which `tools/kazarov/include/kazarov/port.h` defines as nothing:

```cpp
PORT_OVERRIDE void mgInit(int screen_mode, int video_mode) {
```

`port/CMakeLists.txt` builds the two halves on every platform, with `PORT` and `PAL` defined:

1. `tools/kazarov/scripts/port_override.py list` reads the tags in `port/src` and writes the name each tagged definition links under to `port/build/pc/port_overrides.txt`.
2. `port_override.py strip` writes a copy of every unit in `ps2/src` under `port/build/pc/ps2_src` without those definitions. Lines keep their numbers, so diagnostics point into `ps2/src`.
3. `port_override.py check` stops the build if a tagged name is defined by no unit of `ps2/src`, since its replacement would be unused wherever the signatures differ.
4. The copies are compiled with `PORT` defined, and the units in `port/src` are linked with them.

Each name then has one definition. A replacement without the tag leaves `ps2/src`'s definition in, and the link fails on the duplicate symbol. A replacement takes the signature `ps2/src` gives the function: parameter types as spelled; names and default arguments may differ. By convention a replacement goes in the file that mirrors its unit.

A `static` function cannot be replaced, so its non-static callers are replaced instead. A `static` function written in assembly is the one exception. It is guarded `#ifndef PORT` in `ps2/src`, and the port defines a non-static function of the same name, declared to the unit by `port/include/stubs/<unit>.hpp`.

The 22 `#ifndef PORT` guards are 21 in the eight `.cpp` files that hold assembly (`collision.cpp`, `editcoll.cpp`, `editmap2.cpp`, `gameutil.cpp`, `mg_drawprim.cpp`, `mg_frame.cpp`, `mg_math.cpp` and `mg_visual.cpp`), and one in `ps2/include/sce/eekernel.h` around `ExitHandler`, which expands to `asm volatile("sync.l; ei")`. The port defines each guarded function untagged in `port/src/*_asm.cpp` or `port/src/mg_math.cpp`.

## Per-unit adjustments

`port/include/stubs/<unit>.hpp`, when it exists, is included ahead of `ps2/src/<unit>.cpp`, after `port.h` and nothing else. It declares what the port defines in place of code `PORT` leaves out, and it renames or supplies what the unit takes from MWCC or from the PS2 link alone. Only the port's build sees `port/include`.

The stub headers present are `collision`, `dataread`, `dng_main`, `editmap2`, `event_func`, `gameutil`, `mainloop`, `mg_frame`, `mg_math` and `mglib`. Their renames are below.

## Header fixups

`scripts/port/header_fixups.py` mirrors `ps2/include` into `port/build/pc/ps2_include` before the units compile, with these textual fixups. The game's headers include each other with quotes, so the port compiles against the mirror.

- `dng_event.hpp`: `CRedMarkModel::Draw` is declared `int`, not `void`.
- `dng_hud.hpp`: `CLockOnModel::Draw` is declared `int`, not `void`.

MWCC accepts a `void Draw()` that hides the base's `int Draw()` and gives it a new vtable slot. clang rejects that, and no standard spelling keeps the PS2 vtables. The `int` declarations make the two draws overrides instead. This is an accepted difference: their port definitions are `PS2_UNIMPLEMENTED` overrides (`port/src/dng_event.cpp`, `port/src/dng_hud.cpp`).

- `nd_meswin.hpp`: `#ifndef MES_WINDOW_OUT_OF_LINE_INIT` becomes `#if 1`, so `ClsMes::Init` is inline in every unit. `dngmenu.cpp`'s out-of-line copy was a strong definition and collided with the COMDAT copies on COFF.
- `collision.hpp`: `memset` takes `static_cast<void *>`, because `mgVu0FBOX` is not trivially copyable and `-Wnontrivial-memcall` rejects it.
- `mapload.hpp`: `CFuncPoint` gets a memberwise copy assignment on its constructor's line. `InventData`'s `mgVu0FBOX` makes the union's implicit copy deleted under clang; MWCC generated it, and `CFuncPointMngr::GetLight` and `CMap::GetCharaLight` assign points with it. Retail's bodies then compile unchanged.
- `sce/eekernel.h`: `CreateThread`, `TerminateThread` and `GetThreadId` get the asm link names `Ps2CreateThread`, `Ps2TerminateThread` and `Ps2GetThreadId`, because kernel32 exports the unprefixed names.
- MWCC-only `#pragma cpp_extensions ...`, `#pragma push` and `#pragma pop` lines are blanked, keeping the line count. `port/src` builds with `-Wall` and without `-Wno-unknown-pragmas`, so they cannot stay.

Headers then go through the long64 rewrite (see "Decision 1" under "What Phase 2 does").

Each fixup's text must occur exactly once in its file, or the script stops before it writes anything.

### Unit fixups

`header_fixups.py --unit <copy>` runs on each `ps2_src` copy after the override strip and before long64:

- `map.cpp`: `CMapParts::CMapParts()` becomes `inline`.
- `dng_debug.cpp`: `CTreasureBox::Initialize()` becomes `inline`.

Both match their other identical copies, so the COFF link has no strong-versus-COMDAT duplicates. `CTreasureBox`'s vtable becomes COMDAT with it. Each unit fixup's text must also occur exactly once.

## Names the PS2 build renames

The PS2 link binds some names through the object fixups and the linker script, not through the source. The port reproduces each one:

- `stubs/dng_main.hpp` renames `MainBuffer`, `BuffWorkData` and `EventCamera` to their `__2` retail names.
- `stubs/event_func.hpp` renames `VectMatMul` to `VectMatMul__2`.
- `stubs/dataread.hpp` and `stubs/mglib.hpp` rename `Exit__2` to `Exit`. Retail's `Exit__2` is a separate symbol; the port terminates through the SDK's `Exit`.
- `port/src/linknames.cpp` defines the VU microprogram placeholders `My_dma_start0`, `Vu_progmain`, `Vu_prog0`, `Vu_prog_sdw`, `Vu_prog_3dsp` and `Vu_prog_wtr`, and `_gp`. The retail microcode lives only in the retail ELF's `.vutext` and `.vudata`. The placeholders keep the addresses the game stores resolvable.
- `port/src/cameracontrol.cpp` defines `CameraCtrlParam::operator=`, which retail has only as assembly.
- `port/src/menudraw_primquad.cpp` forwards `PrimQuad<float>` and `PrimQuad<int>` to retail's `PrimQuad_f_` and `PrimQuad_i_`, since the template has no definition.

## Windows link

- `-Wl,--allow-multiple-definition` stays only as a guard. The eekernel.h fixup gives the PS2 kernel's three thread calls their own link names, so they no longer shadow KERNEL32's import thunk. The Windows link now reports no duplicate symbols.
- `-Xclang -mno-constructor-aliases`: a constructor alias points C1 into a COMDAT section that lld discards under `--allow-multiple-definition`.
- `-municode` and a `wmain` forwarder: the arguments arrive as UTF-16 and are converted to UTF-8.
- `port.h` supplies `index()` for mingw, whose `<strings.h>` has none; the PS2 libc's `index()` is `strchr()`.
- `u_long` is 64-bit on the host (`port.h` defines it as `u64`). The long64 rewrite makes the retail `unsigned long` 64-bit on Windows too (see "Decision 1" under "What Phase 2 does").

## The stub list

Every entry below is a `PORT_OVERRIDE` definition whose body calls `PS2_UNIMPLEMENTED()`, except `main`, the entry point. The reason column names what the retail body touches:

- **register**: a DMAC, GS, IPU or T0 register, or the GS CSR.
- **uncached**: an uncached (`0x20000000`) or physical mirror of RAM.
- **scratchpad**: the `0x70000000` scratchpad.
- **dev kit**: a dev-kit absolute address or the PS2 host file system.
- **packet**: builds a GS or VIF packet for the draw path, which the port does not emit yet.
- **SDK**: calls the MPEG or GIF packer.
- **fixup**: clang rejects the retail signature (see "Header fixups").
- **not yet ported**: the retail body touches no hardware that is named here; the subsystem is unported.

| File | Function | Reason |
|---|---|---|
| `dng_event.cpp` | `CRedMarkModel::Draw` | fixup |
| `dng_hud.cpp` | `CLockOnModel::Draw` | fixup |
| `gamepad.cpp` | `CGamePad::Capture` | dev kit (`PAD_CAPTURE_BUFFER`) |
| `gamepad.cpp` | `CGamePad::Play` | dev kit (`PAD_CAPTURE_BUFFER`) |
| `gamepad.cpp` | `CGamePad::SaveCapture` | dev kit (host file system) |
| `gamepad.cpp` | `CGamePad::LoadCapture` | dev kit (host file system) |
| `gyorace.cpp` | `sgLoopGyoRace` | not yet ported |
| `main.cpp` | `main` | entry point, not a stub |
| `menucapt.cpp` | `MenuChapterInit` | not yet ported |
| `menucls1.cpp` | `CMenuItemUse::UseItem` | not yet ported |

The table lists 9 stubs and `main`.

The executable has 14 `Ps2Unimplemented` call sites, one per function: 9 game-side (the table) and 5 Kazarov SDK (`sceCdSearchFile`, `sceCdSeek`, `sceMcRename`, `scePadEnd`, `scePadPortClose`). The SDK stubs live in `tools/kazarov/port/sce/stubs` and are listed by family there. No `mg*`, `water`, `outline`, `screeneffect`, font or `menudraw` function calls it.

`mgCVisualMDT::SetMaterialRef` is replaced and returns 0: only the retail packet builders call it, and they are replaced (its body also holds 128-bit GS literals clang cannot represent). `mgSendPacket` only flips the data buffer. The retail movie player in `movie.cpp` is not linked: see "Movies".

To regenerate the count, run:

```sh
tools/kazarov/bin/kazarov shell bash -c "objdump -d --no-show-raw-insn port/build/pc/darkchronicle | awk '/^[0-9a-f]+ <.*>:\$/{fn=\$2} /Ps2Unimplemented/{print fn}' | sort -u | c++filt"
```

The asm-replacement files, with their `PS2_UNIMPLEMENTED` counts:

| File | Stub bodies | Notes |
|---|---|---|
| `collision_asm.cpp` | 0 | all bodies real |
| `editcoll.cpp` | 0 | all bodies real |
| `editmap2.cpp` | 0 | all bodies real |
| `gameutil_asm.cpp` | 0 | all bodies real |
| `mg_drawprim_asm.cpp` | 0 | all bodies real |
| `mg_frame_asm.cpp` | 0 | all bodies real |
| `mg_math.cpp` | 0 | `mgZeroVectorW`, `mgUnitMatrix` and `mgZeroMatrix` are real C++ |
| `mg_visual_asm.cpp` | 0 | `SendDMA` is empty: packets are bookkeeping and nothing is sent (see "Packets"). It replaces `mg_visual.cpp`'s `SendDMA`, whose body is under `#ifndef PORT` |

The SDK families under `tools/kazarov/port/sce` are real where the PS2's behaviour can be reproduced on the host, and stubs where it cannot:

- Real: `libvu0` (the VU0 maths), `eekernel`, `eekernel_threads` (the fibers, see "Threads and timing"), `libgraph` (`sceGsSyncV` reports the field from the clock), `libpad`, `libmc` (3 stub bodies in 545 lines), `sifdev`, `sifrpc` (8 stub bodies in 66 lines) and `libcdvd` (12 in 60, the disc reads). `unimplemented.cpp` holds the shared `Ps2Unimplemented`, which prints the line shown above and aborts.
- Stubs: `libdev`, `libdma`, `libgraph` (in `sce/stubs`, beside the real `sce/libgraph.cpp`), `libmpeg`, `libpkt`, `libsdr`, `modmsin` and `sifdma`. Every function in them calls `PS2_UNIMPLEMENTED()`.

## Hardware audit

Grep for hardware addresses and registers in the unit copies:

```sh
grep -nE "0x1[0-2][0-9A-F]{6}\b|0x7000[0-9A-F]{4}|0x20000000|0x0FFFFFFF|DmaCH[0-9]|D_STAT|D_CTRL|IPU_|T0_(COUNT|MODE)" port/build/pc/ps2_src/*.cpp
```

It gives 137 hits:

- 105 are constants or names that only look like hardware.
- 1 is a comment.
- 21 are packet-tag writes.
- 10 are real hardware accesses, none of them linked. `mg_visual.cpp`'s `SendDMA` body is under `#ifndef PORT`, and its replacement is empty. In `movie.cpp`, none of the retail player survives the link (see "Movies").

Scratchpad access goes through `GetScrPad`, a stub. The five `CreateRenderInfoPacket` functions (`mgC3DSprite`, `mgCShadowMDT`, `mgCVisualMDT`, `mgCVisualPrim`, `CWater`) reach it first.

`nm` of the executable shows no unresolved hardware symbol. The hardware-named definitions are Kazarov `sce` replacements or game code that is not `PS2_UNIMPLEMENTED`.

## Pointers and 32-bit integers

Pointers are 64-bit on PC, and the arenas lie above 4 GiB (see "Arenas"). An address the game keeps in a 32-bit integer is truncated. `docs/port/truncations.md` is the per-site table of every cast of a pointer to a narrower integer in `ps2/src`, `port/src` and their headers. `scripts/port/truncations.py` writes it.

### The types

`ps2/include/types.h` defines `uptr` and `sptr` as `__UINTPTR_TYPE__` and `__INTPTR_TYPE__` under `PORT`, and as `unsigned int` and `int` on the PS2. Every operation compiles to the same instructions on the PS2 except the signed-sensitive ones.

### The policy

Rule 12. `uptr` replaces every cast of a pointer to `int`, `u_int`, `s32`, `u32` or `long`, and every local, global, non-record field and return type that carries an address.

`sptr` is used only where the integer is compared with `<`, `>`, `<=` or `>=`, shifted right, divided or reduced modulo, converted to `float` or `double`, widened into a 64-bit `long` or `u_long`, or stored in a signed field and so used. It is used only where the original type was signed. An original unsigned type becomes `uptr` everywhere.

Parameter types never change, because they are part of mangled names. Declarations that are already pointers are not touched. Pointer arithmetic is not rewritten as integer arithmetic.

### Records

Rule 13. Structs read from a file in place keep their 32-bit offsets and never take `uptr`. The inventory is `docs/port/records.md`: 598 structs with a `STATIC_ASSERT`, of which 65 are records and 533 are runtime. 57 records keep the disc layout and carry `PORT_RECORD_ASSERT`; `DATA_HEADER` is PS2 only (the host index replaces DATA.HD4); the seven save-image structs that hold `USER_PICTURE_INFO::image` are host layout on PC, sized by sizeof in memcard.cpp. `scripts/port/records.py` checks the table, the asserts, the host sizes and that no asserted record has a pointer member (ctest `Records.Check`).

`funcdata::name` (`ps2/include/runscript.hpp`) is the one pointer-typed declaration changed, a documented exception to the uptr rule that a declaration already typed as a pointer never changes: retail declares it `char *`, but in the `.stb` record it is a 32-bit field the script compiler writes as 0 in every disc script and retail never resolves, so it is `u32`, which keeps `local` and `arg` at their disc offsets 8 and 12 on PC. The error messages in `runscript.cpp` print it through `(char *) (uptr)`.

`PORT_RECORD_ASSERT(expr)` in `port/include/port.h` is a `static_assert` on PC. `ps2/include/common.h` defines it empty for the PS2.

### Strides and hand-counted sizes

A hand-written stride is rewritten as indexing only where `verify.py` stays identical.

| Function | Unit | State |
|---|---|---|
| `mgCTextureManager::SearchHash` (`hash_table[bucket]`) | - | indexed, landed |
| `CEditGrid::GetFast` (`&data[x + y * num_x]`) | - | indexed, landed; correct on PC as it stands |
| `mgCTextureManager::Initialize` (blocks `<< 4`, `hash_table` `<< 2`) | `port/src/mg_texture.cpp` | stride fix; the port override keeps the indexing; in-place rewrite reverted, MWCC code differs |
| `mgCTextureManager::SetTableBuffer` | `port/src/mg_texture.cpp` | stride fix; the port override keeps the indexing |
| `mgCTextureAnime::NewTexAnimeGroupData` (`(group << 2) + this + 0x64`) | `port/src/mg_tanime.cpp` | stride fix: the override appends to `list[group]`; in-place rewrite reverted, MWCC code differs |
| `CScene::ClearStack` (`(u8 *) this + 8 + i * 4`) | `port/src/scene.cpp` | stride fix: the override walks `stack[i]`; in-place rewrite reverted, MWCC code differs |
| `mgCDrawManager::BeginDraw`, `ClearTable`, `PreEndDraw` (pointer tables sized `n / 4 + 1` quadwords and cleared as `int`s) | `port/src/mg_drawprim.cpp` | stride fix: the overrides size the tables by `sizeof` and clear them as pointers |
| `mgRENDER_INFO::GetPlight` (`index * 0x30 + ... + 0x90`) | - | in-place rewrite reverted, MWCC code differs; correct on PC, since `mgLIGHT_INFO` holds floats only |
| `CRepairManager::Generate` (`slot * sizeof(mgCMemory)`) | - | in-place rewrite reverted, MWCC code differs; correct on PC as it stands |
| `CAutoMapGen::CreatTermParts` | - | correct on PC as it stands |
| `_SWE_SET_COLOR`, `_SWE_SET_TEXTURE`, `_SWE_START_EFFECT` (`(slot << 2) + chara + offsetof(sword_effect)`) | `event_func.cpp` | in-place rewrite reverted, MWCC code differs; stride fix that needs an override, since `sword_effect` is an array of pointers and `<< 2` is wrong on PC; owner: event_func's sweep owner, W4 sweep-dungeon, or records |
| `CVillagerMngr::GetAppearVlgr` (`time * sizeof(void *) + schedule + offset + 8`) | - | stride fix; owner records |
| `mgCMemory::SetHeapMem`, `mgCMemory::StartStackMode` | - | stride fix; owner arena; kept |

Hand-counted allocation sizes are rewritten with `sizeof` and are byte-identical: the script stacks in `SetActionScript`, `SetEffectScript` (stack), `SetEventScript` and `SetMonsterScript` (`(sizeof(RS_STACKDATA) * N + 15) / 16` and `(sizeof(RS_CALLDATA) * N + 15) / 16`), and `mgCDrawManager::AddPacket` (`(sizeof(mgSORT_PACKET) + 15) / 16`). The former `mgCVisualMDT::CopyMDTDataPointer` host allocation is superseded by the upstream GPU-mesh path in `port/src/mg_visual.cpp`, with face-mesh behavior covered by `mg_visual_test.cpp`.

### Override pairs

An address passed through an `int` parameter is fixed by replacing the callee and its callers on PC, under the owner.

| Callee#param | Callers | Owner |
|---|---|---|
| `ezMidi(int, int)`, arg 2 | `CSound` | audio-game |
| `ezBgm(int, int)`, arg 2 | `CSound` | audio-game |
| `CSound::LoadHdBd`, `CSound::LoadHdBd2`, `CSound::LoadHdBdAdd`, args 1 and 3 | `sndLoadSound` | audio-game |
| `CSound::LoadSeq`, arg 1 | `sndLoadSound` | audio-game |
| `align_size(u_int, u_int)` | `InitFileCache` | data (already overridden) |
| `sendToIOP(int iop_addr, ...)` | `audioDecCreate` | movies (unit replaced whole) |

### Other corrections

`SPI_STACK::operator=` copies `value.string`, the widest member, instead of `value.integer`. String arguments of every cfg script lost their upper half.

`CScriptInterpreter::GetArg` tests `text[(uptr) length - 1]` for a closing quote, where retail wrote `(u32)`. An empty argument makes the index `-1` modulo the address width: `text - 1` on the PS2, but `text + 4 GiB` on PC with `u32`. `uptr` keeps the PS2 code identical (`length - 1` and `(s32) ((u32) length - 1)` do not).

### What is left

- 39 narrowing sites remain in compiled `ps2/` code. Each has an owner:
  - DMA, VIF and GS packet words written as `u_int` in packet-building code. The W3 streams replace that code through the seam: `mg_sprite` and `mg_drawprim` to draw2d, `mg_visual` and `visualmotion` to prog0, `water` and `mg_shadow` to fx-shadow, `mglib` to present-core.
  - IOP addresses, to audio-game and movies.
  - The save record `CMenuSystemData::item_cursor` and the script stack slot in `_SET_INDEXOBJ_SIZE` and `_GET_INDEXOBJ_SIZE`, to records.
- 18 more are benign: low bits (`& K`), residues (`% K`) and differences of two addresses.
- 13 sit in replaced functions.
- 65 literal placement-new runs stay with arena, which grows them at run time.
- `effscript.cpp`, `SetEffectScript`: the `RS_CALLDATA` `Alloc(1)` is undersized for a call depth of 2, in retail too. The correct value, 2, is not byte-identical.
- Open round trips in linked code: 0. All eight that were open are closed by the conversion, and so is `MenuCalcBufAlignment`'s, which crashed `LoadGameDataAnalyze`.

### The check

```sh
tools/kazarov/bin/kazarov shell python3 scripts/port/truncations.py --build port/build/pc --check
tools/kazarov/bin/kazarov shell python3 -m unittest scripts/port/test_truncations.py
```

`truncations.py` runs libclang over `compile_commands.json` and parses each `ps2/src` unit as compiled. Header fixups and long64 are applied in memory, so replaced functions are seen too.

`--check` fails on:

- a stale `docs/port/truncations.md`;
- an open round trip in linked code;
- a narrowing site in compiled code with no `NARROWING_OWNER` entry;
- a stale `PHASE_OF` entry.

The ctest case is `Truncations.Check` (Linux; it skips when `clang.cindex` is missing). `scripts/port/test_truncations.py` covers the classifier. `--worklist FILE` writes the per-site TSV used for the conversion. `--verbose` prints libclang diagnostics; all of them are in replaced bodies.

The checker sees casts and implicit conversions. It does not see an address copied through an int member of a union; `SPI_STACK` was found at run time.

The tests are `port/src/tests/uptr_test.cpp` (`Uptr.*`). They cover pointer width, an address above 4 GiB through `(u_char *) ((uptr) p + ofs)`, through `MenuCalcBufAlignment`, through a `uptr` field and through a `uptr` return.

### Rule text for later streams

> An address the game keeps in an integer is a `uptr` (ps2/include/types.h: 32-bit on the PS2, std::uintptr_t on PC; `sptr` is its signed twin, used only where the original type was signed and the value is compared with < > <= >=, shifted right, divided, reduced modulo, converted to float or widened to 64 bits). Never change a parameter type or a declaration that is already a pointer. Never write a port copy of a retail function whose only change is a widened cast: send `NEEDS: ps2/<file>: <change>`. Structs read from a file in place (docs/port/records.md) keep their 32-bit fields.

## Keeping the PS2 build matching

Edits to `ps2/src` or `ps2/include` made for the port must leave the PS2 build byte-identical. They are corrections that are standard C++ either way, never code for the port. The edits, from `git diff -- ps2`:

- `ps2/include/mg_tanime.hpp`: declaration order. The inline `mgRect<float>::mgRect()` moves below the `Set` declaration it calls.
- `ps2/include/sce` is untouched. Its `libmc.h` and `libpad.h` differ from Chronicle's transcriptions, and changing a prototype's parameter types changes MWCC's code for the callers, so Kazarov's `libmc.cpp` and `libpad.cpp` take those types from the header they are compiled with (`tools/kazarov/port/sce/sdk_params.hpp`).
- `ps2/src/editmenu.cpp`, `effect.cpp`, `inventmn.cpp`, `mainloop.cpp`, `menuaqua.cpp`, `title.cpp` and `nd_meswin.cpp`: split initialisations. A `switch` jumps over them, so each becomes a declaration and an assignment.
- `ps2/src/menusys.cpp`: switch-case scope braces, with the locals a case declares hoisted above the `switch` in `MenuItemDebugDraw`. The same unit has a split initialisation and an enum cast in arithmetic (`(int) ITEM_SELECT_COLUMNS`).
- `ps2/src/menuop.cpp`: switch-case scope braces (`SUB_SAVE_SPACE_ASK`) and a split initialisation.
- `ps2/src/fishing.cpp`: a named union, `EventDataUnion`, with an empty constructor.
- `ps2/src/inventmn.cpp`: an enum cast in arithmetic (`(int) INVENT_ALBUM_PHOTO_NUM`).
- `ps2/src/mg_tanime.cpp`: a cast of `MG_TEX_ANIME_AMPLITUDE_FULL` to `float` in arithmetic.
- `ps2/src/memcard.cpp`: `unsigned long` spelled as `u_long` in `CheckOmake` and `GetCostumeList`.
- `ps2/src/mg_visual.cpp`: `unsigned long` spelled as `u_long` in both `mgSetPkTEX0` overloads.

Retail's own mistakes stay in `ps2/src`, because the match reproduces them. The port gives them defined behaviour instead, through flags `ct_ps2` sets in `port/CMakeLists.txt`: `-ftrivial-auto-var-init=zero`, `-fno-strict-return`, and `-fno-strict-aliasing` with `-fwrapv`.

### Checking the PS2 build

After any edit to `ps2/`, run `./build.sh ps2-pal`, then `BUILD_DIR=build/pal python3 scripts/build/verify.py -c`. The check must pass. The 22 `#ifndef PORT` guards are part of the same tree and are checked the same way.
