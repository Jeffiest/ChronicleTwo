# Chronicle-Two (Dark Chronicle PAL PC Port) — Project State

## ⛔ RULES
1. Any edit to code, config, scripts, build files, or paths MUST be recorded in this state file before reporting done.
2. ZERO .word MIPS Assembly: genuine, portable, human-readable C++.
3. ZERO Empty Placeholder Stubs: full decompiled logic implemented.
4. ZERO Algorithmic Guesswork: decompiled source of truth from `ps2/` and Ghidra.
5. Live PCSX2 / Recomp A/B Verification: compare against retail PAL and recomp ground truth.

---

## Current Status Summary (2026-10-11)
- **Upstream integration in progress**: user authorized combining the Windows port with
  `TheMoonPeople/ChronicleTwo:master`. Fresh upstream tip `4ee9003031d9379aebdb7dba6094ce000370d6f1`;
  shared base `872e1250`, 401 local commits and five upstream commits. Preserve current compatibility
  changes in a checkpoint before merging on `port/master-sync-2026-10-11`. Review build/configuration,
  upstream retail corrections and overlapping native implementations separately; build centrally.
  Fork `master` also diverges (407 local / 332 fork commits); syncing upstream does not by itself
  establish compatibility with that separate branch. No main branch will be overwritten.
- **Active compatibility work**: Windows branch `port/host-compatibility`, based on pushed checkpoint
  `132ba152`. First batch implemented: all four script builders' host storage, checked arena operations,
  source hazard inventory enforced by normal builds, and typed monster arena initialization.
  Windows RelWithDebInfo game build and `dc_compatibility_tests` pass. `ps2/` remains untouched.
  Changes are local/uncommitted on this branch. User reached controllable Monica; menu initialization remains
  under repair. New shared color readback and corrected menu capture reserve padded output before writing and
  read the completed frame before reopening recording. Photo/effect callers during recording still abort explicitly.
  Input/menu navigation integration and controller lifecycle fixtures pass. Arena diagnostics now identify owners,
  high-water usage, first failure and binding addresses without changing the reserved tail or optional-null rules.
  Title Options exposed two four-byte pointer-array traversals: common filename loading and Options minimap
  selection are repaired and tested. Checked generated-host rewriting preserves private Options state and stops
  source drift. User confirms Options and dungeon menu open, battle movement and lock-on work, but A/Cross
  does not attack. Items crashes in shared model loading after the menu model arena exhausts 39,616 blocks.
  PID 12768 has exited; trace preserved in `logs/menu-option-err.log`. Items and attacking remain unresolved;
  no new gameplay launch after this report. The Xbox A action press/hold/release fixture passes, and shared
  Xbox mapping/polling matches DC1's pinned shared source. In-game combat state/script cause is unproven.
  Latest game build and 26 native cases pass (`logs/pointer-storage-build.log`), plus eight Python audit/gate
  fixtures and script/menu/photo rewrite checks. Generated/native byte-stride pointer-array check now gates
  normal builds independently of inventory freshness; both villager output paths are typed and tested.
  Latest executable SHA256 `D71B5C02B33EEA77FD6BE9244C592265BC0B0A9962D627CBE6F25D1071CAB765`.
  Plan: `reports/SYSTEMATIC_PORT_PLAN_2026-10-11.md`; next model-owner contract is
  `win/ChronicleTwo/docs/port/MODEL_MEMORY_CONTRACT.md`. Full combat, skip and map reentry remain unverified.
  The 583/584 pool is `LoadFilePictureName` photo-subject storage. PAL has 294 records; host record width is 16
  versus retail 8 bytes. Checked host setup now preserves retail storage plus that 147-block delta (731 blocks),
  owns backing across loader return and makes required failures explicit. Actual PAL load and all 294 name
  lookups pass without graphics/gameplay (`logs/photo-names-local-pal.log`). Active photo capture is separate.
- **Port executable**: Builds and runs on PC (Linux WSL / server / local copy).
- **Vulkan pipelines**: 504 GPU pipelines compiled and cached.
- **PC port rendering (latest)**: Select Costume renders Max correctly including both hands (`c8f8d4163`, skin-bone
  frame-number bound). New Game reaches the first dungeon (`map/d/d01`, `d01_1.stb`) after the character-stack fix
  (`3139f445f`). Fixed the subsequent HUD fault — `CPreSprite::SetAlphaBlend` ← `DrawActiveItemCursor` ←
  `DrawMainUnitStatusBord` ← `DngMainDraw` ← `LoopDungeonMain`, right after `texture over 0:s510109`. The HUD fault
  comes from `CPreSprite::SetAlphaBlend` writing through the unused host DMA packet pointer. Host overrides for
  this helper and `SetScirror` now use `Direct` to update active draw state; Windows RelWithDebInfo build passes.
  User's separate run rendered the prologue (knight and Monica's corridor), then looped a running scene and
  crashed after skip. Windows Application event 1000 at 14:37:54 CDT identifies RVA `0xafcae`, symbolized to
  `CCharacter2::DeleteExtMotion` (`ps2/src/character.cpp:1456`): fixed PS2 offset/stride used to access `images[]`.
  A host override now uses the typed member; Windows RelWithDebInfo build passed. Replaced the idle diagnostic
  instance with a fresh detached traced game. User could not see restricted-shell launches; relaunched with desktop
  access as PID 16916 (`logs/skip-fix-desktop-{out,err}.log`); user confirmed it stalls in the running scene.
  Live inspection found `_OBJS_CHECK(0)` waiting with both paths' `spline.speed = NaN`. Path setup samples a
  zero-duration endpoint, producing `0/0` on the host. New `port/src/sceneseq.cpp` overrides frame/distance
  stepping to read that marker's stored endpoint directly. Windows RelWithDebInfo build and captured-path
  regression pass; original host stepping fails at frame 57. Relaunched with desktop access as PID 29348 to
  `logs/spline-fix-{out,err}.log`. User confirms the entire cutscene completes with some remaining visual/behavior
  bugs; battle does not start. Fresh logs show `stack over 38/0`, `64/0`, `384/0`, then a crash during monster
  script setup (`SetMonsterScript` <- `CMonsterMan::SetActiveMonster` <- `AutoSetMonster`). User requested a
  broader upstream/Dylan/DC1 reevaluation before further symptom fixes. Completed review:
  `reports/PORT_REEVALUATION_2026-10-11.md`. Next implementation is a host-compatibility foundation pass,
  host-sized script arrays and the raw monster-arena offset have since been repaired in the first batch;
  broader ownership/lifetime checks and pointer/layout/hardware review remain. Battle is the first gameplay
  acceptance checkpoint. `LoadMonsterFile(int,int)` used a PS2 offset/stride to initialize PC arenas; this
  confirmed defect is repaired; the subsequent user run now reaches controllable Monica. Combat remains unverified.
  The previous detached diagnostic
  instance was still at language selection, so `logs/hud-fix-{out,err}.log` does not capture the user's crash.
  The texture message reports a VRAM block spill, not necessarily exhaustion. Crash tracing:
  `DC_CRASH_TRACE=1` + `llvm-symbolizer` (`port/src/crash_trace.cpp`).
- **W1 retail boot diagnostic**:
  - Without `--jump`, game boots into `SCElogoFade` (`ps2/src/nowload.cpp:640`) and executes `TitleLangSelInit` / `TitleLangSelKey`, looping waiting for controller input on the PAL Language Select screen.
  - `--jump edit:0` shortcut exhibits event script warning `<START EVENT> OLD MAP ERR` because there is no previous map in memory on a cold jump (`_GET_OLD_MAP_NO` returns -1).
- **Reusable Recomp Intelligence (`e:\DC2_Recom-Jeff`)**:
  - `title_map_pipeline.inc`: Documents the exact title 3D background map ("s19") loading pipeline (`TitleInit` -> `LoadMap` @ 0x285ce0), why title maps fail if `MainScene` stack slots / `CMdsListSet` slot counts are 0.
  - `ps2_vu1_lower.cpp` / `ps2_vu1_upper.cpp`: Fully working C++ implementation of all VU1 lower/upper instructions (`ERSQRT`, `ESQRT`, `ERATAN`, `EBP`, `MFIR`, `MTIR`).
  - `SOUND.HD3`: Format identified and mapped (86 KB table indexing 3,101 48 kHz 16-bit PCM streams in `SOUND.DAT`), solving upstream plan's open unknown.
  - GS Rasterizer: Depth testing Z24/Z16, PSMT4/PSMT8 CLUT palette swizzling, ABE/PABE alpha blending.

### dc1-sync (DC1 host-layer sync, worktrees `ct-dc1-*`)
- The earlier audio/platform slice review found DC2-specific features worth retaining: per-port banks, voice-held bank lifetime and effect pitch, plus existing SDK calls. That limited comparison did not establish that DC1 had no other useful architecture. The October 11 reevaluation finds its guarded arenas, typed allocation sizing, serialized-record boundaries and CI pointer-truncation audit directly relevant. Placement-run growth is already partially adopted here; raw arrays, capacity/lifetime checks and audit coverage are incomplete. Keep PAL timing and game-specific behavior while adapting those safeguards.
- Known issue (slice worktrees): `darkcloud_tests` cannot link because it needs files outside any single slice (`port/src/dataalloc.hpp`, `arena.hpp`, `dataset.hpp`, `memorycardaccess.hpp`, `texture.hpp`, `tools/dcdata/dcdata.hpp`), none present in `ct-dc1-*`. Slices are verified with `dc_host` + `dc_port` objects; the game/test link is the integrator's step.

---

## Reevaluation baseline and next work (2026-10-11)

- Checkpoint branch: `port/checkpoint-2026-10-11-cutscene` in the Windows clone, created from `3139f445f`.
  Pushed commit `132ba1529d68c9414af31714b8cedf1cf8402ada`; remote branch hash matches and working tree is clean.
  Fork remote `github-fork`: `https://github.com/Jeffiest/ChronicleTwo.git`; local `origin` still points to WSL.
  The checkpoint includes the three host fixes plus archived review/state/handoff and regression files under
  `docs/checkpoints/2026-10-11/`. Root files remain canonical; archived copies capture checkpoint preparation.
- Report: `E:\Chronicle-Two\reports\PORT_REEVALUATION_2026-10-11.md`.
- Fresh GitHub review refs: upstream master `4ee90030` (five new commits beyond shared base `872e1250`),
  Dylan `port-bootstrap` `56555a37` / master `6a24d4f2`, DC1 master `706ce00e`, Kazarov main `ce459c1`.
  Upstream and Dylan bootstrap are earlier milestones than this executable; no merge, checkout or game-code
  update was performed. Parent refresh with submodule recursion disabled completed successfully.
- Audit by defect category, beginning with script allocation and scene/monster arena lifecycle. Preserve
  fixed-format record assertions separately from expanded runtime objects. Use layout/truncation inventories
  and hardware coverage checks before more scene fixes; adapt upstream tooling rather than assume it is complete.
- First batch repairs all eight event/action/effect/monster value/call allocations via checked generated-host
  rewriting and typed required allocation. A failed tracked placement growth now aborts before construction;
  optional raw allocation retains null fallback. Typed monster setup replaces raw offset 4 / stride 0x30.
  Single-run tracking still cannot prove older allocation sizes; guard pages and full lifetime tracking remain.
- `docs/port/COMPATIBILITY_AUDIT.md` documents the build gate and limits. Inventory: 5336 lexical findings,
  seventeen repaired source sites with evidence; other findings remain open and may include false positives or
  replaced/dead code. A clean inventory check is not proof that all portability hazards are solved.
- Run focused tests: `cmake --build E:/Chronicle-Two/win/build-rel --target dc_compatibility_tests`.
- Validation gate: adequate non-overlapping script arrays and valid arena state before script execution,
  followed by user-controlled full cutscene, skip cleanup, monster spawn/combat and map exit/reentry.
  Current cutscene and controllable Monica verified by the user; combat and skip cleanup not yet verified.
  Options and dungeon menu opening are now user-verified. All seven detected byte-stride pointer-array sites
  have repair evidence, with a separate generated/native build gate for this narrow pattern. Broader compiler
  analysis remains incomplete. Items exhausts shared menu model storage; see `MODEL_MEMORY_CONTRACT.md` for
  the partition/write-before-reserve evidence and DC1 ownership/guard reference. Windows ABI probes measure
  frame 304 vs retail 272 bytes, motion visual 320 vs 272, face 48 vs 48 and face group 56 vs 32; earlier
  claims that the host face itself is wider are superseded. Primary-character doubling remains headroom.
- Current systemic plan: `reports/SYSTEMATIC_PORT_PLAN_2026-10-11.md`. Audit closure requires implementation,
  sibling-callsite review and contract fixtures; inventory freshness alone is insufficient. Plan review made
  no game-code changes and did not launch or drive the game.
- Audio reference verified against all local PAL SOUND headers: 3,101 mono 48 kHz 16-bit PCM WAVE entries,
  variable RIFF chunk order, first-match duplicate handling required. Host streams currently play nothing
  and report end after 1.5 seconds. Shared stream state/backend work follows controllability; the exact
  missing Monica voice path is unidentified. Reference: `C:\Users\armor\Downloads\SOUND_DAT.md`.

## Session Log

| 2026-10-11 | `port/host-compatibility` / upstream integration preparation | Refreshed upstream master and fork branches; planned checkpoint, integration branch, bounded conflict review, merge, centralized game/compatibility validation and branch publication. Preserving all current compatibility implementations and canonical state in a checkpoint before the merge. | Upstream `4ee90030`, base `872e1250`, divergence 401/5. Dry-run identifies overlapping bootstrap/build/runtime and retail correction conflicts. Fork master is separately divergent 407/332. Fetch succeeded with desktop credential access; no game launch. |

| Date | Branch / Path | What changed / discovered | Evidence |
|---|---|---|---|
| 2026-10-11 | `port/host-compatibility` / Windows clone | Completed concurrent villager pointer-output and photo-name storage packages while integrating a separate generated/native pointer-pattern gate. Added actual-method schedule/output/canary fixture, checked photo rewrite, persistent derived backing, required-allocation checks, eight audit/gate fixtures, CMake/runner wiring and local PAL asset-loader case. Extended A/B/R2 game-action assertions; corrected inaccurate wider-face comment after actual ABI probes. Updated ledger, coverage, systemic plan and model-memory contract documentation. | `logs/pointer-storage-build.log`: game + 26 native cases pass, including ten expected failures; eight Python fixtures and script/menu/photo rewrite checks pass. Gate failed on both original villager writes and passes after override generation. `logs/photo-names-local-pal.log`: actual PAL loader and all 294 stored name lookups pass. User confirms Options/menu and battle movement/lock-on; A attacks fail, Items crashes. Trace: model arena 39603/39616 rejects 37 then CreateFace dereferences null; shared partition and write-before-reserve path identified, repair pending. DC1 ArenaBlock/guard/headroom source reviewed; shared Xbox mapping/polling files identical. No further game launch or automated gameplay; all changes local. |
| 2026-10-11 | `port/host-compatibility` / Windows clone | Recorded concurrency preference: continue useful independent work during delegated packages, preserve spending/ownership limits and centralized builds. Started bounded villager pointer-output and measured photo-name storage packages, with a read-only active-capture lifecycle review; integration owner adds generated/native pointer-pattern enforcement. | Title Options process 12768 remains running with no crash trace at check; in-game success not yet confirmed. Implementation and validation evidence will be appended when this batch completes. |
| 2026-10-11 | `port/host-compatibility` / Windows clone | Repaired Options minimap pointer indexing through exact checked generated-host rewriting, retaining private form guard and retail selection behavior. Added fixture extraction from the actual generated host unit and a native Options target to the shared runner. Expanded pointer-array detector/regression; reviewed seven detected sites, recorded five repaired dispositions (two earlier overrides, two common-loader sites, one Options site), with two villager-output sites open. Added input/capture coverage matrix and implementation update to the systemic plan; updated state/handoff. | `logs/menu-option-build.log`: RelWithDebInfo game and 19 native cases pass; four audit fixtures and script/menu rewrite checks pass. Options fixture tests map choices 0/1/2, other rows, anger condition, Japanese camera rows and inactive guard. One fixture-link repair added minimal unused base/member initialization stubs; tested methods remain extracted unchanged. `git diff --check` clean, retail/submodule diff empty. Desktop PID 12768, logs `menu-option-{out,err}.log`, in-game acceptance pending. Local/uncommitted changes. |
| 2026-10-11 | `port/host-compatibility` / Windows clone | Added input/menu lifecycle integration and language binding fixture; shared bounded color readback and padded menu capture with completed-frame timing; named arena first-failure/lifecycle diagnostics and image-relative caller evidence. Repaired shared menu filename traversal, validated path/mode/size and reserved arena space before reads. Added color/input/menu fixtures, CMake targets and runner; expanded audit to byte-stride pointer arrays. Updated canonical handoff and audit coverage documentation. | RelWithDebInfo game build and 18 native cases pass (`logs/menu-loader-build.log`); graphics fixture uses real Vulkan with resize, formats, alpha, texture targeting and actual MenuCapture checks. Four source-audit fixtures pass. PID 9112 crashed in retail common filename traversal; PID 56068 progressed into Options then crashed at `menuop.cpp:1621`, another four-byte pointer stride. `logs/input-capture-err.log` and `logs/menu-loader-err.log` preserve both traces. Photo name arena binding resolves to `LoadFilePictureName`; PAL list has 294 records. Options follow-up repair pending; no automated gameplay or retail source edits. |
| 2026-10-11 | `port/host-compatibility` / Windows clone | Completed first foundation batch from checkpoint `132ba152`: script storage helper and eight checked generated-source replacements; arena validation/overflow-safe accounting and fatal tracked placement-growth failures; source inventory/decision ledger and default build gate; native typed monster-buffer setup. Added native script/arena/monster fixtures and Python source-drift checks with shared `dc_compatibility_tests` CMake target. Root state/handoff updated. | RelWithDebInfo game build passes. Compatibility target passes: three audit fixtures, script-builder coverage, ten native success/failure cases including expected abort diagnostics and all 24 monster arenas. Generated code contains all eight typed allocations; nine ledger sites have repair evidence, 5322 total lexical findings. `git diff --check` clean; no `ps2/` changes. Desktop launch PID 36556, title Dark Chronicle, logs `compat-foundation-{out,err}.log`; gameplay result pending. Local uncommitted changes; checkpoint branch preserved. |
| 2026-10-11 | `upsync-winport` (`E:\Chronicle-Two\win\ChronicleTwo`) | Diagnosed prologue running-scene stall: script loop polls `_OBJS_CHECK(0)`; both `ObjectSeq` spline speeds are NaN. Added host spline stepping overrides that sample a zero-duration endpoint from its stored position instead of dividing by zero. Added standalone regression source/runner `workers/spline-regression.cpp` and `.ps1` using unchanged path routines from source plus built override. | Live PID 16916: script pc at code+0x2a2c, ext 450, object 0 frame 1.62301981 of 58, both paths `speed=NaN`, run=1/end=0. Retail disassembly `C3DSpline::Step` at 0x2589e0 confirms marker sampling and division at 0x258b3c. Windows RelWithDebInfo build exit 0 after closing stalled game; `git diff --check` clean. Regression exit 0: speeds 8.530462/8.097962, both paths finish in 58 steps at correct endpoint; camera eye/reference finish; original host code fails nonfinite at frame 57 (`logs/spline-regression.log`). Relaunched with desktop access to `logs/spline-fix-{out,err}.log`; in-game result pending; `ps2/` untouched. |
| 2026-10-11 | `upsync-winport` / Windows launch | User reported the newly launched game was not visible. Closed the idle restricted-shell process 53952 and relaunched the same rebuilt executable with desktop access and crash tracing; updated handoff launch guidance. | Desktop-access launch returned PID 16916; logs `logs/skip-fix-desktop-out.log` / `logs/skip-fix-desktop-err.log`. User visibility and gameplay verification pending. |
| 2026-10-11 | `upsync-winport` (`E:\Chronicle-Two\win\ChronicleTwo`) | User confirmed prologue rendering after HUD fix, reported running scene stuck and crash after skip. Added `DeleteExtMotion` host override preserving retail cleanup but replacing `this + 0x2C4 + j * 4` with `images[j]`. | Windows Application event 1000 at 14:37:54 CDT: process 0x20c8, executable `win/build-rel/darkchronicle.exe`, access violation RVA `0xafcae`; llvm-symbolizer resolves `character.cpp:1456:38` (archive traversal). Saved event in `logs/skip-crash-event.log`. Windows RelWithDebInfo build exit 0 (closed idle PID 6528 to release executable before relink); override registered, generated retail body stripped, `git diff --check` clean. Relaunched detached with tracing to `logs/skip-fix-{out,err}.log`. Screenshots show knight and corridor; running-scene hang remains undiagnosed and skip fix awaits user verification. |
| 2026-10-11 | `upsync-winport` (`E:\Chronicle-Two\win\ChronicleTwo`) | Replaced `CPreSprite::SetAlphaBlend` and `SetScirror` packet writes with host `Direct` register updates in `port/src/mg_drawprim.cpp`; preserved retail blend constants, ignored modes, and scissor packing. HUD crash was an unused DMA packet pointer, not a missing-texture dereference. | Retail `ps2/src/prespr.cpp:49` writes `packet->data`; host `BeginDma` only opens a CPU vertex run. `texture over` in `mg_texture.cpp` denotes a spill. Windows RelWithDebInfo build exit 0; override list includes both helpers and generated retail bodies are stripped; `git diff --check` clean. Detached game launched with `DC_CRASH_TRACE=1 DC_WIDE_DRAWRECT=1`; manual result pending in `logs/hud-fix-out.log` / `logs/hud-fix-err.log`. |
| 2026-10-11 | `port-crew` (`f70d66d3`) | Diagnosed W1 retail boot & audited `e:\DC2_Recom-Jeff` for reusable data | `w1-retail.log` (`SCElogoFade` language select spin); `title_map_pipeline.inc`; `ps2_vu1_lower.cpp`; `PORT_STATE.md` created |
| 2026-10-11 | `dc1sync-audio` (`ct-dc1-audio`, `56a9bd6d1`) | dc1-sync audio+sce: merged DC1's Win32 guards into `port/src/sce/libmc.cpp` and `sifdev.cpp`; made `sceCdRead` a loud `PS2_UNIMPLEMENTED()` (was a silent success no-op); no audio file changed (DC1's soundtrack/streams has no DC2 consumer and DC1's mixer would regress DC2's per-port banks + effect pitch) | `~/reports/dc1sync-audio.md`; `cmake --preset linux-x64 -DCMAKE_PREFIX_PATH=/home/jeffie/chronicle-deps`; `ninja dc_host`/`dc_port` OK; `darkcloud_tests` blocked by missing headers outside the slice |
| 2026-10-11 | `dc1sync-plat` (`ct-dc1-plat`, `12e384669`) | dc1-sync platform: brought DC2 `port/src/platform/**` up to `dc1/master` selectively — took files/display/paths/window/memory(Win32 backend)/imagefile/gamepad_map/mouse/input_script/audio(surround upmix)/firstrun, plus config change hooks; skipped DC1-only clock (60 Hz, kept PAL 50), overlay font, discord/glyphs/ttffont, save_state/touchpad, and DC1 game-option/gyro/touchpad config+input features. Neither version maps arenas at a fixed address or below 4 GiB. Added an stb_image.h fetch to `port/CMakeLists.txt` for imagefile | `~/reports/dc1sync-plat.md`; `ninja dc_host`+`dc_port` OK; `PlatformConfig`/`PlatformClock`/`PlatformGamepadMap` 18/18 + `PathsXdg` 6/6 pass in a scratch gtest runner against `dc_host` objects; `darkcloud_tests` link blocked by the same missing headers |
| 2026-10-11 | `port-crew` | Investigated missing costume torso/body rendering; located exact Pugsly recomp fix logs (G40 vf0 determinant zero, G37 AABB collapse, G15/G16 MSCNT dispatch, visualmotion scratch allocation) | `MODEL_RENDERING_RESEARCH.md` created; cross-referenced `phase-G40-fix-log.md` and `phase-G37-fix-log.md` |
| 2026-10-11 | `port-up-sync` (`ct-port-up`, `c8f8d4163`) | Fixed Max's left hand pointing inward. `BuildSkinMatrices` (`port/src/dynamicanime.cpp`) rejected any bone whose **frame number** was ≥ 32; the hands/ankles are frames 41..78, so their skin matrices stayed identity and they never deformed (the left hand kept its inward bind pose). Retail's `CreateExtRenderInfoPacket` (`ps2/src/visualmotion.cpp:738`) only null-checks `frame[slot]` — the 32 cap is on the *bone count*, not the frame numbers. Bounded by `frame[0]->frame_num` instead. Verified in-game: both hands point outward at Select Costume | `verify` screenshots (Select Costume + zoomed model, both hands correct); WSL compile `ninja -C port/build/pc darkchronicle` clean; Windows `build-rel` relink clean; log `handbone ... skin diag` was exactly `[1.00 1.00 1.00]` before the fix |
| 2026-10-11 | `port-up-sync` (`ct-port-up`, `3139f445f`) | Added `DC_CRASH_TRACE=1` crash tracer (`port/src/crash_trace.cpp`, own TU — windows.h clashes with the game headers) printing fault address + raw backtrace with load-relative offsets for `llvm-symbolizer`. Fixed the New Game crash: `GetCharaMemAllocPtr` sized Monica's skin stack `stacks[1]` at 40000 PS2 blocks (`ps2/src/charasetup.cpp:52/248`) but host `mgCFrame`/`mgCFace` are wider, so it overran (`stack over 40025/40000`), `mgCMemory::Alloc` returned NULL and `CreateFace` dereferenced it (`ps2/src/mg_visual.cpp:485`, 0xC0000005). `PORT_OVERRIDE GetCharaMemAllocPtr`/`GetCharaMemAllocSize` in `port/src/character.cpp` doubles every size (BuffCharacter ~2.3 MB → ~4.6 MB). New Game now reaches `map/d/d01` (`d01_1.stb`, `RUN SYS_SCRIPT 1010`) and dies in the HUD: `CPreSprite::SetAlphaBlend` ← `DrawActiveItemCursor` (`ps2/src/dng_status.cpp:196`) ← `DrawMainUnitStatusBord` ← `DngMainDraw` ← `LoopDungeonMain` (`ps2/src/dng_main.cpp:1035`), right after `texture over 0:s510109` — suspect the texture manager exhausted blocks loading `s51.img` | Backtrace symbolized with `llvm-symbolizer --obj=build-rel/darkchronicle.exe`; `[N]STACK` after the fix: 0) 61772/137000, 1) 40356/80000, 2) 2197/14000, 3) 1359/9300, 4) 1794/6000, 5) 10233/19800, 6) 4095/9000; logs `logs/crash-run-out.log`, `logs/crash-run-err.log`; WSL compile clean |

| 2026-10-11 | `upsync-winport` / script allocation audit | Recorded a separate host-width allocation issue for later work: `SetEventScript` allocates 1024 bytes for 128 value slots and 6144 bytes for 512 call entries; host widths require 2048 and 12288. | Live value stack starts at 0x7ff618ca6580, advertised end 0x7ff618ca6d80, call stack starts at 0x7ff618ca6980 (overlap). Only four value slots/two call entries active in captured wait, so no current corruption established. Added known issue to handoff; no allocation code changed. |
| 2026-10-11 | `upsync-winport` / GitHub reevaluation | User verified full cutscene; battle transition still fails. Began systematic upstream/Dylan/DC1 comparison and fetched isolated review refs, preserving working files. | Current GitHub tips: upstream `4ee90030`, DC1 `706ce00e`, Dylan bootstrap `56555a37`, Kazarov `ce459c1`; upstream has five commits beyond shared base `872e1250`. Parent fetch submodule warnings resolved for review by direct Kazarov fetch. Battle trace points to monster script setup after zero-capacity arena allocation failures; no new game-code edits. |
| 2026-10-11 | `upsync-winport` / root review documents | Completed `reports/PORT_REEVALUATION_2026-10-11.md`; updated canonical next work and handoff to a host-compatibility audit beginning with script sizing and scene/monster arena lifecycle. Corrected the overly broad prior DC1 comparison. | Fresh isolated GitHub fetches exit 0 without submodule recursion; reviewed upstream Phase 1, Dylan layout/inventory sources and unchanged script-stack allocations, DC1 guarded arena implementation and CI truncation audit. Local allocator and latest crash log establish separate width and zero-capacity issues; zero-capacity origin remains open. No new game-code changes, merges or playthrough; existing three host fixes preserved. |
| 2026-10-11 | `port/checkpoint-2026-10-11-cutscene` (`E:\Chronicle-Two\win\ChronicleTwo`) | Created and pushed checkpoint commit `132ba152` and configured `github-fork` remote. Included the existing HUD, image cleanup and spline fixes, plus snapshots of root state/handoff, reevaluation report, regression source/runner and results under `docs/checkpoints/2026-10-11/`. Root documents remain canonical and record final push verification; archived copies capture precommit preparation. | GitHub branch `Jeffiest/ChronicleTwo:port/checkpoint-2026-10-11-cutscene` matches local `132ba1529d68c9414af31714b8cedf1cf8402ada`; working tree clean, upstream tracking configured. Fresh spline regression exit 0: both captured paths finish in 58 steps, camera passes, unchanged baseline reproduces NaN at frame 57. Existing Windows build/user full-cutscene evidence retained; battle remains broken. Checkpoint makes no `ps2/` changes. |
| 2026-10-11 | `port/host-compatibility` / systemic plan and root documents | Recorded user verification of controllable Monica and Xbox Y menu termination; added `reports/SYSTEMATIC_PORT_PLAN_2026-10-11.md` with ordered shared input/capture, memory ownership, compiler/record enforcement, lifecycle and PCM stream packages plus bounded execution and acceptance gates. Updated canonical status and handoff; corrected earlier uncertainty about SOUND PCM format. Documentation only this pass. | Latest stderr ends at `port/src/sce/libgraph.cpp:25`, explicit `sceGsSetDefStoreImage` abort; source menu route reaches it and Xbox Y mapping is correct. Same log has 46 failures in one unnamed arena at 583/584, owner unknown. Header-only checks validate all 3,101 local PAL SOUND entries as mono 48 kHz 16-bit PCM WAVE, one duplicate name, valid extents. Stream source still uses silent fixed 1.5-second completion. No game code edits, build, launch, audio playback or automated gameplay during review; prior passing build/tests preserved. |
| 2026-10-11 | Root handoff / usage policy | Recorded user authorization to choose model and reasoning effort for value: inexpensive bounded routine work, stronger reasoning for ambiguity/integration, minimal context, two repair attempts and centralized builds. | User reports roughly 30% usage consumed. Documentation-only preference update; no gameplay/code changes or new delegated work. |
| 2026-10-11 | `port/host-compatibility` / `win/ChronicleTwo` | Extended the input lifecycle fixture through Xbox button mapping and PAL English action bindings, covering A confirm/held, B cancel, and RT/R2 press/release. | Added assertions in `win/ChronicleTwo/port/src/tests/input_lifecycle_test.cpp`; build not run per task scope. |
