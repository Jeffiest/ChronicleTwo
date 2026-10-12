# 06: Texture atlas storage and palette identity

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_texture.cpp and new port/src/tests/wp06_* only.

## Work

Compare actual PAL menu/HUD atlas headers, GS TBW/TH/PSM, decoded pixels, backing storage and CLUT references. Distinguish image dimensions from storage and sampling requirements. A scratch-row dimension mismatch is a hypothesis until metadata proves it. Preserve the existing failed-allocation dirty/retry behavior.

## Required acceptance

Real atlas fixtures cover valid scratch-region copy/sampling, indexed palette identity, retry and reset/reload. Show a meaningful before/after failure for any change. Agree on the boundary with 05; resources.cpp belongs to 07.

Return `results/06-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
