# Serialized record boundary coverage

This batch records which fixed byte layouts can be asserted and where runtime decoding currently
crosses a serialized boundary. `port/src/serialized_record_contracts.hpp` defines byte-width disk
views and asserts the fixed-width MDT record shapes, TIM2 field offsets, DATA.HD4 row, the 0x4c
pack entry, and save-image header prefix. It intentionally does not assert pointer-bearing runtime
objects: `DATA_HEADER` has a union whose `name` arm is a host pointer, and the save image's full
0x659c0 extent remains asserted by the existing game header/`port/src/savedata.cpp` checks.

## Current decoder evidence

- `port/src/dataread.cpp::ReadIndex` reads DATA.HD4 with explicit little-endian byte loads. It
  checks the 12-byte table extent and name offsets before reading names. This is already bounded;
  the standalone fixture pins byte order and table/range boundary arithmetic without replacing
  that decoder.
- `ps2/src/mg_texture.cpp::mgCTextureManager::EnterTexture` treats a TIM2 buffer as
  `TM2_head`/`TM2_PICTURE`, then uses `header_size`, `image_size`, `mipmap_count`, and
  `mip_sizes` to form pixel and palette addresses. `port/src/tests/tex_fixture.hpp` emits a
  16-byte file prefix and picture fields at the asserted retail offsets. The active hook is the
  `EnterTexture(..., TM2_head *head, ...)` boundary; it currently has no buffer extent argument,
  so callers must guarantee the full TIM2 picture extent.
- `ps2/src/collision.cpp` and `port/src/mg_visual.cpp` derive MDT material and face pointers from
  `MDT_HEADER` offsets, then iterate counts and variable face indices. The declarations are
  fixed-width disk records, but there is no shared bounded decoder. The unsafe acceptance hooks
  are `mgCVisualFixMDT::DataAssignMDT(MDT_HEADER *, ...)` in `port/src/mg_visual.cpp` and the
  corresponding original routines in `ps2/src/collision.cpp` / `ps2/src/mg_visual.cpp`.
- `ps2/src/dataread.cpp::GetPackFile` directly walks `PACK_ENTRY` records using the on-disk
  `next`, `offset`, and `size` values. The 0x4c record is fixed-width; there is no base extent
  passed to the routine, so malformed entries cannot be bounded at this API. The runtime file
  cache (`FILE_CACHE`) holds host pointers and is a different object, not this record.
- `SAVEDATA_FORMAT` is written/read by `port/src/memcard.cpp` as a fixed 0x659c0-byte image;
  `ps2/include/memcard.hpp` and `port/src/savedata.cpp` already assert its extent and key stored
  offsets. This batch pins the 0x80-byte header prefix only and does not claim semantic validation
  of saves.

## Limits

`ct_serialized_records_test` is wired into the completion aggregates and CI, and passes on Windows.
Its disk-view declarations are independent byte-layout mirrors rather
than aliases of the engine's runtime structs. It covers unaligned little-endian DATA.HD4 reads and exact/overflow
range/table boundaries. It does not exercise runtime game loaders or make unchecked TIM2, MDT, or
pack APIs safe. Those require a separate change that adds source extents at their call sites and
validates every offset/count before pointer formation.
