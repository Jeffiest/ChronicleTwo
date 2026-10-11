# scriptinterpreter: reverse-engineering notes

Header: `ps2/include/scriptinterpreter.hpp`. Configuration-script interpreter used by ~29 units
(map, mapinfo, character, mdslist, mg_tanime, menus ...). Every user builds a
`CScriptInterpreter` on its stack, calls `SetTag(table)`, `SetScript(buf, size)`, `Run()`.
Tag routines have signature `int f(SPI_STACK *stack, int argument_count)` and read their
arguments with `spiGetStackInt/Float/String/Vector`.

## Symbol binding (retail ELF, `readelf -s rom/pal/extracted/iso/SCES_511.90`)
- LOCAL: `SkipSpace(input_str&)`, `CheckChar(char)`, `PreProcess(input_str&)` -> `static` in
  the .cpp, not in the header. Suggested prototypes: `static int SkipSpace(input_str &in)`
  (returns `position < size`), `static int CheckChar(char c)` (0 for `\r \n \t ' '`, else 1),
  `static void PreProcess(input_str &in)` (overwrites `//...` to end of line and `/*...*/` with
  spaces).
- Weak (binding 13): `input_str::input_str()`, `input_str::get`, `input_str::back`,
  `SPI_STACK::operator=`, `CScriptInterpreter::SetStack`, `SetStringBuff`. These are inline
  functions emitted out of line, so they are defined in the class bodies. The bodies written in
  the header are reconstructions from the asm and are NOT yet verified by a match (nothing in the
  .cpp emits them yet). `SPI_STACK::operator=` may instead be compiler-generated; if the explicit
  inline does not match, try removing it. Each weak copy sits right after its first user
  (`get` after `GetLine`, `__as__` after `PushStack`, `input_str()` after the
  `CScriptInterpreter` ctor, `back` after `GetArg`).
- `@size` values in the header are the ELF symbol sizes (manifest sizes include padding).

## input_str (0xC)
- 0x0 `char *buffer`, 0x4 `int size`, 0x8 `int position`: from `__ct__9input_str` (zeroes all
  three), `get` (`lbu buffer[position]`, `position++`, returns `position <= size`), `back`
  (`if (position > 0) position--`). `SkipSpace` reads bytes with `lb` (passes `char`).
- `GetLine(char *line, int line_size, char *terminator)`: terminator defaults to local copy of
  `"\r\n"` (`at_165`, .sdata); memcmp at `buffer+position`; copies at most `line_size-1` chars;
  returns 1 when terminator found (position advanced past it), 0 at end. Only caller:
  `InitMapSelect` (mapselect).
- First game: same class name used as base of its CScriptInterpreter.

## SPI_STACK (0x8)
- 0x0 `int type` (SPI_STACK_TYPE), 0x4 union int/float/char*. Stride 8 in `PushStack`
  (`stack + count*8`), `spiGetStackVector` (+0, +8, +0x10).
- Types: 0 string (`spiGetStackString`), 1 int, 2 float (`spiGetStackInt` converts float via
  `fptosi`; `spiGetStackFloat` converts int), 0xFF = invalid (GetArg pushes it when the text is
  empty or a non-quoted argument has non-numeric chars; value 0).
- `PushStack(SPI_STACK)` by value (caller passes pointer to copy); prints
  `"SPI stack over!!\n"` (`at_215`) when `stack_count >= stack_size`. Returns void.

## SPI_TAG_PARAM (0x8)
- `{ char *name; SPI_TAG_FUNCTION function; }`: stride 8 in `SetTag` and `GetNextTAG`, function
  pointer at +4 called as `function(this->stack, argument_count)`; return value ignored.
  Callbacks (e.g. `mapFAR_CLIP`) return 0/1. Table ends at a null name or an empty string
  (`SetTag` counting loop). E.g. `cfg_tag` (mapload) is 0x88 = 17 entries.
- Tag names must start with 'A'..'Z' (`SearchCommand`), else index -1.

## SPI_TAG_HASH (0x10) -- name is not retail
- Built by `SetTag` at `this+0x1E0 + i*0x10`: +0 next (chain), +4 name, +8 index; +0xC never
  touched. Chains appended at tail. `SearchCommand` walks `hash_table[hash(name)]` comparing +4,
  yields +8.

## CScriptInterpreter
| Off | Field | Evidence |
|---|---|---|
| 0x00 | input_str base | ctor calls `input_str()` on `this`; `get`/`back`/`SkipSpace`/`PreProcess` take `this` |
| 0x0C | stack_count | SetStack zeroes; PushStack increments |
| 0x10 | stack_size | SetStack; PushStack bound |
| 0x14 | stack (SPI_STACK*) | SetStack; passed to tag routine |
| 0x18 | string_buff_size | SetStringBuff; GetArg overflow check `next+len+1 > buff+size` |
| 0x1C | string_buff_next | SetStringBuff = buff; GetArg advances by len+1 |
| 0x20 | string_buff | SetStringBuff |
| 0x24 | binary | SetScript: 1 if `strncmp(script, "BIN", 3) == 0` (`at_382`), then position += 4; else PreProcess. GetNextTAG/SearchCommand branch on it |
| 0x28 | tag_count | SetTag count loop; GetNextTAG bounds |
| 0x2C | tag (SPI_TAG_PARAM*) | SetTag; GetNextTAG `tag[i].function` |
| 0x30 | hash_table | SetTag sets to `this+0x40` (only if tag_count < 0x80, else stays 0); SearchCommand uses linear strcmp search when 0 |
| 0x34 | unk_34[0xC] | never accessed |
| 0x40 | hash_buckets[0x65] | SetTag zeroes 0x65 words (8-unrolled to 0x5D, then remainder); hash is `% 0x65` |
| 0x1D4 | unk_1d4[0xC] | never accessed |
| 0x1E0 | hash_entries[0x80] | SetTag writes entry i at 0x1E0+i*0x10 (computed as `(this+0x40)+0x1A0`); only built when tag_count < 0x80 |
| 0x9E0 | unk_9e0[0x4F0] | never accessed in any unit |

The ctor zeroes 0x0, 0x4, 0x8 (again, after the base ctor), 0xC, 0x14, 0x2C.

### Size 0xED0 (upper bound, see caveat)
All callers keep it on the stack: `CMap::LoadCfgFile` / `mgCTextureManager::LoadCFGFile` /
`CMap::LoadMapFile`: frame 0xF00, object at sp+0x30, nothing above it -> size <= 0xED0 and
> 0xEC0 (frames are rounded to 16). `ScanInfoFile` (character): object at sp+0xA0, an int local at
sp+0xF7C; `mdslist::LoadPCPFile`: object at 0x1060, local at 0x1F3C (gap 0xEDC). So the size is
in 0xEC4..0xED0; 0xED0 asserted. If 0xEC4..0xECC is needed, shrink `unk_9e0`. The tail 0x9E0..
0xED0 and the gaps at 0x34 / 0x1D4 are unexplained (the 0x1D4 gap might be 16-byte alignment of
the entry array, which would also make 0xED0 = 0x1E0 + 207*0x10, but nothing in the code shows
that).

### Members
- `GetNextTAG(int call)`: locals `char[0x2800]` string buffer (SetStringBuff, only when tag
  table set) and `SPI_STACK[0x40]` (SetStack, per tag). Returns -1 if no tag table or
  SearchCommand fails. Binary: always GetArgBin, calls routine if `0 <= idx < tag_count` and
  `call`. Text: unknown tag -> skip to ';' and continue; known -> GetArg, call routine if `call`.
- `Run()`: `while (GetNextTAG(1) >= 0);`
- `hash(char *)`: `h = ((h << 8) + c) % 0x65 & 0xFF` (signed char). Member, non-static (`this`
  passed in $a0).
- `GetArgBin()`: reads s16 count; then count type bytes (3 -> string(0), 2 -> float, 1 -> int)
  into a local u8 array (0x108 bytes); aligns position to 4; reads values (strings in place,
  length+1 padded to 4) and pushes. Returns count.
- `GetArg()`: text parser; handles quotes, `\"` escape, Shift-JIS lead bytes (>=0x80 and not
  0xA1..0xDF take the next byte as well); ',' separates, ';' ends. Type: quoted both ends ->
  string (copied to string buffer, `"SPI string buffer over!!\n"` = `at_524` on overflow,
  value null); contains '.' -> float (`atof`); else int (`atoi`). Returns count.
- `SearchCommand(int *index)`: binary: reads s16 index, returns `index >= 0` (0 at end).
  Text: SkipSpace, reads word up to whitespace or ';' (puts ';' back), char buffer 0x10C.
  Returns 1 with index or -1; 0 at end of text.

## Globals
None with plain names: the unit's data is `at_165` ("\r\n"), `at_215`, `at_382` ("BIN"),
`at_524` (compiler literals).

## First game (`chronicle/ps2/include/scriptinterpreter.hpp`)
Same class names (`CScriptInterpreter : input_str`) but a different design: first game has
`TAG_PARAM`/`SPI_FUNC_PARAM` with argument-type arrays, `ControlCode`, `CallFunction`, size
0x890. This game replaced it with SPI_STACK-based argument passing, `SPI_TAG_PARAM` {name,
routine}, a tag-name hash, and binary ("BIN") scripts. Nothing in the layout was carried over.

## Drafting (job scriptinterpreter.1)
- Matching: `spiGetStackInt`, `spiGetStackFloat`, `Run`, `hash`, `SetScript`. `SetScript`
  calls the static `PreProcess`, declared at the top of the .cpp.
- `hash`: `u8 h; h = ((h << 8) + *name++) % 101` matches (signed char added).
- Inline members (`get`, `back`, `input_str()`, `SPI_STACK::operator=`, `SetStack`,
  `SetStringBuff`) are not inlined in this unit, which is compiled with `-inline off`
  (`UNIT_CC_FLAGS_scriptinterpreter` in `ps2/cmake/Toolchain.cmake`); retail calls their weak
  copies with `jal`. The header bodies remain unverified.
- Constants added to the header: `SPI_LIMIT` enum (`SPI_HASH_BUCKET_COUNT` 101,
  `SPI_HASH_TAG_MAX` 128, `SPI_STACK_SIZE` 64, `SPI_STRING_BUFF_SIZE` 0x2800, `SPI_TOKEN_SIZE`
  0x100). `SPI_TOKEN_SIZE` is a guess for the local buffers: GetArg's word buffer spans sp+0xA0..
  0x1A0 (0x100), SearchCommand's is 0x10C of frame, GetArgBin's type list 0x108 of frame.
- `GetArg` draft: classification rules (quote count, '.', non-numeric chars, CheckChar
  truncation) follow Ghidra; the draft is 0x2C bytes longer than retail, so its control flow is
  likely not the original's. `GetNextTAG` draft is 0x24 longer (retail probably shares the call
  path between binary and text branches).
- Ctor draft writes `buffer/size/position` explicitly after the base ctor (retail zeroes 0x0,
  0x4, 0x8 again); differs because the inline `input_str()` is inlined rather than called.

## Compiler options

The unit is compiled with `-inline off` (`UNIT_CC_FLAGS_scriptinterpreter` in `ps2/cmake/Toolchain.cmake`)
added to the game's `-O3,p`. The object is byte-identical to one built with a `dont_inline on` pragma.
Without inlining, the header's inline members are called rather than expanded, as in retail.
