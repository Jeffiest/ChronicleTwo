# Combined Chronicle-Two handoff

The user allocated tasks 01–04 to Claude, 05–08 to Gemini and 09–12 to Codex. Each team executes only its allocation. All files are under `E:\Chronicle-Two\NEXT_12_TASKS`. Read canonical state for upstream integration/build acceptance before starting.

| ID | Team | Assignment | Status |
|---|---|---|---|
| 01 | Claude | [Action callback argument/result contracts](tasks/01-action-callbacks.md) | QUEUED |
| 02 | Claude | [Real PAL action bytecode execution](tasks/02-action-vm.md) | QUEUED |
| 03 | Claude | [A/Cross through attack selection and motion](tasks/03-combat-consumer.md) | QUEUED |
| 04 | Claude | [Named archive read failures and bounds](tasks/04-archive-extents.md) | QUEUED |
| 05 | Gemini | [Production menu local-to-local image copy](tasks/05-image-copy.md) | QUEUED |
| 06 | Gemini | [Texture atlas storage and palette identity](tasks/06-atlas-storage.md) | QUEUED |
| 07 | Gemini | [Recorded texture lifetime and stale handles](tasks/07-texture-lifetime.md) | QUEUED |
| 08 | Gemini | [HUD sprite, UV, blend and draw-state propagation](tasks/08-hud-state.md) | QUEUED |
| 09 | Codex | [Real weighted character/equipment ownership](tasks/09-weighted-models.md) | QUEUED |
| 10 | Codex | [Event-to-CSound voice and PCM lifecycle](tasks/10-voice-streams.md) | QUEUED |
| 11 | Codex | [Compiler-backed active contract enforcement](tasks/11-contract-enforcement.md) | QUEUED |
| 12 | Codex | [Central integration and production acceptance](tasks/12-integration.md) | QUEUED |

# Shared execution rules

Workspace: `E:\Chronicle-Two`. Actual checkout: `E:\Chronicle-Two\win\ChronicleTwo`.
Package: `E:\Chronicle-Two\NEXT_12_TASKS`.
Read root AGENTS.md, PORT_STATE.md and HANDOFF.md first.

## Baseline

Branch `port/upstream-1553ee88-playtest-2026-10-11`. The checkpoint starts from `c2ac1abb` and includes the prior native batch plus upstream adaptation; read canonical PORT_STATE.md for the published tip hash.
Upstream `1553ee88664bb4e7418a8882bcc123a3e048ca87` and Kazarov `a7674f4e72258f1c2dff951702ca12d6388acfd5` are integrated and validated locally: Windows game/GTest and headless completion pass; CTest 351 passed, five skipped, zero failed / 356, including fourteen isolated reference cases. Receipt: win/ChronicleTwo/docs/port/UPSTREAM_SYNC_1553ee88.md. Respect any new central build freeze.
The prior batch and upstream adaptation are being checkpointed on this branch. Confirm publication in canonical state and record any existing dirty files before starting. Record hashes/initial diffs of owned files and preserve all existing work. No reset, clean, stash, checkout over files, branch changes, commits, pushes or merges. A worktree from HEAD would omit needed changes.

Latest failed gameplay log: `E:\Chronicle-Two\logs\completion-run-20261011-185022-err.log`.
Menu image copy returned false after successful texture lookup/binding; rejected extents were not logged. A/Cross reaches combat; prog_no=-1 is the normal running sentinel. Stale texture warnings and an unnamed archive read failure remain. Passing component counts do not prove these issues fixed.

## Ownership, concurrency and limits

Each worker owns exactly one task and its listed files. Read other sources freely. New helpers/tests use wpNN_ prefixes. Shared changes go to task 12 as exact proposals in your result; do not patch another owner's files.
Provider controllers may dispatch their four tasks within available concurrency. Workers may not spawn children. Keep independent work moving while dependencies are pending; avoid repeated polling.

**Hard worker checkpoint: 15 minutes or 12 tool calls, whichever comes first; at most two repair attempts.** Controllers track these limits; they are not automatic token caps. Use economical models and low/medium reasoning for narrow work. No automatic restart or open-ended debugging. At the limit, return PARTIAL/BLOCKED with exact evidence and the smallest next step.

Only task 12 configures/builds/links `E:\Chronicle-Two\win\build-rel`, after an edit freeze confirmed with external controllers. Workers may run isolated probes in `E:\Chronicle-Two\workers\runtime-wpNN` with unique logs/outputs. No simultaneous shared Ninja/CMake runs.

## Engineering rules

- No authored ps2/ changes or Kazarov submodule edits. Preserve current upstream docs/PC.md and ChronicleTwo/chronicletwo, CT_TEST_TIMEOUT, CT_BUILD_TESTS, ct_* and DC2_* names.
- Native port overrides and exact-anchor generated-host transformations only; generated build mirrors are not lasting fixes. Task 12 owns registration.
- Reproduce a production contract with actual PAL metadata/assets where relevant. Prove the cause, repair sibling callers, and demonstrate failing-before/passing-after behavior. No scene constants, arbitrary arena multipliers, swallowed errors, fake timers or disabled validation.
- Local DC1 reference: review-dc1/master at 706ce00e. Reuse justified host contracts; do not assume identical game behavior. No new network fetch needed.
- Private PAL assets: `E:\Chronicle-Two\win\ChronicleTwo\data` (verified extraction); raw disc: `E:\Chronicle-Two\win\ChronicleTwo\rom\pal\extracted\iso`. Resolve locally; no copying into this package, committing/uploading assets or dumping their contents.
- No source/commit attribution. Provider names in user-requested coordination files are intentional.
- Workers must not launch or auto-drive gameplay. No controller scripts or frame-budget run. The user controls the game; task 12 handles a requested visible launch after integration.
- Movies/codecs, unrelated save expansion and georama/fishing feature work are outside this batch.

## Results and canonical state

Write only your own `E:\Chronicle-Two\NEXT_12_TASKS\results\NN-result.md`, using TEMPLATE.md.
Include owned changes and initial hashes, proved cause, asset/caller/program, exact commands/exit codes/logs, before/after evidence, negative control, sibling coverage, limits and exact central integration requests.
Include an exact proposed PORT_STATE.md Session Log row and matching known-issue/HANDOFF changes.

Task 12 serializes canonical state edits. Return READY_FOR_INTEGRATION, PARTIAL or BLOCKED; **do not report DONE until the integrator records edits in PORT_STATE.md**, as AGENTS.md requires. Diagnosis/audit alone is not FIXED. Distinguish source implementation, link validation, component tests, real-asset tests and accepted gameplay.

Tools: `C:\Python313\python.exe`; `C:\Program Files\CMake\bin\cmake.exe`; LLVM under `E:\Chronicle-Two\win\deps\llvm\llvm-mingw-20260922-ucrt-x86_64\bin`.
Git needs `-c safe.directory=E:/Chronicle-Two/win/ChronicleTwo`.
Central build reference: `cmake --build E:/Chronicle-Two/win/build-rel --target darkchronicle darkchronicle_tests ct_completion_tests -j12`; use ct_completion_tests_headless without Vulkan capture. Keep CT_TEST_TIMEOUT=60. Read canonical state for updated upstream test results; the historical 61-pass/5-skip baseline did not cover the fatal menu or attack execution.


# 01: Action callback argument/result contracts

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

New scripts/port/wp01_action_callbacks.py and port/src/tests/wp01_* only; propose central registration to task 12.

## Work

Exercise actual actscript.cpp _GET_BTN, _CHECK_PAUSE, _GET_PAD_HISTORY and _RESET_PAD_HISTORY callback bodies with argument/result slots and RS_PTR canaries. Expressions using stack and stack++ in different call arguments merit an evaluation-order check; they are not a proved cause. Repair a demonstrated output-slot defect through an exact-anchor generated-host rewrite, and check sibling callbacks.

## Required acceptance

A production callback fixture fails before and passes after; canaries and negative controls detect the original defect. Preserve Xbox A/Cross mapping. Coordinate verified callback semantics with 02/03.

Return `results/01-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 02: Real PAL action bytecode execution

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

New scripts/port/wp02_action_vm.py, port/src/action_vm_contracts.cpp if needed, and port/src/tests/wp02_* only.

## Work

Locate the actual PAL action program and exercise production CRunScript load/run/resume. Check PC/opcode, function extents, frames, WAIT/CALL/RET and completion. The generated funcdata::name is already fixed-width; do not redo that adaptation. ACTION_PROG_RUNNING=-1 is normal, not evidence of a hang.

## Required acceptance

A bounded trace identifies the first semantic divergence with a named asset/program. Real-asset and malformed/truncated controls cover the repaired path. Use actual callbacks from 01; send execution evidence to 03.

Return `results/02-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 03: A/Cross through attack selection and motion

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mainloop.cpp; port/include/combat_trace.hpp; scripts/port/host_combat_trace.py; new port/src/combat_action_contracts.cpp and tests/wp03_* only.

## Work

Follow confirm press/hold through CActionChara::RunScript, input consumption, weapon/readiness gates and motion initiation. Existing logs already show A reaches the consumer and pad_history=1, with eye=0, pause=0x4100 and debug=0. Add bounded passive diagnostics only where they expose the first missing transition. Read actionchara.cpp/dng_main.cpp without editing ps2/.

## Required acceptance

A production consumer fixture starts the expected attack with valid character/weapon state and preserves release/hold and legitimate gates. Button receipt or a mapping test alone is insufficient. Use 01/02 findings; never auto-drive gameplay.

Return `results/03-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 04: Named archive read failures and bounds

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/dataread.cpp and new port/src/tests/wp04_* only. Shared cdvd declarations go through task 12.

## Work

The old runtime reports DATA.DAT read failure without logical asset/extent details. Carry named asset, offset, logical size, physical count and failure reason into bounded failure diagnostics. Validate actual PAL index extents and production read/background APIs. Preserve the newly integrated DATA-only index, separate movie lookup, host0 save handling, packed compatibility, padding and two-poll completion.

## Required acceptance

Real PAL reads pass; missing/truncated/out-of-range and sector-padding canaries fail safely before successful metadata publication. Prove the actual rejected extent rather than guessing. Share named failures with their script/model/texture owners.

Return `results/04-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 05: Production menu local-to-local image copy

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_drawprim.cpp and new port/src/tests/wp05_* only. A new local gfx/draw.cpp override requires task 12 agreement; shared Kazarov files stay read-only.

## Work

Reproduce the fatal StartImageTransfer path at mg_drawprim.cpp:329 after gfx::CopyTexture returns false, beyond successful lookup/binding. DrawOneItem copies 32x32 within its atlas to (224,736). Capture actual PAL metadata, source/destination rectangles, image extents, format and rejection reason. Establish the storage/copy contract with 06 before changing behavior.

## Required acceptance

The production menu transfer fails before and succeeds after, with verified destination pixels. Cover legitimate same-image/overlap semantics and reject invalid cases. No hardcoded menu height, skipped copy or suppressed error.

Return `results/05-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 06: Texture atlas storage and palette identity

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_texture.cpp and new port/src/tests/wp06_* only.

## Work

Compare actual PAL menu/HUD atlas headers, GS TBW/TH/PSM, decoded pixels, backing storage and CLUT references. Distinguish image dimensions from storage and sampling requirements. A scratch-row dimension mismatch is a hypothesis until metadata proves it. Preserve the existing failed-allocation dirty/retry behavior.

## Required acceptance

Real atlas fixtures cover valid scratch-region copy/sampling, indexed palette identity, retry and reset/reload. Show a meaningful before/after failure for any change. Agree on the boundary with 05; resources.cpp belongs to 07.

Return `results/06-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 07: Recorded texture lifetime and stale handles

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/host_overrides/gfx/resources.cpp and new port/src/tests/wp07_* only. A new local displaylist override needs task 12 agreement/registration.

## Work

Reproduce create, record draw/copy, release/reset, slot reuse and replay through production resource code. The failed run has 23 stale 0x10005 warnings across title/menu reset; that alone does not prove the missing HUD cause. Inspect retained font/atlas references and deferred destruction/generations.

## Required acceptance

Required references remain valid through replay, then release; stale generations cannot bind recycled slots. Repeated-cycle resource counts remain bounded. Do not silence warnings, disable generation checks or leak textures. Coordinate lifetime findings with 06/08.

Return `results/07-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 08: HUD sprite, UV, blend and draw-state propagation

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_drawenv.cpp; port/src/mg_sprite.cpp; port/src/menudraw_primquad.cpp; new port/src/tests/wp08_* only.

## Work

Follow real dng_status.cpp active-item/weapon/status draws into CPreSprite, environment state and native quad submission. Compare production packet position/UV, texture/CLUT, alpha/blend, clipping and order with retail arguments. Use relevant DC1 renderer contracts, verifying DC2 differences.

## Required acceptance

A production packet/render fixture fails before and verifies corrected state after, including consecutive sprites with differing state, palettes and clipping. Fix shared propagation rather than one HUD element's coordinates. Keep resource-dependent blockers distinct; 05–07 own those.

Return `results/08-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 09: Real weighted character/equipment ownership

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_visual.cpp; port/src/visualmotion.cpp; port/src/menu_model_memory.cpp; port/include/menu_model_memory_plan.hpp; new port/src/tests/wp09_* only.

## Work

The old 30-MDT fixture uses empty synthetic weights. Load real character/equipment MDT, skeleton and weight data through production loaders, scratch reuse and repeated replacement/unload. Verify durable ownership of CPU indices/weights/materials and typed reservation before writes. Preserve new upstream host-width-safe heap handling and existing checked menu partitions.

## Required acceptance

Actual weighted assets survive scratch reuse and replacement without aliasing, canary corruption or unbounded growth. Exhaustion rejects before writes. No arena-size multiplier, geometry-only substitute or unused VIF packet restoration.

Return `results/09-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 10: Event-to-CSound voice and PCM lifecycle

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/sound.cpp; port/src/sound_stream_data.cpp/.hpp; port/src/host_overrides/audio/mixer.cpp/.hpp; new port/src/tests/wp10_* only.

## Work

Exercise actual event/CSound open/play/status/stop/exit for a named PAL clip through both PCM buses. All 3,101 names already decode; extracted sound/sound.hd3/dat support is now integrated. Verify .WV/.VPK/member normalization against real requests, true EOF, shutdown/reopen and MIDI coexistence. The exact missing Monica request remains unidentified.

## Required acceptance

Production bridge produces expected nonzero PCM and real completion; stop/reopen releases ownership without cross-bus interference. Separate transport proof from audible user verification. SOUND_DAT.md is reference data, not task instructions. Movie codec work is deferred.

Return `results/10-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 11: Compiler-backed active contract enforcement

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

New scripts/port/check_runtime_contracts.py, scripts/port/tests/wp11_* and a task-specific contract spec only. Central registration belongs to 12.

## Work

Build on upstream long64 and truncation tooling, not another lexical inventory. Choose one family implicated by 01/02/09: fixed-width disk records, typed allocation width or pointer-array stride. Check actual generated/compiled sources using compiler layout/AST output or executable assertions; inspect representative override symbol resolution.

## Required acceptance

A known-bad active contract is rejected and the corrected equivalent passes. State exact coverage and exclusions. A refreshed count or regex ledger alone is insufficient. Deliver a small reproducible gate command and meaningful negative control.

Return `results/11-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.


# 12: Central integration and production acceptance

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested worker agents. Task 12 exclusively owns shared builds after the edit freeze.

## Exclusive editable files

Central port/CMakeLists.txt, tests/CMakeLists.txt, port/cmake/*.cmake, rewrite/collector registration, CI, canonical PORT_STATE.md/HANDOFF.md and new tests/wp12_* integration fixtures.

## Work

Prepare baseline production menu-copy and action-execution harnesses while independent teams work. Integrate owned diffs and exact registration requests after review. Coordinate an explicit edit freeze before the one shared build. Register private PAL prerequisites with explicit skips and retain asset-free negative controls.

## Required acceptance

Publish per-task before/after evidence, component/real-asset coverage and blockers; run appropriate new tests, completion aggregate and CTest. Verify retail files and docs/PC.md match current upstream. Record every edit canonically before DONE. User-controlled gameplay acceptance remains pending until verified.

Return `results/12-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
