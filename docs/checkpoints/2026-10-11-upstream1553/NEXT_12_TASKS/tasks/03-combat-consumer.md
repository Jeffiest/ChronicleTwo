# 03: A/Cross through attack selection and motion

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mainloop.cpp; port/include/combat_trace.hpp; scripts/port/host_combat_trace.py; new port/src/combat_action_contracts.cpp and tests/wp03_* only.

## Work

Follow confirm press/hold through CActionChara::RunScript, input consumption, weapon/readiness gates and motion initiation. Existing logs already show A reaches the consumer and pad_history=1, with eye=0, pause=0x4100 and debug=0. Add bounded passive diagnostics only where they expose the first missing transition. Read actionchara.cpp/dng_main.cpp without editing ps2/.

## Required acceptance

A production consumer fixture starts the expected attack with valid character/weapon state and preserves release/hold and legitimate gates. Button receipt or a mapping test alone is insufficient. Use 01/02 findings; never auto-drive gameplay.

Return `results/03-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
