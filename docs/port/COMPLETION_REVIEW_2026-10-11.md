# Port completion review and proposed execution plan

Review date: 2026-10-11. Baseline: `port/upstream-first-2026-10-11`, commit
`c2ac1abb662db9a7c61ea3fe28fd3d83cbc69613`, tracking the Jeffiest/ChronicleTwo fork.
The local HEAD and last verified remote-tracking ref agree. This review did not fetch,
push, merge, change implementation, rebuild or launch the game.

Execution was subsequently authorized. The integrated outcomes, test receipt and remaining
boundaries are recorded in [EXECUTION_BATCH_2026-10-11.md](EXECUTION_BATCH_2026-10-11.md).
The remainder of this document records the pre-execution review.

## Recommendation

Keep the implemented port and use the upstream work packages as an acceptance checklist.
Complete the shared host contracts before expanding gameplay coverage. Move WP7.5's
64-bit audit forward beside WP2.8; it is a prerequisite for dependable model loading,
menus and gameplay, rather than a final cleanup step.

The executable demonstrates boot, rendering and substantial game logic. It does not
establish that the native replacements cover every helper, allocation, render state or
asynchronous operation. The missing coverage spans several subsystems; there is no
evidence for one switch or import that will fix all symptoms.

The supplied plan matches the architecture/work-package baseline in `../PC_PORT_PLAN.md`.
Its startup-abort status and some format assumptions are historical. Its delegation,
paths and commit-flow instructions are reference material; implementation remains
pending the user's review of this proposal. Preserve original ChronicleTwo names,
the restored `../PC.md`, and the upstream retail sources.

## What is implemented, and what is accepted

"Present" means an implementation exists. "Accepted" requires evidence for its behavior.
File existence, a successful link or a fresh audit inventory cannot substitute for that evidence.

| Upstream work packages | Current evidence | Remaining acceptance gap |
|---|---|---|
| Phase 0/1: shared host, build, overrides, launch | Kazarov ce459c1 consumed; Windows game/test builds pass; 504 Vulkan pipelines initialize; explicit source ownership | Current Linux/macOS builds and retail byte matching were not rerun. All 333 retail files equal the imported upstream tree; this is not a retail byte-match result. |
| WP2.1–2.2: native math/collision | Native math/frame/collision implementations support visible models and movement | Complete lane/edge-case and collision equivalence coverage is not established. |
| WP2.3–2.4: data | Native DATA.HD4/DAT and loose-file access exist; actual PAL photo-name load validates 294 records | Planned `tools/dc2data` is absent. Some data/extraction test files are not active targets. Latest run also logs a DATA.DAT read failure; its request/extent needs diagnosis before attributing symptoms to it. |
| WP2.5–2.7, 2.9: config, clock, threads, pads, boot | Config cases pass; language/title/costume/prologue and controllable Monica verified; pad edge/language/menu fixtures pass | Save/load round trip, thread/tick semantics and full gameplay input-to-action behavior need acceptance. A/Cross mapping passing does not prove attacks execute. |
| WP2.8 + WP7.5: memory and 64-bit conversion | Typed script stacks, monster arena setup, seven reviewed pointer-array accesses and allocator diagnostics implemented | Shared model storage still fails; ownership/lifetimes, raw allocations, indirect offsets and compiler-assisted pointer/link coverage are incomplete. |
| Phase 3: VU behavior specifications | Six specification documents, disassembler and its tests exist | Rendering implementations still need comparison against those specifications. |
| WP4.1–4.4: frames, textures, 2D, models | Title/world/characters/skin and menu backgrounds render; color/menu capture has real Vulkan fixtures | HUD correctness, texture lifetime, all primitive/state combinations, fixed/motion model ownership and lighting/material equivalence remain partial. |
| WP4.5–4.10: shadows, billboards, water, animation, effects, culling | Native shadow/sprite/texture-animation paths and outline behavior exist | Full package acceptance is unproved. Recording-time photo/effect capture is unsupported; depth capture is separate. Water and every effect path need linked-implementation review. |
| WP5.1–5.4: audio | Shared mixer and game sound/bank paths exist; supplied SOUND format findings were checked against local PAL headers | SOUND streams are silent and use a fixed completion timer. Real PCM lookup/playback/lifecycle is missing; the specific absent Monica voice request is unidentified. |
| WP6.1–6.4: movies | PSS timing scan and player interface exist | Current player has no installed video decoder; timing progression is not movie playback. Native MPEG-2 decode/audio/frame output remains work. |
| WP7.1–7.3: presentation, aspect, options | Window rendering, config and title Options/menu opening work | Full aspect/resize/presentation checks, usable HUD and every Options/menu transition are unaccepted. |
| WP7.4–7.6: broad integration and delivery | Focused regression targets and CI wiring exist | Georama, fishing, ridepod, shops, saving and map transitions lack representative acceptance evidence. |

Last recorded validation: 56 CTest passes, five prerequisite skips, zero failures;
25 native headless compatibility cases (26 with Vulkan capture), plus eight audit/gate
fixtures. These are complementary suites, not a completion percentage. The main GTest
target selects only three game-side source files plus shared Kazarov tests. Several
other test files still reference DC1 types/headers and are excluded from that target.

The lexical inventory contains 5,334 findings and seventeen reviewed repairs. Findings
include replaced/dead code and possible false positives; the number is not a bug count.
The enforced pointer-array pattern is narrower than whole-program 64-bit safety.

## Current failures and how far the evidence reaches

- **Inventory:** latest traced run failed in `mgCVisualMDT::CreateFace`, through
  `DataAssignMotionMDT` and `CMenuItemInfo::ModelReadEndCheck`. Model arena request 37
  failed at 39,603/39,616 blocks. Packet builders write at the arena top before reserving
  storage. The menu partitions have a separate owner from primary-character storage.
  Increasing the character budget did not repair this owner or the unsafe writer contract.
- **Attacks:** user reports Xbox A/Cross fails while movement and lock-on work. Physical
  mapping and game-action edge fixtures pass. The first rejected combat-state/script
  transition has not been identified; changing bindings speculatively is unwarranted.
- **HUD:** user reports it is incorrect. Earlier HUD packet-pointer helpers were repaired,
  but that does not cover every HUD draw. Latest logs contain invalid texture-handle
  warnings; a causal connection to the reported HUD defect is not yet established.
- **Media states:** silent sound streams and timing-only movies can report progress or
  completion without real playback. Those semantics need review wherever scripts wait
  on them; they are not proved to cause the current attack failure.

## Proposed work packages, in order

### A. Establish the coverage and enforcement baseline

Scope: generated host translation units, actual source lists/overrides, linker symbols,
`scripts/port`, CMake gates and the test manifest. Reuse the local DC1 compiler-audit and
guarded-owner design already reviewed at `706ce00e`; adapt it to DC2's layouts and aliases.

Produce a manifest distinguishing active native implementations, retained CPU logic,
intentional hardware no-ops, aborts and behavior placeholders. Separate serialized
asset/save records from expanded runtime objects. Triage the menu/model, HUD and combat
paths first, then extend the same checks to other subsystems. Unknown virtual/dynamic
reachability stays unresolved. Do not suppress a finding merely because a similarly
named override exists.

Gate: link/source evidence for ownership; compiler-assisted checks for pointer narrowing
and manual offsets in the selected scope; fixed-width record assertions; a manifest of
tests actually built/run. Deliberately reintroducing each covered hazard must fail its
gate. Compiler tooling itself may need a separate bounded follow-up.

### B. Repair the shared model-memory contract

Scope: `mg_memory.cpp`, fixed and motion visual/dataset loading, menu model partition
owners and their native/generated-host adapters. Use actual host type/count/alignment
requirements, reserve before writing, validate the entire parent partition plan after
alignment, and define borrowed aliases and reset/reload lifetimes. Guard backing owners;
guards do not replace checks between adjacent logical slices. Review persistent skin
weights/indices and their release/rebind behavior together.

Gate: fixed/motion model component loads, insufficient/exact-capacity boundaries,
non-overlapping partitions, malformed counts and load/unload/reload. Use representative
local PAL models privately, not just fabricated allocator calls. No failed required
allocation may reach a constructor, writer or face dereference. No unexplained multiplier
is accepted as the repair.

### C. Complete the shared HUD and menu drawing path

Scope: `mg_drawprim`, `prespr`, texture registry, draw-state translation, menu primitives,
render targets and capture. Review every helper used by status boards, item icons,
cursors, text and menu forms. Trace texture creation/destruction and queued-draw lifetime;
classify every warning. Review blend/alpha, scissor, depth, UV and logical 512x480 mapping.

Gate: deterministic textured HUD/menu component scenes with pixel checks for blending,
clipping, state changes and texture reuse; resize/4:3/widescreen checks. Record supported
and unsupported primitive/capture paths explicitly. Items navigation additionally waits
for B. Active photo/depth/effects capture is a separate dependent deliverable.

### D. Verify controller-to-combat behavior and runtime state

Scope: real pad update -> language-adjusted game action -> camera/menu/control locks ->
combat script/opcode -> animation/hit state. Preserve the verified Xbox mapping. Compare
native state transitions and argument evaluation order with the decompiled PAL logic;
review related script operators as a family. Check input consumption across menu close,
first-person exit, cutscene end and pause.

Gate: a production-path component fixture for attack press/hold/release, permitted and
blocked states, menu resume and script evaluation. If gameplay evidence is required to
locate the first divergence, use one bounded environment-gated passive trace during a
user-controlled test; report the exact blocker before making a fix. Do not claim a cause
from the existence of a camera lock alone.

### E. Make asynchronous/media operations truthful

Scope: sound stream state, background reads, cooperative threads/semaphores and movie
completion. Audio WP5.3's old streamed-VAG assumption is superseded: local PAL validation
found 3,101 mono 48 kHz 16-bit PCM RIFF/WAVE entries. Implement bounded chunk walking,
first-match duplicate lookup and real play/pause/stop/seek/EOF through the shared mixer.
Trace the requested voice path before promising Monica's dialogue. Add native MPEG-2
movie decode and audio/frame synchronization as the separate WP6 deliverable.

Gate: sample-derived duration/state tests, short reads/cancellation, thread/pause/shutdown
behavior, one known audible stream, and decoded movie frames with timestamps. A no-op
is acceptable only for hardware work whose removal preserves the game-visible contract.

### F. Expand subsystem acceptance and delivery

Reconcile each remaining upstream WP against its behavioral specifications. Cover saves,
equipment/items, map transition/reentry, collision, shadows, billboards, water, texture
animation, photo/effects, georama, fishing and ridepod by shared component contracts plus
representative user checkpoints. Validate Linux/macOS and retail matching when their
toolchains are available. Generic host improvements belong in Kazarov with both consumers
validated before a new pin; keep DC2-specific glue here.

Gate: every claimed package has implementation, sibling-call review, tests and stated
limits. Asset/record checks and unsupported-path checks run in normal builds/CI. A
representative checkpoint supplements these checks; an exhaustive scene-by-scene repair
campaign is not the implementation strategy.

## First batch proposed for approval

Start A narrowly alongside B/C/D. The integrator owns the coverage manifest, CMake,
shared headers and one central build. Three independent assignments cover model-memory
ownership, HUD/texture behavior, and combat-state diagnosis. Coordinate packet/render
files explicitly before edits; analysis can overlap, edits to the same file cannot.

Use Luna with concise context for inventory, fixture and narrow repair work. Keep
cross-subsystem architecture and integration with the primary agent. Each assignment:
one deliverable, no nested agents, at most 10–12 tool calls or 15 minutes, whichever
comes first; at most two repair attempts. Escalate a blocker promptly. At the limit,
return evidence and remaining work; no automatic continuation. These limits are not
a promise that a complete subsystem fits in one assignment.

Do not run repeated full builds per agent. Integrate coherent changes centrally and run
the affected checks once, repeating only for changes/failures. Then request one short
manual checkpoint: Options; Monica movement/lock-on/attack; menu -> Items -> return and
attack again. Exercise reload with repeated menu opening after the component tests pass.
The user drives the game; no automated gameplay scripts.

Review decision: approve A–D as the first batch, revise its scope, or defer implementation.
Audio, movies and the remaining subsystem acceptance remain explicit later packages.

## Evidence

- `E:/Chronicle-Two/PORT_STATE.md` and `E:/Chronicle-Two/HANDOFF.md`: canonical history.
- `E:/Chronicle-Two/logs/upstream-first-run-20261011-175430-{out,err}.log`: latest run.
- `MODEL_MEMORY_CONTRACT.md`, `COMPATIBILITY_AUDIT.md`, `INPUT_CAPTURE_COVERAGE.md`:
  owner/ABI measurements and existing contract coverage.
- `../PC_PORT_PLAN.md`: upstream WP briefs and acceptance baseline.
- `../../port/src/tests/CMakeLists.txt`, `../../port/cmake/CompatibilityTargets.cmake`:
  actual active test lists; `NativeOwnership.cmake` and `HostOverrides.cmake`: source ownership.
- `../../port/src/movie.cpp`, `movie_port.hpp`, `sound.cpp`: present media limitations.
- `E:/Chronicle-Two/reports/PORT_REEVALUATION_2026-10-11.md` and
  `SYSTEMATIC_PORT_PLAN_2026-10-11.md`: previous DC1/Dylan review and verified SOUND findings.
