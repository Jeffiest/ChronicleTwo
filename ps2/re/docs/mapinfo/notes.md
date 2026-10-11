# mapinfo: reverse-engineering notes

Unit owns `CCameraInfo` and `CMapInfo` (declared in `ps2/include/mapinfo.hpp`). `CMapLightingInfo`
and `CCameraDrawInfo` are owned by `mapload` (their header was not yet written when this was done);
`mapinfo.hpp` includes `mapload.hpp` because `CCameraInfo` holds `CCameraDrawInfo draw_info[4]` by
value. Until `mapload.hpp` exists, `mapinfo.hpp` and `mapinfo.cpp` do not compile; with a stub
`CCameraDrawInfo { int; int; ctor; }` the header compiles and both size asserts hold.

No first-game counterpart exists for either class.

## Linkage
Every tag handler (`mapIMG` ... `mapCHARA_LIGHT_ADJUST`, `amapIMG`, `amapPCP`) is LOCAL in retail
-> `static` in the .cpp, not in the header. All data is LOCAL too -> `static` in the .cpp, no externs:

| Symbol | Size | Meaning |
|---|---|---|
| `mapinfo_tag` | 0xC0 | `SPI_TAG_PARAM[24]`: 23 tags (order below) + null terminator. |
| `add_mapinfo_tag` | 0x18 | `SPI_TAG_PARAM[3]`: `IMG`->`amapIMG`, `PCP`->`amapPCP`, terminator. |
| `MapInfo` | 4 | `CMapInfo *` being filled by the running script. |
| `MapInfoStack` | 4 | `mgCMemory *` the strings / copies are allocated from. |
| `now_img_num`, `now_pcp_num` | 4 | `int` next free index of `img_name` / `pcp_name` (LoadMapInfo only). |
| `LightingInfo` | 4 | `CMapLightingInfo *` selected by `LIGHT_SET`, cleared by `LIGHT_SET_END`. |

`mapinfo_tag` order (name string `at_360`..`at_382__2`): IMG, PCP, ACTIVE_LIGHT_SET, LIGHT_SET, FOV,
BGCOLOR, BGCOLOR2, AMBIENT, LIGHT, PLIGHT, FOG_ENABLE, FOG, LIGHT_SET_END, FLOOR, CHARA_POS,
TIME_FLAG, TIME_LIGHT_NUM, DEF_FOOT, SKY_INFO, LENS_FLARE, TIME_CFADE, ALL_SCISSOR,
CHARA_LIGHT_ADJUST. Handler signature is `int (SPI_STACK *, int)` (`SPI_TAG_FUNCTION`); they return
1/0 (several return `LightingInfo != 0`).

`at_704`..`at_713__2` are OutputLightData's sprintf formats:
`"MPL_ACTIVE_LIGHT_SET %d;\n"`, `"MPL_LIGHT_SET %d;\n"`, `" MPL_FOV 52;\n"`, `" MPL_BGCOLOR %d,%d,%d;\n"`,
`" MPL_BGCOLOR2 %d,%d,%d;\n"`, `" MPL_AMBIENT %d,%d,%d;\n"`, `" MPL_LIGHT %d,%f,%f,%f,%d,%d,%d,1;\n"`,
`" MPL_FOG_ENABLE %d;\n"`, `" MPL_FOG %f,%f,%d,%d,%d,%d,%d;\n"`, `"MPL_LIGHT_SET_END;\n"`.

## CCameraInfo (size 0xD0)
Size: `mapCAMERA_INFO` (mapload) `__construct_new_array(..., __ct__11CCameraInfoFv, 0, 0xD0, n)`;
`CMap::GetCameraInfo` stride 0xD0. Ctor (inline, emitted in mapload at 0x1642C0) constructs
`CCameraDrawInfo` at 0xA8..0xC8 in steps of 8, then calls `Initialize()`.

| Off | Field | Evidence |
|---|---|---|
| 0x00 | `int pos_num` | Initialize = 0; `mapFIX_CAMERA_POS2` sets it to max(pos_num, idx+1). |
| 0x10 | `sceVu0FVECTOR pos[8]` | Initialize zeroes 8 x 16 bytes with `mgZeroVectorW`; POS2 writes `pos[idx]` (idx < 8); `mapFIX_CAMERA_POS` writes pos[0]; `CMap::FixCameraPartsOnOff` measures distance to pos[0] (< 10.0). 0x04..0x10 is alignment padding. |
| 0x90 | `int rect_num` | Initialize = 4; `mapFIX_CAMERA_RECT` bounds-checks against it. |
| 0x94 | `CColFrame *rect[4]` | Initialize nulls rect_num entries; FIX_CAMERA_RECT stores a new `CColFrame`. |
| 0xA4 | `int draw_info_num` | Initialize = 4; GetDrawInfo bounds check. |
| 0xA8 | `CCameraDrawInfo draw_info[4]` | 8-byte stride; Initialize writes +0 = -1, +4 = 0 (same as `CCameraDrawInfo::Initialize`). +0 = part group number (`CMap::SearchPartsGroupNo`), +4 = int from FIX_CAMERA_OFF_GROUP arg 3. |
| 0xC8 | tail padding to 16-byte alignment | |

`GetDrawInfo(int)` returns `CCameraDrawInfo *` (null when out of range).

## CMapInfo (size 0x100)
Size: `Initialize` memsets 0x100. `CMap` starts with a `CMapInfo` at offset 0 (CMap ctor calls
`CMapInfo::Initialize(this)`; CMap methods read these offsets on `this`) - probably a base class;
the `map` unit decides. Initialize defaults: `time_light_num = 4`, `fixed_time = 12.0f`, `lens_flare = 1`.

| Off | Field | Evidence |
|---|---|---|
| 0x00 | `int img_num` | LoadMapInfo sets 16 and nulls the array; GetImgName bound. |
| 0x04 | `char *img_name[16]` | mapIMG / amapIMG store strdup'd names (amap fills the first null slot). |
| 0x44 | `int pcp_num` | LoadMapInfo sets 16. Note: LoadMapInfo's pcp clearing loop uses `img_num` as its bound (retail quirk). |
| 0x48 | `char *pcp_name[16]` | mapPCP / amapPCP. |
| 0x88 | `char *map_file` / 0x8C `int map_file_size` | LoadMapInfo copies the script; `GetMapFile`. |
| 0x90 | `char *add_map_file` / 0x94 `int add_map_file_size` | AddMapInfo; `GetAddMapFile`. |
| 0x98 | `int active_light_no` | ACTIVE_LIGHT_SET; `GetActiveLightNo` (inline, emitted in mapload 0x162B90). |
| 0x9C | `int lighting_info_num` | LoadMapInfo sets 16. |
| 0xA0 | `CMapLightingInfo *lighting_info` | `new CMapLightingInfo[16]` (stride 0x1D0) from the mgCMemory. |
| 0xA4 | `int time_cfade` | TIME_CFADE (int). No reader found. |
| 0xA8 | `float floor` | FLOOR (float). No reader found. |
| 0xAC | `unk_ac` | never touched in this unit. |
| 0xB0 | `sceVu0FVECTOR chara_pos` | CHARA_POS via `spiGetStackVector` (3 floats). Typed as an aligned vector because of its 16-byte-aligned offset after the unused 0xAC; 0xBC never written. |
| 0xC0 | `int time_enable` | TIME_FLAG arg0; `CMap::GetTimeEnable`, `CMap::GetNowTime`. |
| 0xC4 | `int time_light_blend` | TIME_FLAG arg1; `CMap::GetLightInfo(CMapLightingInfo*)` blends sets by `GetTimeLightingRatio` and sets light 0's direction from `GetSunPoint` when non-zero. |
| 0xC8 | `float fixed_time` | TIME_FLAG arg2 (if >2 args); GetNowTime returns it when time_enable == 0 && fixed_time_enable, else 12.0. |
| 0xCC | `int fixed_time_enable` | TIME_FLAG arg3 (if >3 args). |
| 0xD0 | `int time_light_num` | TIME_LIGHT_NUM; `CMap::GetLightNoTime`, `GetNowTimeLightBand` (4 = fixed bands 9.5/17.5/21.5/6.5 h). |
| 0xD4 | `int def_foot` | DEF_FOOT (int). No reader found. |
| 0xD8 | `int sky_info` | SKY_INFO arg0. Meaning unresolved. |
| 0xDC | `float unk_dc` | SKY_INFO arg1. Meaning unresolved. |
| 0xE0 | `float sun_angle` | SKY_INFO arg2 degrees -> radians via `mgAngleLimit`; `CMap::GetSunPoint` uses it as RotMatrixY angle. |
| 0xE4 | `int lens_flare` | LENS_FLARE; default 1. |
| 0xE8 | `int all_scissor` | ALL_SCISSOR; passed as last arg of `CMdsListSet::LoadPCPFile` in `CMap::LoadData`. |
| 0xEC | `int chara_light_adjust` | CHARA_LIGHT_ADJUST arg0 (int). |
| 0xF0 | `float chara_light_adjust_value[3]` | CHARA_LIGHT_ADJUST args 1-3 (floats). |
| 0xFC | `unk_fc` | never touched. |

Signatures: `GetImgName/GetPCPName -> char *`, `GetMapFile/GetAddMapFile(int *size) -> char *`,
`GetLightingInfo(int) -> CMapLightingInfo *`, `LoadMapInfo/AddMapInfo(char *, int, mgCMemory *) -> void`,
`OutputLightData(char *) -> int` (characters written), `Initialize() -> void`.

LoadMapInfo/AddMapInfo: alloc size `(size + 15) >> 4` quadwords (the code computes `n>>4` plus one
if `n & 0xF`), `memcpy`, then a stack `CScriptInterpreter` (0xED0): ctor, SetTag, SetScript, Run.
LoadMapInfo calls SetScript once before and once after the copy. The lighting array uses
`new (Alloc(stack, algn16(16*0x1D0) + 2)) CMapLightingInfo[16]` (`__nwa__FUiP1`, `__construct_new_array`).

## CMapLightingInfo (owned by mapload; size 0x1D0) - offsets seen here
Ctor `__ct__16CMapLightingInfoFv` (0x1670B0, defined in this unit) is `memset(this, 0, 0x1D0)`.
- 0x000 float: projection distance; FOV tag sets `(mgScreenWidth / 2) / tanf(0.45378563)` (26 deg).
- 0x010 vec4 BGCOLOR rgb + w=128.0; 0x020 vec4 BGCOLOR2 (copied from BGCOLOR if all zero).
- 0x030/0x040/0x050 float[4] each: x/y/z of the 4 normalized light directions (column layout); copy-assign copies 0x30..0x70.
- 0x070 vec4[4] light colours (LIGHT args 4-6, w=0).
- 0x0B0 int point-light enable (PLIGHT sets 1).
- 0x0C0 point light [4], stride 0x30: +0 vec4 pos (w=1), +0x10 vec4 colour (w=0), +0x20 float range. (`operator=` copies 0xC0..0x180 as 6 x 0x20.)
- 0x180 vec4 AMBIENT (w=128).
- 0x190 int FOG_ENABLE.
- 0x1A0 float fog near, 0x1A4 float fog far, 0x1A8..0x1AA u8 fog rgb (default 0xFF), 0x1B0 float (default 0), 0x1B4 float (default 255) - both clamped 0..255 in `LightingEdit`.

## Drafting (job mapinfo.1)
- All 37 functions drafted; the 7 matching `CCameraInfo`/`CMapInfo` accessors are promoted.
- The static tag handlers are referenced by the two C tag tables `mapinfo_tag` and
  `add_mapinfo_tag` in `mapinfo.cpp`, which `SetTag` receives.
- `__ct__16CMapLightingInfoFv` is the inline ctor from `mapload.hpp`, emitted here because
  `LoadMapInfo`'s `new CMapLightingInfo[16]` takes its address; it is emitted only once `LoadMapInfo`
  is C.
- `mglib.hpp` cannot be included with `mapload.hpp` (duplicate `mgFOG_PARAM`, fixlist item), so
  `mgScreenWidth` (used by `mapFOV`) is declared locally in `mapinfo.cpp`. Replace with the include
  once the duplicate is resolved.
- `mapFOV` stores 400.0 before the computed projection (dead store kept from retail).
- `CMapLightingInfo` offsets confirmed by name: PLIGHT arg 1 is `point_light[i].power` (+0x20); FOG
  args 5/6 are `fog.offset` / `fog.far_value` (mg_drawenv names), defaults 0 / 255.
- Retail's string-size rounding is `n >> 4` plus one when `n & 0xF` (unsigned); the lighting array
  takes `Alloc(n * sizeof(CMapLightingInfo) / 16 + 2)`.
- Drafts with ordering-only diffs: `mapBGCOLOR`/`mapAMBIENT`/`mapCHARA_LIGHT_ADJUST`/`mapSKY_INFO`
  /`mapTIME_FLAG` (retail keeps a running `SPI_STACK *`), `mapLIGHT` (likewise), `amapIMG`/`amapPCP`
  (6 words, loop form). `OutputLightData` matches retail at 0x318 bytes while
  indexing the lighting set, colour rows and direction columns through
  `CMapLightingInfo` rather than byte offsets.
