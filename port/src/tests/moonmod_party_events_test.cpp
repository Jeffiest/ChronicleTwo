#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "moonmod_item_table_fixture.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/runtime.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "scriptinterpreter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");
extern int PortLoopNo;

namespace {

namespace fs = std::filesystem;

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-party-event-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void Write(const fs::path &path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    ASSERT_TRUE(output.good()) << path.string();
}

} // namespace

TEST(MoonModPartyEvents, PublishesChangedPartyWritesOnceForQuestObjectives) {
    moonmod::SharedRuntime().Stop();
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CSaveData *save;
        int loop;
        ~Restore() {
            moonmod::SharedRuntime().Stop();
            PortActiveSaveData = save;
            PortLoopNo = loop;
        }
    } restore{previous_save, previous_loop};

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    CUserDataManager &user_data = save->user_data;
    user_data.party_member = (1 << USER_CHARA_MAX) | (1 << USER_CHARA_MONICA) | (1 << USER_CHARA_ROBO);
    user_data.chara_change = user_data.party_member;
    user_data.chara_change_mask = 0xFF;
    user_data.chara_data[USER_CHARA_MAX].hp.now = 80.0f;
    user_data.chara_data[USER_CHARA_MAX].hp.max = 100.0f;
    user_data.chara_data[USER_CHARA_MONICA].hp.now = 70.0f;
    user_data.chara_data[USER_CHARA_MONICA].hp.max = 100.0f;
    user_data.robo_data.hp.now = 60.0f;
    user_data.robo_data.hp.max = 100.0f;
    user_data.SetActiveChrNo(USER_CHARA_MONICA);
    GetMainScene()->battle_area.floor_status = 0;

    TempRoot root;
    const fs::path package = root.path / "mods" / "test.moonmod.party-event";
    Write(package / "mod.json", R"json({
      "schema_version":1,"id":"test.moonmod.party-event","name":"Party Event Probe","version":"1.0.0",
      "framework_api":1,"games":["chronicle-two"],"entry":"main.lua",
      "permissions":["content.register","game.read","game.write"]
    })json");
    Write(package / "main.lua", R"lua(
assert(game.quests.register({
  id = "switch-character", title = "Change active party member",
  objectives = {{id = "switch", title = "Switch to Max", target = 1,
    event = "game.party.active_changed", match = {previous = "monica", active = "max"}}}
}))
assert(game.quests.register({
  id = "recruit-companion", title = "Recruit a companion",
  objectives = {{id = "recruit", title = "Recruit townsperson 7", target = 1,
    event = "game.party.recruited", match = {npc_id = 7, recruited = true}}}
}))
assert(game.quests.register({
  id = "replace-companion", title = "Replace a companion",
  objectives = {{id = "replace", title = "Replace townsperson 7 with 8", target = 1,
    event = "game.party.recruited", match = {npc_id = 8, replaced = "chronicle-two:npc:7"}}}
}))
local applied = false
moonmod.on("game.party.active_changed", function(event)
  assert(event.previous == "monica" and event.active == "max" and event.changed == true)
  assert(event.source_mod == "test.moonmod.party-event")
  moonmod.log("info", "PARTY_EVENT:active")
end)
moonmod.on("game.party.recruited", function(event)
  assert(event.changed == true)
  assert(event.source_mod == "test.moonmod.party-event")
  if event.npc_id == 7 then
    assert(event.id == "chronicle-two:npc:7" and event.recruited == true)
    moonmod.log("info", "PARTY_EVENT:recruited")
  elseif event.npc_id == 8 then
    assert(event.id == "chronicle-two:npc:8" and event.replaced == "chronicle-two:npc:7")
    moonmod.log("info", "PARTY_EVENT:replaced")
  else
    error("unexpected companion recruitment event")
  end
end)
moonmod.on("game.tick", function()
  if applied then return end
  assert(game.quests.start({id = "switch-character"}))
  assert(game.quests.start({id = "recruit-companion"}))
  assert(game.quests.start({id = "replace-companion"}))
  assert(assert(game.party.set_active({id = "max"})).changed)
  assert(not assert(game.party.set_active({id = "max"})).changed)
  local recruited = assert(game.party.recruit({npc_id = 7}))
  assert(recruited.recruited and recruited.changed)
  local repeated = assert(game.party.recruit({npc_id = 7}))
  assert(not repeated.recruited and not repeated.changed)
  local replacement = assert(game.party.recruit({npc_id = 8}))
  assert(replacement.recruited and replacement.changed and replacement.replaced == "chronicle-two:npc:7")
  assert(not assert(game.party.recruit({npc_id = 8})).changed)
  applied = true
end)
)lua");
    Write(root.path / "save" / "moonmod" / "profile.json",
          R"json({"format_version":1,"enabled":{"test.moonmod.party-event":true},"settings":{}})json");

    std::vector<std::string> logs;
    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    moonmod::ConfigureChronicleTwoApi(config);
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    config.world_location_reader = [] {
        return moonmod::ApiCallResult{moonmod::ApiCallStatus::Failed, nullptr, "no active world in party event probe"};
    };
    config.log = [&](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(moonmod::SharedRuntime().Start(std::move(config), error)) << error;
    ASSERT_EQ(moonmod::SharedRuntime().Mods().size(), 1u);
    ASSERT_EQ(moonmod::SharedRuntime().Mods()[0].state, moonmod::ModState::Loaded);

    moonmod::SharedRuntime().Pump();
    EXPECT_TRUE(std::ranges::none_of(logs, [](const std::string &message) {
        return message.find("PARTY_EVENT:") != std::string::npos;
    }));
    moonmod::SharedRuntime().Pump();
    for (const std::string_view event_name : {"PARTY_EVENT:active", "PARTY_EVENT:recruited", "PARTY_EVENT:replaced"}) {
        EXPECT_EQ(std::ranges::count_if(logs, [event_name](const std::string &message) {
            return message.find(event_name) != std::string::npos;
        }), 1) << "missing " << event_name << "; runtime logs: " << moonmod::Json(logs).dump();
    }
    const moonmod::ApiCallResult quests = moonmod::SharedRuntime().Quests("test.moonmod.party-event");
    ASSERT_EQ(quests.status, moonmod::ApiCallStatus::Ok) << quests.message;
    ASSERT_EQ(quests.value.at("quests").size(), 3u);
    EXPECT_TRUE(std::ranges::all_of(quests.value.at("quests"), [](const moonmod::Json &quest) {
        return quest.at("status") == "completed" && quest.at("objectives").at(0).at("progress") == 1;
    })) << quests.value.dump();
}

TEST(MoonModPartyEvents, PublishesWeaponEquipForQuestObjectivesOnNextPump) {
    moonmod::SharedRuntime().Stop();
    CSaveData *previous_save = PortActiveSaveData;
    struct Restore {
        CSaveData *save;
        ~Restore() {
            moonmod::SharedRuntime().Stop();
            PortActiveSaveData = save;
            GameItemDataManage.Initialize();
        }
    } restore{previous_save};

    GameItemDataManage.Initialize();
    constexpr int current_weapon_no = 0x51;
    constexpr int target_weapon_no = 0x50;
    SPI_STACK common_count{};
    common_count.type = SPI_STACK_TYPE_INT;
    common_count.value.integer = 2;
    ASSERT_EQ(moonmod_test::ItemTableInit(&common_count, 1), 1);
    for (int index = 0; index < 2; ++index) {
        SPI_STACK common_fields[11]{};
        const int item_no = index == 0 ? target_weapon_no : current_weapon_no;
        const int common_values[] = {item_no, ITEM_DATA_MAX_MELEE, index, 1, 1, 144, 4, 23 + index, 300 + index};
        for (std::size_t field = 0; field < std::size(common_values); ++field) {
            common_fields[field].type = SPI_STACK_TYPE_INT;
            common_fields[field].value.integer = common_values[field];
        }
        common_fields[9].type = SPI_STACK_TYPE_STRING;
        common_fields[9].value.string = const_cast<char *>(index == 0 ? "TARGET_SWORD" : "CURRENT_SWORD");
        common_fields[10].type = SPI_STACK_TYPE_INT;
        common_fields[10].value.integer = ITEM_ATTRIBUTE_UNIQUE;
        ASSERT_EQ(moonmod_test::ItemTableAdd(common_fields, 11), 1);

        CDataCommon &common = GameItemDataManage.common_data[index];
        std::memset(&common, 0, sizeof(common));
        common.type = ITEM_DATA_MAX_MELEE;
        common.item_no = index == 0 ? target_weapon_no : current_weapon_no;
        common.list_no = index;
        common.icon_no = 23 + index;
        common.message_no = 300 + index;
        common.max_num = 144;
        std::strcpy(common.file_name, index == 0 ? "TARGET_SWORD" : "CURRENT_SWORD");
        common.stack_num = 1;
        common.attribute = ITEM_ATTRIBUTE_UNIQUE;
        common.name = const_cast<char *>(index == 0 ? "Target Sword" : "Current Sword");

        CDataWeapon &weapon = GameItemDataManage.weapon_data[index];
        weapon.durability = 88;
        weapon.levelup_exp = 27;
        weapon.status[0] = 12;
        weapon.status[1] = 34;
        weapon.status_max[0] = 99;
        weapon.status_max[1] = 120;
    }
    GameItemDataManage.common_num = 2;
    GameItemDataManage.weapon_num = 2;
    GameItemDataManage.max_item_no = current_weapon_no;

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    CGameDataUsed *owned = save->user_data.GetUsedDataPtr(0);
    ASSERT_NE(owned, nullptr);
    owned->used_type = USED_ITEM_TYPE_WEAPON;
    owned->item_no = target_weapon_no;
    owned->item_type = ITEM_DATA_MAX_MELEE;
    owned->data.weapon.whp.max = 88.0f;
    owned->data.weapon.whp.now = 61.0f;
    owned->data.weapon.abs.max = 100.0f;
    owned->data.weapon.abs.now = 42.0f;
    owned->data.weapon.level = 2;

    CGameDataUsed &currently_equipped = save->user_data.chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_MELEE];
    currently_equipped.used_type = USED_ITEM_TYPE_WEAPON;
    currently_equipped.item_no = current_weapon_no;
    currently_equipped.item_type = ITEM_DATA_MAX_MELEE;
    ASSERT_EQ(GameItemDataManage.GetDataType(target_weapon_no), ITEM_DATA_MAX_MELEE);
    ASSERT_NE(save->user_data.SearchItemOnItemBrd(target_weapon_no, ITEM_BOARD_ALL), nullptr);
    int equip_slot = -1;
    ASSERT_EQ(IsItemtypeWhoisEquip(target_weapon_no, &equip_slot), USER_CHARA_MAX);
    ASSERT_EQ(equip_slot, CHARA_EQUIP_MELEE);

    TempRoot root;
    const fs::path package = root.path / "mods" / "test.moonmod.weapon-equip-event";
    Write(package / "mod.json", R"json({
      "schema_version":1,"id":"test.moonmod.weapon-equip-event","name":"Weapon Equip Event Probe","version":"1.0.0",
      "framework_api":1,"games":["chronicle-two"],"entry":"main.lua",
      "permissions":["content.register","game.read","game.write"]
    })json");
    Write(package / "main.lua", R"lua(
assert(game.quests.register({
  id = "equip", title = "Equip a weapon",
  objectives = {{id = "weapon", title = "Equip the target sword", target = 1,
    event = "game.weapon.equipped", match = {native_item_no = 80, character = "max", changed = true}}}
}))
local done = false
moonmod.on("game.weapon.equipped", function(event)
  assert(event.native_item_no == 80 and event.character == "max")
  assert(event.equipped == true and event.changed == true)
  assert(event.source_mod == "test.moonmod.weapon-equip-event")
  moonmod.log("info", "WEAPON_EQUIP_EVENT:" .. tostring(event.native_item_no))
end)
moonmod.on("game.tick", function()
  if done then return end
  assert(game.quests.start({id = "equip"}))
  local equipped, failure = game.weapons.equip({id = "chronicle-two:weapon:80", character = "max"})
  assert(equipped, failure and failure.message or "weapon equip failed")
  assert(equipped.equipped and equipped.changed)
  done = true
end)
)lua");
    Write(root.path / "save" / "moonmod" / "profile.json",
          R"json({"format_version":1,"enabled":{"test.moonmod.weapon-equip-event":true},"settings":{}})json");

    std::vector<std::string> logs;
    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    moonmod::ConfigureChronicleTwoApi(config);
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    config.world_location_reader = [] {
        return moonmod::ApiCallResult{moonmod::ApiCallStatus::Failed, nullptr, "no active world in weapon event probe"};
    };
    config.log = [&](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(moonmod::SharedRuntime().Start(std::move(config), error)) << error;
    ASSERT_EQ(moonmod::SharedRuntime().Mods().size(), 1u);
    ASSERT_EQ(moonmod::SharedRuntime().Mods()[0].state, moonmod::ModState::Loaded);

    moonmod::SharedRuntime().Pump();
    EXPECT_TRUE(std::ranges::none_of(logs, [](const std::string &message) {
        return message.find("WEAPON_EQUIP_EVENT:") != std::string::npos;
    }));
    moonmod::SharedRuntime().Pump();
    EXPECT_EQ(std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("WEAPON_EQUIP_EVENT:80") != std::string::npos;
    }), 1) << moonmod::Json(logs).dump();
    const moonmod::ApiCallResult quests = moonmod::SharedRuntime().Quests("test.moonmod.weapon-equip-event");
    ASSERT_EQ(quests.status, moonmod::ApiCallStatus::Ok) << quests.message;
    ASSERT_EQ(quests.value.at("quests").size(), 1u);
    EXPECT_EQ(quests.value.at("quests").at(0).at("status"), "completed");
    EXPECT_EQ(quests.value.at("quests").at(0).at("objectives").at(0).at("progress"), 1);
}
