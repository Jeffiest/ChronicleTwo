# Chronicle-Two (Dark Chronicle PAL) native PC port: handoff

Written 2026-10-11. Highest rule from the user: never run out of usage without a handoff file like this one. Keep it updated.

## Hard rules from the user
- NO AI attribution anywhere: no Co-Authored-By, no "Generated with", no tool names in commits, PRs, comments, docs.
- Do NOT edit `ps2/` (no container here, so the PS2 byte-identical check cannot run). Everything goes through `port/`, CMake, scripts.
- Loud stubs only (`PS2_UNIMPLEMENTED()`), no PS2 emulation, no memory overlays, no hardcoded resolution/framerate.
- Comments: terse, only non-obvious WHY.
- The user is cost-conscious: no open-ended agent debugging. Use small env-gated logs and ask for ONE run per hypothesis.
- **Do not auto-drive the game.** The user controls it. Launch it detached (recipe below), then give the user a short numbered checklist (language, New Game, costume, ...) and read the logs after. Do not invent input scripts to fake menu navigation.
- Update `PORT_STATE.md` + this file on every edit (see AGENTS.md) — including intermediate diagnostics, not just the final fix.

## Repos, branches, machines
- Current Windows checkpoint branch: **port/checkpoint-2026-10-11-cutscene**, based on `3139f445f`, with the
  three latest host fixes and `docs/checkpoints/2026-10-11/` snapshots of review/state/handoff and regression files.
  Remote **github-fork** is `https://github.com/Jeffiest/ChronicleTwo.git`; `origin` remains the WSL repository.
  Root `PORT_STATE.md` and this file remain canonical; archived copies capture checkpoint preparation.
  Regression rerun passes, including the expected failing original-code negative control. Battle remains broken.
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
  `Start-Process -FilePath E:\Chronicle-Two\win\build-rel\darkchronicle.exe -ArgumentList '--data','E:\Chronicle-Two\win\ChronicleTwo\rom\pal\extracted\iso','--width','1280','--height','720' -WorkingDirectory E:\Chronicle-Two\win\build-rel -RedirectStandardError E:\Chronicle-Two\logs\play-err.log -RedirectStandardOutput E:\Chronicle-Two\logs\play-out.log`.
  `--frames N` and `--screenshot PATH` run a fixed budget and EXIT (that is why a scripted run closes on you; useful for automated checks, not for playing). `--input FILE` / `DC_INPUT` drives pad 1 from a script of `frame [pad1|pad2] button... [lx ly rx ry]` lines; `key:<name>`/`mouse:dx,dy` drive devices. Keyboard works directly too (Enter = confirm, arrows = move).
- First-time flow (keyboard): boot → **Language Select** (Cross/Enter picks the highlighted row; arrows move) → memory-card check → copyright → title **PUSH START** (Start/Enter) → title menu → **New Game** (Cross/Enter, it is the default row) → **Select Costume** (L/R turn, U/D zoom). No config/save needed.
- Config/saves: `%LOCALAPPDATA%\chronicle-two\save\config.json` (own dir; the DC1 port uses `\chronicle`, keep them separate). Delete `?` none needed; first boot shows Language Select.
- WSL shell tip: from PowerShell write a script file under `C:\Users\armor\AppData\Local\Temp\claude\E--Chronicle-Two\...\scratchpad\x.sh` and run `wsl -d Ubuntu -- bash /mnt/c/.../x.sh`; inline quoting breaks.
- Debugger: `E:\Chronicle-Two\win\deps\llvm\llvm-mingw-20260922-ucrt-x86_64\bin\lldb.exe` (helper `E:\Chronicle-Two\win\run-debug.ps1`).
- Frame extraction from recordings: ffmpeg is installed (WinGet). Window capture helper used: PowerShell + CopyFromScreen.

## What works now
Boots, 504 Vulkan pipelines, language select, title, New Game, and the **Select Costume screen renders Max correctly** (body, head, hat, bag, shoes, amber outline, blinking, **both hands correct — left hand fixed, see below**). New Game gets past the costume screen, loads the first dungeon (`map/d/d01`, `d01_1.stb`, `RUN SYS_SCRIPT 1010`), and renders the knight/corridor prologue after the HUD sprite fix. User verified the entire cutscene now completes, with some remaining bugs; battle does not start. Skip cleanup's host image-slot fix still awaits explicit verification. The next task is the foundation audit below, not another isolated scene patch. Root causes already fixed (all in `port/src`):
1. CreateFace index arrays and skin weights lived in scratch arenas reused by later loads: kept in host memory (`visualmotion.cpp`).
2. Skinned normals broken: base-pose normals by default (`DC_SKIN_NORMALS=1` restores) (`dynamicanime.cpp`).
3. Bone palette must be indexed by position in the bone list, not frame number (`dynamicanime.cpp` BuildSkinMatrices).
4. `mgSetPkFrameBuffer` never redirected to the off-screen texture used by the outline pass (`mglib.cpp`).
5. ScreenXScale used the main 640 logical width even for a 512-wide target (`mg_drawenv.cpp`).
6. The off-screen target's own depth was never cleared: cleared once per frame (`mglib.cpp`).
7. Also: read-queue pending semantics (`dataread.cpp`), per-visual private vertex arrays (`dynamicanime.cpp`), base-pose reset on model reload, VU0 routines implemented in `vu0_gaps.cpp`, own app dir.
8. Hand/ankle bones' frame numbers (41..78) rejected by a `>= 32` bound in `BuildSkinMatrices` (`dynamicanime.cpp`) — commit `c8f8d4163`.
9. Character stacks sized in PS2 blocks overflow on host struct widths — `PORT_OVERRIDE` doubling in `port/src/character.cpp` — commit `3139f445f`.

## RESOLVED (2026-10-11): Max's LEFT HAND pointed inward; ankle rect clipped
Root cause: `BuildSkinMatrices` (`port/src/dynamicanime.cpp`) skipped any bone whose **frame number** was `>= 32`.
`visual->bone[]` stores frame numbers, and the hands/ankles are frames 41..78, so all their skin matrices were left as the
initialized identity and the hand never deformed — it kept its inward bind pose. Retail's `CreateExtRenderInfoPacket`
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
every table size doubled (pool `BuffCharacter` ~2.3 MB → ~4.6 MB, main buffer holds it). After the fix the in-game
`[N]STACK used/capacity` print (`ps2/src/charasetup.cpp:370`) reads: 0) 61772/137000, 1) 40356/80000, 2) 2197/14000,
3) 1359/9300, 4) 1794/6000, 5) 10233/19800, 6) 4095/9000 — so there is headroom now.

## Latest continuation (2026-10-11): prologue renders; skip cleanup crash
User confirms the complete cutscene now runs, with some remaining bugs, but battle does not start. Logs
`spline-fix-out.log` show `stack over 38/0`, `64/0`, `384/0`; trace enters `SetMonsterScript` (`runscript_opcodes.cpp:4682`)
via `CMonsterMan::SetActiveMonster` -> `AutoSetMonster` -> event script. User requested broader reevaluation of
latest upstream master, Dylan's fork and DC1's port before continuing individual crash fixes.
Completed review: `E:\Chronicle-Two\reports\PORT_REEVALUATION_2026-10-11.md`.
Fetched review-only refs in Windows clone: `review-upstream/master`=4ee90030, `review-dc1/master`=706ce00e,
`review-dylan/port-bootstrap`=56555a37 / master=6a24d4f2; Kazarov `review-upstream/main`=ce459c1.
No merges/checkouts/game-code updates. Initial parent fetch submodule warnings were followed by a successful
direct Kazarov fetch and clean parent refresh with recursion disabled. Preserve the three checkpoint fixes.

**Next implementation: host-compatibility foundation audit.** Begin with host-sized script arrays and
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
- Live inspection also showed `SetEventScript`'s value/call stack allocations still use PS2 byte widths:
  128 host `RS_STACKDATA` slots need 2048 bytes, but it allocates 1024; 512 host `RS_CALLDATA` entries need 12288,
  but it allocates 6144. The live stack/call address ranges overlap. No corruption was proven in this scene
  (only four value slots/ two call entries active), and this is separate from the proven spline NaN stall.
  Implement host-sized allocation before debugging deeper script-stack failures.
- **Moving black bar (user's read, to verify):** the 2D background images are only as big as they are and the game rotates them to fake movement; a wider screen leaves black area. User's direction: **upscale that texture during unpacking** rather than fix it at draw time. Not investigated yet. The port's `black2d` diagnostic (`DC_DUMP_DRAWS=1`, `port/src/mg_drawprim.cpp:386`) prints untextured black quads (`target 0x10009`, `rgba 0 0 0 0`) and is a lead, not a conclusion.
- Movies: only a timing stub (`port/src/movie.cpp`) — no MPEG decode. Mechanism: a PSS that is present "plays" its PTS duration of pure black on the game clock (`Play`/`SwitchThread`), and a PSS missing from the extracted tree is `ended` immediately so the mode skips it. So the attract movie's black screen is expected, not a bug. Needs PSS demux + LGPL FFmpeg for real video. Audio streams not done. `SOUND.HD3` format unverified (another agent claimed PCM; likely wrong).
- `--jump dungeon:0` segfault; `--jump edit:0` ends at `OLD MAP ERR`/`ext 93` (probably --jump artefacts).
- 23 dropped font draws (`gfx: a draw names texture 0x10005`) at the title->menu texture-manager reset (moji font texture released): likely the title flicker.
- Six VU0 functions implemented (`vu0_gaps.cpp`) but unverified against hardware.
- `STATIC_ASSERT` is off by default (`-DDC_STATIC_ASSERT=ON` enables; 310 pointer-size failures).
- PS2 byte-identical check never run; branch still carries ~35 edited `ps2/` files from the first port pass.
- Kazarov `port_override.py` fixes are carried in `scripts/port/port_override.py` and should go upstream.
- Windows only: `u_long` macro in `port/include/port.h`, `scripts/port/win_rewrite.py` rewrites literal `long` sites in generated copies; no `wmain` (non-ASCII argv paths mangled), no window icon.
- Push of port-up-sync needs the user (token scope). Recent local commits to include: `c8f8d4163` (hand), `3139f445f` (crash trace + character stacks). Check `git log origin/port/upstream-sync..port-up-sync` against the fork before pushing. Suggested PR text lives in the conversation: base upstream master 872e125, lists Kazarov host layer, PORT_OVERRIDE, Windows build, VU0 routines, render fixes; known issues as above.

## Reports and notes on disk
`E:\Chronicle-Two\PC_PORT_WORKUP.md`, `MODEL_RENDERING_RESEARCH.md` (recomp-project research, partly useful), `workers\dc1sync\*.md` (agent briefs and COMMON rules), `/home/jeffie/reports/*.md` (agent reports, WSL), memory notes in `C:\Users\armor\.claude\projects\E--Chronicle-Two\memory\`.
