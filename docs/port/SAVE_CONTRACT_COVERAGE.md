# DC2 save contract coverage

`port/src/tests/dc2_save_roundtrip_standalone.cpp` exercises the production host memory-card adapter through the DC2 SDK declarations in `ps2/include/sce/libmc.h`. It links `port/src/sce/libmc.cpp` and Kazarov's real path implementation, then calls `PathsSetSaveRoot` with a unique temporary directory before any card operation. The test therefore cannot touch the user's configured save tree.

The fixture creates the in-game directory names `/BESCES-51190dkcl0`, `/BESCES-51190dc2album` and `/BESCES-51190dc2omake`. It writes and flushes a deterministic 0x659c0-byte game-save image at `/BESCES-51190dkcl0/BESCES-51190dkcl0`, checks its size via `sceMcGetDir` using the SDK's `sceMcTblGetDir` structure, reopens it, seeks to byte `0x45`, checks the retail-cleared incomplete marker, and reads back every byte. It also creates the album and omake files at the paths used by `CMemoryCardManager`, deletes the game save through `sceMcDelete`, and checks that deletion left the unrelated album and omake files intact. Every SDK call is followed by a sync assertion for its function number and result.

On 2026-10-11 the standalone fixture compiled with the configured llvm-mingw compiler and passed on Windows both normally and with `-DNDEBUG`. Its local check macro still evaluates SDK calls in release builds. The run reported: `DC2 SDK save round trip passed: 416192 bytes, typed stat, offset 0x45, album/omake paths, delete, isolated root`.

This is filesystem and SDK-contract evidence for the host adapter. It does not instantiate `CMemoryCardManager`, construct a retail `SAVEDATA_FORMAT`, validate game checksum/version semantics, or prove full save/load serialization correctness. The save image is deterministic fixture data with byte `0x45` set to the value the retail host save path leaves on disk.

Integration adds `ct_dc2_save_roundtrip_test` to both completion aggregates and CI. The production-linked
Windows executable passes with assertions active; see `EXECUTION_BATCH_2026-10-11.md` for the receipt.
