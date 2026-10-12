# 05: Production menu local-to-local image copy

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_drawprim.cpp and new port/src/tests/wp05_* only. A new local gfx/draw.cpp override requires task 12 agreement; shared Kazarov files stay read-only.

## Work

Reproduce the fatal StartImageTransfer path at mg_drawprim.cpp:329 after gfx::CopyTexture returns false, beyond successful lookup/binding. DrawOneItem copies 32x32 within its atlas to (224,736). Capture actual PAL metadata, source/destination rectangles, image extents, format and rejection reason. Establish the storage/copy contract with 06 before changing behavior.

## Required acceptance

The production menu transfer fails before and succeeds after, with verified destination pixels. Cover legitimate same-image/overlap semantics and reject invalid cases. No hardcoded menu height, skipped copy or suppressed error.

Return `results/05-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
