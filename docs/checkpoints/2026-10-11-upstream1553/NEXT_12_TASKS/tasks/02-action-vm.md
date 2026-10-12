# 02: Real PAL action bytecode execution

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

New scripts/port/wp02_action_vm.py, port/src/action_vm_contracts.cpp if needed, and port/src/tests/wp02_* only.

## Work

Locate the actual PAL action program and exercise production CRunScript load/run/resume. Check PC/opcode, function extents, frames, WAIT/CALL/RET and completion. The generated funcdata::name is already fixed-width; do not redo that adaptation. ACTION_PROG_RUNNING=-1 is normal, not evidence of a hang.

## Required acceptance

A bounded trace identifies the first semantic divergence with a named asset/program. Real-asset and malformed/truncated controls cover the repaired path. Use actual callbacks from 01; send execution evidence to 03.

Return `results/02-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
