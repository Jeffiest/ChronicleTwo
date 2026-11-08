# Capacity

The limits the native port raises above retail, where each is defined, what depends on it and what still blocks
the rest. `port/include/capacity.hpp` is the single source: every port header that defines one of these constants takes
its value from a `capacity::k*` constant and names the retail value in its comment; the PS2 build's headers
(`ps2/include`) are untouched and keep retail (the port tree is `PORT`-only, so it carries no `#ifdef PORT`). `port/src/tests/capacity_test.cpp` asserts each
value, fills each structure to its limit and reads it back, and round-trips a save at the new counts.

## Audit

Header lines are the port definition. "Save" says whether the value reaches a save record; the port's JSON
save (`port/src/save_json.cpp`) writes every array at its full count and reads an array written with a smaller
count (retail's, or an older port's) by filling the front and leaving the rest clear (`save_json.cpp:810`); an
array longer than this build holds is refused (`SaveJsonError::BadValue`).

| Constant | Header | Retail | PORT | Dependent sites (port/src unless noted) | Save |
|---|---|---|---|---|---|
| `AUTOMAP_ROOM_MAX` (`DNG_ROOM_MAX`) | `automap.hpp:43` | 64 | 160 (`kDungeonRooms`) | `automap.hpp:494` room array and its STATIC_ASSERT (`automap.hpp:796`, by name); `automap.cpp:1812,1814,2327,2393`; `dng_main.cpp:727`; `dng_event.cpp:2101`; `moonmod/chronicle_two_adapter.cpp:5599-5622` (`set_layout` range); `test_hooks.cpp:422` | no |
| `DNG_GRID`, `DNG_GRID_CELLS` | `automap.hpp:42,49` | 30 (900 cells) | 60 (3600 cells) (`kDungeonGrid`) | `dng_main.cpp:738-746` grid allocation (was the literal 900 and 30); `automap.cpp:2574` picks the live size from `DngLayoutArea`; `chronicle_two_adapter.cpp:5599,5617,5621` | no |
| `DNGMAP_FLOOR_NUM` | `dngfloor.hpp:141` | 40 | 128 (`kDungeonFloors`) | `dngfloor.cpp:1148` floor search; `moonmod/savedatadungeon_moonmod.hpp:10` `SAVE_DUNGEON_FLOOR_ID_MAX`; `dng_event.hpp:180` `TRESURE_BOX_FLOOR floor[128]` already covers it | indirectly (floor records) |
| `TREASURE_BOX_MAX` | `dng_event.hpp:67` | 24 | 128 (`kTreasureBoxes`) | `dng_event.hpp:617` box array; `dng_event.cpp:674-878,2149` (by name); `dng_main.cpp:608` arena object | no |
| `DNG_PULL_ITEM_NUM` | `dng_main.hpp:115` | 72 | 256 (`kPullItems`) | `dng_main.cpp:276,879,886,1444,2087` (by name) | no |
| `MONSTER_ACTIVE_MAX` | `monster.hpp:38` | 24 (upstream port 60) | 128 (`kActiveMonsters`) | `monster.hpp:397` `put_flag` is a `std::bitset<MONSTER_ACTIVE_MAX>` (retail `u32`): `monster.cpp:751-761,827`, `dng_event.cpp:2246`, `tests/moonmod_entities_api_test.cpp`; `monster.hpp:425,428` slot arrays; `dng_main.hpp:99` `DNG_CHARA_SLOT_MONSTER_END`; `monster.cpp:3218` `more_monsters` latch; `options/rows.cpp:378`, `platform/config.hpp:62` text; `moonmod/monster_moonmod.cpp:74-169` | no |
| `MONSTER_LOCATE_MAX` | `monster.hpp:40` | 32 | 128 (`kMonsterLocateEntries`) | `monster.hpp:401-402`; `monster.cpp:829`; `dng_event.cpp:2247` (by name) | no |
| `MONSTER_REFER_MAX` | `monster.hpp:39` | 12 | 32 (`kMonsterKinds`) | `monster.hpp:429` kinds array; texture blocks `MONSTER_TEXB_REFER` 0x28..0x47 (`monster.cpp:809,1387,1418,3074`), below the dungeon's next fixed block 0x51 (`dng_main.cpp:429`) | no |
| `SCENE_CHARA_SLOT_NUM` | `scenevillager.hpp:56`, `scenesnd.hpp:361` | 0x80 | 256 (`kSceneCharaSlots`) | `scene.cpp:1103` (was the literal 128); `colprim.hpp:194` `CColPrim::hit_characters` bitset (checked against the array at `colprim.cpp:19`); `scenevillager.cpp:342`; holds `DNG_CHARA_SLOT_MONSTER_END` = 24 + 128 and keeps retail's game object slots 0x78..0x7B | no |
| `SCENE_VILLAGER_SLOT_NUM` | `scenevillager.hpp:42` | 16 | 16 (`kVillagerSlots`, blocked) | `scenevillager.cpp:376-1250`; retail event scripts (`EVENT_DATA_CHARA_SLOT`, `event_func.cpp:8525,17061`) address slots by number | no |
| `SCENE_SUB_VILLAGER_SLOT_NUM` (`_TOP`) | `scenevillager.hpp:43-44` | 8 (top 0x18) | 8 (`kSubVillagerSlots`, blocked); top derived as `TOP + NUM` | as above | no |
| `VILLAGER_DATA_MAX` (`CVillagerMngr::data`) | `villagermngr.hpp:254` | 32 | 128 (`kVillagerRecords`) | `villagermngr.cpp:75` (was the literal 32), `GetData` bounds by `data_num` | no |
| `NPC_BASE_DATA_MAX` (`NpcBaseData`) | `npccfg.hpp:73`, `stubs/npccfg.hpp` | 180 | 512 (`kNpcRecords`) | `npccfg.cpp:31` definition (was the literal 180), `npccfg.cpp:230` loader bound by `std::size` | no |
| `SAVE_DUNGEON_FLOOR_NUM` | `savedatadungeon.hpp:22` | 162 | 512 (`kSaveDungeonFloorRecords`) | `savedatadungeon.hpp:80` record array and `PORT_RECORD_ASSERT` (`:116`, now `0x3C + N * 0x14`); `savedatadungeon.cpp` `GetFloorInfoPtr`: retail's 162 keep their layout, floors from each dungeon's `limmit_table` count up take `SAVE_DUNGEON_FLOOR_EXTRA` = 50 records each after them; `dngfloor.cpp:1148`; `moonmod/savedatadungeon_moonmod.cpp:12` keeps `limmit_table` (mod floors stay keyed by stable id in the extension) | yes: `save.save_dungeon.floor_info` |
| `SAVE_BUILD_PARTS_MAX` | `savedata.hpp:48` | 0x100 | 1024 (`kSaveBuildParts`) | `savedata.hpp:234`; `savedata.cpp:114,131,139,166` bounds (were the literal 0x100) | yes: `save.build_parts_num` |
| `USED_DATA_PLACE_MAX` (`MENU_BAG_PLACE_MAX`) | `userdata.hpp:120`, `menusys.hpp` | 150 | 400 (`kInventoryPlaces`) | `userdata.hpp:1170`; `userdata.cpp:2482,2510,2520,2530,3194,3210,3389,4394,5953` (were the literal 150); `menudraw.hpp` fish arrays and `MenuDrawItemInfo`, `menudraw.cpp:1412-1502,7299`; `menucommon.cpp:1981`; `menusys.cpp:8544-12746` (by name). The menus page through `ITEM_BOARD_PLACE_NUM` = 150 (`userdata.cpp:3104`), see blockers | yes: `save.user_data.used_data` |
| `GEORAMA_PARTS_LIST_MAX` | `editmenu.hpp:31` | 0x180 | 1024 (`kGeoramaPartsList`) | `editmenu.hpp:179,181` lists; `editmenu.cpp:2436-2438` (were the literal 0x180), `2516,3591,5843,5889,5926`; `chronicle_two_adapter.cpp:6853,6914` piece table cap (were 0x180) | no |
| `EDIT_DATA_PARTS_MAX` | `editdata.hpp:60` | 300 | 300 (`kEditDataParts`, record kept) | `editdata.hpp` record arrays and `PORT_RECORD_ASSERT(sizeof(CEditData) == 0x5510)`; placed parts are uncapped at run time (`editmap.cpp:2076-2149` `kPortEditSlotCeiling`, `save_json.cpp:913` TownLayout) | yes: `TownLayout` JSON, unlimited |
| `EDIT_DATA_HOUSE_MAX`, `EDIT_MAP_HOUSE_MAX` | `editdata.hpp:71`, `editmap.hpp:37` | 32 | 32 (`kEditDataHouses`, record kept) | as above; houses are uncapped in `TownLayout` (`save_json.cpp:939`) | yes: `TownLayout` JSON |
| `USER_CHARA_NUM` | `userdata.hpp` | 4 | 4 (`kPlayableCharacters`) | registered playable characters are work package E | yes (accounts) |
| `ITEM_COMMON_DATA_MAX` | `gamedata.hpp:230` | 432 | 1024 (`kCommonItems`) | `stubs/gamedata.hpp` `local_com_itemdata`; `gamedata.cpp:483,532,654,678,685,1040,1519`; `gamedata_moonmod.cpp:18`; `InitItemMes` clears eight at a time (`kCommonItems % 8 == 0`) | item ids in saves |
| `ITEM_NO_MAX` | `gamedata.hpp:229` | 0x200 | 2048 (`kItemNumbers`) | `stubs/gamedata.hpp` conversion table; `gamedata.cpp`, `gamedata_moonmod.cpp:12,96,200`; `SHOP_PRICE_MAX` follows it | yes: item numbers are `s16` |
| `ITEM_DATA_MAX` (`local_itemdata`) | `gamedata.hpp:231` | 162 | 512 (`kItemRecords`) | `gamedata.cpp:490` (was the literal 162), `stubs/gamedata.hpp`, `moonmod/gamedata_moonmod.hpp` | by id |
| `WEAPON_DATA_MAX` (`local_weapondata`) | `gamedata.hpp:232` | 116 | 512 (`kWeaponRecords`) | `gamedata.cpp:497` (was the literal 116), `stubs/gamedata.hpp`, `gamedata.cpp:1589` cursor end | by id |
| `SHOP_ITEM_MAX` | `menushop.hpp:92` | 0x40 | 256 (`kShopGoods`) | `menushop.hpp:138-139`; `menushop.cpp:2699-2703` | no |
| `SHOP_PRICE_MAX` | `menushop.hpp:93` | 0x200 | `ITEM_NO_MAX` | `menushop.hpp:141`; `menushop.cpp:2718` | no |
| `MAP_PLACE_PARTS_TOWN` | `map.hpp:32` | 0x140 | 2048 (`kMapPlacementsTown`) | `object/world/mapjump.cpp:276,350` (`GetLoadMapInfo` and the interior jump, were the literal 0x140); the table is carved from the map's scene stack by `CMap::SetPlacePartsBuff` (`map.cpp:2215`, 0x310 bytes and a pointer per entry: 1.6 MB for the town and as much for an interior, from `MainDataBuff`/`SubDataBuff`, which borrow the `MainBuffer` arena root of 0x1A0000 quadwords times `kArenaHeadroom` = 4); placements past it fail with `TracePlacementFailure` (`mapload.cpp:1200`). DC2-Toolkit's `MAX_PLACEMENTS` (work package B) authors to the same 2048 | no |
| `MAP_PLACE_PARTS_DUNGEON` | `map.hpp:33` | 400 | 4640 (`kMapPlacementsDungeon`) | `object/dun/dng_event.cpp:2014` `LoadDungeonMapFile` (was the literal 400); `automap.cpp:746` overview pass `saved_clip` (was the literal 1024); the generator places one part per used cell of the `DNG_GRID` grid and `SetDummyTree` one per free cell of the grid widened by four on each side (disjoint), plus 15 fixed parts, so the table holds (60 + 8)^2 + 16, checked by a `static_assert` at `CAutoMapGen` placement (`automap.cpp:2168`); 3.7 MB from `BuffStageMain` (the `MainBuffer` rest) | no |
| Title map placements | `title.cpp:735` | 0x190 | 0x190 (retail) | the title scene loads one fixed retail map into `TitleMapBuffer`; nothing authors more | no |

Arena budgets raised with the sizes: `tests/arena_placement_test.cpp` lists `CShop` (`menushop.cpp:1874`,
price table of `ITEM_NO_MAX` entries) and `CMenuGeorama` (`editmenu.cpp:1162`, two lists of
`GEORAMA_PARTS_LIST_MAX`) among the types that outgrow their retail quadword runs; both are placed by the host
allocator, which grows them. `CTreasureBoxManager` and the grid cells are host allocations in `MainBuffer`
(`dng_main.cpp:608,738`), which the port's arenas size on demand.

## Costs

- A new-game save document is about 824 KB of pretty-printed JSON (512 floor records, 400 places, 1024 part
  counts); `SaveJsonWrite` takes about 150 ms and `SaveJsonRead` about 90 ms on the gauntlet machine
  (`Capacity.SaveRoundTripsTheRaisedCountsAndLoadsRetailCounts`, instrumented once). The two save slot cases that
  write a catalogue of 31 saves are in `CT_CPU_SLOW_TESTS` (`port/src/tests/CMakeLists.txt`, 20 s) for that reason.
- Map placement tables: 2048 entries of 0x310 + 8 bytes (1.6 MB) for a town and for an interior, 4640 (3.7 MB) for
  a dungeon floor, reserved on the map's scene stack at load and initialised by `ClearPlaceParts`. The arena roots
  hold four times retail (`kArenaHeadroom`), so this fits beside retail's own use with room to spare. Retail-sized
  maps cost nothing at draw time beyond the per-frame scans of `place_parts_max` entries in `map.cpp`
  (`DrawScreenFunc`, the sound and function-point searches), which read one byte per entry. `NewPlaceParts` finds
  the first free entry linearly, so placing n parts is O(n^2): negligible at retail floor sizes, tens of milliseconds
  for a completely filled 60 x 60 floor.
- `CScene` holds 256 character slots, `CMonsterMan` 128 slot memories, `CShop` a price table of 2048 entries and
  `CMenuGeorama` two lists of 1024: static and arena sizes grow accordingly (the arena test lists the latter two).

## Blockers: limits left at retail

- **Villager slots** (`SCENE_VILLAGER_SLOT_NUM` 16, `SCENE_SUB_VILLAGER_SLOT_NUM` 8; targets 64 and 32).
  Retail event scripts address scene character slots by number (`EVENT_DATA_CHARA_SLOT`, the `SCENE_TALK_SLOT_END`
  = 0x40 search range and the villager placement in `scenevillager.cpp`), and the sub-map block starts right after
  the main block (0x18). Widening either block moves every slot after it and breaks the retail scripts' numbers,
  so the raise needs a slot remapping layer (retail number to port slot) first. The scene itself already holds 256
  slots, so the blocks can grow in place once the remapping exists.
- **Inventory paging** (`ITEM_BOARD_PLACE_NUM` 150 of `USED_DATA_PLACE_MAX` 400). The storage and the save hold
  400 places, but `GetItemBoardMaxNum(ITEM_BOARD_ALL)` keeps 150: the item board's geometry is retail's
  (138 or 144 board places plus a 12 or 6 place overflow, `GetItemBoardOverNum`), and the menus' row and column
  arithmetic (`menusys.cpp`, `menudraw.cpp`, `menu_mouse_*.cpp`) is laid out for it. Paging the remaining 250
  places needs a menu layout and a setting that enables it; until then items beyond 150 are never filled.
- **Georama record arrays** (`EDIT_DATA_PARTS_MAX` 300, `EDIT_DATA_HOUSE_MAX` 32). The `CEditData` record keeps
  retail's shape (`PORT_RECORD_ASSERT(sizeof(CEditData) == 0x5510)`) because the port already lifts the limit at
  run time: placed parts and houses live in the `TownLayout` beside the record, bounded only by
  `kPortEditSlotCeiling`, and a retail record decodes into one (`TownLayoutFromRecord`). Raising the record would
  gain nothing and change the retail image.
- **Playable characters** (`USER_CHARA_NUM` 4): registration of further characters is work package E; the party
  records (`CHARA_DATA chara_data[2]`, `PARTY_CHARA_INFO`) are shaped by retail and move with that package.
- **Legacy host-layout images**: a flat `SAVEDATA_FORMAT` image or an emulated memory card file written by a
  port build before this raise (`save_slots.cpp:133,176`, `memcard.cpp`) has the old host layout and is reported
  as `WrongSize`; the JSON save is the supported format and reads either count.
