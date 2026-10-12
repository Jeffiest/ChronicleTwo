# Chronicle-Two (Dark Chronicle PAL) native PC port: handoff

Written 2026-10-11. Highest rule from the user: never run out of usage without a handoff file like this one. Keep it updated.

## Hard rules from the user
- NO AI attribution anywhere: no Co-Authored-By, no "Generated with", no tool names in commits, PRs, comments, docs.
- Do NOT author new `ps2/` changes (no container here, so the PS2 byte-identical check cannot run). Everything goes through `port/`, CMake, scripts. Retail files now equal imported upstream `1553ee88`; retail byte matching is unverified.
- Loud stubs only (`PS2_UNIMPLEMENTED()`), no PS2 emulation, no memory overlays, no hardcoded resolution/framerate.
- Comments: terse, only non-obvious WHY.
- The user is cost-conscious: no open-ended agent debugging. Use small env-gated logs and ask for ONE run per hypothesis.
- Usage policy (2026-10-11): user reports about 30% consumed and authorizes choosing model/reasoning per workload. Prefer inexpensive low/medium reasoning for narrow edits and fixtures; reserve stronger reasoning for ambiguous memory/rendering defects and integration. Delegate only independent, bounded deliverables with minimal context; at most two repair attempts, then report a blocker. Avoid duplicate builds, repeated broad reviews and idle polling. Existing 10â€“15 minute checkpoints remain; they are work limits, not token caps.
- Concurrency policy: while independent packages run, continue useful integration, sibling-site review, fixtures, enforcement or canonical documentation. Wait only when remaining work depends on outstanding results. Keep ownership disjoint and build centrally; concurrency does not relax spending limits.
- **Do not auto-drive the game.** The user controls it. Launch it detached (recipe below), then give the user a short numbered checklist (language, New Game, costume, ...) and read the logs after. Do not invent input scripts to fake menu navigation.
- Update `PORT_STATE.md` + this file on every edit (see AGENTS.md) â€” including intermediate diagnostics, not just the final fix.

## Repos, branches, machines
- Playtest checkpoint preparation at user request: publish current native/upstream work on
  port/upstream-1553ee88-playtest-2026-10-11 to Jeffiest/ChronicleTwo, then launch visibly.
  Fresh game/dc2data build passes with unchanged executable hash; publication and launch pending.
  Next twelve assignments remain queued. Archive root documents/task package in
  docs/checkpoints/2026-10-11-upstream1553; archived documents are preparation snapshots.
- Upstream 1553ee88 integration is validated locally; seventeen commits since 4ee90030 and
  clean required Kazarov a7674f4. Native ownership fixes remain. Game/GTest builds and headless
  completion pass; CTest 351 passed, five skipped, zero failed / 356, including fourteen isolated
  GS reference cases that do not prove native rendering. Source ownership: 69 native / 420
  override / 230 collector units; zero leaks among 37 exclusions. Long64: 91 passed.
  Receipt: win/ChronicleTwo/docs/port/UPSTREAM_SYNC_1553ee88.md. Executable SHA256
  788599B315A105B02C0595CBAE6734223AADE2B2263D8AB426F5C51D30CD2E6C.
  Branch/HEAD remain port/upstream-first-2026-10-11 / c2ac1abb; dirty/untracked work is required.
  No commit/push/master rewrite or gameplay launch in this integration. Initial backup remains
  E:/Chronicle-Two/logs/upstream-1553ee88-backup. Runtime acceptance remains unresolved.
- NEXT_12_TASKS is the requested queued package: Claude 01-04, Gemini 05-08, Codex 09-12.
  Give the external providers CLAUDE_HANDOFF.md and GEMINI_HANDOFF.md; ALL_12_HANDOFF.md
  combines all rules/briefs. Twenty-one Markdown files / twelve tasks, no dispatch yet.
  Fifteen minutes or twelve calls per worker, two repair attempts; no worker children.
  Task 12 exclusively coordinates edit freeze, shared builds/registrations and canonical state.
- dc2data verified 8,762 private extracted PAL files under win/ChronicleTwo/data, including
  8,751 DATA files. Real index/edit.cfg tests pass. The folder is ignored; never publish assets.
  SOUND reader supports raw and extracted sound layouts. Raw source disc remains untouched.
- Latest acceptance failed: user reports no visible progress. PID 15224 is no longer present;
  `logs/completion-run-20261011-185022-err.log` ends at `mg_drawprim.cpp:329` in StartImageTransfer,
  where CopyTexture returned false after texture lookup/binding succeeded. Rectangles/extents are
  absent from the log; do not claim lookup failure or guess the rejected-copy cause. This shared
  menu-copy path was not exercised by the new component tests. A/Cross reaches combat and sets
  pad_history, but `prog_no=-1` means ACTION_PROG_RUNNING and does not alone diagnose a stall.
  Stale texture 0x10005 warnings and one unnamed DATA.DAT failed extent remain unresolved.
  Component/build results below remain valid; user-visible repair is not accepted. Scope the next
  work around actual production menu-copy/action execution fixtures, not another twelve-part sweep.
  No agents, source edits or new launch during this diagnosis.
- Previous user-controlled run (exited): incremental darkchronicle build and normal gates pass;
  `logs/completion-interactive-build.log`. PID 15224 launched visibly with desktop access,
  `DC2_CRASH_TRACE=1`, `DC2_TRACE_COMBAT=1`, no frame limit/input script. Desktop window 56822766
  was independently captured on Select Costume; RTX 4070 Ti has all 504 pipelines ready.
  Logs `E:/Chronicle-Two/logs/completion-run-20261011-185022-{out,err}.log`. User is testing
  Items/A attacks/HUD/audio. No additional agents dispatched; use runtime evidence to scope the
  next batch. Earlier no-launch entries below describe the completed implementation batch.
- All twelve authorized bounded completion assignments returned and are integrated locally;
  at most three ran concurrently under the four-slot session ceiling. Receipt and remaining work:
  `win/ChronicleTwo/docs/port/EXECUTION_BATCH_2026-10-11.md`. Prior review-pending text is historical.
  Branch remains `port/upstream-first-2026-10-11`, based on published `c2ac1abb`; current batch is
  uncommitted. No game launch, push, master rewrite or retail edits occurred in this batch.
- Shared model repair: checked aligned menu partitions with retail capacities; typed required
  allocations before writes; native motion draws retain CPU indices/weights and omit unused VIF
  packets. Synthetic reload/scratch/reset and exhaustion controls pass. Thirty PAL MDT geometry
  records load in the unchanged 39,616-block slot; real-asset motion fixture uses empty weights,
  so full character/Items acceptance remains pending. Texture allocation now retries after failure,
  but the reported HUD issue is not proved fixed. Passive `DC2_TRACE_COMBAT` hooks record changed
  logical input/gates/program/history around real dispatch (256 records); A/Cross binding retained.
- Production SOUND reader decodes all 3,101 indexed names (481,470,146 PCM samples). Dedicated
  two-bus PCM transport replaces fake stream timers; two mixer cases pass. Reader/mixer fixtures
  are separate from complete CSound/event acceptance; Monica's missing line remains unverified.
  PSS demux fixture passes but no codec development libraries are present, and movie.cpp still
  uses timing-only behavior. See `docs/port/MEDIA_CONTRACT_COVERAGE.md` for precise boundaries.
- Validation: Windows game/GTest, seven new component executables, full/headless 26/25 compatibility
  cases, eight Python audit/gate fixtures and two ownership controls pass. CTest: 61 passed,
  five prerequisite skips, zero failed / 66 registered. CI now requests completion aggregates;
  hosted Linux/macOS/Windows results pending. Normal builds reject superseded-source leaks;
  manifest 68 native units / 413 overrides / 229 collector units, zero leaks among 37 exclusions.
  Five selected game test files have 25 declared cases. Inventory 5,376 findings / seventeen repairs.
  Compiler-assisted pointer analysis, linker resolution and runtime reachability remain incomplete.
  All 333 retail files and docs/PC.md are exact upstream copies; original names retained.
- Evidence: `E:/Chronicle-Two/logs/completion-{validation-final,headless,ctest,model-pal,model-negative,sound-pal}.log`.
  Executable SHA256 `81F1F783B93A73095B9637E478464E4212CC5F3197065B28033CD2EB3978D516`.
  Build/check: `cmake --build E:/Chronicle-Two/win/build-rel --target darkchronicle darkchronicle_tests ct_completion_tests -j12`;
  use `ct_completion_tests_headless` without Vulkan capture. Next acceptance is one user-controlled
  Items/attack/HUD/audio run with `DC2_CRASH_TRACE=1` and `DC2_TRACE_COMBAT=1`, launched visibly
  through desktop access. No process was launched in this batch. Full save serialization,
  thread/semaphore timing, rendering equivalence and georama/fishing/ridepod/map acceptance remain open.
- Latest interactive run of `c2ac1abb`: PID 59804 launched Dark Chronicle visibly, then crashed;
  DC2_CRASH_TRACE=1, no frame limit or input script. Logs
  `E:/Chronicle-Two/logs/upstream-first-run-20261011-175430-{out,err}.log`.
  All 504 pipelines initialized on RTX 4070 Ti. Symbolized fault RVA 0x2282fa is
  mgCVisualMDT::CreateFace, through DataAssignMotionMDT and CMenuItemInfo::ModelReadEndCheck.
  Arena request 37 at used 39603 / capacity 39616 reproduces the existing Items model-storage crash.
  Process exit was verified; no game remains from this launch. Next work is the shared model-storage
  ownership/writer contract already identified below, not another unmodified run.
- Naming correction: preserve upstream general names. `docs/PC.md`, `port/src/identity.cpp`, the test
  entry point, root CMake and ignore file are exact upstream copies. Use `CT_TEST_TIMEOUT`,
  `CT_BUILD_TESTS`, `ct_*` targets, `chronicletwo` app ID/folder and `DC2_*` runtime variables.
  Current default saves/config are `%LOCALAPPDATA%\chronicletwo\save`; earlier user files are not moved.
  Build and 25 headless compatibility cases pass; CTest 56 passed / five prerequisite skips / zero failed,
  using CT_TEST_TIMEOUT=60. Logs naming-{configure,build,ctest}.log. Earlier run records retain historical names.
  Published correction `c2ac1abb662db9a7c61ea3fe28fd3d83cbc69613` on the same redo branch;
  remote verified, working tree clean. CTest JSON verifies all 61 timeouts are 60 seconds.
- Upstream-first redo is validated on `port/upstream-first-2026-10-11`, based on fork master `117d29c8`.
  All 333 retail files match upstream `4ee90030`; all 24 omitted upstream files are restored. Upstream
  ct_* build framework, runtime helpers and quad forwarders are adopted; native extensions and source/SDK
  ownership are explicit. No duplicate-symbol suppression. Generated mirrors hold host adaptations.
  Old `d2cc2df2` branch remains recovery. Windows game/GTest, 26 full and 25 headless compatibility cases,
  eight Python fixtures, CTest (56 passed / five prerequisite skips / zero failed), and PAL 294-name loader
  pass. Inventory 5,334 findings / seventeen reviewed dispositions. Evidence logs redo-{build,compatibility,ctest,local-pal}.log;
  audit `win/ChronicleTwo/docs/port/UPSTREAM_SYNC.md`. Published commit
  `1ded020925fc02b88c6d28727ac2e1f0a13b8d58`; fork branch hash verified equal, working tree clean,
  zero upstream files deleted. Fork master remains `117d29c8`; upstream remains `4ee90030`.
  Continue from this redo branch. Master and gameplay untouched.
- Upstream integration authorized 2026-10-11: fresh `upstream/master` is `4ee90030` (five commits
  beyond shared base `872e1250`; local branch has 401). Checkpoint current host compatibility work,
  then merge on `port/master-sync-2026-10-11`, preserving working native implementations and checked
  generated-source transformations. Resolve only reviewed conflicts; import upstream retail corrections
  without inventing new `ps2/` changes. Central game and compatibility builds precede publication.
  The earlier fork-master divergence is superseded by the user's update: fork master is now
  `117d29c8cbb2e3b78cb627757d31add3986338a0` (PR #7 merge), one commit beyond `d2cc2df2`
  with an identical source tree. Fresh fetch, ancestor check and empty tree diff verify it.
  Compatibility checkpoint is `563cb177`; active branch is `port/master-sync-2026-10-11`.
  Imported upstream SDK pin `ce459c1`, configuration routes/CI and identity helper while preserving
  current runtime, save directory, checked transforms and scheduler. GTest executable is now
  `darkchronicle_tests`; `dc_compatibility_tests_headless` skips only the Vulkan capture case.
  Windows RelWithDebInfo game, both compatibility suites (26/25 cases), actual PAL 294-name loader and
  CTest (56 passed, five prerequisites skipped, zero failed) pass. SDK CreateThread has an isolated COFF
  symbol; mirrored headers declare byproducts so consumers rebuild in the same invocation. Source inventory
  and generated pointer checks pass. Logs `master-sync-{build,ctest,local-pal}.log`; conflict audit
  `win/ChronicleTwo/docs/port/UPSTREAM_SYNC.md`. Merge `bb0eaf49` published on
  `github-fork/port/master-sync-2026-10-11`; remote hash and both parents verified. Linux/macOS, hosted CI,
  retail matching and gameplay after merge remain unverified. No game launched.
  Final documentation tip `d2cc2df2f2f52e136eb64af50c8b1a1f6db05b43` matches the remote; tree and
  submodule are clean. Continue work from the integration branch, not the older compatibility branch.
  The user completed the master update; no further master push is needed. Existing build/test evidence
  covers the identical tree; gameplay has not been retested after integration.
- Compatibility foundation: **port/host-compatibility**, checkpoint `563cb177` based on `132ba152`. Changes are
  committed and included in the integration branch: all script builders' typed storage, arena safeguards, source inventory gate, and a native
  `LoadMonsterFile(int,int)` override replacing PS2 byte offset 4 / stride 0x30 with typed monster arenas.
  RelWithDebInfo game build and `dc_compatibility_tests` pass. Foundation left `ps2/` untouched; upstream merge exception is above.
  The user reached controllable Monica. Input/menu lifecycle, shared color readback, corrected menu capture,
  arena-owner diagnostics and common menu filename loading are now implemented with focused fixtures.
  RelWithDebInfo game and 26 native cases pass (`logs/pointer-storage-build.log`). PID 56068 exited at
  `CMenuOption::UpdateOptionForm`, `ps2/src/menuop.cpp:1621`, four-byte pointer stepping; this is now repaired
  through checked generated-host rewriting, with a fixture of actual generated method bodies. User verifies
  Options and dungeon menu opening, plus battle movement/lock-on. A/Cross attacks fail; Items crashes after
  shared menu model storage exhausts (39603/39616, request 37, then CreateFace null dereference). PID 12768
  has exited, logs `menu-option-{out,err}.log`. No new launch; fix the common model-owner/write contract first.
  Earlier PID 9112 failed in common filename traversal; that shared loader now uses typed indexing and reserves
  memory before reads. Both traces preserved in `logs/{input-capture,menu-loader}-err.log`.
  Photo/effect readback while recording remains explicitly unsupported; depth remains separate.
  Combat, skip cleanup and map reentry remain unverified. Do not auto-drive the game.
  Run native/source checks: `cmake --build E:/Chronicle-Two/win/build-rel --target ct_compatibility_tests`.
  Audit documentation: `win/ChronicleTwo/docs/port/COMPATIBILITY_AUDIT.md`; 5336 lexical findings, seventeen repaired
  sites, other findings open. Normal builds also reject the reviewed byte-stride pointer-array pattern in
  actual generated/native game code; this is a narrow enforcement gate, not full compiler pointer analysis.
  Limits: optional null allocation remains supported; the single-run tracker cannot prove older placements;
  no guard pages or complete lifetime registry yet. Compiler-based pointer analysis remains future work.
- Current Windows checkpoint branch: **port/checkpoint-2026-10-11-cutscene**, based on `3139f445f`, with the
  three latest host fixes and `docs/checkpoints/2026-10-11/` snapshots of review/state/handoff and regression files.
  Remote **github-fork** is `https://github.com/Jeffiest/ChronicleTwo.git`; `origin` remains the WSL repository.
  Root `PORT_STATE.md` and this file remain canonical; archived copies capture checkpoint preparation.
  Regression rerun passes, including the expected failing original-code negative control. Battle remains broken.
  Pushed commit **132ba1529d68c9414af31714b8cedf1cf8402ada**; remote branch hash verified equal, working tree clean,
  tracking `github-fork/port/checkpoint-2026-10-11-cutscene`. Push succeeded using Windows Git.
- Main repo (WSL Ubuntu): `/home/jeffie/ctwork`. Worktree used for all work: `/home/jeffie/ct-port-up`, branch **port-up-sync** (our port merged onto upstream ChronicleTwo master + Kazarov shared host code + PORT_OVERRIDE strips + Windows scripts). Latest commit at handoff: **3139f445f** (check `git log`). Unpushed commits since the last fork push: `c8f8d4163` (hand fix), `3139f445f` (crash trace + character stacks).
- Other worktrees (old, can ignore): ct-port, ct-up-kz, ct-up-win, ct-up-ovr, ct-up-winport, ct-up-vu0, ct-up-title, ct-dc1-*.
- Pushed on the user's fork https://github.com/Jeffiest/ChronicleTwo : port/crew, port/dc1-sync (older). The user pushes port-up-sync themselves from the Windows clone as `port/upstream-sync`:
  `git -c safe.directory=* -C E:\Chronicle-Two\win\ChronicleTwo push https://github.com/Jeffiest/ChronicleTwo.git HEAD:refs/heads/port/upstream-sync`
  (WSL git cannot push it: the token lacks the `workflow` scope.)
- History of port-up-sync had AI trailers stripped (backup tag `backup/port-up-sync-pre-clean`).
- Windows native clone: `E:\Chronicle-Two\win\ChronicleTwo`. Sync from WSL after each commit:
  `git -c safe.directory=* -C E:\Chronicle-Two\win\ChronicleTwo fetch -q origin '+refs/heads/port-up-sync:refs/remotes/origin/port-up-sync'` then `merge --ff-only origin/port-up-sync`.
- Dev toolchain on Windows: deps under `E:\Chronicle-Two\win\deps` (llvm-mingw, SDL3, Ninja, glslang, Vulkan headers).
- Build dirs: `E:\Chronicle-Two\win\build` (Debug, slow) and **`E:\Chronicle-Two\win\build-rel`** (RelWithDebInfo, use this). Build: `& E:\Chronicle-Two\win\ChronicleTwo\tools\windows\build.ps1 -BuildDirectory E:\Chronicle-Two\win\build-rel` (about 1.5 min incremental). Configure once: `configure.ps1 -BuildDirectory <dir> -BuildType RelWithDebInfo`.
- Linux build (compile check only, WSL): in `/home/jeffie/ct-port-up`: `ninja -C port/build/pc darkchronicle -j4` (configured with clang++-20, `-DCMAKE_PREFIX_PATH=/home/jeffie/chronicle-deps`).
- Run (Windows), interactive (stays open): launch **detached** from a shell with desktop access so there is no frame budget. Restricted-shell launches created live processes that the user could not see; do not count process existence as visible launch verification.
  `Start-Process -FilePath E:\Chronicle-Two\win\build-rel\darkchronicle.exe -ArgumentList '--data','E:\Chronicle-Two\win\ChronicleTwo\data','--width','1280','--height','720' -WorkingDirectory E:\Chronicle-Two\win\build-rel -RedirectStandardError E:\Chronicle-Two\logs\play-err.log -RedirectStandardOutput E:\Chronicle-Two\logs\play-out.log`.
  `--frames N` and `--screenshot PATH` run a fixed budget and EXIT (that is why a scripted run closes on you; useful for automated checks, not for playing). `--input FILE` / `DC2_INPUT` drives pad 1 from a script of `frame [pad1|pad2] button... [lx ly rx ry]` lines; `key:<name>`/`mouse:dx,dy` drive devices. Keyboard works directly too (Enter = confirm, arrows = move).
- First-time flow (keyboard): boot â†’ **Language Select** (Cross/Enter picks the highlighted row; arrows move) â†’ memory-card check â†’ copyright â†’ title **PUSH START** (Start/Enter) â†’ title menu â†’ **New Game** (Cross/Enter, it is the default row) â†’ **Select Costume** (L/R turn, U/D zoom). No config/save needed.
- Config/saves: `%LOCALAPPDATA%\chronicletwo\save\config.json` (own dir; the DC1 port uses `\chronicle`, keep them separate). Delete `?` none needed; first boot shows Language Select.
- WSL shell tip: from PowerShell write a script file under `C:\Users\armor\AppData\Local\Temp\claude\E--Chronicle-Two\...\scratchpad\x.sh` and run `wsl -d Ubuntu -- bash /mnt/c/.../x.sh`; inline quoting breaks.
- Debugger: `E:\Chronicle-Two\win\deps\llvm\llvm-mingw-20260922-ucrt-x86_64\bin\lldb.exe` (helper `E:\Chronicle-Two\win\run-debug.ps1`).
- Frame extraction from recordings: ffmpeg is installed (WinGet). Window capture helper used: PowerShell + CopyFromScreen.

## What works now
Boots, 504 Vulkan pipelines, language select, title, New Game, and the **Select Costume screen renders Max correctly** (body, head, hat, bag, shoes, amber outline, blinking, **both hands correct â€” left hand fixed, see below**). New Game gets past the costume screen, loads the first dungeon (`map/d/d01`, `d01_1.stb`, `RUN SYS_SCRIPT 1010`), and renders the knight/corridor prologue after the HUD sprite fix. User verified the entire cutscene completes, with some remaining bugs, then controllable Monica, battle movement/lock-on, Options and dungeon menu opening. A/Cross attacks fail and Items crashes in shared model loading. Full combat, skip cleanup and map reentry remain unverified. Next work follows the shared model-memory contract below. Root causes already fixed (in the host layer):
1. CreateFace index arrays and skin weights lived in scratch arenas reused by later loads: kept in host memory (`visualmotion.cpp`).
2. Skinned normals broken: base-pose normals by default (`DC_SKIN_NORMALS=1` restores) (`dynamicanime.cpp`).
3. Bone palette must be indexed by position in the bone list, not frame number (`dynamicanime.cpp` BuildSkinMatrices).
4. `mgSetPkFrameBuffer` never redirected to the off-screen texture used by the outline pass (`mglib.cpp`).
5. ScreenXScale used the main 640 logical width even for a 512-wide target (`mg_drawenv.cpp`).
6. The off-screen target's own depth was never cleared: cleared once per frame (`mglib.cpp`).
7. Also: read-queue pending semantics (`dataread.cpp`), per-visual private vertex arrays (`dynamicanime.cpp`), base-pose reset on model reload, VU0 routines implemented in `vu0_gaps.cpp`, own app dir.
8. Hand/ankle bones' frame numbers (41..78) rejected by a `>= 32` bound in `BuildSkinMatrices` (`dynamicanime.cpp`) â€” commit `c8f8d4163`.
9. Character stacks sized in PS2 blocks overflow on host struct widths â€” `PORT_OVERRIDE` doubling in `port/src/character.cpp` â€” commit `3139f445f`.

## RESOLVED (2026-10-11): Max's LEFT HAND pointed inward; ankle rect clipped
Root cause: `BuildSkinMatrices` (`port/src/dynamicanime.cpp`) skipped any bone whose **frame number** was `>= 32`.
`visual->bone[]` stores frame numbers, and the hands/ankles are frames 41..78, so all their skin matrices were left as the
initialized identity and the hand never deformed â€” it kept its inward bind pose. Retail's `CreateExtRenderInfoPacket`
(`ps2/src/visualmotion.cpp:738`) null-checks `frame[slot]` with no upper bound; 32 caps the *bone count*, not the frame
numbers the bones name. Fix (commit `c8f8d4163` on `port-up-sync`): bound by `visual->frame[0]->frame_num`. Verified
in-game: both hands point outward on Select Costume. Still open: drop `DC_WIDE_DRAWRECT` once `GetDrawRect` is fixed
(the rect still clips the hand/ankle; the widen hack is only a workaround).
Historical evidence (kept for reference):
- Reference model data: `E:\DC2_Recom-Jeff\MyUnityProject\Assets\Resources\vg_resource_full\extracted\Max Outfit 1\c01fuku1\c01f.dae` (+ `c01f_anim.dae`). R_te node: position (-9.951, 21.368, -0.3195), rot (0.34deg,0,0). L_te node: position (9.951, 21.368, -0.3195), rotation (-179.66deg, 0, -180deg) = 180 deg about Y. L_te mesh is a z-mirrored copy of R_te (distinct 163-vertex geometry). Hands are 6.3x2.98x3.62; ankles R_asi/L_asi are 8-vertex rigid meshes at (-1.7/+1.7, 8.1, -0.4). Scripts: `E:\Chronicle-Two\workers\dc1sync\scratch-tex\daeinfo.py`, `daejoints.py`, `handcmp.py`.
- In the game the hand frames are at the right positions (frame world pos rel. to root (15,-14,4): +/-9.9, 21.4) but BOTH hand frames have ~identity rotation (only the root's 5.7deg yaw), `use_srt 0`, matrices straight from `object->matrix` (ps2 `CreateFrameVisual`: `frame->SetTransMatrix(object->matrix)`). Left hand draws with identity so its mesh (local x -5.6..0.7) extends INWARD.
- The two hand visuals have distinct vertex arrays (115 verts each, kind 3 = MotionMDT, weights non-null), but **only the 347-vertex body visual ever shows up in the skin diagnostics: the hands are probably never skinned** (so the 180 deg that retail gets through the VU1 bone matrices (current pose vs bind) never happens). Last step taken: added `skinwhy` log (once per motion visual: vertex/frame/base_matrix/weight/weight_num/slots) in `SkinMotionVisual` to find which early check rejects them. READ `E:\Chronicle-Two\logs\rel-err.log` lines starting `skinwhy`.
- **UPDATE (latest log, supersedes the guess above):** the `skinwhy` log shows the two 115-vertex hand visuals (7 slots each, weights non-null, frame table and base_matrix non-null) DO enter `SkinMotionVisual` and pass every early check, so they ARE skinned. My earlier "only the body is skinned" came from log aliasing (the skin diagnostic samples every 15th call and 5 visuals are skinned per tick, so it always hit the same one). Visuals skinned per tick: 347 (body), 172, 462, 115 (R hand), 115 (L hand). So the left hand's skin matrices come out near identity: current bone-frame LW and `base_matrix` (bind) for the left-hand bones agree, so no outward 180 deg is produced. NEXT: in `SkinMotionVisual` for the two 115-vertex visuals print, per slot 0..6, the rotation (3x3 diagonal) of `visual->base_matrix[bone[i]]`, of `visual->frame[bone[i]]->GetLWMatrix`, and of `skin_matrix[i]`; fix the sampling by counting per visual. If current LW == bind for the left hand, the pose that gives retail its outward hand must come from the motion/animation that should be setting the bone frames' matrices each tick (check whether the skeleton motion (`MotionProc`/`DeformMesh` path in ps2/src/gameutil.cpp, ChangeMotion in CCharacter2) runs in the costume menu and whether a port override skips it), or the L hand's vertices need the frame's mirror. The DAE (see above) is the ground truth for the final shape: left hand = rot180Y(L_te mesh) placed at (+9.951, 21.368, -0.3195).
- **LAST STEP DONE (commit after a23cd3773, "log each hand bone's bind, current and skin matrices"):** the per-slot log is in and the build-rel game was relaunched with DC_DUMP_DRAWS=1 DC_WIDE_DRAWRECT=1. TO USE IT: go English -> New Game to the costume screen, then read `E:\Chronicle-Two\logs\rel-err.log` for lines `handbone visual ... slot k bone b bind diag [..] t [..] cur diag [..] t [..] skin diag [..] t [..]` (two lines groups of 7 slots, one group per hand visual; the left hand is the visual whose frame world x is about +24.9, the right about +5.1; match with `drawrect`/`visual` lines). Interpretation: if `cur diag` of the left-hand bones is about (-1, 1, -1) and `bind diag` about (1, 1, 1), the skin diag will be (-1, 1, -1) and the hand should already be rotated, so the bug is elsewhere (draw/composite); if `cur` and `bind` match (both rotated or both identity), the missing rotation is in how the skeleton pose is driven (the motion that should set the left hand bones is not applied or applied without the turn): inspect `CCharacter2` motion setup in the costume menu (`ps2/src/menuchr.cpp`, `ps2/src/character.cpp` ChangeMotion/MotionProc paths) and the port overrides in `port/src/gameutil_motion.cpp` / `dynamicanime.cpp`.
- Retail skin matrix (ps2/src/visualmotion.cpp CreateExtRenderInfoPacket ~line 684) = inverse(matrix) * frame[slot]->GetLWMatrix * inverse(root_local * root_matrix * base_matrix[slot]); port equivalent `BuildSkinMatrices` in `port/src/dynamicanime.cpp`.
- The VU1 notes (`docs/vu1/prog0.md`) say the microprogram supports a "mirror word" (negative-determinant model matrices flip the culling facing).
- Rect issue: the retail outline composite rectangle (from `mgCFrame::GetDrawRect`, my implementation in `port/src/vu0_gaps.cpp`) cuts off the hands. `DC_WIDE_DRAWRECT=1` (grows rects by 60 px) makes the hands appear; the proper fix is to compute the rect correctly (hand rect is 62x90 px near the wrist and does not contain the hand). Remove the widen hack once fixed.
- Next steps: (1) read `skinwhy` lines, make the hand visuals skin (check `visual->frame`, `base_matrix`, `slot_count`, whether `PortMgSkinMotionTree` reaches the hand frames: they may not be linked as children of the root's tree); (2) verify the left hand then points outward and is a proper left hand; (3) fix the GetDrawRect box for hand/ankle frames (probably `bound->corner` is stale for deformed frames; retail calls `RemakeBBox` in `SetDeformMesh`), then drop `DC_WIDE_DRAWRECT`; (4) the ankle (R_asi/L_asi rigid 8-vertex meshes) probably has the same rect problem.

## RESOLVED (2026-10-11): New Game crashed loading Monica's skin (a stack overflow, not a movie)
`GetCharaMemAllocPtr` sized the skin stack `stacks[1]` at **40000** PS2 16-byte blocks (`ps2/src/charasetup.cpp:52`,
handed to `LoadSkin` at `charasetup.cpp:248`), but the host's `mgCFrame`/`mgCFace` are wider than the PS2's 0x110-byte
frame and 3-block face. Monica's skin overran it (`stack over 40025/40000`, then `40001/40000`), `mgCMemory::Alloc`
returned NULL (`ps2/src/mg_memory.cpp:218`) and `CreateFace` dereferenced it (`ps2/src/mg_visual.cpp:485`, 0xC0000005).
Commit **3139f445f** adds `PORT_OVERRIDE` `GetCharaMemAllocPtr` + `GetCharaMemAllocSize` in `port/src/character.cpp` with
every table size doubled (pool `BuffCharacter` ~2.3 MB â†’ ~4.6 MB, main buffer holds it). After the fix the in-game
`[N]STACK used/capacity` print (`ps2/src/charasetup.cpp:370`) reads: 0) 61772/137000, 1) 40356/80000, 2) 2197/14000,
3) 1359/9300, 4) 1794/6000, 5) 10233/19800, 6) 4095/9000 â€” so there is headroom now.

## Latest implementation (2026-10-11): input, capture, arena diagnostics and shared menu loading

Three bounded work packages implemented input lifecycle, shared color capture and arena diagnostics; a
short follow-up repaired common menu filename loading. Controller fixtures cover Xbox Y/Triangle, real
pad edge transitions, menu Escape behavior, stick thresholds, disconnect/reconnect and Japanese/English
confirm/cancel tables. `MenuModeOn/Off` now notify the host input layer.

`mgStoreImage` reads a completed canonical frame or explicit texture through bounded native color conversion.
`MenuCapture` ends recording before readback, reserves the full padded output, averages the visible half-size
image, zeroes padding and reopens recording. Real Vulkan fixtures cover resize, target selection, orientation,
16/24/32-bit formats, alpha, padding and actual menu-capture storage. Photo/effect recording-time capture and
depth readback remain unresolved; this is partial shared coverage, not whole-graphics completion.

Arena sidecar diagnostics preserve retail optional-null/reserved-tail semantics. They track bounded owner
records, high-water usage, generations/resets/rebinds, first failure and binding PCs with executable base.
The 583/584 arena resolves to `LoadFilePictureName` in `inventmn.cpp`; local PAL `neta2.lst` contains 294
`PIC_NAME_INFO` records, 16 bytes on host versus 8 retail. A persistent host backing vector now adds the
derived 147 quadwords to the retail budget; actual PAL load and all 294 lookups pass. Required record/string
failures are explicit, with bounded count/arithmetic checks. This does not repair active photo capture.

Title Options revealed `MenuCommonReadData` indexing `char **names` in four-byte steps. Its native override
uses typed indexing, validates path/mode/size, reserves actual allocator storage before reads and keeps
per-file 64-byte alignment. Four fixtures include actual allocator boundaries and synchronous background
read contract stubs; they do not prove real filesystem backend behavior. Build/tests pass. User's next run
progressed into `CMenuOption::UpdateOptionForm` then crashed at the same pattern in minimap button selection.
`scripts/port/host_menu_pointers.py` now rewrites that exact generated-host site, removes the byte offset,
preserves its private guard and fails on source drift. The Options fixture compiles method bodies extracted
from the production generated unit and passes all choices/conditions/language/guard checks. No production
test bridge is added. Expanded lexical scan found seven byte-stride pointer-array sites: five now have repair
evidence (including earlier character/scene overrides). Both `CVillagerMngr::GetAppearVlgr` output paths now
use typed arrays; primary/fallback/day/night/progress/canary fixture passes. All seven detected sites have
reviewed repairs. A separate build gate checks the generated/native host trees and refuses this lexical
pattern even if the inventory is refreshed; four negative/positive/missing-tree fixtures cover it.

Logs: `input-capture-build.log`, `capture-component-{build,test}.log`, `menu-loader-build.log`,
`input-capture-{out,err}.log`, `menu-loader-{out,err}.log`, `menu-option-build.log`. The first two instances
exited; PID 12768 also exited after Items. User screenshot verifies Options and reports dungeon menu opens.
Latest executable SHA256 `D71B5C02B33EEA77FD6BE9244C592265BC0B0A9962D627CBE6F25D1071CAB765`.
All changes remain local/uncommitted; `ps2/` and Kazarov are unchanged. `PORT_STATE.md` contains the session
record. Photo storage now uses a persistent host vector sized retail 584 plus the checked runtime-record
delta (731 blocks for 294 records), with count/arithmetic/required-failure checks. Full PAL load and all
294 name lookups pass in a component fixture; no assets are copied or gameplay driven. Photo capture remains
separate. Logs `pointer-storage-build.log`, `photo-names-local-pal{,-build}.log`; 26 default native cases,
eight audit/gate fixtures and script/menu/photo rewrite checks pass.

Next shared work is `win/ChronicleTwo/docs/port/MODEL_MEMORY_CONTRACT.md`: model partition ownership,
complete parent/child sizing and packet reservation before emission. DC1's `arena.hpp`/`dataset.cpp` provide
owned guarded backing, fourfold headroom, high-address checks and diagnostics. Preserve DC2 borrowed-view
aliases; do not replace every buffer binding blindly. Actual ABI probes: frame 304/272 host/retail, motion
visual 320/272, face 48/48, face group 56/32 (existing four-block group reservation fits). Earlier wider-face
explanations were inaccurate. Items remains unfixed; do not request another Items gameplay run yet.

Attack: user used Xbox A, the correct PAL English Cross action. Real pad/action press/hold/release fixture
passes; actual game combat state remains unknown. DC1 pins shared Kazarov `ce459c1`; its Xbox mapping and
input polling files are identical to this checkout's shared code. First-person mode gates combat scripts,
but reported movement/lock-on does not establish that it caused this failure. No speculative mapping edit.

## Earlier review (2026-10-11): controllable Monica; shared subsystem plan

Plan: `E:\Chronicle-Two\reports\SYSTEMATIC_PORT_PLAN_2026-10-11.md`. This review changed documentation only;
the prior foundation implementation remains local/uncommitted. Xbox Y -> Triangle -> menu mapping is correct.
Source traces the menu route through `MenuInternInit` / `MenuCapture` / `mgStoreImage`; the log ends at
`port/src/sce/libgraph.cpp:25` (`sceGsSetDefStoreImage`, explicit abort). The paired execution call is also a stub.
Implement shared color capture with explicit frame/target/pixel/buffer contracts and review menus, photos and
effects together. Kazarov readback rejects active frames/recording; retail menu capture also uses unreserved
scratch tail storage. A direct call substitution is insufficient. Depth readback needs its own disposition.

First implementation wave: input/menu lifecycle fixtures and missing `InputSetMenuNavigation` integration;
shared capture and its callers; named arena ownership/first-failure diagnostics. Exactly 46 allocation failures
in the latest log share an unnamed arena at 583/584 blocks; owner/cause unknown. Preserve the reserved tail.
Then complete typed allocation/lifetime coverage, compiler/link-aware pointer checks, fixed-record decoding
and scene/resource completion fixtures. The lexical inventory is not a compatibility certificate.
Use bounded work packages (10â€“15 minute checkpoints, at most two repair attempts, blocker report after two
minutes), central integration build, then a short user-controlled menu/open/navigate/close/move check.
Do not request a full playthrough for each edit. See the plan for gates and later milestones.

The supplied `C:\Users\armor\Downloads\SOUND_DAT.md` was checked against local PAL headers: all 3,101 entries
are mono 48 kHz 16-bit PCM RIFF/WAVE, with variable chunk ordering and one duplicate name. The old claim below
that PCM was likely wrong is superseded. Current stream code is silent and reports end after 1.5 seconds;
replace the shared stream state/backend after controllability work. The exact missing Monica voice request
is not identified. No audio playback or game input was performed during this review.

## Earlier continuation (2026-10-11): prologue renders; skip cleanup crash
User confirms the complete cutscene now runs, with some remaining bugs, but battle does not start. Logs
`spline-fix-out.log` show `stack over 38/0`, `64/0`, `384/0`; trace enters `SetMonsterScript` (`runscript_opcodes.cpp:4682`)
via `CMonsterMan::SetActiveMonster` -> `AutoSetMonster` -> event script. User requested broader reevaluation of
latest upstream master, Dylan's fork and DC1's port before continuing individual crash fixes.
Completed review: `E:\Chronicle-Two\reports\PORT_REEVALUATION_2026-10-11.md`.
Fetched review-only refs in Windows clone: `review-upstream/master`=4ee90030, `review-dc1/master`=706ce00e,
`review-dylan/port-bootstrap`=56555a37 / master=6a24d4f2; Kazarov `review-upstream/main`=ce459c1.
No merges/checkouts/game-code updates. Initial parent fetch submodule warnings were followed by a successful
direct Kazarov fetch and clean parent refresh with recursion disabled. Preserve the three checkpoint fixes.

**Historical foundation plan (first batch implemented below).** Begin with host-sized script arrays and
scene/monster arena initialization, reset, ownership and lifetime checks. Local placement-new growth covers
only the latest run, misses raw cast arrays and can continue after overflow. Event and monster stacks both
allocate using PS2 widths. Latest battle trace reaches `SetMonsterScript` through `SetActiveMonster` and
`AutoSetMonster` after `stack over 38/0`, `64/0`, `384/0`; why the arena has zero capacity is not yet proven.
Trace the normal `LoadMonsterFile` buffer setup across the event transition before choosing a repair.
Then adapt Dylan's layout/hardware inventory and DC1's pointer audit, guarded arenas and record boundaries.
Dylan still has the same script-stack literals and PS2 arena budgets, so its audit needs expanded coverage.
Our `CScene::ClearStack` already has a typed override; do not reapply that repair or assume it is the cause.
Keep `ps2/` untouched while the byte check is unavailable. Acceptance gate: valid arenas and non-overlapping
host-sized script arrays, then user-controlled full cutscene, skip, monster spawn/combat and map reentry.
Upstream's five new commits are Phase 1 scaffolding ending at `mgInit`, not a more advanced gameplay port;
review build/CI/source fixes individually. DC1's earlier audio/platform comparison was too narrow to dismiss
its memory architecture. No new playthrough was run during reevaluation.

**First foundation batch now implemented:** All four builders have checked typed host allocations through
`port/include/script_storage.hpp` and generated-host rewriting (`scripts/port/host_script_storage.py`).
Private opcode tables stay in their original units; `ps2/` is unedited. The event-argument builder was already
typed. Native `LoadMonsterFile(int,int)` used a raw PS2 arena stride, whereas the no-argument overload and
event deletion setup already used typed indexing. The new override/helper fixes this concrete defect and
checks required buffer allocation before use. It plausibly explains zero capacities, pending live confirmation.
Allocator diagnostics now identify operation, owner, request, buffer, used/capacity and reason; optional
failure semantics remain. Failed growth of the last tracked placement allocation aborts before construction.
Build and focused tests pass. Next: read the user-controlled run's result, then continue systematic review of
remaining allocation/pointer/record/lifetime findings; a green lexical audit does not certify compatibility.

**Running-scene stall diagnosed in live PID 16916:** `EventScript` loops in `_OBJS_CHECK(0)` (external 450),
pc code+0x2a2c. `ObjectSeq[0]` and `[1]` both have `pas.spline.speed = NaN`, run=1/end=0, still on first segment.
Object 0 now_frame=1.62301981, total requested frame=58, four points with segment lengths 19/19/19/0.
`CCharaPas::Setup` measures length by `C3DSpline::Step`; the final zero-duration marker is evaluated as a cubic,
so host `0/0` poisons the accumulated length and speed. Retail instructions at 0x2589e0..0x258ba0 confirm this
sampling (division at 0x258b3c), but PS2 floating-point arithmetic keeps the constant endpoint finite.
Added `port/src/sceneseq.cpp` host overrides for Step/StepS: sample zero-duration keys from `d` directly,
preserving the stepping/finish control flow. Windows RelWithDebInfo build passed after closing stalled PID 16916;
`git diff --check` clean. Regression command `& E:\Chronicle-Two\workers\spline-regression.ps1` passes (exit 0),
log `logs/spline-regression.log`: captured paths have finite speeds 8.530462/8.097962 and finish in 58 distance
steps at the proper endpoints; camera eye/reference paths finish too. Original host routines reproduce NaN at
frame 57, so its printed FAIL is the expected negative control. Runner compiles unchanged path functions from
source to avoid unrelated scene-command dispatch tables; it links the built spline override and vector helpers.
No live state modified. Relaunched detached **with desktop access**, tracing to `logs/spline-fix-out.log` and
`logs/spline-fix-err.log`; user confirmed the complete cutscene plays, but battle transition fails.
All three fixes are included in the Windows checkpoint branch; WSL worktree has not been synced.

User's separate launch of the rebuilt `win/build-rel/darkchronicle.exe` rendered the knight and Monica's corridor,
then stayed in a running scene; skipping crashed. Windows Application event 1000 at 14:37:54 CDT reports process
`0x20c8`, access violation RVA `0xafcae`. llvm-symbolizer resolves this to `CCharacter2::DeleteExtMotion`,
`ps2/src/character.cpp:1456:38`. Retail accesses image slots via `this + 0x2C4 + j * 4`, invalid on the host.
Added a `PORT_OVERRIDE` in `port/src/character.cpp` retaining retail cleanup and using `images[j]` instead.
Windows RelWithDebInfo build passed (exit 0); override registered and generated retail body stripped;
`git diff --check` clean. Saved Windows crash evidence in `logs/skip-crash-event.log`. The detached instance PID
6528 was still at language selection (debugger attached, inspected, then detached); closed it to release the
executable for relink. Its `logs/hud-fix-{out,err}.log` does not capture the user's prologue run.
User reported the restricted-shell launch was not visible. Closed that idle instance (PID 53952) and relaunched
with desktop access, `DC_CRASH_TRACE=1 DC_WIDE_DRAWRECT=1`, PID 16916, to `logs/skip-fix-desktop-out.log` and
`logs/skip-fix-desktop-err.log`. User confirmed visibility and the running-scene stall; live inspection and spline
fix are described above. This instance was closed for relink and replaced by PID 29348 with `spline-fix` logs.
The spline stall and image cleanup crash are separate defects. Changes are included in the Windows checkpoint
branch, not synced to WSL.

## FIXED, USER VERIFIED (2026-10-11): crash in the in-game HUD
Continuation in Windows clone `E:\Chronicle-Two\win\ChronicleTwo`, branch `upsync-winport`: added host overrides
for `CPreSprite::SetAlphaBlend` and `SetScirror` in `port/src/mg_drawprim.cpp`. These retail helpers directly write
through `write`, but host `BeginDma` opens a CPU vertex run without allocating a DMA packet. The HUD fault is that
packet write. Overrides use the existing `Direct` handler for active ALPHA/SCISSOR state, preserving retail values
and ignored blend modes. Windows RelWithDebInfo build passed (exit 0); override list includes both helpers and
generated retail bodies are stripped; `git diff --check` clean. Detached game launched with `DC_CRASH_TRACE=1`
and existing `DC_WIDE_DRAWRECT=1` workaround; logs `E:\Chronicle-Two\logs\hud-fix-out.log` and `hud-fix-err.log`.
User's separate run reached the rendered prologue, confirming it passed this crash. Changes are local to the Windows
clone; not committed or copied back to WSL. `ps2/` untouched.

Previous crash evidence:
New Game now gets past the costume screen, past the skin load and into the dungeon loop (`map/d/d01`, `d01_1.stb`,
`RUN SYS_SCRIPT 1010`, map `s51.img`), then dies drawing the HUD. Logs: `E:\Chronicle-Two\logs\crash-run-out.log`
(stdout) and `crash-run-err.log` (stderr + trace). Backtrace (`DC_CRASH_TRACE=1`, symbolized with llvm-symbolizer):
```
CPreSprite::SetAlphaBlend(int)      ps2/src/prespr.cpp          <-- fault (null deref, rva 0x246d1b)
DrawActiveItemCursor(int,int,float) ps2/src/dng_status.cpp:196
DrawMainUnitStatusBord(float)       ps2/src/dng_status.cpp
DngMainDraw()                       ps2/src/dng_main.cpp
LoopDungeonMain()                   ps2/src/dng_main.cpp:1035
MainLoop()                          port/src/mainloop.cpp:585
```
Last stdout before it: `texture over 0:s510109` then `5:258`. This message comes from `EnterIMGFile` in
`ps2/src/mg_texture.cpp:1171` when a texture spills to the next VRAM block; it does not establish exhaustion.
`SetAlphaBlend` does not access a texture. The earlier missing-texture hypothesis is superseded by the packet-write
diagnosis above. NEXT: obtain one user-controlled run in the relaunched traced game. If the running scene stalls,
verify the spline correction against the captured paths, build, relaunch with desktop access, and have the user
confirm the corridor scene progresses automatically. Then verify skip cleanup if needed and inspect fresh logs.

## Crash tracing (new, 2026-10-11)
`DC_CRASH_TRACE=1` (`port/src/crash_trace.cpp`) installs a Windows unhandled-exception filter that writes to stderr:
`crash code <code> fault <addr> base <load base> rva <off>` plus `#n <addr> rva <off>` frames. Symbolize each rva with
`E:\Chronicle-Two\win\deps\llvm\llvm-mingw-20260922-ucrt-x86_64\bin\llvm-symbolizer.exe --obj=E:\Chronicle-Two\win\build-rel\darkchronicle.exe --functions=linkage --demangle 0x140000000+<rva>`
(image base 0x140000000). It lives in its own unit because `<windows.h>` clashes with the game headers (macros `B`/`R`,
`struct RECT`). Note stderr is buffered: its line order is NOT chronological (a `cannot read DATA.DAT` can show up
"before" a much later fault); stdout is unbuffered and in order.

## Env-gated diagnostics (all off by default; set in the launching shell)
`DC_DUMP_DRAWS` (draw groups, drawrect/frame data/drawmat, black2d, skin, skinwhy, frame buffer redirects), `DC_DUMP_TEXTURES`, `DC_TRACE_TEXTURES`, `DC_NO_LIGHT`, `DC_RTT_NODEPTH`, `DC_NO_SKIN_NORMALS` (now default behaviour; `DC_SKIN_NORMALS=1` restores skinning), `DC_WIDE_DRAWRECT`, `DC_CRASH_TRACE`. stdout is unbuffered so logs survive kills.

## Other open items (lower priority)
- Fixed in the first compatibility batch: live inspection showed `SetEventScript`'s value/call stack allocations used PS2 byte widths:
  128 host `RS_STACKDATA` slots need 2048 bytes, but it allocates 1024; 512 host `RS_CALLDATA` entries need 12288,
  but it allocates 6144. The live stack/call address ranges overlap. No corruption was proven in this scene
  (only four value slots/ two call entries active), and this is separate from the proven spline NaN stall.
  All event/action/effect/monster builders now allocate host-sized arrays with required-allocation checks.
- **Moving black bar (user's read, to verify):** the 2D background images are only as big as they are and the game rotates them to fake movement; a wider screen leaves black area. User's direction: **upscale that texture during unpacking** rather than fix it at draw time. Not investigated yet. The port's `black2d` diagnostic (`DC_DUMP_DRAWS=1`, `port/src/mg_drawprim.cpp:386`) prints untextured black quads (`target 0x10009`, `rgba 0 0 0 0`) and is a lead, not a conclusion.
- Movies: `port/src/movie.cpp` remains timing-only with no MPEG decode. Bounded PSS/PES demux now has a passing fixture but is not connected to the player; codec development headers/link libraries are absent. Audio now uses the validated SOUND reader and independent PCM buses with real cursor/EOF, replacing the fixed timer. Audible/event acceptance remains pending. See `docs/port/MEDIA_CONTRACT_COVERAGE.md`.
- `--jump dungeon:0` segfault; `--jump edit:0` ends at `OLD MAP ERR`/`ext 93` (probably --jump artefacts).
- 23 dropped font draws (`gfx: a draw names texture 0x10005`) at the title->menu texture-manager reset (moji font texture released): likely the title flicker.
- Six VU0 functions implemented (`vu0_gaps.cpp`) but unverified against hardware.
- `STATIC_ASSERT` is off by default (`-DDC_STATIC_ASSERT=ON` enables; 310 pointer-size failures).
- PS2 byte-identical check was not run; all 333 `ps2/` files now equal upstream `4ee90030` with no authored retail edits.
- Kazarov `port_override.py` fixes are carried in `scripts/port/port_override.py` and should go upstream.
- Windows only: `u_long` macro in `port/include/port.h`, `scripts/port/win_rewrite.py` rewrites literal `long` sites in generated copies; no `wmain` (non-ASCII argv paths mangled), no window icon.
- Older WSL push attempts lacked workflow scope. Windows Git successfully pushed the new checkpoint branch, including `c8f8d4163`, `3139f445f` and latest `132ba152`; `port/upstream-sync` was not moved. No PR created. Suggested older PR text in the conversation would need updating for current status and foundation audit.

## Reports and notes on disk
`E:\Chronicle-Two\PC_PORT_WORKUP.md`, `MODEL_RENDERING_RESEARCH.md` (recomp-project research, partly useful), `workers\dc1sync\*.md` (agent briefs and COMMON rules), `/home/jeffie/reports/*.md` (agent reports, WSL), memory notes in `C:\Users\armor\.claude\projects\E--Chronicle-Two\memory\`.
