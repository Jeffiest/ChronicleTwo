# PC port

`PLATFORM=PC` builds the PAL game's code (`SCES_511.90`) as a native x64 program, compiled by clang as C++26, on SDL3 and Vulkan 1.4. ChronicleTwo ships only the PAL release, so PAL is the only region the port builds for. `docs/PC_PORT_PLAN.md` is the plan the port follows and records its phases.

The port is at Phase 1. The executable opens a window, builds every renderer pipeline and aborts at its first PS2 hardware call. The game does not run past that call. This document describes what is built.

## Building and running

### Linux

```sh
./build.sh linux-x64
tools/kazarov/bin/kazarov shell ctest --test-dir port/build/pc --output-on-failure -j4
```

`./build.sh linux-x64` builds in the Kazarov container, from the image in `tools/kazarov/image` (built locally the first time), with the Debug preset into `port/build/pc`. `linux-x64-release` builds Release into `port/build/pc-release`. `CLEAN=1` discards the build directory first; `JOBS=N` sets the parallel job count.

Without the container, `cmake --preset linux-x64`, `cmake --build --preset linux-x64` and `ctest --preset linux-x64` do the same work. The host needs what the image provides: clang 20 or newer with lld, CMake 3.25 or newer (the presets need it), Ninja, `glslangValidator`, SDL3 3.4, the Vulkan 1.4 headers and loader, and Python 3.

The renderer's cases need a Vulkan device with the features `tools/kazarov/port/gfx/README.md` lists under "Device". The container's Mesa lavapipe provides them, so no GPU is needed there.

`./run.sh linux-x64` builds, then runs `port/build/pc/darkchronicle` on a Linux host.

### Windows

```bat
.\build.bat windows-x64
.\run.bat windows-x64
```

`build.bat` and `run.bat` pass the configuration through Kazarov's host launcher to `tools/windows/kazarov.ps1`. That script builds into `port/build/<configuration>`. The first run fetches the toolchain into `.kazarov/windows`, through `tools/windows/acquire.ps1`: llvm-mingw 20260922 (clang and lld), Ninja 1.13.2, SDL3 3.4.18 (MinGW), glslang 16.6.0 and the Vulkan 1.4.363.0 headers. Each archive is checked against its SHA-256 before it is extracted. `windows-x64` is a Debug build; `windows-x64-release` (and `windows`) is RelWithDebInfo.

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

## What Phase 1 does

1. `main` (`port/src/main.cpp`) sets the paths and the window identity (`port/src/identity.cpp`), parses the options and opens the window, or the offscreen surface with `--headless`. It then initialises input and the renderer with a logical size of 512x480 at a 4:3 display aspect. The renderer builds every pipeline during `RendererInit`, uses `save/pipeline_cache.bin` as its pipeline cache, and prints `compiling shaders n/total` as it goes.
2. The game's static constructors run before `main`. The three VU0 helpers they need, `mgUnitMatrix`, `mgZeroMatrix` and `mgZeroVectorW` (`port/src/mg_math.cpp`), are real C++. On the PS2 the constructors run after the IOP set-up inside `mwInit`; on the host `mwInit` is empty (`port/src/runtime.cpp`). An SIF RPC client that no game code has bound yet gets an answer from `sceSifCheckStatRpc` and `sceSifCallRpc` instead of an abort, because the constructors reach the RPC layer before the game binds its clients (`tools/kazarov/port/sce/sifrpc.cpp`).
3. `main` calls `MainLoop()` (`ps2/src/mainloop.cpp`), which runs the game's own set-up and calls `mgInit`.
4. `mgInit` (`port/src/mglib.cpp`) is a `PS2_UNIMPLEMENTED` stub. Its retail body writes the DMAC and GS registers. The plan expected the abort to name `sceDmaReset`, but `mgInit` is replaced as a whole, so the abort names `mgInit` before any of its calls would run:

```
port/src/mglib.cpp:13: mgInit: PlayStation 2 code is not implemented on PC
```

The test `Smoke.LaunchAbortsAtFirstHardwareCall` (`port/src/tests/CMakeLists.txt`) runs `darkchronicle --headless --offscreen` and passes when that line appears. It runs the executable under a generated CMake script, because ctest counts a process killed by `abort()` as failed even when `PASS_REGULAR_EXPRESSION` matches its output.

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

The submodule is pinned at `0d72aa1`, the commit in the index. Changes to the shared code go to Kazarov's `main` first. ChronicleTwo, like Chronicle, must build and test green on that commit before either game pins it. A game never edits a submodule checkout in place.

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

The stub headers present are `collision`, `dataread`, `dng_main`, `editmap2`, `event_func`, `gameutil`, `mg_frame`, `mg_math` and `mglib`. Their renames are below.

## Header fixups

`scripts/port/header_fixups.py` mirrors `ps2/include` into `port/build/pc/ps2_include` before the units compile, with two textual fixups. The game's headers include each other with quotes, so the port compiles against the mirror.

- `dng_event.hpp`: `CRedMarkModel::Draw` is declared `int`, not `void`.
- `dng_hud.hpp`: `CLockOnModel::Draw` is declared `int`, not `void`.

MWCC accepts a `void Draw()` that hides the base's `int Draw()` and gives it a new vtable slot. clang rejects that, and no standard spelling keeps the PS2 vtables. The `int` declarations make the two draws overrides instead. This is an accepted difference: their port definitions are `PS2_UNIMPLEMENTED` overrides (`port/src/dng_event.cpp`, `port/src/dng_hud.cpp`).

Each fixup's text must occur exactly once in its file, or the script stops before it writes anything.

## Names the PS2 build renames

The PS2 link binds some names through the object fixups and the linker script, not through the source. The port reproduces each one:

- `stubs/dng_main.hpp` renames `MainBuffer`, `BuffWorkData` and `EventCamera` to their `__2` retail names.
- `stubs/event_func.hpp` renames `VectMatMul` to `VectMatMul__2`.
- `stubs/dataread.hpp` and `stubs/mglib.hpp` rename `Exit__2` to `Exit`. Retail's `Exit__2` is a separate symbol; the port terminates through the SDK's `Exit`.
- `port/src/linknames.cpp` defines the VU microprogram placeholders `My_dma_start0`, `Vu_progmain`, `Vu_prog0`, `Vu_prog_sdw`, `Vu_prog_3dsp` and `Vu_prog_wtr`, and `_gp`. The retail microcode lives only in the retail ELF's `.vutext` and `.vudata`. The placeholders keep the addresses the game stores resolvable.
- `port/src/cameracontrol.cpp` defines `CameraCtrlParam::operator=`, which retail has only as assembly.
- `port/src/menudraw_primquad.cpp` forwards `PrimQuad<float>` and `PrimQuad<int>` to retail's `PrimQuad_f_` and `PrimQuad_i_`, since the template has no definition.

## Windows link

- `-Wl,--allow-multiple-definition`: COFF has no weak and strong symbols, so an inline header function and its out-of-line copy collide. The flag also lets the game's PS2 `CreateThread` stub shadow KERNEL32's import thunk. Calls through `__imp_` are unaffected.
- `-Xclang -mno-constructor-aliases`: a constructor alias points C1 into a COMDAT section that lld discards under `--allow-multiple-definition`.
- `-municode` and a `wmain` forwarder: the arguments arrive as UTF-16 and are converted to UTF-8.
- `port.h` supplies `index()` for mingw, whose `<strings.h>` has none; the PS2 libc's `index()` is `strchr()`.
- `u_long` is 64-bit on the host (`port.h` defines it as `u64`). `mgSetPkTEX0`'s bodies still store through `unsigned long *`, which is 32 bits on Windows, so they truncate. This is open.

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
| `dbg_font.cpp` | `dbgCJISFont::PrintDirect` | not yet ported |
| `dng_event.cpp` | `CRedMarkModel::Draw` | fixup |
| `dng_hud.cpp` | `CLockOnModel::Draw` | fixup |
| `funcpoint.cpp` | `CFuncPointMngr::GetLight` | not yet ported |
| `gamepad.cpp` | `CGamePad::Capture` | dev kit (`PAD_CAPTURE_BUFFER`) |
| `gamepad.cpp` | `CGamePad::Play` | dev kit (`PAD_CAPTURE_BUFFER`) |
| `gamepad.cpp` | `CGamePad::SaveCapture` | dev kit (host file system) |
| `gamepad.cpp` | `CGamePad::LoadCapture` | dev kit (host file system) |
| `gyorace.cpp` | `sgLoopGyoRace` | not yet ported |
| `main.cpp` | `main` | entry point, not a stub |
| `map.cpp` | `CMap::GetCharaLight` | not yet ported |
| `menucapt.cpp` | `MenuChapterInit` | not yet ported |
| `menucls1.cpp` | `CMenuItemUse::UseItem` | not yet ported |
| `mg_drawprim.cpp` | `mgCDrawPrim::Begin2` | packet (GS `ZBUF`, VIF1 call) |
| `mg_shadow.cpp` | `mgCShadowMDT::CreateFacePacket` | packet (uncached mirror) |
| `mg_shadow.cpp` | `mgCShadowMDT::CreatePacket` | packet (uncached mirror) |
| `mg_sprite.cpp` | `mgC3DSprite::BeginCreatePacket` | packet |
| `mg_sprite.cpp` | `mgCSprite::CreatePacket` | packet |
| `mg_visual.cpp` | `GetScrPad` | scratchpad |
| `mg_visual.cpp` | `mgCVisualMDT::CreatePacket` | packet (uncached mirror) |
| `mg_visual.cpp` | `mgCVisualFixMDT::CreatePacket` | packet (uncached mirror) |
| `mg_visual.cpp` | `mgCVisualMDT::CreateFacePacket` | packet |
| `mg_visual.cpp` | `mgCVisualMDT::SetMaterialRef` | packet |
| `mglib.cpp` | `VSyncCallBack` | register (GS CSR) |
| `mglib.cpp` | `mgInit` | register (DMAC, GS) |
| `mglib.cpp` | `mgBeginFrame` | register (VIF1 packet, GS frame) |
| `mglib.cpp` | `mgEndFrame` | register (GS store image, DMAC sync) |
| `mglib.cpp` | `mgSendPacket` | register (DMAC channel 1) |
| `mglib.cpp` | `StoreImage` | register (GS store image) |
| `movie.cpp` | `CMovie::Load` | register (DMAC control and status) and SDK (MPEG) |
| `movie.cpp` | `CMovie::Play` | not yet ported (movie thread) |
| `movie.cpp` | `CMovie::videoDecFlush` | not yet ported (movie) |
| `movie.cpp` | `videoCallback` | SDK (MPEG callback) |
| `movie.cpp` | `setD3_CHCR` | register (DMAC channel 3) |
| `movie.cpp` | `setD4_CHCR` | register (DMAC channel 4) |
| `movie.cpp` | `viBufCreate` | uncached (tag address) |
| `movie.cpp` | `viBufReset` | register (DMAC channel 4) |
| `movie.cpp` | `viBufAddDMA` | register (DMAC channel 4) |
| `movie.cpp` | `viBufStopDMA` | register (DMAC channel 4, IPU) |
| `movie.cpp` | `viBufRestartDMA` | register (DMAC channel 4, IPU) |
| `movie.cpp` | `viBufDelete` | register (DMAC channel 4) |
| `movie.cpp` | `viBufGetTs` | register (DMAC channel 4) |
| `movie.cpp` | `setImageTag` | SDK (GIF packer) |
| `sceneevent.cpp` | `CScene::DrawSky` | not yet ported |
| `sceneload.cpp` | `CScene::LoadMapFromMemory` | register (T0, through `timer0_count`) |
| `visualmotion.cpp` | `mgCVisualMotionMDT::CreateFaceMotionPacket` | packet |
| `water.cpp` | `CWater::CreatePacket` | packet (GS `TEXA`) |

That is 46 stubs and `main`. The `movie.cpp` entries are the movie path, which the port does not play yet.

The asm-replacement files, with their `PS2_UNIMPLEMENTED` counts:

| File | Stub bodies | Notes |
|---|---|---|
| `collision_asm.cpp` | 3 | `CCollisionMDT::PickUpNearPoly`, `pre_trance_normal`, `trance_normal` |
| `editcoll.cpp` | 1 | `ClipBoxXZ` |
| `editmap2.cpp` | 1 | `PlaneNormalXZ` |
| `gameutil_asm.cpp` | 4 | `testVUnew`, `MotionProc2`, `CheckHit`, `CheckHits` |
| `mg_drawprim_asm.cpp` | 4 | `mgCDrawPrim::Data0`, `Data4`, `Vertex`, `Color` |
| `mg_frame_asm.cpp` | 8 | `test1`, `test2`, `mgInsideScreen`, `GetLocalMatrix`, `GetBBoardMatrix`, `GetLWMatrix`, `GetLWMatrixTopBottom`, `GetDrawRect` |
| `mg_math.cpp` | 30 | `mgZeroVectorW`, `mgUnitMatrix` and `mgZeroMatrix` are real C++ |
| `mg_visual_asm.cpp` | 1 | `SendDMA` |

The SDK families under `tools/kazarov/port/sce` are real where the PS2's behaviour can be reproduced on the host, and stubs where it cannot:

- Real: `libvu0` (the VU0 maths), `eekernel`, `libgraph` (`sceGsSyncV` reports the field from the clock), `libpad`, `libmc` (3 stub bodies in 545 lines), `sifdev`, `sifrpc` (8 stub bodies in 66 lines) and `libcdvd` (12 in 60, the disc reads). `unimplemented.cpp` holds the shared `Ps2Unimplemented`, which prints the line shown above and aborts.
- Stubs: `eekernel_threads`, `libdev`, `libdma`, `libgraph` (in `sce/stubs`, beside the real `sce/libgraph.cpp`), `libmpeg`, `libpkt`, `libsdr`, `modmsin` and `sifdma`. Every function in them calls `PS2_UNIMPLEMENTED()`.

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
