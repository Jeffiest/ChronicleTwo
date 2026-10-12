# 08: HUD sprite, UV, blend and draw-state propagation

Allocation: **Gemini**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/mg_drawenv.cpp; port/src/mg_sprite.cpp; port/src/menudraw_primquad.cpp; new port/src/tests/wp08_* only.

## Work

Follow real dng_status.cpp active-item/weapon/status draws into CPreSprite, environment state and native quad submission. Compare production packet position/UV, texture/CLUT, alpha/blend, clipping and order with retail arguments. Use relevant DC1 renderer contracts, verifying DC2 differences.

## Required acceptance

A production packet/render fixture fails before and verifies corrected state after, including consecutive sprites with differing state, palettes and clipping. Fix shared propagation rather than one HUD element's coordinates. Keep resource-dependent blockers distinct; 05–07 own those.

Return `results/08-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
