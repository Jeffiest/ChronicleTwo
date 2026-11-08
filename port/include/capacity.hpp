#pragma once

/**
 * @file
 * The port's capacities: every limit the native port raises above retail, each beside the retail value it
 * replaces. The port's headers define their constants from here and name the retail value in their comments;
 * the PS2 build's headers (ps2/include) keep retail. port/docs/capacity.md lists each limit's dependent sites.
 *
 * A capacity that reaches a save record says so; the port's JSON saves write the full count and read a save
 * written with a smaller count (the retail count, or an older port's) by leaving the rest of the array clear.
 */

namespace capacity {

// Dungeon generation (automap.hpp, dng_main.cpp).
inline constexpr int kDungeonRooms = 160;                          /**< AUTOMAP_ROOM_MAX; retail 64. */
inline constexpr int kDungeonGrid = 60;                            /**< DNG_GRID, side of the floor grid; retail 30. */
inline constexpr int kDungeonGridCells = kDungeonGrid * kDungeonGrid; /**< CAutoMapParts cells of a floor; retail 900. */
inline constexpr int kDungeonFloors = 128;                         /**< DNGMAP_FLOOR_NUM, floors searched per dungeon; retail 40. */
inline constexpr int kTreasureBoxes = 128;                         /**< TREASURE_BOX_MAX; retail 24. */
inline constexpr int kPullItems = 256;                             /**< DNG_PULL_ITEM_NUM, floor pickups; retail 72. */

// Monsters (monster.hpp). The extra active slots are used only with the more_monsters setting.
inline constexpr int kActiveMonsters = 128;       /**< MONSTER_ACTIVE_MAX; retail 24, upstream port 60. */
inline constexpr int kMonsterLocateEntries = 128; /**< MONSTER_LOCATE_MAX; retail 32. */
inline constexpr int kMonsterKinds = 32;          /**< MONSTER_REFER_MAX; retail 12. Texture blocks 0x28..0x47, below the laser gun's 0x4A. */

// Scenes (scenesnd.hpp, scenevillager.hpp). The villager slot blocks keep their retail numbers: retail event
// scripts address scene slots 8..0x40 by number, so widening the blocks needs a slot remapping layer first.
inline constexpr int kSceneCharaSlots = 256;  /**< SCENE_CHARA_SLOT_NUM; retail 0x80. Holds 24 + kActiveMonsters dungeon slots. */
inline constexpr int kVillagerSlots = 16;     /**< SCENE_VILLAGER_SLOT_NUM; retail 16 (target 64, blocked). */
inline constexpr int kSubVillagerSlots = 8;   /**< SCENE_SUB_VILLAGER_SLOT_NUM; retail 8 (target 32, blocked). */
inline constexpr int kVillagerRecords = 128;  /**< CVillagerMngr::data entries; retail 32. */
inline constexpr int kNpcRecords = 512;       /**< NpcBaseData entries; retail 180. */

// Save records (savedatadungeon.hpp, savedata.hpp, userdata.hpp). These reach the JSON save.
inline constexpr int kSaveDungeonFloorRecords = 512; /**< SAVE_DUNGEON_FLOOR_NUM; retail 162, the sum of limmit_table. */
inline constexpr int kSaveBuildParts = 1024;         /**< SAVE_BUILD_PARTS_MAX, part definitions whose held count is kept; retail 0x100. */
inline constexpr int kInventoryPlaces = 400;         /**< MENU_BAG_PLACE_MAX and CUserDataManager::used_data; retail 150. */

// Georama (editmenu.hpp, editdata.hpp). Placed parts and houses are uncapped at run time (TownLayout,
// kPortEditSlotCeiling); the CEditData record keeps retail's arrays as the shape of a retail save.
inline constexpr int kGeoramaPartsList = 1024; /**< GEORAMA_PARTS_LIST_MAX; retail 0x180. */
inline constexpr int kEditDataParts = 300;     /**< EDIT_DATA_PARTS_MAX, record array; retail 300 (TownLayout holds the rest). */
inline constexpr int kEditDataHouses = 32;     /**< EDIT_DATA_HOUSE_MAX and EDIT_MAP_HOUSE_MAX, record array; retail 32. */

// Items (gamedata.hpp, menushop.hpp).
inline constexpr int kItemNumbers = 2048;   /**< ITEM_NO_MAX and SHOP_PRICE_MAX; retail 0x200. */
inline constexpr int kCommonItems = 1024;   /**< ITEM_COMMON_DATA_MAX; retail 432. */
inline constexpr int kItemRecords = 512;    /**< ITEM_DATA_MAX, usable item records; retail 162. */
inline constexpr int kWeaponRecords = 512;  /**< WEAPON_DATA_MAX, weapon records; retail 116. */
inline constexpr int kShopGoods = 256;      /**< SHOP_ITEM_MAX; retail 0x40. */

// Party (userdata.hpp). Registered playable characters are work package E.
inline constexpr int kPlayableCharacters = 4; /**< USER_CHARA_NUM; retail 4. */

// Map placements (map.hpp): the placed-part table a map reserves on its scene stack when it loads
// (CMap::SetPlacePartsBuff, 0x310 bytes and a pointer per entry). The title map keeps retail's 0x190.
inline constexpr int kMapPlacementsTown = 2048; /**< MAP_PLACE_PARTS_TOWN, town and interior maps (mapjump.cpp); retail 0x140. */
/** MAP_PLACE_PARTS_DUNGEON, dungeon floors (LoadDungeonMapFile); retail 400. A generated floor places one part
 * per used grid cell and SetDummyTree one per free cell of the grid widened by four on each side, so the table
 * holds (kDungeonGrid + 8) squared and the generator's handful of fixed parts (gio, random stones, pot, mountain). */
inline constexpr int kMapPlacementsDungeon = (kDungeonGrid + 8) * (kDungeonGrid + 8) + 16;

static_assert(kDungeonGrid >= 30 && kDungeonRooms >= 64);
static_assert(kActiveMonsters >= 60 && kMonsterLocateEntries >= kActiveMonsters);
static_assert(kSceneCharaSlots >= 24 + kActiveMonsters);
static_assert(kSceneCharaSlots >= 0x80, "retail scene slots, the game objects at 0x78..0x7B among them, keep their numbers");
static_assert(kSaveDungeonFloorRecords >= 162 && (kSaveDungeonFloorRecords - 162) % 7 == 0,
              "the records past retail's 162 are shared equally by the seven dungeons");
static_assert(kInventoryPlaces >= 150 && kSaveBuildParts >= 0x100);
static_assert(kCommonItems % 8 == 0, "CGameData::InitItemMes clears the common table eight entries at a time");
static_assert(kItemNumbers >= 0x200 && kItemNumbers <= 0x7FFF, "item numbers are s16 in saves");
static_assert(kMapPlacementsTown >= 0x140 && kMapPlacementsDungeon >= 400);

} // namespace capacity
