#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "automap.hpp"
#include "capacity.hpp"
#include "colprim.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "dngfloor.hpp"
#include "editdata.hpp"
#include "editmap.hpp"
#include "editmenu.hpp"
#include "gamedata.hpp"
#include "map.hpp"
#include "menushop.hpp"
#include "menusys.hpp"
#include "monster.hpp"
#include "npccfg.hpp"
#include "save_json.hpp"
#include "savedata.hpp"
#include "savedatadungeon.hpp"
#include "scenesnd.hpp"
#include "scenevillager.hpp"
#include "stubs/gamedata.hpp"
#include "stubs/npccfg.hpp"
#include "userdata.hpp"
#include "villagermngr.hpp"

// Every limit the port raises takes its PORT value from capacity.hpp (DC2_PLAN.md "Where the limits are").

namespace {

using Json = nlohmann::json;

/** Saved floor records of each dungeon in retail, as limmit_table in savedatadungeon.cpp. */
constexpr int kRetailFloors[SAVE_DUNGEON_NUM] = {9, 16, 25, 21, 23, 29, 39};

// The document's JSON with a change applied, and without the checksum that the change would break.
std::string Edited(const std::string &text, const auto &edit) {
    Json json = Json::parse(text);
    json.erase("checksum");
    edit(json);
    return json.dump(2);
}

} // namespace

TEST(Capacity, HeadersTakeTheirPortValuesFromCapacity) {
    static_assert(AUTOMAP_ROOM_MAX == capacity::kDungeonRooms && AUTOMAP_ROOM_MAX == 160);
    static_assert(DNG_GRID == capacity::kDungeonGrid && DNG_GRID == 60 && DNG_GRID_CELLS == 3600);
    static_assert(DNGMAP_FLOOR_NUM == capacity::kDungeonFloors && DNGMAP_FLOOR_NUM == 128);
    static_assert(static_cast<int>(SAVE_DUNGEON_FLOOR_ID_MAX) == static_cast<int>(DNGMAP_FLOOR_NUM));
    static_assert(TREASURE_BOX_MAX == capacity::kTreasureBoxes && TREASURE_BOX_MAX == 128);
    static_assert(DNG_PULL_ITEM_NUM == capacity::kPullItems && DNG_PULL_ITEM_NUM == 256);
    static_assert(MONSTER_ACTIVE_MAX == capacity::kActiveMonsters && MONSTER_ACTIVE_MAX == 128);
    static_assert(MONSTER_LOCATE_MAX == capacity::kMonsterLocateEntries && MONSTER_LOCATE_MAX == 128);
    static_assert(MONSTER_REFER_MAX == capacity::kMonsterKinds && MONSTER_REFER_MAX == 128);
    static_assert(MONSTER_TEXB_REFER_NUM == capacity::kMonsterKindBlocks && MONSTER_TEXB_REFER_NUM == 32);
    static_assert(MONSTER_MORE_MAX == capacity::kActiveMonsters);
    static_assert(MonsterReferTexb(31) == 0x47 && MonsterReferTexb(32) == MONSTER_TEXB_REFER_EXTRA);
    static_assert(SCENE_CHARA_SLOT_NUM == capacity::kSceneCharaSlots && SCENE_CHARA_SLOT_NUM == 256);
    static_assert(SCENE_VILLAGER_SLOT_NUM == capacity::kVillagerSlots && SCENE_VILLAGER_SLOT_NUM == 16);
    static_assert(SCENE_SUB_VILLAGER_SLOT_NUM == capacity::kSubVillagerSlots && SCENE_SUB_VILLAGER_SLOT_NUM == 8);
    static_assert(SCENE_SUB_VILLAGER_SLOT_TOP == 0x18);
    static_assert(VILLAGER_DATA_MAX == capacity::kVillagerRecords && VILLAGER_DATA_MAX == 128);
    static_assert(NPC_BASE_DATA_MAX == capacity::kNpcRecords && NPC_BASE_DATA_MAX == 512);
    static_assert(SAVE_DUNGEON_FLOOR_NUM == capacity::kSaveDungeonFloorRecords && SAVE_DUNGEON_FLOOR_NUM == 512);
    static_assert(SAVE_DUNGEON_RETAIL_FLOOR_NUM == 162 && SAVE_DUNGEON_FLOOR_EXTRA == 50);
    static_assert(SAVE_BUILD_PARTS_MAX == capacity::kSaveBuildParts && SAVE_BUILD_PARTS_MAX == 1024);
    static_assert(USED_DATA_PLACE_MAX == capacity::kInventoryPlaces && USED_DATA_PLACE_MAX == 400);
    static_assert(static_cast<int>(MENU_BAG_PLACE_MAX) == static_cast<int>(USED_DATA_PLACE_MAX) && ITEM_BOARD_PLACE_NUM == 150);
    static_assert(GEORAMA_PARTS_LIST_MAX == capacity::kGeoramaPartsList && GEORAMA_PARTS_LIST_MAX == 1024);
    static_assert(EDIT_DATA_PARTS_MAX == capacity::kEditDataParts && EDIT_DATA_PARTS_MAX == 300);
    static_assert(EDIT_DATA_HOUSE_MAX == capacity::kEditDataHouses && static_cast<int>(EDIT_MAP_HOUSE_MAX) == EDIT_DATA_HOUSE_MAX);
    static_assert(ITEM_NO_MAX == capacity::kItemNumbers && ITEM_NO_MAX == 2048);
    static_assert(ITEM_COMMON_DATA_MAX == capacity::kCommonItems && ITEM_COMMON_DATA_MAX == 1024);
    static_assert(ITEM_DATA_MAX == capacity::kItemRecords && ITEM_DATA_MAX == 512);
    static_assert(WEAPON_DATA_MAX == capacity::kWeaponRecords && WEAPON_DATA_MAX == 512);
    static_assert(SHOP_ITEM_MAX == capacity::kShopGoods && SHOP_ITEM_MAX == 256);
    static_assert(static_cast<int>(SHOP_PRICE_MAX) == static_cast<int>(ITEM_NO_MAX));
    static_assert(USER_CHARA_NUM == capacity::kPlayableCharacters && USER_CHARA_NUM == 4);
    static_assert(MAP_PLACE_PARTS_TOWN == capacity::kMapPlacementsTown && MAP_PLACE_PARTS_TOWN == 2048);
    static_assert(MAP_PLACE_PARTS_DUNGEON == capacity::kMapPlacementsDungeon && MAP_PLACE_PARTS_DUNGEON == 4640);
    static_assert(MAP_PLACE_PARTS_DUNGEON >= (DNG_GRID + 8) * (DNG_GRID + 8) + AUTOMAP_RANDOM_STONE_MAX + 3);
    // The record arrays are sized from the same constants.
    static_assert(std::extent_v<decltype(CSaveDataDungeon::floor_info)> == SAVE_DUNGEON_FLOOR_NUM);
    static_assert(std::extent_v<decltype(CSaveData::build_parts_num)> == SAVE_BUILD_PARTS_MAX);
    static_assert(std::extent_v<decltype(CUserDataManager::used_data)> == USED_DATA_PLACE_MAX);
    static_assert(std::extent_v<decltype(CTreasureBoxManager::box)> == TREASURE_BOX_MAX);
    static_assert(std::extent_v<decltype(CMonsterLocateInfo::monster_id)> == MONSTER_LOCATE_MAX);
    static_assert(std::extent_v<decltype(CMonsterMan::memory)> == MONSTER_ACTIVE_MAX);
    static_assert(std::extent_v<decltype(CMonsterMan::refer)> == MONSTER_REFER_MAX);
    static_assert(std::extent_v<decltype(CScene::chara)> == SCENE_CHARA_SLOT_NUM);
    static_assert(decltype(CColPrim::hit_characters){}.size() == SCENE_CHARA_SLOT_NUM);
    static_assert(decltype(CMonsterLocateInfo::put_flag){}.size() == MONSTER_ACTIVE_MAX);
    static_assert(std::extent_v<decltype(CAutoMapGen::room)> == AUTOMAP_ROOM_MAX);
    static_assert(std::extent_v<decltype(CMenuGeorama::stock_list)> == GEORAMA_PARTS_LIST_MAX);
    static_assert(std::extent_v<decltype(CMenuGeorama::make_list)> == GEORAMA_PARTS_LIST_MAX);
    static_assert(std::extent_v<decltype(CShop::item_no)> == SHOP_ITEM_MAX);
    static_assert(std::extent_v<decltype(CShop::price)> == SHOP_PRICE_MAX);
    static_assert(std::extent_v<decltype(CVillagerMngr::data)> == VILLAGER_DATA_MAX);
    static_assert(std::extent_v<decltype(PullItem)> == DNG_PULL_ITEM_NUM);
    static_assert(std::extent_v<decltype(NpcBaseData)> == NPC_BASE_DATA_MAX);
    static_assert(std::extent_v<decltype(local_itemdata)> == ITEM_DATA_MAX + MOONMOD_ITEM_CAPACITY);
    static_assert(std::extent_v<decltype(local_weapondata)> == WEAPON_DATA_MAX + MOONMOD_WEAPON_CAPACITY);
    static_assert(std::extent_v<decltype(local_com_itemdata)> == ITEM_COMMON_DATA_MAX + MOONMOD_ITEM_CAPACITY + MOONMOD_WEAPON_CAPACITY);
    static_assert(std::extent_v<decltype(local_itemdatano_converttable)> == ITEM_NO_MAX);
    // The dungeon's monster slots, and retail's fixed game object slots, fit the scene.
    static_assert(static_cast<int>(DNG_CHARA_SLOT_MONSTER_END) <= static_cast<int>(SCENE_CHARA_SLOT_NUM));
    static_assert(SCENE_GAMEOBJ_SLOT_BOOK < SCENE_CHARA_SLOT_NUM && SCENE_TALK_SLOT_END <= SCENE_CHARA_SLOT_NUM);
    SUCCEED();
}

TEST(Capacity, TreasureBoxesFillToTheLimitAndReadBack) {
    auto manager = std::make_unique<CTreasureBoxManager>();
    manager->Initialize();
    for (int i = 0; i < TREASURE_BOX_MAX; ++i) {
        manager->box[i].state = TREASURE_BOX_STATE_UNOPENED;
        manager->box[i].item[0] = static_cast<s16>(i + 1);
        manager->box[i].num[0] = static_cast<s16>(TREASURE_BOX_MAX - i);
    }
    for (int i = 0; i < TREASURE_BOX_MAX; ++i) {
        EXPECT_EQ(manager->box[i].state, TREASURE_BOX_STATE_UNOPENED) << i;
        EXPECT_EQ(manager->box[i].item[0], i + 1) << i;
        EXPECT_EQ(manager->box[i].num[0], TREASURE_BOX_MAX - i) << i;
    }
    EXPECT_EQ(manager->near_box, -1);
}

TEST(Capacity, MonsterLocateListFillsAndCountsEverySlot) {
    CMonsterLocateInfo locate{};
    locate.num = MONSTER_LOCATE_MAX;
    for (int i = 0; i < MONSTER_LOCATE_MAX; ++i) {
        locate.monster_id[i] = static_cast<s16>(0x400 + i);
        locate.param[i] = static_cast<s16>(i);
    }
    // Every active slot has a bit: filling one twice counts it once, and a slot past the end is ignored.
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        locate.SetPutFlag(slot, 1);
        locate.SetPutFlag(slot, 1);
    }
    locate.SetPutFlag(MONSTER_ACTIVE_MAX, 1);
    locate.SetPutFlag(-1, 1);
    EXPECT_EQ(locate.put_num, MONSTER_ACTIVE_MAX);
    EXPECT_TRUE(locate.put_flag.all());
    for (int i = 0; i < MONSTER_LOCATE_MAX; ++i) {
        EXPECT_EQ(locate.monster_id[i], 0x400 + i) << i;
        EXPECT_EQ(locate.param[i], i) << i;
    }
    for (int slot = MONSTER_ACTIVE_MAX - 1; slot >= 0; --slot) {
        locate.SetPutFlag(slot, 0);
        locate.SetPutFlag(slot, 0);
        EXPECT_EQ(locate.put_num, slot) << slot;
    }
    EXPECT_TRUE(locate.put_flag.none());
}

TEST(Capacity, GeoramaPartsListsHoldTheirEntries) {
    auto menu = std::make_unique<CMenuGeorama>();
    for (int i = 0; i < GEORAMA_PARTS_LIST_MAX; ++i) {
        menu->stock_list[i].no = i;
        menu->stock_list[i].num = 1 + i % 9;
        menu->make_list[i].no = GEORAMA_PARTS_LIST_MAX - i;
    }
    menu->stock_num = GEORAMA_PARTS_LIST_MAX;
    menu->make_num = GEORAMA_PARTS_LIST_MAX;
    for (int i = 0; i < GEORAMA_PARTS_LIST_MAX; ++i) {
        EXPECT_EQ(menu->stock_list[i].no, i) << i;
        EXPECT_EQ(menu->stock_list[i].num, 1 + i % 9) << i;
        EXPECT_EQ(menu->make_list[i].no, GEORAMA_PARTS_LIST_MAX - i) << i;
    }
}

TEST(Capacity, InventoryPlacesFillAndReadBackThroughTheManager) {
    auto manager = std::make_unique<CUserDataManager>();
    for (int i = 0; i < USED_DATA_PLACE_MAX; ++i) {
        CGameDataUsed *place = manager->GetUsedDataPtr(i);
        ASSERT_NE(place, nullptr) << i;
        place->item_no = static_cast<s16>(1 + i);
    }
    EXPECT_EQ(manager->GetUsedDataPtr(USED_DATA_PLACE_MAX), nullptr);
    EXPECT_EQ(manager->GetUsedDataPtr(-1), nullptr);
    for (int i = 0; i < USED_DATA_PLACE_MAX; ++i) {
        EXPECT_EQ(manager->used_data[i].item_no, 1 + i) << i;
    }
    // The menus page through retail's 150 places until a layout for the rest exists (capacity.md).
    static_assert(ITEM_BOARD_PLACE_NUM <= USED_DATA_PLACE_MAX);
}

TEST(Capacity, FloorRecordsCoverRetailFloorsInPlaceAndExtraFloorsAfterThem) {
    auto save = std::make_unique<CSaveDataDungeon>();
    save->Initialize();
    std::set<DNG_FLOOR_SAVE *> records;
    int                        retail_index = 0;
    for (int dungeon = 0; dungeon < SAVE_DUNGEON_NUM; ++dungeon) {
        for (int floor = 0; floor < kRetailFloors[dungeon]; ++floor) {
            DNG_FLOOR_SAVE *record = save->GetFloorInfoPtr(dungeon, floor);
            ASSERT_EQ(record, &save->floor_info[retail_index++]) << dungeon << " " << floor;
            records.insert(record);
        }
        for (int extra = 0; extra < SAVE_DUNGEON_FLOOR_EXTRA; ++extra) {
            DNG_FLOOR_SAVE *record = save->GetFloorInfoPtr(dungeon, kRetailFloors[dungeon] + extra);
            ASSERT_EQ(record, &save->floor_info[SAVE_DUNGEON_RETAIL_FLOOR_NUM + dungeon * SAVE_DUNGEON_FLOOR_EXTRA + extra])
                << dungeon << " " << extra;
            records.insert(record);
            record->visit_count = static_cast<s16>(1 + extra);
            record->kill_count = static_cast<s16>(dungeon);
        }
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, kRetailFloors[dungeon] + SAVE_DUNGEON_FLOOR_EXTRA), nullptr) << dungeon;
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, -1), nullptr) << dungeon;
    }
    EXPECT_EQ(retail_index, SAVE_DUNGEON_RETAIL_FLOOR_NUM);
    EXPECT_EQ(records.size(), static_cast<std::size_t>(SAVE_DUNGEON_FLOOR_NUM));
    EXPECT_EQ(save->GetFloorInfoPtr(SAVE_DUNGEON_NUM, 0), nullptr);
    EXPECT_EQ(save->GetFloorInfoPtr(-1, 0), nullptr);
    // Initialize opened floors 0 and 1 of every dungeon, as retail does, and the extra records read back.
    for (int dungeon = 0; dungeon < SAVE_DUNGEON_NUM; ++dungeon) {
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, 0)->visit_count, 1) << dungeon;
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, 1)->flag, 1) << dungeon;
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, kRetailFloors[dungeon] + SAVE_DUNGEON_FLOOR_EXTRA - 1)->visit_count,
                  SAVE_DUNGEON_FLOOR_EXTRA)
            << dungeon;
        EXPECT_EQ(save->GetFloorInfoPtr(dungeon, kRetailFloors[dungeon])->kill_count, dungeon) << dungeon;
    }
}

// A map's placed-part table takes every placement up to the limit, refuses the next, and reads each back by index.
TEST(Capacity, MapPlacementTablesFillToTheLimitAndReadBack) {
    for (const int limit : {static_cast<int>(MAP_PLACE_PARTS_TOWN), static_cast<int>(MAP_PLACE_PARTS_DUNGEON)}) {
        const auto             bytes = static_cast<std::size_t>(limit) * (sizeof(CMapParts) + sizeof(CMapParts *));
        std::vector<u_long128> storage(bytes / sizeof(u_long128) + 64);
        mgCMemory              memory;
        memory.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        auto map = std::make_unique<CMap>();
        ASSERT_TRUE(map->SetPlacePartsBuff(&memory, limit)) << limit;
        ASSERT_EQ(map->place_parts_max, limit);
        for (int i = 0; i < limit; ++i) {
            CMapParts *parts = map->NewPlaceParts();
            ASSERT_EQ(parts, &map->place_parts[i]) << limit << " " << i;
            std::string name = "p" + std::to_string(i);
            parts->SetName(name.data());
        }
        EXPECT_EQ(map->NewPlaceParts(), nullptr) << limit;
        map->place_parts_num = limit;
        for (int i = 0; i < limit; ++i) {
            CMapParts *parts = map->GetPlaceParts(i);
            ASSERT_NE(parts, nullptr) << limit << " " << i;
            EXPECT_STREQ(parts->name, ("p" + std::to_string(i)).c_str()) << limit << " " << i;
        }
        EXPECT_EQ(map->GetPlaceParts(limit), nullptr) << limit;
    }
}

TEST(Capacity, ItemAndWeaponTablesHoldTheRaisedCounts) {
    EXPECT_EQ(std::size(local_itemdata), static_cast<std::size_t>(ITEM_DATA_MAX + MOONMOD_ITEM_CAPACITY));
    EXPECT_EQ(std::size(local_weapondata), static_cast<std::size_t>(WEAPON_DATA_MAX + MOONMOD_WEAPON_CAPACITY));
    EXPECT_EQ(std::size(local_itemdatano_converttable), static_cast<std::size_t>(ITEM_NO_MAX));
    // The last record of every table is addressable and keeps what is written.
    local_itemdata[ITEM_DATA_MAX - 1].use_flags = 0x1234;
    local_weapondata[WEAPON_DATA_MAX - 1].durability = 77;
    local_com_itemdata[ITEM_COMMON_DATA_MAX - 1].item_no = static_cast<s16>(ITEM_NO_MAX - 1);
    local_itemdatano_converttable[ITEM_NO_MAX - 1] = static_cast<short>(ITEM_COMMON_DATA_MAX - 1);
    EXPECT_EQ(local_itemdata[ITEM_DATA_MAX - 1].use_flags, 0x1234u);
    EXPECT_EQ(local_weapondata[WEAPON_DATA_MAX - 1].durability, 77);
    EXPECT_EQ(local_com_itemdata[ITEM_COMMON_DATA_MAX - 1].item_no, ITEM_NO_MAX - 1);
    EXPECT_EQ(local_itemdatano_converttable[ITEM_NO_MAX - 1], ITEM_COMMON_DATA_MAX - 1);
    // Item numbers are s16 in the save records.
    EXPECT_LE(ITEM_NO_MAX - 1, 0x7FFF);
}

TEST(Capacity, VillagerAndNpcRecordsHoldTheRaisedCounts) {
    auto manager = std::make_unique<CVillagerMngr>();
    EXPECT_EQ(manager->data_num, VILLAGER_DATA_MAX);
    for (int i = 0; i < VILLAGER_DATA_MAX; ++i) {
        ASSERT_NE(manager->GetData(i), nullptr) << i;
        EXPECT_EQ(manager->GetData(i), &manager->data[i]) << i;
    }
    EXPECT_EQ(manager->GetData(VILLAGER_DATA_MAX), nullptr);
    EXPECT_EQ(std::size(NpcBaseData), static_cast<std::size_t>(NPC_BASE_DATA_MAX));
    NpcBaseData[NPC_BASE_DATA_MAX - 1].chara_no = 4321;
    EXPECT_EQ(NpcBaseData[NPC_BASE_DATA_MAX - 1].chara_no, 4321);
}

TEST(Capacity, SceneSlotsHoldTheDungeonMonsterSlots) {
    auto scene = std::make_unique<CScene>();
    EXPECT_EQ(std::size(scene->chara), static_cast<std::size_t>(SCENE_CHARA_SLOT_NUM));
    EXPECT_GE(static_cast<int>(SCENE_CHARA_SLOT_NUM), static_cast<int>(DNG_CHARA_SLOT_MONSTER_END));
    EXPECT_GE(static_cast<int>(SCENE_CHARA_SLOT_NUM), 0x80);
}

TEST(Capacity, SaveRoundTripsTheRaisedCountsAndLoadsRetailCounts) {
    auto       document = std::make_unique<SaveDocument>();
    CSaveData &save = document->image.save_data;
    // What a read gives back for a new document: the Ver4 version, initialised town records, and photo pointers
    // left for the album to bind.
    std::strcpy(document->image.version, "dc2Ver4");
    for (CEditData &data : save.edit_data) {
        std::memset(data.place_log, 0, sizeof(data.place_log));
        data.InitPlaceData();
    }
    for (USER_PICTURE_INFO &photo : save.user_data.invent_data.photo) {
        auto *start = reinterpret_cast<unsigned char *>(&photo) + offsetof(USER_PICTURE_INFO, unk_c) + sizeof(photo.unk_c);
        std::memset(start, 0, sizeof(photo) - (offsetof(USER_PICTURE_INFO, unk_c) + sizeof(photo.unk_c)));
    }
    for (int i = 0; i < SAVE_DUNGEON_FLOOR_NUM; ++i) {
        save.save_dungeon.floor_info[i].visit_count = static_cast<s16>(1 + i % 100);
        save.save_dungeon.floor_info[i].kill_count = static_cast<s16>(i);
    }
    for (int i = 0; i < SAVE_BUILD_PARTS_MAX; ++i) {
        save.build_parts_num[i] = static_cast<s16>(i % 10);
    }
    for (int i = 0; i < USED_DATA_PLACE_MAX; ++i) {
        save.user_data.used_data[i].item_no = static_cast<s16>(1 + i);
    }
    const std::string text = SaveJsonWrite(*document);

    // New saves write the full counts.
    const Json json = Json::parse(text);
    EXPECT_EQ(json["save"]["save_dungeon"]["floor_info"].size(), static_cast<std::size_t>(SAVE_DUNGEON_FLOOR_NUM));
    EXPECT_EQ(json["save"]["build_parts_num"].size(), static_cast<std::size_t>(SAVE_BUILD_PARTS_MAX));
    EXPECT_EQ(json["save"]["user_data"]["used_data"].size(), static_cast<std::size_t>(USED_DATA_PLACE_MAX));

    auto read = std::make_unique<SaveDocument>();
    ASSERT_EQ(SaveJsonRead(text, *read), SaveJsonError::None);
    EXPECT_EQ(std::memcmp(&read->image, &document->image, sizeof(document->image)), 0);
    EXPECT_EQ(read->image.save_data.save_dungeon.floor_info[SAVE_DUNGEON_FLOOR_NUM - 1].kill_count, SAVE_DUNGEON_FLOOR_NUM - 1);
    EXPECT_EQ(read->image.save_data.user_data.used_data[USED_DATA_PLACE_MAX - 1].item_no, USED_DATA_PLACE_MAX);

    // A save written with retail's counts fills the front of each array and leaves the rest clear.
    const auto retail = Edited(text, [](Json &value) {
        auto truncate = [](Json &array, std::size_t count) {
            while (array.size() > count) {
                array.erase(array.size() - 1);
            }
        };
        truncate(value["save"]["save_dungeon"]["floor_info"], SAVE_DUNGEON_RETAIL_FLOOR_NUM);
        truncate(value["save"]["build_parts_num"], 0x100);
        truncate(value["save"]["user_data"]["used_data"], ITEM_BOARD_PLACE_NUM);
    });
    auto older = std::make_unique<SaveDocument>();
    ASSERT_EQ(SaveJsonRead(retail, *older), SaveJsonError::None);
    const CSaveData &loaded = older->image.save_data;
    for (int i = 0; i < SAVE_DUNGEON_FLOOR_NUM; ++i) {
        const bool kept = i < SAVE_DUNGEON_RETAIL_FLOOR_NUM;
        EXPECT_EQ(loaded.save_dungeon.floor_info[i].visit_count, kept ? 1 + i % 100 : 0) << i;
        EXPECT_EQ(loaded.save_dungeon.floor_info[i].kill_count, kept ? i : 0) << i;
    }
    for (int i = 0; i < SAVE_BUILD_PARTS_MAX; ++i) {
        EXPECT_EQ(loaded.build_parts_num[i], i < 0x100 ? i % 10 : 0) << i;
    }
    for (int i = 0; i < USED_DATA_PLACE_MAX; ++i) {
        EXPECT_EQ(loaded.user_data.used_data[i].item_no, i < ITEM_BOARD_PLACE_NUM ? 1 + i : 0) << i;
    }
    // Retail's records keep their places: the last retail floor record is dungeon 6's floor 38.
    EXPECT_EQ(older->image.save_data.save_dungeon.GetFloorInfoPtr(6, 38), &older->image.save_data.save_dungeon.floor_info[SAVE_DUNGEON_RETAIL_FLOOR_NUM - 1]);

    // A save written with more than this build holds is refused rather than cut.
    const auto larger = Edited(text, [](Json &value) { value["save"]["build_parts_num"].push_back(1); });
    EXPECT_EQ(SaveJsonRead(larger, *older), SaveJsonError::BadValue);
}
