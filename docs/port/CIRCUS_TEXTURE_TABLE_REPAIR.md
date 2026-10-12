# Circus texture table corruption

The circus transition crashed while adding a texture because the texture manager's tables still
used PS2 byte counts for host pointers and hash nodes. The native replacement allocates all five
tables from their actual element types. Retail source is unchanged.

## Evidence

- User run: `db22dc36-df9c-4d04-b182-cdf4254112e8/Pasted text.txt`, ending after the disguised
  Monica event resources load. The user also reports invisible playable Max and circus NPCs,
  with occasional visibility depending on position.
- Windows Application Error at 2026-10-11 22:18:55: `0xc0000005`, executable RVA `0x22e4ac`.
- `darkchronicle.exe.37776.dmp` resolves that instruction to `mgCTextureManager::AddHash`,
  inlined into `EnterTexture`. It writes `node->next` through RAX `0x66632e30756e656d`.
- The manager has 350 hash records, with `hash_num == 177` after the failed pop. Hash stack
  entry 176 contains the bytes `menu0.cf`; following bytes contain menu file paths. The free
  pointer was overwritten by data allocated after the undersized table.
- The old code reserves four bytes per pointer and eight bytes per hash record. On this
  build pointers occupy eight bytes and hash records occupy sixteen. Placement growth for
  constructed objects cannot repair raw pointer casts or an explicitly undersized byte count.

## Validation

`RuntimeContract.13_texture_tables` exercises the production manager with 350 textures and
221 blocks. It checks that all five complete typed extents are disjoint and inside the allocated
arena, then fills a following allocation with a canary. Eight full initialize/add/search/delete
cycles cover every hash record, collisions, table exhaustion and free pointer initialization.

- Baseline: fails with `texture table 3 exceeds its arena allocation`.
- Repair: passes every cycle without changing the following canary.
- Windows game and GTest build: pass; headless completion: pass.
- CTest: 365 passed, five prerequisite skips, zero failed / 370 registered.
- Compatibility inventory regenerated; the two reviewed raw table allocation sites now name
  the host override and its regression. Ordinary ownership checks strip the retail definition.
- Executable SHA256: `95F51E2A6CB436FFAB0DAAA559F653E0DCD0950C49A83541F172DA71070F52FB`.

Logs are under `E:/Chronicle-Two/logs/texture-tables-*`.

## Remaining work

The crash has a proved allocation cause. Character visibility/deformation has no gameplay
acceptance on this repaired build. Existing real PAL model tests cover loading, bind poses,
translated clones and scratch reuse; they do not yet establish correctness of actual cutscene
motion keys. Raw vertices in the sampled workshop/circus packs have homogeneous w of one,
and their sampled rotation keys have unit quaternion norms; those two input hypotheses did
not explain the screenshots. Recomp G40's zero emulated vf0 lane is not evidence of a defect
in the native C++ inverse. Next model work should compare posed transforms and bounds.

Movies remain timing-only: the player has no installed `VideoSource` decoder backend. Timing
and skip behavior do not decode or display PSS video. Decoder integration, frame upload and
movie audio synchronization remain implementation work; the texture repair does not add them.

Run the rebuilt game with the same command, enabling crash output:

```powershell
$env:DC2_CRASH_TRACE = '1'
& 'E:\Chronicle-Two\win\build-rel\darkchronicle.exe' --data 'E:\Chronicle-Two\win\ChronicleTwo\data' --width 1280 --height 720
```

No game launch or automated input was performed for this repair.
