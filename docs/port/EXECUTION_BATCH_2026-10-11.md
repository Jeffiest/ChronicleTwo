# Authorized completion batch — 2026-10-11

User authorized twelve low-cost assignments covering the completion review. The session
supports four active agents including the integrator, so at most three assignments run
concurrently. Dependencies and shared files remain sequential. No master rewrite, remote
publication, retail-source changes or automated gameplay is part of this batch.

Each assignment uses a 15-minute or twelve-tool-call hard stop, at most two repair
attempts, no nested delegation and no duplicate full builds. The integrator owns CMake,
shared headers, state/handoff and validation. A package may return a specific blocker
rather than an unverified implementation; its status must say so.

| # | Deliverable | Integrated outcome | Evidence / remaining boundary |
|---|---|---|---|
| 01 | Menu model backing ownership and checked partitions | Implemented, component verified | Complete aligned parent extent, seven borrowed child views and remainder; retail capacities preserved. Menu acceptance pending. |
| 02 | Fixed/motion model allocation and lifetimes | Implemented, component verified | Required typed allocations before writes; native motion loading omits unused VIF packets. Repeated scratch/reload and 30 actual PAL geometry records pass. Real skin weights and Items flow still need acceptance. |
| 03 | Controller-to-combat state | Diagnostic integrated | Existing A/Cross mapping retained; passive `DC2_TRACE_COMBAT` hooks added to native/generated host code, capped at 256 changed-state records. Runtime divergence unproved. |
| 04 | HUD texture lifecycle | Narrow repair integrated | Failed renderer texture creation remains dirty for retry. No injected allocation-failure fixture or proof that this explains the reported HUD defect. |
| 05 | Active implementation/coverage enforcement | Ownership gate integrated; analysis partial | Normal builds reject excluded-source leaks; two negative-control fixtures pass. Manifest reports compiled sources and declared tests. Compiler-assisted pointer analysis, linker resolution and runtime reachability remain open. |
| 06 | Serialized records | Boundary tools/fixtures integrated | Fixed-width disk views and checked ranges pass; model fixture also asserts actual MDT types. Production decoder extents and remaining record layouts are incomplete. |
| 07 | SOUND index/PCM reader | Implemented, real assets verified | First-match duplicate lookup and bounded RIFF chunk walking; all 3,101 PAL indexed names decode as mono 48 kHz PCM16. |
| 08 | Sound playback/lifecycle | Implemented, mixer verified | Two independent PCM buses beside MIDI ports; real render cursor/EOF, volume, pause/resume/stop/close. Two active mixer tests pass. Audible gameplay and the absent Monica request remain unverified. |
| 09 | Movies | Demux subset verified; decoder blocked | Bounded PSS/PES parsing fixture passes. No decoder development headers/import libraries available; movie player still uses timing-only behavior. |
| 10 | Save round trip | SDK contract verified | Isolated 416,192-byte game-size SDK round trip, stat/seek/delete and album/bonus names pass. Full game serialization/load acceptance remains open. |
| 11 | Background reads/scheduler | Read repair and contract verified; scheduler partial | Failed physical reads no longer publish size or busy state. Missing/truncated/pending/completed/cancel and sector-padding canary checks pass. Thread/semaphore timing remains unverified. |
| 12 | Remaining subsystem acceptance | Ledger and three active math cases integrated | Box contact/separation and vector normalization cases pass. Rendering equivalence, georama, fishing, ridepod, equipment and map transitions remain open. |

## Subsequent gameplay acceptance: failed

User reports no visible improvement in the completion build. The interactive run
`completion-run-20261011-185022` terminates at `mg_drawprim.cpp:329` in StartImageTransfer:
texture lookup/binding succeeded, then `gfx::CopyTexture` returned false. The log does not include
the rejected rectangles/extents. The new component suites did not exercise this shared menu-copy
path. Combat trace proves A/Cross reaches the action consumer and sets history; `prog_no=-1` is
the normal running sentinel, not proof that the action script is stalled. Attack execution remains
unaccepted. Stale texture 0x10005 warnings and an unnamed DATA.DAT read failure also remain.

The build/component evidence below is retained, but it does not demonstrate a repaired gameplay
loop. Further work should prioritize production menu-transfer and action-execution acceptance,
including actual asset extents, before another broad package dispatch.

## Component validation receipt

All twelve assignments returned; outcomes above distinguish implementation from partial work.
The current changes are local and uncommitted on `port/upstream-first-2026-10-11`, based on
`c2ac1abb`. No game launch, master rewrite or remote publication occurred in this batch.

- Windows RelWithDebInfo game and GTest executables build. CTest: 61 passed, five prerequisite
  skips, zero failed out of 66 registered cases. Five new cases cover mixer and math contracts.
- `ct_completion_tests` passes seven component executables plus all 26 existing compatibility
  cases (including Vulkan capture), eight Python audit/gate fixtures and two ownership fixtures.
  `ct_completion_tests_headless` uses the same seven components and 25 compatibility cases.
  CI now requests these aggregate targets; hosted Linux/macOS/Windows results are pending.
- Model negative control rejects required-storage exhaustion before a null write.
  PAL geometry: c01fuku1 = 14 MDTs, c02fuku4 = 15, ws050 = 1. Maximum fixed geometry storage
  is 10,853 blocks and motion geometry 6,598 against the 39,616-block menu slot. The motion
  asset fixture uses synthetic empty weights; these measurements are not complete character budgets.
- Production SOUND reader decodes 3,101 indexed names, 481,470,146 PCM samples. The duplicate
  name is resolved to its first record, so this is indexed-name coverage, not a unique-entry count.
- Active manifest: 68 compiled native units, 413 override names, 229 collector units;
  37 superseded units, zero compiled/collector leaks; five selected game test files, 25 declared
  cases. Lexical inventory: 5,376 findings, seventeen retained reviewed repairs; findings are not bugs.
- Upstream `ps2/` and `docs/PC.md` are unchanged. Original CT/ct/ChronicleTwo naming is retained.

Logs under `E:/Chronicle-Two/logs/`: `completion-validation-final.log`, `completion-headless.log`,
`completion-ctest.log`, `completion-model-pal.log`, `completion-model-negative.log`,
and `completion-sound-pal.log`. Executable SHA256:
`81F1F783B93A73095B9637E478464E4212CC5F3197065B28033CD2EB3978D516`.

Integration discarded an incorrectly sized motion-packet reservation approach in favor of the
native renderer's existing CPU-geometry path. Synthetic tests were corrected to corrupt the
RIFF length field, distinguish incomplete PES from malformed headers, and reserve a whole disc
sector. The real PAL SOUND terminator points to an empty string inside the index, not its end.
The async fixture identified and now verifies the repaired size-publication failure path.

Next acceptance is one user-controlled run: enter/leave Items, attempt A/Cross attack and observe
HUD/audio, with `DC2_CRASH_TRACE=1` and `DC2_TRACE_COMBAT=1`. Component results do not establish
that the reported gameplay failures are gone. Remaining movie, compiler/link analysis and
subsystem work is documented rather than reported complete.
