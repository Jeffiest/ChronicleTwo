# Task 12 combined integration receipt

Status: READY_FOR_INTEGRATION. User confirmed Claude and Gemini finished and stopped editing before the shared build. This follow-up used one controller, no further worker dispatch.

Integrated Claude tasks 01-04, Gemini tasks 05-08 and Codex sound/compiler/weighted probes. Initial results and before/after evidence remain in their NN-result.md files and logs/claude-wp01-04-verification.log. Sources remain uncommitted on port/upstream-1553ee88-playtest-2026-10-11; no Git history/remote action, gameplay launch or automated inputs.

## Result
- Configure: exit 0; combined darkchronicle/darkchronicle_tests/ct_completion_tests_headless/ct_runtime_contract_tests build: exit 0.
- CTest complete=True, zero failures=True. Summary: 100% tests passed out of 367; Total Test time (real) =  20.03 sec; The following tests did not run:.
- Private PAL weighted fixture: 29 actual MDTs, eight reloads, scratch invalidation/canaries pass. This extends coverage of existing production ownership; no new weighted production repair or full unload/exhaustion acceptance claimed.
- CSound Exit fixture: baseline fails, patched passes with actual 0010461.wav through both buses. Audible/event-to-Monica acceptance remains open.
- Normal game builds now require Clang AST material allocation validation and its rejected negative control. Coverage is one active typed material allocation, not complete compiler enforcement.
- Retail ps2/ and docs/PC.md equal upstream 1553ee88: True. Kazarov pin a7674f4e72258f1c2dff951702ca12d6388acfd5, clean=True.
- Game SHA256: A704D20D95232A0B7D6F5F7D325AE8A2CFFD3CD257778070EC90DCB8BB1710E0.

## Integration repairs
Registered exact action callback rewrite before passive combat instrumentation; loading arena now copies the complete host mgCMemory after long64 transformation. Fixed extracted callback fixtures to omit scene-only observations (including CRLF input). Refreshed compatibility inventory/ledger: 5506 findings.

Reviewed shared atlas code: repaired unreachable clone retain, release on identity change and dirty image re-upload. Different shapes at the same address keep distinct resources. Restored loud image-transfer error handling and downward row-copy order. These integration review repairs have current-source component coverage, but clone partial release and arbitrary texture-format overlap need stronger regression coverage.

The original wp06 probe could print success while bypassing all Vulkan assertions. wp05/wp06 now initialize a hidden renderer and explicitly skip without one; wp06 binding failure with a renderer fails. Windows hosted CI excludes these two device tests with the existing rendering exclusions; Linux/macOS keep them. Hosted jobs are unverified. Test 07 directly sets renderer resource state; it does not prove complete display-list retirement. Test 08 covers selected state propagation, not full HUD visual acceptance. Gemini 05-08 lacked recorded baseline execution; do not call those failing-before/passing-after proofs.

## Commands and logs
`cmake --build E:/Chronicle-Two/win/build-rel --target darkchronicle darkchronicle_tests ct_completion_tests_headless ct_runtime_contract_tests -j12`

`ctest --test-dir E:/Chronicle-Two/win/build-rel --output-on-failure -j4`

Logs: logs/wp12-integration-configure.log, wp12-integration-build-final.log, wp12-integration-ctest.log, wp12-isolated-probes-final.log. CMake registration connects the new tests to ordinary GTest/completion builds and CTest. The isolated wp05_08_probes.py helper remains a local historical runner with Windows paths; it is not the CI runner.

## Remaining acceptance
One user-controlled run must verify A/Cross visibly starts attacks, Items opens/changes equipment without termination, and the HUD draws correctly. Mapping was preserved. Voice playback, weighted equipment/full unload/exhaustion, and broader compiler coverage remain open. No gameplay success is claimed.

Final CTest count: **362 passed, 5 skipped, 0 failed / 367**; all eleven new runtime contracts passed. CTest exit 0. The new CTest summary format was reconciled against individual result lines; no rerun was needed.
