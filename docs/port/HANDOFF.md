# Handoff: Dark Chronicle PC port, Wave 4 (Phase 7) in progress

Written at the W4 push point (13 Oct 2026), when the session stopped at its usage limit. This is the one document to read before resuming. Then `docs/PC.md` (what is built), `docs/PC_PORT_PLAN.md` (plan and phase status) and `docs/port/process/` (the schedule this work followed, its addenda, the NEEDS lists and the wave records).

## 1. Status

| Phase | State |
|---|---|
| 0-3 (Kazarov shared code, boot, data, survey and VU1 specs) | done, pushed |
| 4-6 (rendering, audio, movies; W2, W2.u, W3) | done, pushed (`cc2f461` and earlier) |
| W3 follow-ups (`records`, New Game background read, font sampling) | done, pushed (`79e5d84`) |
| 7 (W4: presentation, sweep, options) | partial; opening reaches the first battle tutorial and Palm Brinks is reachable from the copied user save; the built-in debug menu reaches Dungeon 2 Floor 1 loading, but map placement stalls before gameplay |

In the game, with real data, headless and windowed: language select, Level-5 logo movie, title over its 3D map with music, the attract loop, New Game, the costume screen, and "Confirm Max's costume?" (`tools/input/sweep-costume.input`, frame 4800). Selecting YES loads the opening event (floor 20, event 1010). The frame-tree search loop and a later draw-environment alignment crash are repaired; a scripted run reaches the first battle tutorial and shows character models at frame 7000. An unattended battle run reaches Game Over by frame 9000. A scratch copy of the user's File 2 save loads Palm Brinks at "A night at the circus"; Max and several NPCs render during a headless camera sweep, including NPCs past the old 4:3 edge. The host omits the retail frame outline when Fill expands the visible extent. Frame 15140 (partial right turn), frame 15400 (full right turn), and a frame-15400 left turn show different town angles without inner 4:3 lines or bright dropped-draw blocks. The earlier capture logged 366 main-target self-sampling warnings; snapshotting the main frame when it is sampled during an open frame reduces them to zero, with no missing-texture warnings in these replays. The reported bench/cutscene disappearance remains un-reproduced. The opening-to-town progression, first dungeon, full model visibility/deformation, and movie playback remain open.

Correction: the W3 records (`docs/port/process/wave3-report.md`, the old version of this file) said the attract demo at frame 4500 shows the opening event with skinned characters. It does not: the attract loop is only the title and the opening movie (2D, no meshes). The later scripted opening run does show Monica and enemies at frame 7000; this confirms model draws in that scene, not correct placement or visibility throughout the game.

## 2. Repositories and shas

| Repository | Branch | Pushed tip | Notes |
|---|---|---|---|
| ChronicleTwo (`C:\Users\Adubbz\Development\PS2\Projects\ChronicleTwo`) | `port/pc`, pushed as `origin/master` | the `Docs:` commit on top of `e0defd4` | `master` is kept equal to `port/pc` |
| Kazarov (`...\Kazarov`) | `main` | `a0a5ff8` | TheMoonPeople/Kazarov `main` |
| Chronicle (`...\Chronicle-phase0`, worktree of `Chronicle` on `master`) | `master` | `Kazarov: pin main@a0a5ff8` on `d4bae80b` (pin `5bec132`) on `c6a8c918` | upstream unchanged since `109c6eff` |

Both games pin Kazarov `a0a5ff8`. `C:\Users\Adubbz\Development\PS2\Projects\Chronicle` (branch `shared-port`) holds the user's own uncommitted work: never open, build or run git there. Chronicle is built and gated only in `Chronicle-phase0` (pull it to `origin/master` first).

### What W4 merged (ChronicleTwo `79e5d84..master`, Kazarov `f1b8e80..a0a5ff8`)

In order, each rebased onto the growing `port/pc` (stream pin commits dropped, one pin of `main` at the end):

1. `present` (`s/present`, `kz/present`): ChronicleTwo `af735bb f179c04`; Kazarov `5c2112a`, plus the integrator's README note `5bec132`.
2. `sweep` (`s/sweep`): `1ac66bc b20417c 43672cb`.
3. `text2d` (`s/text2d`): `646c0c1`.
4. Integrator: `b2e49c2` (pin `main@5bec132`), then `7bad499` (Docs: this file, PC.md intro, plan status, `tools/input/sweep-costume.input`, `docs/port/process/ADDENDUM-W3.md`), pushed as a first push point.
5. `buildperf` (`s/buildperf`, `kz/buildperf`): ChronicleTwo `01f7d21 8e23335`; Kazarov `a0a5ff8` (rebased from `dc8b81a`); integrator pin `e0defd4` (`main@a0a5ff8`), then the final `Docs:` commit (this section, `docs/port/process/kazarov-persistent-container.patch`).

## 3. What each W4 stream delivered

### 3.1 `present` (W4, Kazarov stream)

Done:
- Bone-palette interpolation (Kazarov `port/gfx/displaylist.cpp` `Interpolated`, `BlendProg0Bones`, `MoveProg0`; `Replay` takes a per-record prog0 override). The prog0 block's `model_world`, `clip_or_view`, object-space `eye` and point lights follow the interpolated model and camera. Chronicle records no prog0 block. Tests: `GfxDisplayList.BonesBlendPerBoneAtHalf`, `GfxMeshDc2.SkinnedPaletteInterpolatesInDisplayFrame` (`port/tests/gfx_displaylist_test.cpp`, suite GfxMeshDc2 for the 30 s budget).
- HUD anchors: `port/src/dng_status.cpp` (`DrawStatusBord`, top-left), `port/src/automap.cpp` (mini map, top-right). `gfx::UiAnchor::Side(x, y, true)` moves the piece's scissor with it (`GfxMeshDc2.AnchoredScissorMovesWithTheDraw`). DC2 has no town clock HUD.
- Letterbox (`video.aspect = 4:3`) clips every main-target draw and clear to the frame. Frame flanks sample with clamped wrap, which removes the 1-px seam at the frame's leading edges. Tests `GfxMeshDc2.LetterboxClipsMeshesAndClearsToTheFrame`, `FillKeepsClearsAcrossTheWindow`. This is a behaviour change for Chronicle too, only for full-frame repeating-texture draws in non-4:3 windows (accepted; noted in Kazarov `README.md` and Chronicle `docs/PC.md` "Window aspect"; Chronicle's 4:3 baseline screenshot is byte-identical).
- `mgSetPkMoveImage` of the frame (5-argument overloads: `sceneevent.cpp` heat haze, `map.cpp` screen, menu grabs) goes to a frame target as a 1:1 logical copy; draws into a grabbed TBP go to the grab (`FrameGrabAt`). The 6-argument stretch (`screeneffect.cpp`) is unchanged. Test `MglibPresent.MoveImageOfTheFrameUsesAFrameTarget`.
- `--screenshot-display N` (display frames of the screenshot's frame) and `--screenshot-fps`.

Measured (Windows, RTX 4090, title frame 2500): windowed 2560x1440 immediate, `max_fps` 0: 215-404 FPS at 420-590 draws, TICK 50.0/50; interpolation off: 50/s at the title, 25/s in the movie. Headless `--display-per-tick 4` byte-identical at 1024x768 and 1280x720. Letterbox bars black; the centre against a 960x720 render: 23 px differ by 1. Logical-frame PSNR (4:3 frame, box-resampled to 512x480) against 1024x768: 1280x720 38.5 dB, 3840x2160 40.9, 800x1200 35.0, 800x600 38.4, 960x720 39.7 (the same-aspect floor; 40 dB is not reachable from resolution alone). Title `--screenshot-display 4`: consecutive display frames differ (~16% of pixels, camera).

### 3.2 `sweep` (W4, town path first)

Runs are Windows headless (`darkchronicle.exe --headless --data data --save port/build/windows-x64/save --input <script> --frames N --screenshot out.png`) plus one windowed run with a fresh default config.

- `ac8f518` (pushed before this point): New Game hung, or exited 1 when windowed. `CMenuCostumeSel::LoadMenuData` (`ps2/src/menuchr.cpp:6668`) spins on `do {} while (ReadBGSync() == 0);`, but the port's `LoadFileBG` finished every read on the spot. Now `ReadBG` issues a read on one call and completes it on the next, and `ReadBGSync` calls `ReadBG` first, as retail does (`port/src/dataread.cpp`). Tests `DataRead.BackgroundReadCompletesOnTheSecondSync`, `DataRead.CostumeLoaderWaitSeesPendingRead`.
- `1ac66bc`: confirming the costume starts the opening (`InitDungeonMain`, floor 20, event 1010), which aborted with an arena overflow (4543 of 4500 quadwords, garbage name) in `SetActionScript` (`ps2/src/actscript.cpp:2613`) <- `LoadActionFile` <- `SetupMainUnit` (`charasetup.cpp:287`). Two causes:
  - `SetupMainUnit` indexes `stacks[6]` of the 6-element `BaseCharacter`; on the PS2 that slot is `BuffTempData` by link adjacency. `port/src/dng_main.cpp` overrides the variable as `PORT_OVERRIDE mgCMemory BaseCharacter[CHARA_MEM_STACK_NUM]` (7). Merging the array in `ps2/src` is not byte-identical (`__sinit_dng_main_cpp` and `.bss` change).
  - `mem_table` counted `RS_STACKDATA`/`RS_CALLDATA` at PS2 pointer widths. `CHARA_SCRIPT_RUNTIME_DELTA` in `ps2/src/charasetup.cpp` is 0 on the PS2 and 448 quadwords on PC, added to the script slots (row 0 slots 5 and 6, row 1 slot 4). `ps2-pal` OK in every section including `.bss`. Test `DngMain.BaseCharacterHoldsTheTempStack`.
- `b20417c`: `CRedMarkModel::Draw` and `CLockOnModel::Draw` were stubs only because of the `void` -> `int` header fixup; now the retail bodies returning 0 (`port/src/dng_event.cpp`, `port/src/dng_hud.cpp`). Tests `DngDraw.RedMarkDrawsOnlyWhenRequested`, `DngDraw.LockOnWithoutPlayerDrawsNothing`. `Ps2Unimplemented` callers: 12 (was 14).
- Input scripts: `tools/input/sweep-title.input` (language select -> title -> START; title menu at 2800), `tools/input/sweep-newgame.input` (+ New Game; costume screen by 4000), `tools/input/sweep-costume.input` (+ START, three downs and cross: "Confirm Max's costume?" at 4800). Add `4920 left`/`4926`/`4950 cross`/`4956` to select YES and load the opening.

### 3.3 `text2d`

- `79e5d84` (pushed before this point) and `646c0c1`: nearest-filtered sprites sample the GS's texel, including a sample exactly on a texel boundary (`kMgPortNearestTexelBias`, +1/64 texel, `port/src/mg_drawprim.cpp` `AssembleSprite`). Glyphs no longer bleed the next cell, and 1:1 background tiles lose their black seams at 2.5x. The `DrawVersatileWin` backings cover the frame opening, a deliberate deviation from retail (docs/PC.md "Sprites and window backings"). Tests `MgDrawPrimRender.TileSeamHasNoForeignTexel`, `MgDrawPrim.NearestTileSeamSamplesTheTileTexel`.
- Integration check: the 1280x960 costume screen differs from the previous capture over most of the frame (171k pixels). At 4x the changes are one-texel column picks at 2.5x scale, with no blur and no moved text, and the black seam at the "Select Costume" title is gone. Accepted.

### 3.4 `buildperf` (merged at the end; see the gate note)

- Kazarov `scripts/port_override.py`: `check --split DIR --cache FILE` (opt-in; without the flags every subcommand behaves as before, so Chronicle is unchanged by construction) and `strip_unit()`; 9 new pytest cases.
- ChronicleTwo `port/CMakeLists.txt`, `scripts/port/unit_copy.py`, `scripts/port/header_fixups.py`: each `ps2/src` copy is one Python process (strip, unit fixups, long64 in-process, written only when its bytes change); each copy depends on its own overrides list written by the check; the `ps2/include` mirror files are BYPRODUCTS of the mirror step, which fixes a stale build after a header edit (dependents were only recompiled by the following build). All 149 copies and 178 mirror headers are byte-identical to the old pipeline.
- Measured: a `ps2/src` edit on Windows 12.3 s -> 4.8 s; adding or removing a `PORT_OVERRIDE` recopies one unit (82 s -> 4.1 s); header edits are correct in one build. Numbers in `docs/port/buildperf.md`. Headless Windows runs start ~2 s faster with `SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_RAWINPUT=0 SDL_JOYSTICK_WGI=0 SDL_JOYSTICK_DIRECTINPUT=0`.
- Gate status on the current Windows tree: game and test builds pass; full CTest completes 853 cases with zero failures and 24 expected/environment skips; `native_gate.py` is clean; the headless costume confirmation reaches frame 4800; a windowed run with a fresh default config initializes and exits cleanly. These replace the older W4 gate note for this checkout. Linux and hosted CI gates are not established by this Windows run.
## 4. Open items

### 4.1 Opening sweep: frame-tree loop and draw crash repaired; path still ahead

`mgCFrame::SearchFrame` now uses iterative depth-first traversal with cycle and visit-count bounds; regressions cover child order, a general cycle, and the nine-frame opening cycle. The opening proceeds into its battle tutorial. A second crash in `mgCDrawEnv::operator=` came from aligned SIMD stores to an eight-byte-aligned stack object; field-wise assignment and an alignment regression fix it. The corrected Windows build passes the full 853-case CTest suite with zero failures (24 expected/environment skips), the native-VU gate, the frame-4800 headless costume gate, and a 180-frame windowed launch with a fresh default config. A 7000-frame opening run shows Monica and enemies; a 9000-frame unattended combat run reaches Game Over. Palm Brinks was loaded from a scratch copy of File 2. The new debug route reaches Dungeon 2 Floor 1 loading; its event-point placement loop stalls before gameplay, and the continuous opening-to-town route remains unverified.

### 4.2 Debug mode (user-requested: on by default, documented). Implemented; Dungeon 2 floor reached, loading stall remains

Findings (sweep):
- Boot (`MainLoop`, retail `ps2/src/mainloop.cpp:508-515`, PC copy `port/src/mainloop.cpp:97-104`): `MasterDebugCode = MASTER_DEBUG_CODE` (0x5D44, `mainloop.hpp:203`) when pad 2 holds L1+L2+R1+R2; then `DebugFlag = false` and `GamePad__2.DebugKeyLock(1)` locks pad 2. The only consumer is `title.cpp:2377`: pad 2 is unlocked only if `MasterDebugCode` matches and `MasterDebugModeOn` is set, which comes from `CMemoryCardManager::CheckDebugCode()` (`memcard.cpp:460`, a save with `debug_code > 0`, `title.cpp:2281`). Retail also needed a developer save.
- `DebugFlag` (`mainloop.cpp:2112`) is the real switch, toggled by pad 2 Circle (`mainloop.cpp:677`).
- Pad 2 also drives, ungated by `DebugFlag`: pause on Select, then Right/pad 1 Circle steps a frame and pad 2 Start stores a frame image (`mainloop.cpp:724-741`, in edit, dungeon and title); L1 toggles the performance meter (`mainloop.cpp:671`); in town Right/Left scrub the time and R2 toggles rain (`editloop.cpp:1080-1092`, `1879`).
- With `DebugFlag` on, pad 1: title Start+Select -> `LOOP_MENU` debug menu (`title.cpp:848`; map/dungeon/title/chrview/texview/mapview/sound/movie views, language, items, save edit, `dbg/game%d.cfg`, save convert; its event select holds chapter starts, sub-games, Palm Brinks, event viewer, boss battle, future map (`mainloop3.cpp:115`), diorama, HDD, extra). Dungeon: R3 opens the dungeon debug menu (`dng_debug.cpp`), Start+Select exits the floor. Town/georama: R3 opens the edit debug menu (`editdebug.cpp`), R2 makes materials free, L3/Square moves at 3x. Menus: R3 toggles `menu_debug_flag`. Events: Start skips a movie (`event_func.cpp:4776`).
- PC gaps: the keyboard feeds only pad 1 (Kazarov `input.cpp` `Compose`), so pad 2 needs a second controller; the `developer_menu`/`debug_menu` host actions are bound (paddle2/paddle1) but nothing reads them; Capture/Play on the debug top screen hit the `CGamePad` capture stubs; GyoRace hits `sgLoopGyoRace`.

Implementation: `game.debug_mode` now defaults to true and is parsed and serialized by `port/src/platform/config.*`. Before the first title loop, the host applies `MasterDebugCode`, `MasterDebugModeOn` and `DebugFlag` from that setting, satisfying the retail title's unlock checks without a developer save. F10 (also Gamepad:paddle1) consumes Kazarov's existing `debug_menu` host action; the retail Circle toggle remains. `docs/DEBUG.md` documents both routes; config and global-state tests cover defaults, serialization, unlock state and toggling. The direct route and real-data script `tools/input/sweep-debug-dungeon.input` reach Dungeon 2's `map/d/d02/d02_1.stb`; repeated Cross selects floor 1 and loads `d02m01a.img`. The chart's first accessible floor box, immediately right of ENTER, is already highlighted on this fresh-save route; another right moves to a locked `?` node. After the floor transition, `SearchMapFlatPosition` repeatedly returns false, printing `flat put err`; this matches the unbounded treasure-placement retry in `AutoSetTreasureBox`, which does not clear `searching` on failure. The underlying absence of a valid flat collision hit is unresolved, and no playable-floor screenshot has been produced.

### 4.3 Sweep path still ahead

Opening event (4.1) -> first town (Palm Brinks) -> first dungeon, by `--input`, fixing each stop. A copied user save reaches Palm Brinks, "A night at the circus," but the opening-to-town route is not yet scripted. The direct debug route reaches Dungeon 2's floor chart, selects floor 1 and begins loading `d02m01a.img`; it stalls before gameplay while event-point placement retries without a valid flat collision hit. Investigate why the dungeon collision query produces no acceptable floor, then rerun the route and continue the broader scene sweep. An 80-frame right-stick sweep shows Max and several NPCs, including actors past the old 4:3 boundary. Partial-right, full-right and full-left captures show the expected camera change with Max and nearby NPCs visible and no inner boundary lines. `MainLoop` keeps the retail frame outline only when the visible extent fits inside the native screen, so Fill layouts have no inner boundary. `GrabFrameIfSampled` snapshots the main target when the game samples it during an open frame: the saved-scene replay went from 366 self-sampling warnings to zero, and the bright dropped-draw blocks disappeared. The user's farther bench/cutscene disappearance is not reproduced by these town captures and remains the next model-visibility probe. Known stubs on that path: GyoRace (`sgEffectDrawGyoRace` + `sgLoopGyoRace`, `port/src/gyorace.cpp`), `MenuChapterInit`, `CMenuItemUse::UseItem`, the four `CGamePad` capture functions; SDK `sceCdSearchFile`, `sceCdSeek`, `sceMcRename`, `scePadEnd`, `scePadPortClose`. `event_func` `_SWE_SET_COLOR`/`_SWE_SET_TEXTURE`/`_SWE_START_EFFECT` `(slot << 2)` strides. `effscript.cpp:5564` `RS_CALLDATA` is one quadword for call depth 2 (undersized in retail too).

### 4.4 `present` leftovers

- In-game proof of interpolated skinned poses, and the HUD probe and the resolution matrix in the first town and a dungeon (needs the sweep past 4.1). Max on the costume screen is the first skinned model reachable now: run `--window WxH --frames N --screenshot X.png --screenshot-display 4` with `sweep-newgame.input` and compare consecutive display frames.
- Host projection and frame-culling checks now cover a 3D mesh beyond the native 4:3 frame in a 1280x720 Fill target; the real save run also shows NPCs past the 4:3 edge. Main-target self-sampling is snapshotted during an open frame, and the saved-scene replay is warning-free; the camera-dependent loss in the user's reported circus cutscene is still open.
- The resolution and rate matrix as a ctest case. The matrix scripts were `<scratchpad>/present/matrix.sh` and `imgcmp.py` (not in the repo; numpy + Pillow on the Windows host's `py`).
- Audit #14: render targets keyed by handle instead of TBP0; not started. `FrameGrabAt` adds one more TBP-keyed lookup.
- The 4K offset: the sweep saw a 3840x2160 windowed frame sitting right of centre with a black bar on the left (`ChronicleTwo-sweep/port/build/sweep/hf-w.png`, fresh default config, costume screen). `present` could not reproduce it on a 4K panel at 150% scaling (client 3840x2160 at 0,0, presented frame and canonical image centred; also 2560x1440 and 1280x720). Next step: the exact command line and config of the sweep run, the monitor's scaling, and whether the window went fullscreen or was moved.
- Letterbox centre is within 1 of a 960x720 render on 23 pixels, not byte-identical (3D rasterised in a wider viewport).
- FPS counter in the game's font: not done on purpose (the font texture is per mode block and `FontTex[0]` dangles across modes; Chronicle made the same choice). It uses the overlay's font.
- `mgPortInterpScope` restore resets the outer teleport/blend (no gfx getters).

### 4.5 `text2d` leftovers

- A thin dark vertical line on the costume screen, left of Max's head (about x 865, y 130-300 at 1280x960 in `gate-4100.png`/`confirm-4800.png`). Not investigated: find which draw writes it (a sprite edge or a 3D line).
- The boundary sampling rule is not applied to textured triangles or to linear-filtered sprites (GS bilinear at 1:1 integer UVs blends the texel before the edge; the shader's footprint models that but it is unverified at upscales).
- Verify: `ctest -R "MgDrawPrim|MesWin"`; the costume screen at `--window 1280x960 --frames 3500`: no dots under glyphs, no black lines at window x 117/757, the dialog's upper section evenly dark.

### 4.6 `buildperf` leftovers (ranked by expected saving)

1. Debug optimisation for game code (`-Og`/`-O1` for ct_ps2/ct_port/ct_host in Debug, for example a `CT_DEBUG_OPT` cache option in `port/CMakeLists.txt`): the release title run is 4.6 s vs 18 s in debug with a byte-identical screenshot. Re-run both test suites under it.
2. Persistent build container and the data junction mount (Kazarov `lib/host.sh`, `bin/kazarov`, `host/kazarov.ps1`: `kazarov up`/`down`, exec into a running container, `KZ_PERSIST=1`, a bind mount for each top-level junction such as `data/`): saves 6-18 s per Linux command and lets Linux data-dependent runs work in worktrees. Implemented and reverted because the tty exec path and the PowerShell path were not verified. The patch is `docs/port/process/kazarov-persistent-container.patch` (against Kazarov `dc8b81a`; apply with `git -C <Kazarov worktree> apply`); its 9 bash and 4 ps1 tests passed in the container.
3. Do not initialise SDL's joystick/gamepad subsystems under `--headless` (Kazarov `port/platform` or `port/src/main.cpp`): ~2 s per headless run.
4. Linux container filesystem: ninja stats ~2,229 bind-mounted files at ~2 ms each plus a 2 s glob check per build; a build directory on a Docker volume, or `CONFIGURE_DEPENDS` off in the container.
5. ctest: higher `-j` for CPU-only cases, GPU and focus-dependent cases (`MouseCapture.*`) serial through `RESOURCE_LOCK`/labels. `Truncations.Check` (35 s) and `Records.Check` (20 s) are the Linux long poles.
6. A one-command Windows loop (`tools/windows/quick.ps1`: ninja directly, DLL copy only when changed, headless run with a screenshot, joystick env set).
7. Chronicle could adopt the split/cache check and single-process copies (Chronicle's owner).
8. ccache/sccache: not installed; needs `tools/windows/acquire.ps1` and `image/Dockerfile` changes (an image rebuild).

### 4.7 Carried from W3 and `records`

- `CMenuItemInfo::ExitEnd` (`ps2/src/menusys.cpp:6860`) reads `MenuCommonInfo->cursor` as a `void *` and truncates it; needs an override by the menus' owner (right on little-endian hosts).
- Save images are host layout on PC; `USER_PICTURE_INFO::image` is stale until `CInventUserData::ResetAddress` (`ps2/src/inventmn.cpp:843`) runs. No reader before menu init found.
- `mgCVisualFixMDT::DataAssignMDT` (`ps2/src/mg_visual.cpp:610`) stores an address in `mgCFace::packet_tag_word` (`u_int`).
- `CMenuSystemData::item_cursor` save field, `_SET_INDEXOBJ_SIZE` (only `_GET_INDEXOBJ_SIZE` is indexed), the `DATA_HEADER` fixup gate (PS2 only).
- draw2d: per-object host tables grow with distinct object addresses (3D sprite records never erased); bound them per mode.
- prog0: GPU meshes for Fix/Motion faces are never freed (live until the face address is reused).
- fx-shadow: `mgStoreZBuffImage` answers one tick late (photo's first distance is 0); no in-game screenshot of shadow, water, outline, DOF or lens flare yet.
- audio-game: end-of-stream 0x8000 held 1 s; `SndInReverb` is a no-op; movie PCM on mixer port 5 rather than a stream channel (PC.md "Movies").
- The older main-target self-sampling warning is resolved by the `GrabFrameIfSampled` snapshot path; recheck startup/logo playback if the warning returns on a new run.
- `GfxMisc.PipelineCache` is slow on Windows since the FFmpeg DLLs (30 s budget).
- Flake: `MouseCapture.CapturesWhenFullscreenStartupGainsFocusLater` (Kazarov) fails on Windows while other `darkchronicle.exe` windows hold focus; passes on rerun.
- WP7.3 (options, first run, `config.json` docs), WP7.5 (64-bit audit closure), WP7.6 (docs, CI headless boot with data) are not started; the `options` brief in `docs/port/process/SCHEDULE.md` section 9 still applies.

### 4.8 User decisions pending

- Licences: neither ChronicleTwo nor Kazarov has a LICENSE file. The Windows build ships FFmpeg DLLs under LGPL v3 (minimal libavcodec for MPEG-2); the LGPL notice and source-offer obligations need the user's decision on the project licence.
- The overlay font's licence (used for the FPS counter and the overlay) is unrecorded.
- Chronicle's Windows `--headless` aborts with `filesystem_error ... __wide_to_char` after exporting the game text (predates Phase 0; for the Chronicle owner).
- A stray `C:\Users\Adubbz\Development\PS2\Projects\Chronicle\port\build\windows-x64-tests` folder was created in the user's Chronicle checkout by an earlier run; the user should delete it (agents must not touch that checkout).
- The user's uncommitted Chronicle lighting work (in `...\Chronicle`, branch `shared-port`: `port/src/gfx/*` bloom, motion, raytracing, material maps, shipwreck fog, Streamline) was written against Chronicle's old in-tree `port/src/gfx`. Chronicle now builds its renderer from Kazarov (`tools/kazarov/port/gfx`), so that work must be rebased onto the Kazarov gfx layout (new Kazarov files behind a config flag, Chronicle-side glue in `port/src`) by the user or with them.

## 5. The W4 plan remaining

0. Run the Windows build, Windows tests and the Windows headless costume run on `master` (3.4 gate note).
1. Sweep: 4.1, then the opening, first town, first dungeon (4.3). One stream, Windows headless, input scripts in `tools/input/`.
2. Debug mode (4.2): small and independent; can run beside the sweep (owns `port/src/platform/config.*`, `port/src/mainloop.cpp`'s debug lines, `docs/DEBUG.md`).
3. `present` leftovers (4.4) once the sweep reaches a town or dungeon; the matrix as a ctest.
4. `options` (WP7.3/7.5/7.6) per its brief.
5. `buildperf` leftovers (4.6).
6. Phase 7 gate (SCHEDULE.md section 9 "W4 integrator"): town and dungeon by `--input` with screenshots, truncations/records clean, stub count equal to the documented list, CI green, the resolution and rate matrix.

## 6. Branches left on origin

None besides `master` (ChronicleTwo, Chronicle) and `main` (Kazarov). Every W4 stream was merged; no `wip/*` branches. All W4 worktrees and stream branches were removed.

## 7. How to resume

- Agent model used: Fable plans (`docs/port/process/SCHEDULE.md`), an Opus orchestrator per stream and one Opus integrator per push point, Haiku agents (`model: "haiku"`, `run_in_background: false`) read and write all code. Orchestrators review by `git diff` only. Every brief = section 9 PREAMBLE + the stream block; the addenda (`ADDENDUM-W0/W1/W3.md`) override it.
- Worktrees: SCHEDULE.md section 4.1. `ChronicleTwo-<stream>` on `s/<stream>` from `port/pc`; `Kazarov-<stream>` on `kz/<stream>` from `main`. `data` and `.kazarov\windows` are junctions to the main checkout; `rom/pal/` holds a hardlink to the ISO for ps2-pal (never a `rom` junction). Before `git worktree remove --force`, remove the junctions with `cmd /c rmdir <wt>\data` and `cmd /c rmdir <wt>\.kazarov\windows`: remove follows junctions and once deleted the main `data/`. Never run `run.sh`/`run.bat` in a worktree (it re-extracts the shared `data/`).
- Linux runs from a worktree see no data (junctions do not resolve in the container): data-dependent Linux checks run in the main checkout; streams test data on Windows.
- Pinning: SCHEDULE.md section 4.2; the integrator merges `kz/*` into Kazarov `main`, drops the streams' pin commits on rebase and pins `main@<sha>` once.
- Gates (run from the checkout root, `KZ=tools/kazarov/bin/kazarov`, one heavy build at a time):
  - Linux: `JOBS=16 ./build.sh linux-x64 && $KZ shell ctest --test-dir port/build/pc --output-on-failure -j4` (798 cases at this push, all pass).
  - `$KZ shell python3 scripts/port/truncations.py --build port/build/pc --check`; `$KZ shell python3 scripts/port/records.py --build port/build/pc --require-clang`; `$KZ shell python3 scripts/port/native_gate.py --binary port/build/pc/darkchronicle`; `$KZ shell bash -c "objdump -d port/build/pc/darkchronicle | grep -c 'call.*Ps2Unimplemented'"` (12).
  - PS2: `JOBS=16 ./build.sh ps2-pal && $KZ shell env BUILD_DIR=build/pal python3 scripts/build/verify.py -c` (`SCES_511.90: OK`).
  - Windows (pwsh 7, checkout as working directory): `.\build.bat windows-x64`; `./tools/windows/configure-tests.ps1 -Root .kazarov/windows -BuildDirectory port/build/windows-x64-tests; ./tools/windows/build.ps1 -Root .kazarov/windows -BuildDirectory port/build/windows-x64-tests -Tests; & 'C:\Program Files\CMake\bin\ctest.exe' --test-dir port/build/windows-x64-tests --output-on-failure -j4` (803 cases, 3 TtfFont skipped). Run it with nothing else heavy running; re-run load timeouts serially.
  - Headless: `$KZ shell port/build/pc/darkchronicle --headless --offscreen --data data --save port/build/pc/save --input tools/input/sweep-newgame.input --frames 4100 --screenshot out.png` (costume screen); Windows `port\build\windows-x64\darkchronicle.exe --headless --data data --save port\build\windows-x64\gate-save --input tools/input/sweep-costume.input --frames 4800 --screenshot out.png` ("Confirm Max's costume?"). Use a fresh save directory.
  - Kazarov pytest, in the container only: `<scratchpad>/kzpytest.sh` ran `docker run --rm -v <Kazarov>:/kz -w /kz kazarov-dev:<tag> bash -c 'python3 -m venv --system-site-packages /tmp/v && /tmp/v/bin/pip -q install pytest && /tmp/v/bin/python -m pytest -q tests/test_kz_mwcc.py tests/test_kz_lock.py tests/test_kz_extract.py tests/test_kz_split_stamp.py tests/test_kz_cached.py tests/test_kazarov_elf.py tests/cmake tests/test_host_ps1.py'` (76 passed, 14 skipped).
  - Chronicle on a Kazarov commit (in `Chronicle-phase0` after `git pull --ff-only`): pin the sha in `tools/kazarov`, Linux build + ctest (357 cases), `darkcloud --headless --data data --save port/build/chronicle-save --frames 3 --screenshot c3.png` compared with `cmp` against the W0 baseline `chronicle-3.png` (kept in the session scratchpad; regenerate from Chronicle `c6a8c918` + Kazarov `f1b8e80` if lost), `ps2-pal` 100%, `build.bat windows-x64`.
- Windows quirks: Windows PowerShell 5.1 aborts on cmake's stderr, use pwsh 7; `ctest` is not on PATH; no Vulkan validation layer on Windows; host Python is `py` (3.11, with numpy and Pillow; `py -I` hides them).
- Docker: image tag = hash of `tools/kazarov/image` (`kazarov-dev:b4dcf4978934` for the build; the pytest helper used `e425e267ff38`). If Docker Desktop will not start, stop a stale `com.docker.extensions` process and restart it.
- Saves: `--save port/build/<config>/save` or a fresh directory per run, never `save/` beside the executable.
- Git policy: author `Adubbz <Adubbz@users.noreply.github.com>`; messages `Port:`/`Kazarov:`/`Build:`/`Docs:` with no co-author or tool attribution; pushes fast-forward only, never force, in the order Kazarov `main`, Chronicle `master` (from `Chronicle-phase0`, rebased onto upstream first: other contributors push there), ChronicleTwo `port/pc:master`. Every pin must exist on Kazarov `main` first. After a push, `git log --format='%an <%ae>%n%b' <old>..origin/master` must show only Adubbz and no `Co-Authored`, `Claude`, `Anthropic` or `Generated with`.

## 8. History (W0-W3)

- W0-W1: Kazarov shared code (`port/{gfx,audio,platform,sce,tests}`), boot, data (`dc2data`, 8751 files), saves, threads, VU1 specs (`docs/vu1/`).
- W2/W2.c/W2.u: mglib, textures, sound base, movies base, memory arenas above 4 GiB, `uptr`/`sptr` in `ps2/src` (`truncations.py --check`), long64.
- W3 (pushed `cc2f461`): present-core (display lists, display frames, aspect, video settings), draw2d (mgCDrawPrim, sprites, TexAnime, no GIF decoder), prog0 (MDT meshes on the GPU, prog0 constants in a DC2-only block), fx-shadow, audio-game, movies, the native-VU audit (`native_gate.py`). Details in `docs/port/process/wave3-report.md` and `W3-NEEDS.md`.
- `records` (after W3): 57 records asserted (`Records.Check`), `funcdata::name` as `u32` (the one approved pointer-declaration exception), `GetAppearVlgr`/`_GET_INDEXOBJ_SIZE` indexed on PC; `docs/port/records.md`.
