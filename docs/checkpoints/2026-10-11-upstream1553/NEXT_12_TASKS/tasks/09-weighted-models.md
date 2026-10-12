# 09: Real weighted character/equipment ownership

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_visual.cpp; port/src/visualmotion.cpp; port/src/menu_model_memory.cpp; port/include/menu_model_memory_plan.hpp; new port/src/tests/wp09_* only.

## Work

The old 30-MDT fixture uses empty synthetic weights. Load real character/equipment MDT, skeleton and weight data through production loaders, scratch reuse and repeated replacement/unload. Verify durable ownership of CPU indices/weights/materials and typed reservation before writes. Preserve new upstream host-width-safe heap handling and existing checked menu partitions.

## Required acceptance

Actual weighted assets survive scratch reuse and replacement without aliasing, canary corruption or unbounded growth. Exhaustion rejects before writes. No arena-size multiplier, geometry-only substitute or unused VIF packet restoration.

Return `results/09-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
