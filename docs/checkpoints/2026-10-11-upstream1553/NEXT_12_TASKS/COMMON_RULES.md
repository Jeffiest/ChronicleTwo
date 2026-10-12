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
