# Capacity layer: progress

Running note for work package A of DC2_PLAN.md. Updated after each milestone so a successor can continue.

## Done

- `port/include/capacity.hpp` written: one `capacity::k*` constant per limit, PORT only, with static checks.
- 23 headers take their value from it (NativeArchitecture.Check forbids `#ifdef PORT` in port/: the port tree is
  PORT-only, so the retail value stays in the comment and ps2/include keeps it):
  automap, dng_event, dng_main, dngfloor, editdata, editmap, editmenu, gamedata, menudraw, menushop, menusys,
  monster, npccfg, savedata, savedatadungeon, scenesnd, scenevillager, userdata, villagermngr, stubs/gamedata,
  stubs/npccfg, moonmod/gamedata_moonmod, moonmod/savedatadungeon_moonmod.

- Headers that use `capacity::` include `capacity.hpp` themselves; `colprim.hpp` `hit_characters` bitset sized
  from `kSceneCharaSlots` (found by the build: `colprim.cpp:19` checks it against `CScene::chara`).
- Source sites edited (see capacity.md "Audit" for the list), save_json.cpp accepts shorter arrays, the
  moonmod_entities_api_test put_flag lines, `port/docs/capacity.md`, `port/src/tests/capacity_test.cpp` written.

- Build green (`ct_build`). Dependent sites the build or tests found and fixed: `colprim.hpp` hit bitset,
  `chronicle_two_adapter.cpp` Georama piece cap, `arena_placement_test` kOutgrowing (+CMenuGeorama, +CShop),
  `moonmod_dungeon_registration_test` (area DNG_GRID + 1), `native_dungeon_test` (MONSTER_LOCATE_MAX + 8 entries),
  `moonmod_entities_api_test` put_flag. save_json.cpp Reader: an array shorter than its field fills the front
  and leaves the rest as the cleared document has it (no memset: the reader's `InitPlaceData` must survive).
- `port/src/tests/CMakeLists.txt`: CT_CPU_SLOW_TESTS group (20 s) for the two SaveSlotTest cases that write 31
  saves; a save is now ~824 KB and SaveJsonWrite takes ~150 ms, SaveJsonRead ~90 ms (default document).
- ctest etiquette (coordinator): write scratchpad/ctest-lock.A while a full run goes, remove after; never relink
  while scratchpad/ctest-lock.F exists.

- Full CPU ctest run 1: 3 new failures by name, all fixed: NativeArchitecture.Check (the `#ifdef PORT` blocks,
  stripped), NativeConfigLoaders.NpcRecordsKeepTheTableBoundsAndNullTexts (180 literal, now NPC_BASE_DATA_MAX),
  TestHooks.JumpAcceptsModesAndBounds (debug jump now bounded by GetSavedDungeonFloorCount, not record presence).
  GfxValidation.LayerIsInstalled passed (was in the baseline). Run 2 in progress (cap_ctest_full2.log).

## Session 3 (finishing)

- Disk ran out at the end of session 2 (run 2 was cut off at 1704/2430; its ***Failed lines for
  EventAssetPaths.KeepsEnglishAndUnrecognizedJsonNames, PlatformConfig.TinyFrameCapsClampButZeroRemainsUncapped and
  MoonModAudio.PackageSoundMixesStopsAndIsCleanedUpByOwner fell in the window the disk was full: to be rerun alone).
- Toolkit note 1 (NpcBaseData): already consistent: definition `npccfg.cpp:31`, declaration `stubs/npccfg.hpp:7`
  both `NPC_BASE_DATA_MAX`, loader clamps by `std::size(NpcBaseData)` (`npccfg.cpp:230`).
- Toolkit note 2 (map placements): raised. `capacity::kMapPlacementsTown` 2048 (retail 0x140) and
  `kMapPlacementsDungeon` (60+8)^2+16 = 4640 (retail 400), as `MAP_PLACE_PARTS_TOWN/DUNGEON` in `map.hpp`; used by
  `mapjump.cpp` (2 sites), `dng_event.cpp` LoadDungeonMapFile, `automap.cpp` overview clip array (was 1024) and a
  static check at the generator. Budget: 0x310+8 bytes per entry from the map's scene stack (MainBuffer arena,
  4x retail headroom). Title keeps 0x190. New test Capacity.MapPlacementTablesFillToTheLimitAndReadBack.
- Build (cap_build12.log): clean. Capacity/MapStride/AutomapStorage/NativeDungeon: 30/30 pass (cap_subset3.log).
  The eight cases that failed in runs 1-2 pass alone (cap_rerun3.log).
- Full run 3 (cap_ctest_full3.log, 2778 tests, 547 s): 13 failed. Baseline: Crash.* (fixture "Required"; now
  eight with Crash.segv-frame), Log.SessionId, LogFiles x2, MoonModManagerUi. Not baseline and not ours:
  GfxMisc.MeshBoundsFollowTheFlagAndAreInvalidatedByUpdates (vendored Kazarov, unchanged since cb6ae0f3:
  CreateMesh computes bounds unconditionally, resources.cpp:1238-1258, while the test expects them only under
  KZ_FOG_NEAR_SAFE=1; fails alone and repeatably, cap_rerun4.log). Disc-only cases are skipped, not passed.
- capacity.md: map placement rows and cost added.

## In progress (session 2)

1. (done) Headers that use `capacity::` include `capacity.hpp` themselves.
2. (done) Source sites: dng_main.cpp grid (900/30), scene.cpp (chara_num = 128), userdata.cpp and menudraw.cpp and
   menucommon.cpp (150), monster.cpp and dng_event.cpp (put_flag bitset), gamedata.cpp (162/116), npccfg.cpp (180),
   villagermngr.cpp (32), editmenu.cpp and chronicle_two_adapter.cpp (0x180), savedata.cpp (0x100 build parts),
   savedatadungeon.cpp (extra floor records), rows.cpp/config.hpp text (60 monsters).
3. save_json.cpp: an array shorter than its field is accepted and the rest cleared (retail-count saves load).
4. Tests: arena_placement_test kOutgrowing (CShop, CMenuGeorama), moonmod_entities_api_test put_flag,
   new port/src/tests/capacity_test.cpp.
5. port/docs/capacity.md (audit table and blockers).
6. Build, full CPU ctest against tools/gauntlet/baselines/linux-x86_64.json.

## Next

See "In progress"; anything unchecked above is still to do.
