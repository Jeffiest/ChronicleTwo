# Dark Chronicle native port reevaluation — 2026-10-11

## Decision

Pause scene-by-scene symptom fixes and complete a bounded host-compatibility audit first. The evidence points to incomplete conversion of the game's 32-bit runtime assumptions, especially allocation sizes and arena lifetimes. Hardware-dependent helpers and floating-point edge cases form additional, separate categories. There is no evidence here that replacing the whole renderer or rewriting the port would solve these problems.

The full opening cutscene now completes according to the user's run; battle does not start. Preserve that working baseline and the three uncommitted host fixes. This review changes documentation only and does not merge upstream, change game code, or drive another playthrough.

## GitHub update check

Fresh fetches on October 11 used isolated review refs in `E:\Chronicle-Two\win\ChronicleTwo`. Parent fetches were repeated with submodule recursion disabled and completed successfully. The branch remains `upsync-winport` at `3139f445f3e55210780d409f673d41a3f9bf31d2`.

| Source | Reviewed tip | Findings and disposition |
|---|---|---|
| [ChronicleTwo master](https://github.com/TheMoonPeople/ChronicleTwo/commit/4ee9003031d9379aebdb7dba6094ce000370d6f1) | `4ee90030` | Five commits beyond shared base `872e1250`: PC build/configuration, Kazarov pin, standard C++ corrections, runtime/hardware stubs, documentation. Its documented Phase 1 smoke test stops at the `mgInit` stub. Useful build/CI and source corrections, but it does not contain a completed gameplay implementation to replace ours. Review changes individually. |
| [Dylan's fork](https://github.com/carrdylan52/ChronicleTwo/tree/port-bootstrap) | `port-bootstrap`: `56555a37`; `master`: `6a24d4f2`; older `port`: `9a9275c3` | The relevant new port work is on `port-bootstrap`, not the fork's master. It supplies a boot trace, hardware inventory, layout audit, repair decisions and guards. It remains an early bootstrap with unreachable guarded paths, not a verified battle-ready port. Adapt its auditing approach and review its repairs together by category. |
| [DC1 / Chronicle master](https://github.com/TheMoonPeople/Chronicle/commit/706ce00e84c8ef6c47213d633170d6ec6aca74ed) | `706ce00e` | Latest tip updates Kazarov; preceding commits move shared platform code into Kazarov and pass game-specific configuration to it. Its existing arena and pointer-audit design is the important gameplay lesson. |
| [Kazarov main](https://github.com/TheMoonPeople/Kazarov/commit/ce459c1) | `ce459c1` | One commit beyond checked-out `0d72aa1f`: additional SDK families and stubs needed by DC2. Relevant infrastructure, not a fix for host object sizes or scene lifetimes. Review interfaces before updating the pin. |

Upstream's five commits are `b0838fe4`, `d4546805`, `66a648d9`, `c21af4b7`, `4ee90030`. Some commit timestamps display October 12 in the author's +11 timezone; the check occurred October 11 locally. Review refs: `review-upstream/master`, `review-dylan/*`, `review-dc1/master`, and the submodule's `review-upstream/main`.

## What is fundamentally incomplete

### 1. Host memory conversion is partial

PS2 pointer-bearing structures expand on a 64-bit host. Allocation counts, byte offsets, pointer casts, serialized records and arena budgets must each be classified; changing one does not fix the others.

Concrete evidence in our tree:

- `port/src/mg_memory.cpp` already grows a placement-new allocation to the host object size. This is a useful DC1 technique we have adopted, but it sees only the latest allocation and cannot size raw cast arrays. If growth fails it prints an overflow and still returns the block, allowing the constructor to overwrite following storage. `Alloc`/`stAlloc` return null on capacity failure; callers can crash later without identifying the original owner/lifetime failure.
- `SetEventScript` (`ps2/src/event.cpp:112`) and `SetMonsterScript` (`ps2/src/runscript_opcodes.cpp:4678`) allocate 64 and 384 sixteen-byte blocks for 128 value entries and 512 call entries. On this host those arrays require 128 and 768 blocks. Live event-stack ranges overlap; only four values and two call entries were active in that captured wait, so corruption there was not established.
- `DeleteExtMotion` used a PS2 object offset and four-byte image-pointer stride. The current host override fixes this. Dylan's source-repair ledger independently identifies the same site. This is a category we can find before reaching each affected scene.
- `CScene::ClearStack` is another raw-offset site identified by Dylan, but our tree already has a typed host override. Do not count it as a newly discovered missing fix or assume it explains the current battle failure.
- Our host configuration disables all `STATIC_ASSERT` checks by default. This removes checks for fixed-format asset records along with checks that legitimately fail for expanded runtime objects. Those two cases need different treatment.

The latest battle log ends with `stack over 38/0`, `64/0`, `384/0`. Its native trace resolves to `SetMonsterScript` through `CMonsterMan::SetActiveMonster`, `AutoSetMonster` and the event interpreter. The zero capacity is evidence of an invalid or uninitialized arena state; it does not establish why that state exists. `LoadMonsterFile` normally assigns buffers to the manager's per-monster arenas (`ps2/src/dng_event.cpp`, near 2360). Trace setup, reset and ownership across the cutscene transition before changing capacities. Host-sized script stacks are necessary independently, but will not make a zero-capacity arena valid.

### 2. Hardware replacement coverage needs an inventory

The HUD crash came from a packet-writing sprite helper left reachable after the parent drawing path was converted to CPU vertices. Replacing a top-level hardware routine does not guarantee every helper and alternate route uses the host implementation. Dylan's inventory tracks register, packet, scratchpad, assembly, SDK and thread dependencies; adopt that coverage model against our linked implementation. An intentional unsupported path must stop with its identity, rather than silently succeed or access a stale packet.

### 3. Host arithmetic and C++ behavior need explicit review

The running-scene stall was a zero-duration spline endpoint producing nonfinite speed. It is separate from memory corruption. Our captured-path regression now covers both character paths and the camera, including a negative control that reproduces the old failure. Dylan also records script argument-evaluation-order repairs. Review arithmetic edge cases and ambiguous stack-increment expressions as categories, preserving retail behavior with small native implementations and focused fixtures.

## What DC1 did that matters

[DC1's port documentation](https://github.com/TheMoonPeople/Chronicle/blob/706ce00e/docs/PC.md), `port/src/arena.hpp`, `dataalloc2_1.cpp`, and `.github/workflows/pc.yml` describe a combination of safeguards:

- Each backing arena has host headroom (currently four times retail), a guard page and a diagnostic abort on overflow. Debug checks ensure high addresses expose pointer truncation. Placement-new runs expand to host object sizes; cast allocations are sized explicitly with typed helpers.
- A libclang-based pointer-round-trip audit follows narrowed addresses and considers linked reachability. CI rejects stale audit output or reachable unresolved round trips.
- Disk records are decoded at the boundary rather than treated as expanded host objects. Game-specific native replacements retain the original logic around the width correction.

We already copied parts of the placement-growth and platform strategy. The missing benefit is the combination of complete allocation coverage, guarded ownership, record boundaries and automated checks. DC1's `CDataAlloc2` cannot simply replace DC2's `mgCMemory`: DC2's scene stacks intentionally carve and alias storage, and those lifetimes must remain correct. A uniform arena multiplier is headroom, not proof of correct sizing.

Our earlier state-file conclusion that DC1's only useful differences were Windows support and soundtrack options was too broad. That comparison covered audio/platform slices; DC1's memory strategy and audits remain directly relevant.

## What to adopt from Dylan, with limits

[The layout ledger](https://github.com/carrdylan52/ChronicleTwo/blob/56555a37/docs/port/LAYOUTS.md) separates repaired sites from guarded but unreached ones, retains assertions for fixed-width records, and inventories allocations, pointer truncations and raw offsets. Its generated tables report 53 repaired allocation rows, 106 guarded-unreached cast rows, and 23 offset rows. These are detector results, not proof that every gameplay path is safe.

The ledger explicitly says arena budgets stay PS2-sized and its allocation detector has limited expression coverage. Both script-stack allocation sites above are still unchanged on that branch. Importing a green audit without extending coverage would preserve known defects. Its source changes inside `ps2/` also need to be expressed through our native override/generated-host workflow while the PS2 byte check is unavailable.

## Replacement workflow and acceptance gates

1. **Establish a checked baseline.** Keep the current three fixes together with their evidence. Record the exact executable, source revision and manual milestone reached. Separate native unit failures, unsupported-path stops, arena invariant failures and gameplay waits in logs. Do not infer success from a visible frame or a surviving process.
2. **Audit and repair host memory by subsystem.** Start with script/event/monster setup, then scene and character storage. Inventory allocation call sites including casts and counts held in variables; use host `sizeof(T) * count`, explicit units and alignment. Track backing arenas and intentional slices, initialization, reset/lock transitions, capacity and high-water marks. Required allocation failures stop at the owner with context; APIs that legitimately allow failure remain explicit. Add guarded backing allocations and checks between logical slices where practical. Verify non-overlapping script arrays, adequate capacities and valid monster buffers before executing a script.
3. **Audit address and record boundaries.** Adapt Dylan's layout ledger and DC1's truncation/reachability checks to this branch and Windows ABI. Classify runtime objects versus file/save formats, enable the latter's layout assertions, and require an explicit decision for reachable narrowing and manual member strides. Repair families of sites in reviewable batches. A green detector is insufficient unless known script-stack cases are covered.
4. **Audit hardware and numerical coverage.** Map each reachable retail hardware helper to a native replacement or a named stop. Include sprite helpers and alternate rendering paths. Add targeted fixtures for known numeric/script-order hazards; keep the existing spline negative control. Broader sanitizer runs can supplement the audit where the host build supports them; they do not replace checks inside custom arenas.
5. **Resume milestone validation.** After those gates pass, use a user-controlled complete cutscene, skip cleanup, first monster spawn, controllable combat, then exit/reenter a map. Inspect invariant reports at transitions. Compare selected behavior against retail PAL, especially scene completion, collision and animation timing. Fix the first violated contract and audit sibling sites; avoid diagnosing a later crash in isolation.

The next implementation slice should therefore be **script allocation and scene/monster arena lifecycle**, supported by a reusable audit and focused native checks. The battle failure is the first acceptance checkpoint for that foundation work. Rendering refinements, movies, and broad platform refactors can wait.

## Review limits and preserved state

This is a source/documentation comparison plus analysis of existing logs and captured debugger evidence. No new gameplay verification or PS2 byte-identical build was performed. Latest GitHub objects were fetched; none were merged. Existing changes remain local to the Windows clone: `port/src/mg_drawprim.cpp`, `port/src/character.cpp`, new `port/src/sceneseq.cpp`. The review itself adds this report and updates the root state and handoff. The exact cause of the monster arena's zero capacity remains open.
