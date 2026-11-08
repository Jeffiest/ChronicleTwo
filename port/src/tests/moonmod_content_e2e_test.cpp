#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "moonmod_item_table_fixture.hpp"
#include "dngfloor.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "mg_memory.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/manager_ui.hpp"
#include "moonmod/runtime.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "scriptinterpreter.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

namespace {

namespace fs = std::filesystem;

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-content-e2e-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

struct ActiveSaveRestore {
    CSaveData *previous = PortActiveSaveData;
    ~ActiveSaveRestore() { PortActiveSaveData = previous; }
};

struct LoopRestore {
    int previous = PortLoopNo;
    ~LoopRestore() { PortLoopNo = previous; }
};

void Write(const fs::path &path, std::string_view contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    ASSERT_TRUE(out.good()) << path.string();
}

void SeedVanillaTemplateItem() {
    GameItemDataManage.Initialize();
    SPI_STACK count{};
    count.type = SPI_STACK_TYPE_INT;
    count.value.integer = 1;
    ASSERT_EQ(moonmod_test::ItemTableInit(&count, 1), 1);

    SPI_STACK fields[11]{};
    const int values[] = {0x112, ITEM_DATA_RECOVERY, 0, 1, 10, 30, 3, 17, 100};
    for (std::size_t i = 0; i < std::size(values); ++i) {
        fields[i].type = SPI_STACK_TYPE_INT;
        fields[i].value.integer = values[i];
    }
    fields[9].type = SPI_STACK_TYPE_STRING;
    fields[9].value.string = const_cast<char *>("VANILLA_TONIC");
    fields[10].type = SPI_STACK_TYPE_INT;
    fields[10].value.integer = ITEM_ATTRIBUTE_USABLE;
    ASSERT_EQ(moonmod_test::ItemTableAdd(fields, 11), 1);

    CDataCommon &common = GameItemDataManage.common_data[0];
    common.item_no = 0x112;
    common.list_no = 0;
    common.icon_no = 17;
    common.message_no = 100;
    common.max_num = 30;
    common.stack_num = 10;
    common.icon_texture_no = 3;
    common.name = const_cast<char *>("Vanilla Tonic");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.max_item_no = common.item_no;

    CDataItem &item = GameItemDataManage.item_data[0];
    item.action_flags = ITEM_ACTION_USE;
    item.use_flags = ITEM_USE_FLAG_RESTORE_HP;
    item.target_flags = ITEM_TARGET_MAX | ITEM_TARGET_MONICA;
    item.value[0] = 8;
    GameItemDataManage.item_num = 1;
}

} // namespace

TEST(MoonModContentE2E, LuaRegistrationMaterializesDungeonAndUsableDataOnlyItem) {
    moonmod::CloseManager();
    moonmod::ResetChronicleTwoContentForTests();
    SeedVanillaTemplateItem();
    LoopRestore restore_loop;

    auto save = std::make_unique<CSaveData>();
    ActiveSaveRestore restore_active_save;
    PortActiveSaveData = save.get();
    save->Initialize();
    CHARA_DATA *max = save->user_data.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(max, nullptr);
    max->hp.max = 100.0f;
    max->hp.now = 40.0f;
    GetMainScene()->battle_area.floor_status = 0;

    TempRoot root;
    const fs::path package = root.path / "mods" / "test.content-e2e";
    Write(package / "mod.json",
          R"json({"schema_version":1,"id":"test.content-e2e","name":"Content E2E","version":"1.0.0","framework_api":1,"games":["chronicle-two"],"entry":"main.lua","permissions":["content.register","game.read","game.write"]})json");
    Write(package / "main.lua", R"lua(
local item = assert(game.items.register({
  id = "starlight-tonic", name = "Starlight Tonic", kind = "consumable",
  template_item = 274, max_count = 5, max_stack = 5,
  effect = {kind = "heal", amount = 25}
}))
assert(item.id == "test.content-e2e:starlight-tonic" and item.status == "pending")
local item_used_event_count = 0
assert(game.quests.register({
  id = "item-use", title = "Use the Starlight Tonic",
  objectives = {
    {id = "use-tonic", title = "Use the tonic", target = 1,
     event = "game.item_used", match = {id = item.id}},
    {id = "story-flag", title = "Set the story flag", target = 1,
     event = "game.story_flag_changed", match = {id = 1700, value = true}}
  }
}))
assert(assert(game.quests.start({id = "item-use"})).status == "active")
local story_flag_event_count = 0
game.events.subscribe("game.story_flag_changed", function(event)
  if event.id == 1700 then
    assert(event.source_mod == "test.content-e2e" and event.value and not event.previous)
    local quest = assert(game.quests.get({id = "item-use"}))
    assert(quest.status == "active" and quest.objectives[1].progress == 0 and
           quest.objectives[2].progress == 1)
    story_flag_event_count = story_flag_event_count + 1
    moonmod.log("info", "MOONMOD_CONTENT_E2E_STORY_FLAG_EVENT_OK")
  end
end)
game.events.subscribe("game.item_used", function(event)
  if event.id == item.id then
    assert(event.source_mod == "test.content-e2e" and event.item_no > 0)
    local quest = assert(game.quests.get({id = "item-use"}))
    assert(quest.status == "completed" and quest.objectives[1].progress == 1)
    item_used_event_count = item_used_event_count + 1
    moonmod.log("info", "MOONMOD_CONTENT_E2E_ITEM_QUEST_EVENT_OK")
  end
end)
game.events.subscribe("quests.completed", function(event)
  if event.id == "test.content-e2e:item-use" then
    local reward = assert(game.items.give({id = item.id, count = 1}))
    assert(reward.added == 1)
    moonmod.log("info", "MOONMOD_CONTENT_E2E_QUEST_REWARD_OK")
  end
end)

local cure_statuses = {"poison", "slow", "stop", "curse", "stone", "dry"}
for _, status in ipairs(cure_statuses) do
  local item = assert(game.items.register({
    id = "cure-" .. status, name = "Cure " .. status, kind = "consumable",
    template_item = 274, max_count = 5, max_stack = 5,
    effect = {kind = "cure_status", status = status}
  }))
  assert(item.status == "pending" and item.effect.kind == "cure_status" and item.effect.status == status)
end
local cure_all = assert(game.items.register({
  id = "cure-all", name = "Panacea", kind = "consumable",
  template_item = 274, max_count = 5, max_stack = 5,
  effect = {kind = "cure_all"}
}))
assert(cure_all.effect.kind == "cure_all")
local power_item = assert(game.items.register({
  id = "power-draught", name = "Power Draught", kind = "consumable",
  template_item = 274, max_count = 5, max_stack = 5,
  effect = {kind = "power"}
}))
assert(power_item.effect.kind == "power")
game.events.subscribe("tests.use.cure_all", function()
  local registered_cure_all = assert(game.items.get(cure_all.id))
  assert(registered_cure_all.status == "registered" and registered_cure_all.effect.kind == "cure_all")
  assert(assert(game.items.give({id = registered_cure_all.id})).added == 1)
  local cure_all_use = assert(game.items.use({id = registered_cure_all.id, target = "max"}))
  assert(cure_all_use.effects_applied == 1 and cure_all_use.consumed == 1 and cure_all_use.remaining == 0)
  moonmod.log("info", "MOONMOD_CONTENT_E2E_CURE_ALL_OK")
end)

local dungeon = assert(game.world.register_dungeon({
  id = "moon-vault", name = "Moon Vault", dungeon = 0,
  floors = {{id = "annex", name = "Moon Vault Annex", source_floor = 1,
             flags = {"exit", "boss"}, seal = "max"},
            {id = "gallery", name = "Moon Vault Gallery", source_floor = "annex",
             flags = {"sub"}, seal = "monica"}}
}))
assert(dungeon.id == "test.content-e2e:moon-vault" and #dungeon.floors == 2)
assert(dungeon.floors[1].status == "pending" and dungeon.floors[2].status == "pending")
assert(dungeon.floors[2].source_floor == "annex")
assert(dungeon.floors[1].flags[1] == "exit" and dungeon.floors[1].flags[2] == "boss")
assert(dungeon.floors[1].seal == "max" and dungeon.floors[2].flags[1] == "sub")
assert(dungeon.floors[2].seal == "monica")

game.events.subscribe("tests.content.materialized", function()
  local item = assert(game.items.get("test.content-e2e:starlight-tonic"))
  assert(item.status == "registered" and item.mod_owned and item.name == "Starlight Tonic")
  assert(item.template_item == 274 and next(item.assets) == nil)

  local listed = assert(game.items.list())
  local found = false
  for _, entry in ipairs(listed) do
    if entry.id == item.id and entry.status == "registered" then
      found = true
      assert(entry.model_key == "VANILLA_TONIC", "the item should inherit the template's native model")
    end
  end
  assert(found, "registered item must be visible in the item catalog")

  local grant = assert(game.items.give({id = item.id, count = 2}))
  assert(grant.added == 2)
  assert(assert(game.player.get()).hp == 40)
    local story_flag = assert(game.world.story_flag_set({id = 1700, value = true}))
    assert(story_flag.changed and story_flag.value)
    local repeated_flag = assert(game.world.story_flag_set({id = 1700, value = true}))
    assert(not repeated_flag.changed and story_flag_event_count == 0,
           "story flag should queue one event only when its value changes")
  local use = assert(game.items.use({id = item.id, target = "max"}))
  assert(use.effects_applied == 1 and use.consumed == 1 and use.remaining == 1)
  assert(use.hp_before == 40 and use.hp_after == 65 and use.healed == 25)
  assert(item_used_event_count == 0, "native item-use event must wait for the runtime pump")
  local removal = assert(game.items.remove({id = item.id, count = 1}))
  assert(removal.removed == 1 and removal.remaining == 0)

  for _, status in ipairs(cure_statuses) do
    local status_args = {status = status, character = "max"}
    if status == "slow" or status == "stop" or status == "stone" then
      status_args.duration_frames = 240
    end
    assert(game.player.status_add(status_args))
    local cure = assert(game.items.get("test.content-e2e:cure-" .. status))
    assert(cure.status == "registered" and cure.effect.kind == "cure_status" and cure.effect.status == status)
    assert(assert(game.items.give({id = cure.id})).added == 1)
    local cure_use = assert(game.items.use({id = cure.id, target = "max"}))
    assert(cure_use.effects_applied == 1 and cure_use.consumed == 1 and cure_use.remaining == 0)
  end
  local registered_power = assert(game.items.get(power_item.id))
  assert(registered_power.status == "registered" and registered_power.effect.kind == "power")
  assert(assert(game.items.give({id = registered_power.id})).added == 1)
  local power_use = assert(game.items.use({id = registered_power.id, target = "max"}))
  assert(power_use.effects_applied == 1 and power_use.consumed == 1 and power_use.remaining == 0)
  assert(power_use.status_changed and power_use.power_duration_frames > 0)
  moonmod.log("info", "MOONMOD_CONTENT_E2E_POWER_ITEM_OK")
  assert(item_used_event_count == 0, "Lua event handlers must not re-enter the item API callback")
  assert(story_flag_event_count == 0, "Lua event handlers must not re-enter the flag API callback")
  moonmod.log("info", "MOONMOD_CONTENT_E2E_LUA_ITEM_OK")
end)
return {}
)lua");

    const fs::path enabled_profile = root.path / "save" / "moonmod" / "profile.json";
    Write(enabled_profile, R"json({"format_version":1,"enabled":{"test.content-e2e":true},"settings":{}})json");

    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    moonmod::ConfigureChronicleTwoApi(config);
    std::vector<std::string> logs;
    std::vector<std::string> errors;
    config.log = [&logs, &errors](std::string_view level, std::string_view, std::string_view message) {
        logs.emplace_back(message);
        if (level == "error") errors.emplace_back(message);
    };

    moonmod::Runtime runtime;
    std::string error;
    ASSERT_TRUE(runtime.Start(std::move(config), error)) << error;
    ASSERT_EQ(runtime.Mods().size(), 1u);
    ASSERT_EQ(runtime.Mods()[0].state, moonmod::ModState::Loaded) << runtime.Mods()[0].status_message;

    // Materialize the Lua-registered item through Chronicle Two's native data manager.
    moonmod::ApplyChronicleTwoContent();
    int item_no = -1;
    std::array<std::pair<const char *, int>, 6> cure_item_numbers = {{{"poison", -1}, {"slow", -1}, {"stop", -1},
                                                                       {"curse", -1}, {"stone", -1}, {"dry", -1}}};
    int power_item_no = -1;
    for (int index = 0; index < GameItemDataManage.common_num; ++index) {
        const CDataCommon &common = GameItemDataManage.common_data[index];
        if (common.name != nullptr && std::string_view(common.name) == "Starlight Tonic") item_no = common.item_no;
        if (common.name != nullptr && std::string_view(common.name) == "Power Draught") power_item_no = common.item_no;
        if (common.name != nullptr) {
            for (auto &[status, item_no_for_status] : cure_item_numbers) {
                if (std::string_view(common.name) == std::string("Cure ") + status) item_no_for_status = common.item_no;
            }
        }
    }
    ASSERT_GT(item_no, 0x112);
    for (const auto &[status, item_no_for_status] : cure_item_numbers) {
        EXPECT_GT(item_no_for_status, 0x112) << status;
    }
    CDataCommon *native_item = GetCommonItemData(item_no);
    ASSERT_NE(native_item, nullptr);
    EXPECT_STREQ(native_item->name, "Starlight Tonic");
    EXPECT_STREQ(native_item->file_name, "VANILLA_TONIC");
    EXPECT_EQ(native_item->icon_no, 17);
    EXPECT_EQ(native_item->icon_texture_no, 3);
    CDataItem *native_item_behavior = GetItemInfoData(item_no);
    ASSERT_NE(native_item_behavior, nullptr);
    EXPECT_EQ(native_item_behavior->value[0], 25);
    EXPECT_EQ(native_item_behavior->use_flags & ITEM_USE_FLAG_RESTORE_HP, ITEM_USE_FLAG_RESTORE_HP);
    const std::array<int, 6> expected_cure_flags = {ITEM_USE_FLAG_CURE_POISON, ITEM_USE_FLAG_CURE_SLOW,
                                                    ITEM_USE_FLAG_CURE_STOP, ITEM_USE_FLAG_CURE_CURSE,
                                                    ITEM_USE_FLAG_CURE_STONE, ITEM_USE_FLAG_CURE_DRY};
    for (std::size_t index = 0; index < cure_item_numbers.size(); ++index) {
        CDataCommon *native_cure_item = GetCommonItemData(cure_item_numbers[index].second);
        ASSERT_NE(native_cure_item, nullptr) << cure_item_numbers[index].first;
        CDataItem *native_cure_behavior = GetItemInfoData(cure_item_numbers[index].second);
        ASSERT_NE(native_cure_behavior, nullptr) << cure_item_numbers[index].first;
        EXPECT_EQ(native_cure_behavior->use_flags, expected_cure_flags[index]) << cure_item_numbers[index].first;
    }
    int cure_all_item_no = -1;
    for (int index = 0; index < GameItemDataManage.common_num; ++index) {
        const CDataCommon &common = GameItemDataManage.common_data[index];
        if (common.name != nullptr && std::string_view(common.name) == "Panacea") cure_all_item_no = common.item_no;
    }
    ASSERT_GT(cure_all_item_no, 0x112);
    CDataItem *native_cure_all_behavior = GetItemInfoData(cure_all_item_no);
    ASSERT_NE(native_cure_all_behavior, nullptr);
    EXPECT_EQ(native_cure_all_behavior->use_flags,
              ITEM_USE_FLAG_CURE_POISON | ITEM_USE_FLAG_CURE_SLOW | ITEM_USE_FLAG_CURE_STOP |
                  ITEM_USE_FLAG_CURE_CURSE | ITEM_USE_FLAG_CURE_STONE | ITEM_USE_FLAG_CURE_DRY);
    ASSERT_GT(power_item_no, 0x112);
    CDataItem *native_power_behavior = GetItemInfoData(power_item_no);
    ASSERT_NE(native_power_behavior, nullptr);
    EXPECT_EQ(native_power_behavior->use_flags, ITEM_USE_FLAG_ADD_POWER);
    EXPECT_EQ(native_power_behavior->value[0], 0);

    // Materialize the Lua-registered floor into a parsed native dungeon graph.
    CDngFloorManager floor_manager;
    floor_manager.Initialize();
    alignas(64) u_long128 arena_storage[1024]{};
    mgCMemory arena;
    arena.stSetBuffer(arena_storage, 1024);
    char dungeon_map[] =
        "TREE_INFO 6,3,3;"
        "GI 1,1,1,0,0; RI 0,0,0; RI_LINK -1,-1,-1,1; RI_TITLE 0,\"Entrance\";"
        "GI 0,2,1,0,0; RT 0,0,0;"
        "GI 1,3,1,0,0; RI 1,1,0; RI_LINK -1,-1,0,-1; RI_TITLE 1,\"Source Floor\";";
    floor_manager.AnalyzeFile(dungeon_map, static_cast<int>(std::char_traits<char>::length(dungeon_map)), &arena);
    ASSERT_EQ(floor_manager.glid_num, 3);
    moonmod::ApplyChronicleTwoDungeons(&floor_manager);
    ASSERT_EQ(floor_manager.glid_num, 7);
    EXPECT_TRUE(floor_manager.IsMoonModFloor(2, "Moon Vault Annex"));
    EXPECT_TRUE(floor_manager.IsMoonModFloor(3, "Moon Vault Gallery"));
    ASSERT_NE(floor_manager.GetDngMapFloorInfo(2), nullptr);
    EXPECT_NE(floor_manager.GetDngMapFloorInfo(2)->flag & DNGMAP_ROOM_FLAG_EXIT, 0);
    EXPECT_NE(floor_manager.GetDngMapFloorInfo(2)->flag & DNGMAP_ROOM_FLAG_BOSS, 0);
    EXPECT_EQ(floor_manager.GetDngMapFloorInfo(2)->seal, DNGMAP_SEAL_MAX);
    ASSERT_NE(floor_manager.GetDngMapFloorInfo(3), nullptr);
    EXPECT_NE(floor_manager.GetDngMapFloorInfo(3)->flag & DNGMAP_ROOM_FLAG_SUB, 0);
    EXPECT_EQ(floor_manager.GetDngMapFloorInfo(3)->seal, DNGMAP_SEAL_MONICA);
    EXPECT_EQ(floor_manager.GetDngMapNextFloorID(1, 0), 2);
    EXPECT_EQ(floor_manager.GetDngMapNextFloorID(2, 0), 3);
    GLID_INFO *next = floor_manager.GetNextRoom(1, GLID_DIR_RIGHT, nullptr, -1, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_EQ(next->type, GLID_TYPE_ROOM);
    EXPECT_EQ(next->room.floor_id, 2);
    GLID_INFO *gallery = floor_manager.GetNextRoom(2, GLID_DIR_RIGHT, nullptr, -1, nullptr);
    ASSERT_NE(gallery, nullptr);
    EXPECT_EQ(gallery->type, GLID_TYPE_ROOM);
    EXPECT_EQ(gallery->room.floor_id, 3);

    PortLoopNo = LOOP_DUNGEON;
    max->status_attr = CHARA_STATUS_DRY;
    ASSERT_TRUE(runtime.PublishToMod("test.content-e2e", "tests.content.materialized", moonmod::Json::object()));
    constexpr int cured_statuses = CHARA_STATUS_POISON | CHARA_STATUS_SLOW | CHARA_STATUS_STOP | CHARA_STATUS_CURSE |
                                   CHARA_STATUS_STONE | CHARA_STATUS_DRY;
    EXPECT_EQ(max->status_attr & cured_statuses, 0);
    EXPECT_NE(max->status_attr & CHARA_STATUS_POWER, 0);
    EXPECT_GT(max->status_time[0], 0);
    max->status_attr = cured_statuses;
    ASSERT_TRUE(runtime.PublishToMod("test.content-e2e", "tests.use.cure_all", moonmod::Json::object()));
    EXPECT_EQ(max->status_attr & cured_statuses, 0);
    EXPECT_EQ(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_ITEM_QUEST_EVENT_OK"), logs.end());
    EXPECT_EQ(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_STORY_FLAG_EVENT_OK"), logs.end());
    EXPECT_EQ(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_QUEST_REWARD_OK"), logs.end());
    runtime.Pump();
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_ITEM_QUEST_EVENT_OK"), logs.end());
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_STORY_FLAG_EVENT_OK"), logs.end());
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_QUEST_REWARD_OK"), logs.end());
    EXPECT_EQ(GetUserDataMan()->GetNumSameItem(item_no), 1);
    EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_LUA_ITEM_OK"), logs.end());
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_CURE_ALL_OK"), logs.end());
    EXPECT_NE(std::find(logs.begin(), logs.end(), "MOONMOD_CONTENT_E2E_POWER_ITEM_OK"), logs.end());
    runtime.Stop();
    EXPECT_EQ(GetUserDataMan()->GetNumSameItem(item_no), 1);
    for (const auto &[status, cure_item_no] : cure_item_numbers) {
        EXPECT_EQ(GetUserDataMan()->GetNumSameItem(cure_item_no), 0) << status;
    }
    EXPECT_EQ(GetUserDataMan()->GetNumSameItem(cure_all_item_no), 0);
    EXPECT_EQ(GetUserDataMan()->GetNumSameItem(power_item_no), 0);
    moonmod::ResetChronicleTwoContentForTests();
    floor_manager.Initialize();
    moonmod::CloseManager();
}
