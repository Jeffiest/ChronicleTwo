# 07: Recorded texture lifetime and stale handles

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/host_overrides/gfx/resources.cpp and new port/src/tests/wp07_* only. A new local displaylist override needs task 12 agreement/registration.

## Work

Reproduce create, record draw/copy, release/reset, slot reuse and replay through production resource code. The failed run has 23 stale 0x10005 warnings across title/menu reset; that alone does not prove the missing HUD cause. Inspect retained font/atlas references and deferred destruction/generations.

## Required acceptance

Required references remain valid through replay, then release; stale generations cannot bind recycled slots. Repeated-cycle resource counts remain bounded. Do not silence warnings, disable generation checks or leak textures. Coordinate lifetime findings with 06/08.

Return `results/07-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
