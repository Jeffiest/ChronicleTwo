# Upstream integration, 2026-10-11

Target: `TheMoonPeople/ChronicleTwo:master`, fetched as `upstream/master` at
`4ee9003031d9379aebdb7dba6094ce000370d6f1`. Shared base is
`872e125032b0cdc4e95f7020ea028ff5dda38ca9`; the starting port has 401 commits
and upstream has five commits beyond that base.

## Plan

1. Commit the current host compatibility work and archive canonical state/handoff.
2. Create `port/master-sync-2026-10-11` from that checkpoint and merge upstream.
3. Review build/configuration, retail corrections and overlapping native implementations separately.
   Preserve working port behavior, checked generated-source transformations and regression coverage.
   Import upstream fixes without adding new retail logic; retail matching cannot run here.
4. Resolve conflicts explicitly, inspect submodule/API compatibility and adapt CI to actual targets.
5. Build the game and compatibility suite centrally; run the local PAL photo loader and source checks.
6. Commit and publish the integration branch, verify both parents are ancestors and the remote hash matches.

No gameplay launch is needed for this integration. The existing Items model-memory fault,
combat attack issue and recording-time capture limits remain separate open issues.

The user's fork `master` is a different divergent history (407 local / 332 fork commits at
preparation). Upstream integration alone does not promise that a pull request into that branch
will be conflict-free.

## Results

Compatibility checkpoint: `563cb177`. Integration branch: `port/master-sync-2026-10-11`.
Merge commit `bb0eaf491df3d1d051f451eb05ccec4954066a98` has checkpoint `563cb177` and upstream
`4ee90030` as parents; it does not rewrite either main branch.

### Conflict decisions

- Kept implemented rendering, input, audio, script storage, arena diagnostics and menu loading.
  Incoming bootstrap abort stubs, duplicate model/vector routines and duplicate linker globals
  were superseded by those implementations. No new aborting overrides replace working retail bodies.
- Preserved current CMake host/source transformations, explicit compatible test sources and Windows
  dependency scripts. Adopted upstream Linux/macOS routes, PS2-only post-build hooks, runtime-data
  ignores and CI, adapted to `DC_BUILD_TESTS`, `DC_TEST_TIMEOUT` and `darkchronicle_tests`.
- Pinned Kazarov to `ce459c1f64dc0953f98da8b62fbd2806a3f7e76d`. Kept the game's scheduler instead
  of new shared thread-query placeholders; cache operations and process exit retain the shared behavior.
- Shared upstream's identity helper with the game's existing `DC_` settings prefix and `chronicle-two`
  save directory. Incoming identity values would have moved existing saves.
- Existing retail PORT guards already cover the overlapping standard-C++ corrections. The only retail
  changes relative to the checkpoint are upstream's exact typedef corrections in `memcard.cpp` and
  `mg_visual.cpp`. Updated checked Windows rewrite counts to match; no new retail logic was authored.
- Isolated the PS2 SDK `CreateThread` COFF symbol from Win32's imported symbol through a checked mirrored
  header rewrite. Declared mirrored headers as build byproducts so changes regenerate consumers in the
  same build. The SDK object exports `dc_CreateThread`; GoogleTest uses Win32 `CreateThread`.
- Added `dc_compatibility_tests_headless` for CI without a Vulkan device. Repaired existing config tests'
  Unix-only temporary path and stale input-default expectations against the preexisting shared defaults.
  Current instructions replace incoming Phase 1 abort documentation; the original plan remains labeled
  as the upstream baseline.

### Validation

- Windows RelWithDebInfo: `darkchronicle`, `darkchronicle_tests`, full compatibility suite (26 cases)
  and headless suite (25 cases) build and pass. Source audit, eight audit/gate fixtures and checked
  script/menu/photo rewrites pass. Inventory remains 5,336 findings with seventeen reviewed repairs.
- CTest: 56 passed, five skipped, zero failed. Three font cases lack a supplied font; two mouse-capture
  cases lack the required desktop surface in this execution environment.
- Production local PAL photo loader: all 294 names and public lookups pass without graphics/gameplay.
- JSON presets, workflow YAML and conflict/whitespace checks pass. The source SDK object exports the
  isolated symbol. Normal builds retain the generated/native pointer-array enforcement gate.
- Executable SHA256: `C940BDEEE582AEC1CE13CABE4D043026B85B591966D75036B5FDAFD143D93024`.
- Evidence: workspace `logs/master-sync-{build,gtest-build,ctest,local-pal}.log`.

Linux/macOS execution, hosted CI and retail byte matching were not run here. No gameplay launch or
automated gameplay occurred. Items, attacks and recording-time photo/effect capture remain open.

Published to `Jeffiest/ChronicleTwo:port/master-sync-2026-10-11`; the remote merge hash matched
`bb0eaf49`, both ancestor checks passed and upstream still pointed to `4ee90030` after publication.
The separate fork-master divergence still requires its own integration before a conflict-free merge
into that branch can be claimed.
