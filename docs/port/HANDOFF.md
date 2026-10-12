# Handoff: Dark Chronicle PC port after Phases 4-6

Written at the Phases 4-6 push point (Wave 3 end, 12 Oct 2026). Read this first, then `docs/PC.md` (what is built), `docs/PC_PORT_PLAN.md` (the plan and phase status) and `docs/port/process/` (the schedule this work followed, its addenda and the wave records).

## 1. State of the three repositories

| Repository | Branch | Tip at push | Remote |
|---|---|---|---|
| ChronicleTwo (`C:\Users\Adubbz\Development\PS2\Projects\ChronicleTwo`) | `port/pc`, pushed as `master` | the `Docs:` commit on top of `1ac1cd1` | origin/master |
| Kazarov (`...\Kazarov`) | `main` | `f1b8e80` | TheMoonPeople/Kazarov `main` |
| Chronicle (`...\Chronicle-phase0`, a worktree of `Chronicle` on `master`) | `master` | `c6a8c918` (pin `main@f1b8e80`) on `cd17ef91` (arenas) on upstream `109c6eff` | origin/master |

Both games pin Kazarov `f1b8e80`. `C:\Users\Adubbz\Development\PS2\Projects\Chronicle` (branch `shared-port`) is someone else's work in progress: never open, build or run git there. Chronicle is built only in `Chronicle-phase0`.

Upstream Chronicle moved during Wave 3 (`88fd2511` .. `109c6eff`, four commits by other contributors in `port/src` and `docs/PC.md`). The two local Chronicle commits were rebased onto it and the Chronicle gate re-run green before the push.

### What Wave 3 merged (ChronicleTwo `fee9c9c..master`)

In order, each stream rebased onto the growing `port/pc`, with its `Kazarov: pin kz/...` commit dropped or stripped of the submodule change:

1. W2.u, `s/uptr`: `135ed08 6bb7a39 1271288 59ee848` (fast-forward). Addresses kept in integers are `uptr`/`sptr` in `ps2/src`; `truncations.py --check`; `docs/port/records.md`.
2. `s/present-core`: `4e62eff 6514f21 d002c5d 3604945`. Ticks as display lists, display frames, the window aspect, the video settings.
3. `s/draw2d`: `4e5ab38 3c507d0 94dd1a8 56e39bf d764543 ca5f059`. mgCDrawPrim, sprites, TexAnime, menu grab, PrintDirect; no GIF decoder. `3c507d0` is draw2d's pin commit reduced to its CMake line (`--long-is-64`).
4. `s/prog0`: `1b1f6e4 74f3d06 7f0bcf8`. MDT visuals as GPU meshes; `1b1f6e4` is prog0's pin commit reduced to its test changes. Its copies of present-core/draw2d commits were dropped (`rebase --onto`).
5. `s/fx-shadow`: `1616b39 8616c87 e78bd4b` (its copy of present-core's seam, `e5a2266`, dropped).
6. `s/audio-game`: `15f61b7 8cea565` (pin dropped).
7. `s/movies`: `65bc53c b646ab5`.
8. Integrator: `e4b4f93` (pin `main@f1b8e80`), `5df989e` (title fixes), `6f47570` (prog0 seam entries, `prog0_meshes`, VIF plumbing retired, widening-only overrides deleted, audio stop at exit), `1ac1cd1` (native gate, FTOI4 to the oracles, 30 s GPU cases), then the `Docs:` commit (PC.md, plan, truncations.md, this file, `docs/port/process/`).

Conflicts were only the uptr one-token lines (`CreatePacket` return types `u_int` -> `uptr` in `mg_sprite.cpp`, `mg_visual.cpp`, `mg_shadow.cpp`, `water.cpp`), resolved toward `uptr`. `mglib.cpp` (present-core, draw2d, prog0 and fx-shadow all touched it) merged automatically and builds.

### What Kazarov merged (`5314543..f1b8e80`)

`a27ddbe` (draw2d: `port_override.py --long-is-64`), `db8e19e` (prog0: Vu_prog0's constants in a set 2 block, `MeshConstants` back to 400 bytes, bone palette per draw), `0b87ccb` (audio-game: stream channel, bank select by CC 0, HS pitch, per-core volume), `f1b8e80` (integrator: `gfx_validation_test.cpp`'s Linux-only message, the `-Wall` warning on Windows). The Chronicle gate ran after each of the three stream merges and on the final tip.

## 2. Gates at the push

- ChronicleTwo Linux (container, main checkout with real data): build ok (only the vendored `stb_*` unused-function warnings, present since the W0 baseline, and `ps2/src` copies); ctest 774 cases, all pass (`DataRead.RealDataIndexesTheDiscFiles` timed out once under load and passed on rerun).
- ChronicleTwo Windows: build ok (no duplicate or undefined symbol lines); tests 780/780 on the RTX 4090; real-data headless runs show the language select (300 frames) and the title (2500 frames).
- `ps2-pal`: `SCES_511.90: OK`, every section.
- `truncations.py --build port/build/pc --check`: clean; 47 rows, 15 narrowing sites in compiled `ps2/` code, all owned, 0 open round trips in linked code. `docs/port/truncations.md` regenerated.
- `native_gate.py --binary port/build/pc/darkchronicle`: clean (also a CI step now).
- `Ps2Unimplemented` callers: 14 (9 game: `sgLoopGyoRace`, `MenuChapterInit`, `CLockOnModel::Draw`, `CRedMarkModel::Draw`, `CMenuItemUse::UseItem`, four `CGamePad` capture functions; 5 SDK: `sceCdSearchFile`, `sceCdSeek`, `sceMcRename`, `scePadEnd`, `scePadPortClose`).
- Chronicle on `f1b8e80`: Linux ctest 352/352 (incl. the new audio tests), headless 3-frame screenshot byte-identical to the W0 baseline, `ps2-pal` 100% (3442 functions), Windows build ok.
- Kazarov pytest (container): 93 passed, 14 skipped.
- In game, real data: `--frames 300` = language select; `--input` past it: Level-5 logo movie frames (frame 540), the title over its 3D map (frame 2500), `--display-per-tick 4` byte-identical at the title, `DC2_AUDIO_WAV` non-silent at the title, the attract demo's opening event (frame 4500, skinned characters).

Not run at this push (W3 exit-gate items left for W4 `present`): the 2560x1440 windowed run's FPS/TICK log, `interpolation = off` in game, the 1024x768 / 1280x720 / 800x1200 aspect screenshots compared by PSNR, the opening and first town by `--input`.

## 3. Where the game stops (the W4 starting point)

Pressing START at the title (frames 2400-2500 of `docs/port/process`'s title script plus a `start` line) makes the game enter the next mode (`REVERB 4 32` logged), then the run stops advancing frames: no crash, no further `mglib: frame` output, the process keeps running. Without START the title loops through its attract demo indefinitely. Debug with gdb in the container: `tools/kazarov/bin/kazarov shell bash -c "gdb -batch -ex run -ex bt --args port/build/pc/darkchronicle --headless --offscreen --data data --save port/build/pc/save --input <script> --frames N"` and interrupt, or add `--frames` just past the press and read where the main loop waits.

The title input script used (frames are game frames; a bare frame number releases everything):

```
300 circle
306
320 cross
326
900 start
906
1200 circle
1206
1500 cross
1506
```

## 4. Open NEEDS and carry-forward (from `docs/port/process/W3-NEEDS.md`, with what happened)

Applied at W3 integration:
- `renderer.prog0_meshes = true` in `main.cpp`; seam entries `mgPortDrawMeshProg0`/`mgPortDrawMeshImmediateProg0`, prog0's `SubmitFaces` uses them.
- `--long-is-64` CMake line taken; the GPU cases (`GfxMeshDc2.*`, `MgTextureGpu.*`, `MgTanimeGpu.*`, `GfxMisc.PipelineCache`) get 30 s.
- PC.md stub table cut to what is linked; `SetMaterialRef` returns 0 noted.
- Widening-only overrides deleted: `mgCMemory::ClearHeapMem`, `EndStackMode`, `stAlloc64`, `StartReadBG`. Kept on purpose: `stAllocTest` (64-bit overflow check), `mgSetPacketBuffer`/`mgSetDataBuffer`/`mgGetFrameBuffer`/`mgSetUserVuProg`/`mgSetUserVuProgAdr` (retail keeps that state in `ps2/src/mglib.cpp` statics the port does not read, so they are state-ownership overrides, not widenings), `mgSetPkFrameBuffer` (registered targets), `SetTableBuffer`/`Initialize` (strides).
- Native-VU audit: no GIF/PACKED decoder, no XYZF2/12.4/FTOI4 packer, no VU1 layout helper linked; `port/src/vu1/` float-only (`Ftoi4` moved to `tests/vu1_oracle/vu1_pack.hpp`); `scripts/port/native_gate.py` is the CI gate. `vu0::Ftoi4` stays in `mg_math.cpp`/`mg_drawprim_asm.cpp`: those are retail VU0 macro functions whose integers the game reads (audit class a).
- Audit #13: the VIF packet arenas and the direct-draw cursor retired (one sentinel `sceVif1Packet`, one never-written direct tag); the data buffers stay.
- prog0's `GetLight`/`GetCharaLight` stubs gone (header-mirror copy of `CFuncPoint`); `DrawSky` stays prog0's port.
- `gfx_validation_test.cpp` warning fixed in Kazarov.
- `mgCVisualMDT::Draw` still calls the virtual `CreateRenderInfoPacket` then `CreatePacket` (shadows and water draw from there): verified.

Deferred, with owner:
- W4 `present`: `mgSetPkMoveImage` frame copies (`fire_work` in `sceneevent.cpp`, `screen` in `map.cpp`) as frame targets (heat haze at wide aspect); audit #14 render targets keyed by handle instead of TBP0; bone-palette interpolation; HUD anchors; display frames replay the tick's prog0 lighting; `mgPortInterpScope` restore resets the outer teleport/blend (no gfx getters); the W3 display/aspect gates not run (above); `GfxMisc.PipelineCache` Windows slowness after the FFmpeg DLLs (now 30 s).
- W4 `sweep-town`: `gyorace.cpp` `sgEffectDrawGyoRace` with `sgLoopGyoRace` (shared unit state; use `mgPortPrimVertex`/`mgPortPrimSubmit`); `MenuChapterInit`, `CMenuItemUse::UseItem`.
- W4 `sweep-dungeon` or `records`: `event_func` `_SWE_SET_COLOR`/`_SWE_SET_TEXTURE`/`_SWE_START_EFFECT` `(slot << 2)` strides; `CRedMarkModel::Draw`, `CLockOnModel::Draw`.
- `records` (running from `59ee848` when this was written; it owns `docs/port/records.md`, `scripts/port/records.py`, `runscript_port.*`, `runscript_opcodes.cpp`, `vlgr_info.cpp`, `villagermngr.cpp`, `editdata.cpp`, `editriver.cpp`, `effscript.cpp`, `records_test.cpp`, PC.md "Records"): villager `GetAppearVlgr` stride, `_SET_INDEXOBJ_SIZE`/`_GET_INDEXOBJ_SIZE`, `CMenuSystemData::item_cursor`, `DATA_HEADER` fixup gate, `funcdata::name`, `USER_PICTURE_INFO`/`CInventUserData` image fixup. It must rebase onto the pushed `master`; `docs/port/truncations.md` was regenerated here, so expect to regenerate it again.
- arena/W4: `effscript.cpp:5564` `RS_CALLDATA` one quadword for call depth 2 (undersized in retail too).
- Open (draw2d): per-object host tables grow with distinct object addresses (3D sprite records never erased); bound them per mode.
- Open (prog0): GPU meshes for Fix/Motion faces are never freed (live until the face address is reused).
- Open (fx-shadow): `mgStoreZBuffImage` answers one tick late (photo's first distance is 0); no shadow/water/outline/DOF/lens-flare in-game screenshot yet (the title and demo do not show them clearly).
- Open (audio-game): end-of-stream 0x8000 held 1 s; `SndInReverb` no-op; movie PCM on mixer port 5 (`kMoviePortAudioPort`) rather than a stream channel, documented in PC.md "Movies".
- Open: "gfx: a draw samples the target it renders to; snapshot it first" is logged during the logo sequence (a draw binds the target it renders into); harmless so far, find the caller.
- Open: Chronicle's Windows `--headless` aborts with `filesystem_error ... __wide_to_char` after exporting the game text (predates Phase 0; for the Chronicle owner).

## 5. The W4 plan

`docs/port/process/SCHEDULE.md` section 5 "Wave 4" and the briefs in section 9 (`present`, `options`, `sweep-town`, `sweep-dungeon`, W4 integrator). Add to the W4 briefs: the deferred lines above, the hang after START at the title as the sweeps' first target, the W3 display/aspect gates that were not run, and that `records` finishes first or in parallel with its own ownership.

## 6. How to resume

- Worktrees and branches: `docs/port/process/SCHEDULE.md` section 4.1 (`ChronicleTwo-<stream>` on `s/<stream>` from `port/pc`; `Kazarov-<stream>` on `kz/<stream>` from `main`; data and `.kazarov\windows` as junctions; `rom/pal/` with the ISO hardlinked for ps2-pal, never a `rom` junction). Remove a worktree only after `cmd /c rmdir <wt>\data` and `cmd /c rmdir <wt>\.kazarov\windows`: `git worktree remove --force` follows junctions and once deleted the main `data/`.
- Pinning a stream's Kazarov commit: section 4.2. The integrator rewrites pins to `main` and strips the submodule change from stream pin commits.
- Gate commands: section 4.4, plus at every gate `kazarov shell python3 scripts/port/truncations.py --build port/build/pc --check`, `kazarov shell python3 scripts/port/native_gate.py --binary port/build/pc/darkchronicle`, and from W3 `records.py` once `records` lands.
- Windows: run `tools/windows/*.ps1` from pwsh 7 with the checkout as the working directory (Windows PowerShell 5.1 aborts on cmake's stderr); `ctest` is `& 'C:\Program Files\CMake\bin\ctest.exe'`; run the Windows test gate with nothing else heavy running (load causes 5 s timeouts); no Vulkan validation layer on Windows.
- Docker: if Docker Desktop will not start, stop a stale `com.docker.extensions` process and restart it. Image tag = hash of `tools/kazarov/image` (currently `kazarov-dev:b4dcf4978934`).
- Linux runs from a worktree see no data (junctions do not resolve in the container): data-dependent Linux checks run in the main checkout.
- Kazarov pytest only in the container: `tools/kazarov/bin/kazarov shell bash -c "python3 -m venv /tmp/v && /tmp/v/bin/pip -q install pytest && cd tools/kazarov && /tmp/v/bin/python -m pytest tests -q"`.
- Saves: `--save port/build/<config>/save`, never a `save/` beside the executable.
- Git policy: author `Adubbz <Adubbz@users.noreply.github.com>`; commit messages `Port:`/`Kazarov:`/`Build:`/`Docs:` with no co-author or tool attribution; pushes fast-forward only, in the order Kazarov `main`, Chronicle `master` (from `Chronicle-phase0`), ChronicleTwo `port/pc:master`, and every pin must exist on Kazarov `main` first. Check upstream Chronicle before pushing it: others push there.
