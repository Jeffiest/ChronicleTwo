# 04: Named archive read failures and bounds

Allocation: **Claude**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/dataread.cpp and new port/src/tests/wp04_* only. Shared cdvd declarations go through task 12.

## Work

The old runtime reports DATA.DAT read failure without logical asset/extent details. Carry named asset, offset, logical size, physical count and failure reason into bounded failure diagnostics. Validate actual PAL index extents and production read/background APIs. Preserve the newly integrated DATA-only index, separate movie lookup, host0 save handling, packed compatibility, padding and two-poll completion.

## Required acceptance

Real PAL reads pass; missing/truncated/out-of-range and sector-padding canaries fail safely before successful metadata publication. Prove the actual rejected extent rather than guessing. Share named failures with their script/model/texture owners.

Return `results/04-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
