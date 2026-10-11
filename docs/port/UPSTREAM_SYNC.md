# Upstream-first integration, 2026-10-11

Branch: `port/upstream-first-2026-10-11`, based on fork master `117d29c8`.
Upstream source: `TheMoonPeople/ChronicleTwo:master` at
`4ee9003031d9379aebdb7dba6094ce000370d6f1`.
Recovery checkpoint: `port/master-sync-2026-10-11` at `d2cc2df2`.

The previous merge favored the existing port too broadly. This revision restores
upstream's source tree and build framework, then adds the required native support.
It retains fork-master ancestry and does not rewrite master.

## Source ownership

- All 333 tracked files under `ps2/` match upstream. No host guards or other authored
  changes remain in that tree. The build prepares a source mirror and applies the
  checked `scripts/port/upstream_host_fixups.patch` only to generated files.
- All 24 previously omitted upstream files are restored: four stub headers and
  twenty source units. None is deleted from the upstream tree.
- Upstream's `ct_host`, `ct_port`, `ct_ps2`, header-fixup mirror, SDK discovery,
  portable link branches and test option form the build framework. Native ownership,
  host overrides, compatibility targets and DLL packaging are explicit CMake modules.
- Upstream `runtime.cpp` is used directly, including retail RNG, saturating conversion
  helpers, array construction, assertion and runtime entry points. Redundant native
  definitions are removed. Upstream camera and compatible declaration headers are
  restored. The upstream quad forwarders now replace their former native duplicates.
- Nineteen restored bootstrap units remain present but are excluded from compilation
  and override collection. `port/cmake/NativeOwnership.cmake` names each actual owner:
  retained retail C++ or an implemented native replacement. Compiling the aborting
  bootstrap definitions would otherwise strip working retail functions. There is no
  duplicate-symbol suppression in the Windows link.
- Game-specific rendering, scheduler, SDK, controller, config and storage implementations
  remain native extensions. The SDK ownership list explicitly prevents duplicate shared
  definitions. Original upstream identity and naming are restored: `chronicletwo`,
  `DC2_*` runtime environment variables, and `CT_*` CMake options/targets.

## Enforcement and validation

The override collector accepts explicit source exclusions. Normal builds still check
all override signatures, reviewed generated transformations, inventory freshness and
unsafe byte-stride pointer arrays. The inventory was regenerated for upstream line
locations: 5,334 findings, preserving all seventeen reviewed dispositions. One villager
fallback site required an explicit mapping because the lexical function heuristic
changed its label; its source expression and tested native owner are unchanged.

Windows RelWithDebInfo game and GoogleTest executables build. CTest passes 56 cases
with five prerequisite skips and zero failures. All 26 full and 25 headless native
compatibility cases pass, along with eight Python audit/gate fixtures and the checked
script/menu/photo transformations. The actual PAL loader verifies all 294 stored names.
Evidence: `E:/Chronicle-Two/logs/redo-{configure,build,compatibility,ctest,local-pal}.log`.
The initial redo executable SHA256 was `5BC5CE2BD9C26CE8C877689635052C5D8FC2FEC1368887270B5976ECD4AB5798`.
The full and headless compatibility targets use `ct_` names consistently in CI and
documented commands. `CT_BUILD_TESTS` enables GoogleTest; `CT_TEST_TIMEOUT` controls
the per-case timeout, including upstream CI's `-DCT_TEST_TIMEOUT=60` setting.

`docs/PC.md`, `port/src/identity.cpp` and the test entry point match upstream exactly.
The naming review examined removed/replaced upstream lines first; remaining deletions
replace hardware/bootstrap behavior and leave upstream's general names intact. Current
implementation status belongs in this integration report and the workspace state files;
the original PC guide is preserved verbatim. Naming validation: Windows game/GTest build,
25 headless compatibility cases, eight Python fixtures and CTest (56 passed, five
prerequisite skips) pass; `CT_TEST_TIMEOUT=60` is used in the configured CTest cases.
Logs: `naming-{configure,build,ctest}.log`. Current executable SHA256:
`795BBDC25DE32F4E794C2C1CF66553FCD4218EF7EEDAAC78ECDE4F648E5106A8`.

Items model-memory failure, combat attacks, recording-time capture and voice playback
remain open. Integration checks do not establish gameplay correctness. No game was
launched for this revision; Linux/macOS and retail matching remain unverified.
