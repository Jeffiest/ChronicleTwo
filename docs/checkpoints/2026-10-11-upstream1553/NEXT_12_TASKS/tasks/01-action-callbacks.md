# 01: Action callback argument/result contracts

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

New scripts/port/wp01_action_callbacks.py and port/src/tests/wp01_* only; propose central registration to task 12.

## Work

Exercise actual actscript.cpp _GET_BTN, _CHECK_PAUSE, _GET_PAD_HISTORY and _RESET_PAD_HISTORY callback bodies with argument/result slots and RS_PTR canaries. Expressions using stack and stack++ in different call arguments merit an evaluation-order check; they are not a proved cause. Repair a demonstrated output-slot defect through an exact-anchor generated-host rewrite, and check sibling callbacks.

## Required acceptance

A production callback fixture fails before and passes after; canaries and negative controls detect the original defect. Preserve Xbox A/Cross mapping. Coordinate verified callback semantics with 02/03.

Return `results/01-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
