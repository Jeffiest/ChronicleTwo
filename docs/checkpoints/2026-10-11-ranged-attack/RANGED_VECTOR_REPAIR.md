User acceptance update: Monica ranged attacks CONFIRMED WORKING. A later cutscene closes at the explicit recording/frame color-capture guard in mgStoreImage; this is the next shared subsystem repair.

# Ranged attack vector contract repair

Current status: DISTANCE_REPAIR_READY_FOR_GAMEPLAY. **Ranged distance repair validated; gameplay pending**: game/GTest/headless completion build exit 0; CTest 364 passed, five skipped, zero failed / 369. Production effect distance callbacks survive VM dispatch, wait/resume and output checks x128; vector rewrite rejects drift, repeat application and new undersized writer buffers. Twelve effect/event callbacks now have thirteen four-float buffers. SHA256 298BAACD94B1462593494CDA5A89AF2262A048E28FE32235ED35974902FF26E0. Logs ranged-distance-build.log and ranged-distance-ctest.log. Previous C3A0 user run failed; no ranged gameplay success claimed.

First repair status: GAMEPLAY_FAILED. Build pass=True, CTest pass=True; 364 passed, 5 skipped. Game SHA256 C3A0B72C78D8CE653730567B872DA26E741DA3136AAB1DDC6ED0727D415EF3A3.

User confirms normal attacks work on the combined build. Monica ranged attack closed the game; original logs combined-playtest-20261011-204547-{out,err}.log contain access violation RVA 0x22a25e. Symbolized before overwriting the executable in logs/ranged-vector-crash-symbols.log: mgCFrame::GetWorldPosition0 -> CCharacter2::GetEntryObjectPos -> _SCN_GET_CHR_ENTOBJ_POS -> effect VM. The faulting instruction is MOVAPS storing 16 bytes through a float pointer. The caller declares float pos[3], which is both too short and not guaranteed 16-byte alignment.

Native GetWorldPosition0 now uses memcpy for all four components, retaining w and supporting ordinary float alignment. Four sibling scene effect position/rotation callbacks now allocate aligned four-float buffers via a count-checked generated-host rewrite: _SCN_GET_CHR_POS, _SCN_GET_CHR_ROT, _SCN_GET_ENTRY_OBJ_POS, _SCN_GET_CHR_ENTOBJ_POS. Scalar three-float distance buffers remain unchanged. No ps2 or submodule edits, Git publication or automated gameplay.

New asset-free production regression exercises a deliberately unaligned four-float output with canaries, CCharacter2 entry frames and the actual effect callback body, with only scene-character lookup substituted. Its output references, missing-character and invalid-argc controls are checked. The rewrite test rejects source drift and double application. Both are registered with the ordinary runtime/GTest/completion targets.

Before evidence is the actual gameplay crash and disassembly. An isolated old-code test compiled and linked but its runner stalled and was stopped; it is NOT a successful isolated failing-before proof. The repaired regression/whole suite results are recorded above. Per user spending limits, no further baseline debugging is scheduled.

Files: port/src/mg_frame.cpp; scripts/port/host_vector_contracts.py; port/src/tests/wp13_character_vectors_standalone.cpp; port/CMakeLists.txt; port/cmake/RuntimeContracts.cmake; compatibility inventory/ledger. Logs ranged-vector-build.log and ranged-vector-ctest.log. Native declaration include and inventory refresh repaired build setup; current-source verification status above.

Monica ranged attack still needs one user-controlled acceptance run. Other float-vector buffers beyond the four reviewed callbacks remain audit work; no whole-game vector safety claim.

## Follow-up distance overflow

**Ranged acceptance FAILED on C3A0B72C**: user reports ranged attack still broken; PID 44024 exited at RVA 0x251960 in CRunScript::exe after effect dispatch. Disassembly proves _DIST_VECTOR and _DIST_VECTOR2 write homogeneous w beyond float[3] into saved RSI; VM then dereferences the corrupted interpreter pointer. Extending count-checked vector contracts to twelve effect/event callbacks (thirteen buffers), plus production distance callbacks through VM with wait/resume x128. Build/test acceptance pending; no gameplay success claimed.

The preceding claim that distance buffers could remain three floats was incorrect: GetStackVector always writes vector[3] = 1.0f. This overwrites saved RSI in both compiled distance callbacks. Evidence: logs/ranged-distance-crash-symbols.log and ranged-distance-before-disassembly.log. The new enforcement scans effect/event external callback bodies for three-float buffers passed to the reviewed four-component APIs and rejects a newly introduced undersized buffer. This remains scoped lexical enforcement, not whole-program pointer analysis.

The event siblings now widened are _EOH_GET_POS, _EOH_GET_ROT, _MINIMAP_DOOR_ENABLE, _POST_TREASURE_BOX and _RANDOM_CIRCLE_GET_POS. Effect siblings added after the first four are _DIST_VECTOR, _DIST_VECTOR2 (two buffers) and _GET_EOH_POS. CRandomCircle::GetPosition and CEohMother position/rotation write four components. No ps2 source or submodule was edited. Regression is asset-free and runs the actual extracted distance callbacks through production CRunScript; full Monica projectile rendering/collision remains manual acceptance.
