# Upstream integration receipt: 1553ee88

Validated locally on 2026-10-11. TheMoonPeople master
`1553ee88664bb4e7418a8882bcc123a3e048ca87` contains seventeen commits after the
previous `4ee90030` import. Kazarov is clean at its required pin
`a7674f4e72258f1c2dff951702ca12d6388acfd5`.

At validation time, the working tree on `port/upstream-first-2026-10-11` contained
these adaptations and the preceding native fixes atop `c2ac1abb`. The user then
requested publication on `port/upstream-1553ee88-playtest-2026-10-11`. This checkpoint
preserves source integration without creating an upstream merge commit; root
PORT_STATE.md records final publication and playtest evidence.
`E:/Chronicle-Two/logs/upstream-1553ee88-backup` preserves the initial binary
patch, 48 dirty/untracked files, hashes and canonical documents.

## Adopted upstream changes

- Upstream PC guide, Phase 2 plan, Kazarov configuration and Windows wrappers;
  original ChronicleTwo/chronicletwo, CT_TEST_TIMEOUT, CT_BUILD_TESTS, ct_* and
  DC2_* naming is retained. `ps2/` and `docs/PC.md` match upstream exactly.
- Long64 token rewriting and header/linkage adaptation run on generated host
  sources/headers, alongside the existing checked native rewrites. The upstream
  long64 suite passes all 91 cases. Native script/menu/photo ownership fixes remain.
- Upstream mg_math and VU lane semantics, with expanded collision, frame and game
  utility tests. Completed native implementations remain owners where imported
  assembly replacement units would duplicate existing methods.
- Host-width-safe heap headers, complete heap/stack mode methods and peak tracking
  are merged into the checked native allocator. Native bounds, typed allocation,
  placement growth and owner diagnostics remain active. Fixtures use production Init.
- Shared cooperative fibers own threads and semaphores. Local duplicate thread
  and interrupt implementations are removed. VSync/frame timing has one owner,
  and controller vibration now runs through the cooperative controller thread.
  Its initial elapsed count follows retail, including initialization fields.
- Configuration change hooks and preservation of unreadable existing config files;
  prog0 pipeline additions merged with native pipeline cache diagnostics.
- dc2data extraction, DATA-only indexing, Shift-JIS disk-name matching and host0
  read/write fallback. Loose reads now zero-fill the same sector tail as packed
  reads. Movies remain available to cdvd lookup without entering the DATA index.
  Native background reads retain the tested two-poll publication contract.
- The SOUND reader supports both raw root SOUND.HD3/DAT and the extractor's
  lowercase sound/sound.hd3/dat layout, with a decode fixture for both layouts.

## Validation and boundaries

Commands use the configured LLVM-MinGW RelWithDebInfo build and CT_TEST_TIMEOUT=60:

```powershell
cmake --build E:/Chronicle-Two/win/build-rel --target darkchronicle darkchronicle_tests ct_completion_tests_headless -j12
ctest --test-dir E:/Chronicle-Two/win/build-rel --output-on-failure -j8
```

Game and test builds pass. CTest registers 356 cases: **351 pass, five skip, zero
fail**. The skips require unavailable TTF glyph and desktop mouse-capture prerequisites.
Fourteen passing `UpstreamReference.MgDrawPrimAsm.*` cases use an isolated packet
reference target. The native renderer submits host draw state, so those packet
checks do not establish native rendering correctness. Ownership enforcement allows
the explicit reference target while a negative control still rejects the same
excluded source entering a production target.

The final headless completion aggregate passes 25 compatibility cases, seven
component executables and audit/ownership controls. Active ownership reports 69
native translation units, 420 override names, 230 collector units, 37 excluded
units with zero production/collector leaks, and twenty selected game test files
with 259 source-declared cases. This is compile-artifact evidence; linker choice
and runtime reachability remain incomplete. Lexical inventory: 5,491 findings;
all seventeen prior reviewed dispositions are retained.

The extractor verified **8,762 local PAL asset files**, including 8,751 DATA files,
under `E:/Chronicle-Two/win/ChronicleTwo/data`. Real DATA index/edit.cfg tests pass.
The extracted tree is ignored and private; no assets were committed or uploaded.
The raw disc directory remains available. Logs are under `E:/Chronicle-Two/logs`:

- `upstream-1553ee88-repair-build.log`: final game/GTest build.
- `upstream-1553ee88-build-validation.log`: headless aggregate and build.
- `upstream-1553ee88-ctest-final.log`: final 356-case run.
- `upstream-1553ee88-extract.log`: extraction and asset verification.

Final game SHA256:
`788599B315A105B02C0595CBAE6734223AADE2B2263D8AB426F5C51D30CD2E6C`.
Other platforms, hosted CI and PS2 byte matching were not run here.

## Remaining production acceptance

No game was launched during this integration. The previous user run still proves
an unresolved menu CopyTexture rejection, stale texture handles and an unnamed
archive failure. A/Cross reaches the action consumer; `prog_no=-1` is the normal
running sentinel, and attack execution remains unaccepted. HUD correctness,
weighted character ownership and event-to-voice playback require production
fixtures and a subsequent user-controlled run.

The requested next package is `E:/Chronicle-Two/NEXT_12_TASKS`: Claude 01–04,
Gemini 05–08, Codex 09–12. Twelve exclusive briefs, provider handoffs, results
templates and integration rules are present. Each worker stops after fifteen
minutes or twelve calls, with at most two repair attempts. The package is queued,
not dispatched; canonical recording and shared builds belong to task 12.
