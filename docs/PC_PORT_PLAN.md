# PC port plan

This is the working plan for the native PC port of Dark Chronicle (PAL,
SCES-511.90). It follows the DC1 port (`TheMoonPeople/Chronicle`, `port/`
branch): the same engine and SDK surface, the same weak-linkage replacement
mechanism, the same SDL3/Vulkan host layer. Keep this file current: when a
phase lands, move its architectural facts out and update its status here.

## 1. Goals and hard rules

- **C++26, SDL3, Vulkan, clang, lld.** The port lives in `port/src` and
  `port/include`. `ps2/src` and `ps2/include` are the PAL game as the PS2
  build compiles it and are never edited for the port beyond `#ifdef PORT`
  guarded re-forms that keep the PS2 image byte-identical.
- **Weak-linkage replacement is the only mechanism.** The 149 `ps2/src` units
  are compiled for the host as `dc_ps2`, merged into one relocatable object
  (`ld.lld -r`) whose definitions are all weakened (`llvm-objcopy --weaken`).
  A strong definition in `port/src` displaces the weak `ps2/src` one at link
  time, and `-fsemantic-interposition` keeps every caller going through the
  linker. A `static` function cannot be replaced, so its non-static callers
  are replaced instead. `port/src/<unit>.cpp` replaces `ps2/src/<unit>.cpp`;
  `port/CMakeLists.txt` is the reference for the build itself.
- **No emulation of the PS2.** No GS/VU/VIF/GIF/DMA/SPU2 register interpreters,
  no mapping of PS2 address ranges. Unreplaced hardware paths stay
  `PS2_UNIMPLEMENTED()` so a hit says so loudly. Every GS semantic the port
  needs (blend, alpha test, fog, texture formats, 12.4 coordinates) is
  reimplemented natively.
- **The host layer never includes a game header.** `port/src/platform`,
  `port/src/gfx` and `port/src/audio` build as `dc_host` without `port.h` or
  the game's include paths; only the replacement units let game types and port
  services meet.
- **No fixed resolution or frame rate.** The window is resizable, 2D is laid
  out in the game's logical space and mapped to the window; the game's logic
  tick rate is a runtime setting, never a compile-time constant in port code.
- **The port is the PAL release.**
- **Comments explain why, never what.**

## 2. Where the port stands

The link is complete: `darkchronicle` builds on the server from
`port/src/main.cpp` + `dc_port` (the replacements) + `dc_host` (platform, gfx,
audio) + the weakened `dc_ps2` object, and runs. What it still lacks is a
driver: `main` is a stub that prints, and nothing calls the game's boot path.

- Round 1 landed ten replacement sets, all merged into `port-crew`:
  - A1 `mg_math.cpp`, `vu0_ops.hpp`: the VU0 math core the link wanted.
  - A2 `mg_frame.cpp`: `test1`/`test2`, the screen-box transforms.
  - A3 `mainloop.cpp`, `mglib.cpp`: the VSync group and main loop on the
    platform clock, and the frame path's packet bookkeeping.
  - A4 `sce/*`, `eekernel.cpp`: the SDK surface, `PS2_UNIMPLEMENTED()` where
    the port must never go.
  - A5 `dataread.cpp`, `cdvd_file.hpp`: the CD layer over the extracted data
    tree, `DATA.HD4` index included.
  - A6 `mg_drawprim.cpp`, `mg_drawenv.cpp`: GS packets into `gfx::Draw2D`.
  - A7 `mg_dataset.cpp`, `mg_texture.cpp`, `mg_sprite.cpp`, `mg_shadow.cpp`,
    `mg_visual.cpp`: datasets, TIM2 textures, sprites, shadows, models.
  - A8 `sound.cpp`, `snd_mngr.cpp`, `snd_seseq.cpp`: the sound manager on
    DC1's HD/BD/SQ player.
  - A9 `gamepad.cpp`, `padcontrol.cpp`: the pad stack on SDL3 input.
  - A10 `memcard.cpp`, `savedata.cpp`: the memory card manager over the host
    card layer.
- `ps2/` currently carries two `#ifdef PORT` re-forms (`inventmn.cpp`,
  `menusys.cpp`) for the host build. The PS2 build must stay byte-identical:
  `JOBS=... bash scripts/build/cmake.sh build ctx objdiff` must end in
  `SCES_511.90: OK`.
- `dc_host` is DC1's host layer already compiling clean against this tree:
  `platform/` (paths, SDL3 window/input/clock/config), `gfx/` (Vulkan
  renderer, display lists, readback; `port/src/gfx/README.md` is its
  reference) and `audio/` (HD/BD/SQ bank, sequencer, synth, mixer).
- `port/src/tests` covers the platform seams, the data reader, texture
  decode and the gfx layer; it builds with `-DDC_BUILD_TESTS=ON`.
- `CMakePresets.json` has `linux-x64` and `macos-arm64` presets (DC1's) and a
  `windows-x64` placeholder mirroring DC1's Windows scheme; the Windows
  toolchain itself is not wired yet.

## 3. Survey summaries

**Entry and frame flow.** `main()` (`ps2/src/main.cpp:138`) sets the thread
priority, `init()` boots the IOP modules and `InitCDFile`, then `MainLoop()`
(`ps2/src/mainloop.cpp`) runs the mode loop. Modes (`MainLoopMode`,
`ps2/include/mainloop.hpp:31`): 0 debug menu, 1 edit, 2 dungeon, 3 title (the
boot mode), 4-9 the debug viewers, 10 ends the game; each mode has an
Init/Main/Exit entry.

**Timing.** A3 replaced the VSync group (`mgInit`, `mgInitVSyncCallBack`,
`mgGetVSyncCount`, `WaitVSync`, `mgBeginFrame`, `mgEndFrame`, `mgWaitFrame`,
...) as one unit: the count is `platform/clock`, every spin-wait is a
`ClockSyncV`/`ClockPump` loop that runs the installed tick callback and never
busy-waits on hardware. `mgEndFrame` preserves PAL's 25 fps cadence.

**Screen and coordinates.** The game runs `mgInit(MG_SCREEN_MODE_512X480)`
(`ps2/src/mainloop.cpp:489`), GS 12.4 coordinates with the game's screen
offsets; A7 maps screen space onto the renderer's 640x480 logical frame and GS
depth through the game's `z_range`.

**Data.** A5 reads the extracted disc tree as it lies (`--data`/`DC_DATA`/
`data/`): `DATA.HD4` is the index into `DATA.DAT`, everything else is served
as the loose file it is (`MOVIE/*.PSS`, `MODULES/*.IRX`), names case-folded;
`host0:` is the only writable device and maps under the save root. Background
reads complete synchronously.

**Rendering.** A6 turns a `mgCDrawPrim` run into one `gfx::Draw2D` with the
draw state its GS registers describe; A7 draws models as `DrawMeshImmediate`
per material group, decodes TIM2/IMG textures, and replaces sprites, shadows
and the visual unit. DMA/VIF/GIF paths stay loud stubs.

**Audio.** A8 drives DC1's audio library from the retail `CSound` seam: banks
by HD/BD bytes, sequences by SQ, SEs through the mixer's F9/FD message
protocol. Known deltas for P3: effect pitch is dropped, the mixer keeps one
bank per port, and EZBGM streams are a polled stub.

**Input and saves.** A9 runs the retail pad state machine on SDL3 input with
the scripted-input path for `--input` tests; A10 keeps retail's card step
machine over the host layer's `<save>/mc0` and `<save>/mc1`.

## 4. Phases

### Round 1 — the replacement crew (landed)

The ten sets in section 2, one agent each, merged into `port-crew`; the link
has no undefined symbols and `darkchronicle` runs.

### Round 2 — towards boot (in progress)

- **P1 the driver** (`port/src/main.cpp`): DC1's driver shape (CLI `--data`,
  `--save`, `--headless`, `--frames`, `--screenshot`, `--width/--height`,
  `--jump`, `--input`, `--fast-load`), platform init, `InitCDFile`, then the
  game's `MainLoop`; screenshot readback on exit. Paths not yet replaced are
  stubbed or branched around with port hooks, each noted in P1's report.
- **P2 docs and presets** (this): the PC port plan, the `windows-x64` preset
  placeholder, and the PS2 byte-verify of the current `ps2/` edit set.
- **P3 audio and input seams**: per-port bank lists in the mixer, the F9
  pitch message, the EZBGM decision, and `InputScriptApply` kept compatible
  with P1's driver.

### Follow-ups

- First boot with the extracted data: `main` -> `InitCDFile` -> `MainLoop` ->
  title, headless screenshots on lavapipe.
- Reachable-stub sweep: every `PS2_UNIMPLEMENTED()` the first boot reaches is
  the worklist that replaces it.
- Windows build hookup (llvm-mingw and dependencies behind the placeholder
  preset) when someone needs it.
