# Shared model-memory contract

## Integrated repair, 2026-10-11

`MenuMemoryAdjust` now validates the complete aligned parent/child partition plan before binding
borrowed views. Retail capacities remain unchanged. Native fixed/motion loading sizes faces,
groups, materials and stream copies using actual runtime types and checks required allocations
before writing. Motion loading no longer emits unused VIF packets; native draws consume retained
CPU indices/weights, following the fixed-model path. This removes the unreserved hardware-packet
writer instead of estimating a larger arena budget.

`ct_model_storage_test` passes fixed/motion loads, scratch reset and repeated reload. Its required
allocation negative control aborts with a diagnostic before a null write. Thirty actual PAL MDT
geometry records load: 14 from c01fuku1, 15 from c02fuku4, one from ws050. Largest fixed/motion
geometry totals are 10,853/6,598 blocks in the existing 39,616-block slot. Real-asset motion tests
use empty synthetic skin weights, so they do not prove complete character/equipment budgets or
renderer skinning equivalence. Planner exact/insufficient/alignment/alias cases also pass.

Items gameplay acceptance remains pending. The sections below preserve the original diagnosis
and design boundary; their proposed implementation is superseded by the repair above. Recording-time
photo/effect capture remains separate work. See `EXECUTION_BATCH_2026-10-11.md` for build/log evidence.

User verification: title Options and the dungeon menu open; battle movement and lock-on work. Pressing Xbox A does not attack, and choosing Items crashes. The correct PAL English attack binding is A/Cross. Its press/hold/release game-action fixture passes; the runtime combat state/script failure is not identified. First-person mode gates the combat script, but the user's reported movement and lock-on do not establish that this explains the failure.

## First violated model contract

`logs/menu-option-err.log` records model arena `stack 1` at 39,603/39,616 blocks rejecting 37 blocks, then a null dereference in `mgCVisualMDT::CreateFace`. The allocation owner resolves to `MenuMemoryDivide`/`MenuMemoryAdjust` in `menuchr.cpp`; the capacity is the retail `menu_chr_memorytbl[1] = 0x9AC0`. The load reaches this path through `MenuItemCharaDataLoadPack` and `CMenuItemInfo::ModelReadEndCheck`.

`DataAssignMotionMDT` builds a packet at `memory->stGetTop()` before calling `Alloc(size)`. Rejection happens after the write. The diagnostic records the first rejected allocation; it must not be confused with the later failed face allocation that dereferences null. This pattern also appears in the fixed-visual loader. A capacity increase alone does not enforce write bounds.

`MenuMemoryAdjust` binds seven model partitions and the remaining file/work arena. It does not validate their complete sum against the parent, and it computes free space before alignment. The primary-character owner is separate: its host table currently doubles the retail table. Repairing that owner did not change the menu model partitions.

Measured Windows ABI sizes, compared with retail assertions:

| Runtime type | Retail bytes | Host bytes | Current observation |
|---|---:|---:|---|
| `mgCFrame` | 272 | 304 | Expanded runtime object |
| `mgCVisualMotionMDT` | 272 | 320 | Expanded runtime object |
| `mgCFace` | 48 | 48 | Existing three-block allocation fits; earlier wider-face explanation was inaccurate |
| `mgFACE_GROUP` | 32 | 56 | Existing four-block allocation already reserves 64 bytes |

The byte counts come from the configured Windows compiler and the actual mirrored game headers. Do not infer required arena growth solely from pointer count or change a fitting allocation because its type contains pointers.

## DC1 reference and implementation boundary

Local `review-dc1/master` (`706ce00e`) supplies `port/src/arena.hpp` and `dataset.cpp`: owned backing allocations, guard pages, fourfold capacity headroom, high-address checks and overflow diagnostics. Its typed allocation/placement and source audits complement those safeguards. Its pinned Kazarov revision is `ce459c1`; the Xbox mapping and input polling files are identical to the currently pinned shared code in this port. DC2's game-facing action state still needs its own verification.

DC1's allocator is a reference for ownership and enforcement. DC2 intentionally carves and aliases `mgCMemory` storage, so substituting independent backing for every `stSetBuffer` would require distinguishing owners from borrowed views first.

The next implementation package should cover the shared model loader and its arena owner together:

1. Inventory model allocation types and packet construction in both fixed and motion loaders. Size runtime objects with actual host types while retaining disk-record widths.
2. Introduce a checked packet-size/count pass or equivalent bounded writer so complete packet storage is reserved before emission. A writer into unused arena tail is not a reservation.
3. Compute and validate the complete menu model partition plan after alignment, including remaining work/file storage. Preserve intentional aliases and reload lifetimes. Guard the backing owner; label any conservative capacity headroom explicitly rather than presenting it as an exact requirement.
4. Exercise a component load/unload/reload fixture with representative local models, fixed/motion packets, exact/insufficient capacities, parent/child bounds and alias/reset transitions. No automated gameplay is needed for this gate.

The Items failure remains unresolved. No additional game launch was made after identifying it. Photo/effect capture during active recording remains separate work: a renderer-owned prefix capture must preserve color, depth/stencil, render target and continuation state without presenting or returning stale pixels.
