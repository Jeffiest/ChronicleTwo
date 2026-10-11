# Port compatibility source audit

`COMPATIBILITY_INVENTORY.md` records lexical findings from the retail `ps2/` tree and the native `port/` tree. `compatibility_decisions.tsv` is a separate human ledger. Every newly detected row starts `open`; updating generated files never marks a site repaired or safe.

Run from the `win/ChronicleTwo` repository root:

```powershell
python scripts/port/compatibility_audit.py --check
```

To refresh after reviewing a source change:

```powershell
python scripts/port/compatibility_audit.py --write
```

Review both generated inventory and decision-ledger diffs. `--check` fails when generated output is stale, when a finding lacks a ledger row, or when a ledger row no longer maps to a finding. An explicit ledger status and rationale are review-maintained. The normal game build runs this check and the script-builder coverage check before compiling game objects.

The first repair batch covers all eight value/call allocations in the event, action, effect and monster script builders. `host_script_storage.py` rewrites only generated host copies, checks the expected sites and preserves private opcode tables. `script_storage.hpp` uses actual host element sizes and stops required allocation failures before script execution. The already-typed event-argument builder is checked separately. `LoadMonsterFile(int,int)` now initializes monster arenas through the typed `memory[]` member. These nine source findings have repair evidence in the ledger; other findings remain open.

The pointer-array scan adds seven sites, all with reviewed repairs: existing typed character-motion and scene-stack overrides, both common-menu filename accesses, the generated Options minimap access, and both typed villager-output paths. The photo-name record allocation now has checked count/arithmetic/required-allocation setup and a persistent pool sized for the measured record-width delta. The ledger contains seventeen repaired findings in total; this is reviewed coverage, not a whole-program safety certificate.

Normal builds also run `check_host_pointer_arrays.py` on the actual generated game translation units and native game source. A matching unsafe expression fails this gate even after refreshing the inventory. The gate depends on generation, so a source override must actually remove the retail body. Four fixtures cover generated/native regressions, typed indexing, excluded test controls and an empty generation tree. This closes one reviewed lexical pattern; it does not cover all pointer arithmetic, macros, alternate cast spellings, shared dependencies or mirrored headers, and it is not compiler/reachability analysis.

Run the native regressions with the configured build:

```powershell
cmake --build E:/Chronicle-Two/win/build-rel --target ct_compatibility_tests
```

On a runner without a Vulkan device, use `ct_compatibility_tests_headless`. It runs the
same source checks and 25 native cases, skipping only the color/menu capture fixture.

This target runs eight audit/gate fixtures and 26 native cases: script buffer separation and failures, arena bounds/growth/reset/alignment/owner diagnostics, all 24 monster buffers, controller edges and A/B/R2 action bindings, menu mode/language bindings, real Vulkan color/menu capture, common multi-file menu loading with the real allocator, Options choices/conditions, villager schedule/output pointers and photo-name ownership/sizing/required failures. The Options fixture extracts unchanged method bodies from the production generated host unit, preserving its private form guard. Fixtures use minimal initialization or file callbacks where permitted; a callback fixture does not verify the filesystem backend. Assertions stay enabled in optimized builds. Shared capture coverage and remaining limits are in `INPUT_CAPTURE_COVERAGE.md`.

Optional component check with local PAL assets, without gameplay or graphics:

```powershell
E:/Chronicle-Two/win/build-rel/ct_photo_name_storage_test.exe local-pal E:/Chronicle-Two/win/ChronicleTwo/rom/pal/extracted/iso
```

This executes the production photo-name loader and validates all 294 names and public lookups against persistent host storage. It does not redistribute assets. The unrelated Items model-capacity/write-before-reserve failure remains open; see `MODEL_MEMORY_CONTRACT.md`.

## What the inventory reports

- Casted raw arena allocation expressions containing `Alloc` or `stAlloc`, including C-style and `reinterpret_cast` script arrays.
- Narrowing casts to common fixed-width or native integer types when the expression also resembles a pointer/address conversion.
- Byte-pointer arithmetic around `this` and common byte aliases, for manual offsets into an object.
- Pointer-to-pointer casts over byte arithmetic, including filename and menu-button traversal through a four-byte offset. The inventory scan covers both retail/native source trees; the narrow generated/native enforcement gate is separate.
- Bounded hardware and platform token families: GS registers, DMA/GIF/VIF, scratchpad, inline assembly/MIPS intrinsics, and PS2 SDK or thread APIs.
- For retail findings, a candidate native implementation file when a `PORT_OVERRIDE` with the matching function name exists. The inventory keeps original `retail/...` rows and `host-source/...` rows separately. A candidate is not proof that the override is linked or behaviorally equivalent.

All findings are evidence for review, not proof of impact. Existing baseline rows remain `open` until an explicit disposition and rationale are recorded. Finding a host override does not close the original retail row; generated-host transformations and allocation rewrites require reviewed evidence.

## Detector limits

This is a deterministic line-oriented Python scanner, not a C++ parser, compiler analysis, symbol-link analysis, or call-graph/reachability analysis. It does not calculate object sizes or allocation adequacy. It can miss multiline expressions, aliases hidden behind helper macros, allocations without a pointer cast on the same line, pointer narrowing through intermediate variables, offsets built from integer arithmetic or struct copies, hardware access hidden behind wrappers, and dependencies in generated or external code. Its token categories intentionally favor bounded, explainable matches over exhaustive hardware proof. Function ownership is heuristic, and override matching is by function name; overloads and link selection need human review. The inventory does not establish that an unresolved retail body is reachable, that an override is effective, or that a site is repaired.

The audit is a drift detector and review queue. A clean `--check` means the tracked inventory and disposition rows match the current lexical scan; it does not certify host compatibility.

Runtime safeguards preserve optional allocation failures and the retail reserved tail block. Failed growth of the tracked most recent placement allocation aborts before construction. The single-run tracker does not prove older allocations' capacities, and optional direct `operator new(..., nullptr)` calls remain nullable; required placement expressions must validate storage at the caller. Guard pages, a complete lifetime registry and compiler-based pointer/reachability analysis remain future work. Gameplay confirmation of the battle transition remains pending.
