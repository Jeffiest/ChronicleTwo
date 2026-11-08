#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dng_main.hpp"
#include "gamedata.hpp"
#include "moonmod_item_table_fixture.hpp"
#include "inventmn.hpp"
#include "mainloop.hpp"
#include "mg_memory.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/runtime.hpp"
#include "savedata.hpp"
#include "scriptinterpreter.hpp"
#include "sphida.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");
extern int PortLoopNo;

namespace {

namespace fs = std::filesystem;

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-chronicle-two-signature-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

struct SaveRestore {
    CSaveData *previous = PortActiveSaveData;
    int previous_loop = PortLoopNo;
    ~SaveRestore() {
        moonmod::SharedRuntime().Stop();
        PortActiveSaveData = previous;
        PortLoopNo = previous_loop;
    }
};

moonmod::ApiCallResult CallApi(moonmod::RuntimeConfig &config, std::string_view path, const moonmod::Json &args) {
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation(path);
    EXPECT_NE(operation, nullptr) << path;
    if (operation == nullptr) return {moonmod::ApiCallStatus::Failed, nullptr, "missing operation"};
    return config.api_handler("test.moonmod.chronicle-two-signature", *operation, args);
}

void SeedSignatureItems() {
    GameItemDataManage.Initialize();
    SPI_STACK count{};
    count.type = SPI_STACK_TYPE_INT;
    count.value.integer = 2;
    ASSERT_EQ(moonmod_test::ItemTableInit(&count, 1), 1);

    const int item_numbers[] = {ITEM_ID_PHOTO_ALBUM, ITEM_ID_CAMERA};
    for (std::size_t index = 0; index < std::size(item_numbers); ++index) {
        SPI_STACK fields[11]{};
        const int values[] = {item_numbers[index], ITEM_DATA_RECOVERY, 0, 1, 10, 30, 3, 17, 100};
        for (std::size_t field = 0; field < std::size(values); ++field) {
            fields[field].type = SPI_STACK_TYPE_INT;
            fields[field].value.integer = values[field];
        }
        fields[9].type = SPI_STACK_TYPE_STRING;
        fields[9].value.string = const_cast<char *>("SIGNATURE_ITEM");
        fields[10].type = SPI_STACK_TYPE_INT;
        fields[10].value.integer = ITEM_ATTRIBUTE_USABLE;
        ASSERT_EQ(moonmod_test::ItemTableAdd(fields, 11), 1);

        CDataCommon &common = GameItemDataManage.common_data[index];
        common.item_no = item_numbers[index];
        common.max_num = 30;
        common.stack_num = 10;
        common.name = const_cast<char *>("Signature Item");
        GameItemDataManage.max_item_no = common.item_no;

        CDataItem &item = GameItemDataManage.item_data[index];
        item.action_flags = ITEM_ACTION_USE;
        item.value[0] = 1;
    }
    GameItemDataManage.common_num = 2;
    GameItemDataManage.item_num = 2;
}

void FakeSphedaSetup(CSphida *sphida, int) {
    sphida->pin_pos[0] = 10.0f;
    sphida->pin_pos[1] = 0.0f;
    sphida->pin_pos[2] = 20.0f;
    sphida->ball_pos[0] = 30.0f;
    sphida->ball_pos[1] = 0.0f;
    sphida->ball_pos[2] = 40.0f;
    sphida->par_count = 3;
}

} // namespace

TEST(MoonModChronicleTwoSignature, UnlocksPhotoIdeasAndListsScoops) {
    SaveRestore restore;
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    CInventUserData &invent = save->user_data.invent_data;
    invent.neta_id[0] = 42;
    invent.photo[0].used = 1;
    invent.photo[0].neta_id = 42;
    invent.photo[0].map_no = 7;
    invent.photo[0].npc_no = -1;
    invent.photo[0].monster_no = -1;
    invent.scoop.info[1].known = 1;
    invent.scoop.info[1].obtained = 1;
    moonmod::ResetChronicleTwoSignatureForTests();

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    const moonmod::ApiCallResult ideas = CallApi(config, "photos.ideas_list", moonmod::Json::array());
    ASSERT_EQ(ideas.status, moonmod::ApiCallStatus::Ok) << ideas.message;
    ASSERT_EQ(ideas.value.at("ideas").size(), 1u);
    EXPECT_EQ(ideas.value.at("ideas")[0].value("idea_id", -1), 42);
    ASSERT_EQ(ideas.value.at("photos").size(), 1u);
    EXPECT_EQ(ideas.value.at("photos")[0].value("slot", -1), 0);

    const moonmod::ApiCallResult unlocked = CallApi(
        config, "photos.idea_unlock", moonmod::Json::array({{{"idea_id", 43}}}));
    ASSERT_EQ(unlocked.status, moonmod::ApiCallStatus::Ok) << unlocked.message;
    EXPECT_TRUE(unlocked.value.value("changed", false));
    EXPECT_GE(unlocked.value.value("slot", -1), 0);
    EXPECT_GE(invent.CheckNetaFlag(43), 0);
    const moonmod::ApiCallResult repeated = CallApi(
        config, "photos.idea_unlock", moonmod::Json::array({{{"idea_id", 43}}}));
    ASSERT_EQ(repeated.status, moonmod::ApiCallStatus::Ok) << repeated.message;
    EXPECT_FALSE(repeated.value.value("changed", true));

    const moonmod::ApiCallResult scoops = CallApi(config, "photos.scoops_list", moonmod::Json::array());
    ASSERT_EQ(scoops.status, moonmod::ApiCallStatus::Ok) << scoops.message;
    EXPECT_EQ(scoops.value.value("count", -1), INVENT_SCOOP_TABLE_NUM);
    ASSERT_FALSE(scoops.value.at("scoops").empty());
    EXPECT_EQ(scoops.value.at("scoops")[0].value("scoop_id", -1), 1000);
    EXPECT_TRUE(scoops.value.at("scoops")[0].value("known", false));
    EXPECT_TRUE(scoops.value.at("scoops")[0].value("obtained", false));
    EXPECT_GE(scoops.value.value("known_count", -1), 1);
}

TEST(MoonModChronicleTwoSignature, RegistersCraftsAndListsModInvention) {
    SaveRestore restore;
    SeedSignatureItems();
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    moonmod::ResetChronicleTwoSignatureForTests();

    CInventUserData &invent = save->user_data.invent_data;
    invent.SetNetaFlag(1);
    invent.SetNetaFlag(2);
    invent.SetNetaFlag(3);
    const int seeded = save->user_data.GetItem(ITEM_ID_PHOTO_ALBUM, 5);
    ASSERT_EQ(seeded, 5);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    const moonmod::ApiCallResult registered = CallApi(
        config, "inventions.register",
        moonmod::Json::array({{{"id", "signature-invention"},
                               {"item", ITEM_ID_CAMERA},
                               {"ideas", {1, 2, 3}},
                               {"materials", {{{"id", ITEM_ID_PHOTO_ALBUM}, {"count", 1}}}}}}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.value("id", ""), "test.moonmod.chronicle-two-signature:signature-invention");
    EXPECT_EQ(registered.value.value("item_no", -1), ITEM_ID_CAMERA);

    const moonmod::ApiCallResult listed = CallApi(config, "inventions.list", moonmod::Json::array());
    ASSERT_EQ(listed.status, moonmod::ApiCallStatus::Ok) << listed.message;
    EXPECT_GE(listed.value.value("count", -1), 1);
    const auto mod_recipe = std::ranges::find_if(listed.value.at("recipes"), [](const moonmod::Json &recipe) {
        return recipe.value("id", "") == "test.moonmod.chronicle-two-signature:signature-invention";
    });
    ASSERT_NE(mod_recipe, listed.value.at("recipes").end());

    const moonmod::ApiCallResult crafted = CallApi(
        config, "inventions.craft",
        moonmod::Json::array({{{"id", "test.moonmod.chronicle-two-signature:signature-invention"}, {"count", 1}}}));
    ASSERT_EQ(crafted.status, moonmod::ApiCallStatus::Ok) << crafted.message;
    EXPECT_EQ(crafted.value.value("added", -1), 1);
    EXPECT_EQ(crafted.value.value("source", ""), "craft");
    EXPECT_EQ(save->user_data.GetNumSameItem(ITEM_ID_CAMERA), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(ITEM_ID_PHOTO_ALBUM), 4);

    const moonmod::ApiCallResult duplicate = CallApi(
        config, "inventions.register",
        moonmod::Json::array({{{"id", "signature-invention"},
                               {"item", ITEM_ID_CAMERA},
                               {"ideas", {1, 2, 3}}}}));
    EXPECT_EQ(duplicate.status, moonmod::ApiCallStatus::InvalidArgument);
}

TEST(MoonModChronicleTwoSignature, ReadsAndAdjustsRidepodStateAndParts) {
    SaveRestore restore;
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    moonmod::ResetChronicleTwoSignatureForTests();
    CUserDataManager &user_data = save->user_data;
    user_data.robo_data.hp.max = 100.0f;
    user_data.robo_data.hp.now = 40.0f;
    user_data.robo_data.shield_kit_num = 2;
    std::strcpy(user_data.robo_data.name, "Steve");
    CGameDataUsed &inventory_part = user_data.used_data[0];
    inventory_part.used_type = USED_ITEM_TYPE_ROBO_PART;
    inventory_part.item_no = ITEM_ID_CANNONBALL_ARM;
    inventory_part.item_type = ITEM_DATA_ROBO_ARM;
    inventory_part.data.robopart.defence = 3;
    inventory_part.data.robopart.whp.max = 20.0f;
    inventory_part.data.robopart.whp.now = 20.0f;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    const moonmod::ApiCallResult before = CallApi(config, "player.ridepod_get", moonmod::Json::array());
    ASSERT_EQ(before.status, moonmod::ApiCallStatus::Ok) << before.message;
    EXPECT_FLOAT_EQ(before.value.value("fuel", -1.0f), 40.0f);
    EXPECT_FLOAT_EQ(before.value.value("max_fuel", -1.0f), 100.0f);
    EXPECT_EQ(before.value.value("shield_kits", -1), 2);
    EXPECT_EQ(before.value.value("name", ""), "Steve");

    const moonmod::ApiCallResult changed = CallApi(
        config, "player.ridepod_set",
        moonmod::Json::array({{{"fuel", 75}, {"shield_kits", 3}, {"voice_enabled", true},
                               {"parts", moonmod::Json{{"arm", ITEM_ID_CANNONBALL_ARM}}}}}));
    ASSERT_EQ(changed.status, moonmod::ApiCallStatus::Ok) << changed.message;
    EXPECT_TRUE(changed.value.value("changed", false));
    EXPECT_FLOAT_EQ(user_data.robo_data.hp.now, 75.0f);
    EXPECT_EQ(user_data.robo_data.shield_kit_num, 3);
    EXPECT_EQ(user_data.robo_data.voice_flag, 1);
    EXPECT_EQ(user_data.robo_data.parts[ROBO_PART_ARM].item_no, ITEM_ID_CANNONBALL_ARM);
    EXPECT_EQ(inventory_part.item_no, 0);

    const moonmod::ApiCallResult restored = CallApi(
        config, "player.ridepod_set", moonmod::Json::array({{{"restore", 100}}}));
    ASSERT_EQ(restored.status, moonmod::ApiCallStatus::Ok) << restored.message;
    EXPECT_FLOAT_EQ(user_data.robo_data.hp.now, 100.0f);
}

TEST(MoonModChronicleTwoSignature, ReadsAndAdjustsMonsterBadges) {
    SaveRestore restore;
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    moonmod::ResetChronicleTwoSignatureForTests();
    CUserDataManager &user_data = save->user_data;
    MOS_CHANGE_PARAM &badge = user_data.monster_box.monster[0];
    badge.monster_id = 42;
    badge.enable = 0;
    badge.level = 1;
    badge.abs.max = 200.0f;
    badge.abs.now = 0.0f;
    user_data.monster_id = 42;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    const moonmod::ApiCallResult before = CallApi(config, "player.transform_get", moonmod::Json::array());
    ASSERT_EQ(before.status, moonmod::ApiCallStatus::Ok) << before.message;
    EXPECT_EQ(before.value.value("active_monster_id", -1), 42);
    ASSERT_EQ(before.value.at("badges").size(), 64u);
    EXPECT_FALSE(before.value.at("badges")[0].value("enabled", true));

    const moonmod::ApiCallResult changed = CallApi(
        config, "player.transform_set",
        moonmod::Json::array({{{"monster_id", 42}, {"enabled", true}, {"level", 10}, {"experience", 50}}}));
    ASSERT_EQ(changed.status, moonmod::ApiCallStatus::Ok) << changed.message;
    EXPECT_TRUE(changed.value.at("badge").value("enabled", false));
    EXPECT_EQ(changed.value.at("badge").value("level", -1), 10);
    EXPECT_FLOAT_EQ(changed.value.at("badge").value("experience", -1.0f), 50.0f);
    EXPECT_FLOAT_EQ(badge.abs.max, 1100.0f);

    const moonmod::ApiCallResult clamped = CallApi(
        config, "player.transform_set",
        moonmod::Json::array({{{"badge", 1}, {"experience", 999999}}}));
    ASSERT_EQ(clamped.status, moonmod::ApiCallStatus::Ok) << clamped.message;
    EXPECT_FLOAT_EQ(badge.abs.now, badge.abs.max);
}

TEST(MoonModChronicleTwoSignature, ReadsAndSpawnsActiveSphedaState) {
    SaveRestore restore;
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    DngStatus.dungeon_no = 1;
    save->save_dungeon.floor_id[1] = 2;
    DNG_FLOOR_SAVE *floor = save->save_dungeon.GetFloorInfoPtr(1, 2);
    ASSERT_NE(floor, nullptr);
    floor->spheda_clear = 0;
    // The floor game is published from an arena allocation, as the dungeon publishes it.
    const dc2::memory::ArenaBuffer previous_sphida = dc2::sphida::Lease();
    alignas(16) static u_long128 sphida_storage[(sizeof(CSphida) + sizeof(u_long128) - 1) / sizeof(u_long128) + 4]{};
    mgCMemory sphida_memory;
    sphida_memory.stSetBuffer(sphida_storage, static_cast<int>(std::size(sphida_storage)));
    CSphida *sphida = dc2::memory::NewObject<CSphida>(sphida_memory);
    ASSERT_NE(sphida, nullptr);
    InitSphida();
    ASSERT_TRUE(dc2::sphida::Publish(dc2::memory::ArenaBuffer::Bind(
        sphida_memory, {reinterpret_cast<u8 *>(sphida), sizeof(*sphida)})));
    moonmod::SetChronicleTwoSphedaSetupRoutineForTests(FakeSphedaSetup);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    const moonmod::ApiCallResult spawned = CallApi(
        config, "dungeons.spheda_spawn", moonmod::Json::array({{{"par", 5}}}));
    ASSERT_EQ(spawned.status, moonmod::ApiCallStatus::Ok) << spawned.message;
    EXPECT_TRUE(spawned.value.value("active", false));
    EXPECT_TRUE(spawned.value.value("session", false));
    EXPECT_EQ(spawned.value.value("par", -1), 5);
    EXPECT_FLOAT_EQ(spawned.value.at("pin").value("x", -1.0f), 10.0f);
    EXPECT_FLOAT_EQ(spawned.value.at("ball").value("z", -1.0f), 40.0f);

    const moonmod::ApiCallResult read = CallApi(config, "dungeons.spheda_get", moonmod::Json::array());
    ASSERT_EQ(read.status, moonmod::ApiCallStatus::Ok) << read.message;
    EXPECT_EQ(read.value.value("dungeon_id", -1), 1);
    EXPECT_EQ(read.value.value("floor_id", -1), 2);
    EXPECT_TRUE(read.value.value("active", false));
    EXPECT_EQ(read.value.at("gauge").value("state", -99), sphida->pow_gage.state);

    moonmod::SetChronicleTwoSphedaSetupRoutineForTests(nullptr);
    InitSphida();
    if (!previous_sphida.Bytes().empty()) {
        dc2::sphida::Publish(previous_sphida);
    }
}

TEST(MoonModChronicleTwoSignature, PublishesPhotoInventionAndSphedaEvents) {
    moonmod::SharedRuntime().Stop();
    SaveRestore restore;
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;
    moonmod::ResetChronicleTwoSignatureForTests();

    TempRoot root;
    const fs::path package = root.path / "mods" / "test.moonmod.chronicle-two-signature";
    Write(package / "mod.json", R"json({
      "schema_version":1,"id":"test.moonmod.chronicle-two-signature","name":"Signature Event Probe","version":"1.0.0",
      "framework_api":1,"games":["chronicle-two"],"entry":"main.lua",
      "permissions":["game.read","game.write"]
    })json");
    Write(package / "main.lua", R"lua(
moonmod.on("game.photo.taken", function(event)
  assert(event.subject_id == 1000 and event.kind == "scoop" and event.scoop.known == true)
  moonmod.log("info", "SIGNATURE_EVENT:photo")
end)
moonmod.on("game.invention.created", function(event)
  assert(event.native_item_no == 369 and event.ideas[1] == 1)
  moonmod.log("info", "SIGNATURE_EVENT:invention:" .. event.source)
end)
moonmod.on("game.spheda.completed", function(event)
  assert(event.dungeon_id == 1 and event.floor_id == 2 and event.clear_value == 1)
  moonmod.log("info", "SIGNATURE_EVENT:spheda")
end)
)lua");
    Write(root.path / "save" / "moonmod" / "profile.json",
          R"json({"format_version":1,"enabled":{"test.moonmod.chronicle-two-signature":true},"settings":{}})json");

    std::vector<std::string> logs;
    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    moonmod::ConfigureChronicleTwoApi(config);
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    config.world_location_reader = [] {
        return moonmod::ApiCallResult{moonmod::ApiCallStatus::Failed, nullptr, "no active world"};
    };
    config.log = [&](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(moonmod::SharedRuntime().Start(std::move(config), error)) << error;
    ASSERT_EQ(moonmod::SharedRuntime().Mods().size(), 1u);
    moonmod::SharedRuntime().Pump();

    CInventUserData &invent = save->user_data.invent_data;
    invent.scoop.info[1].known = 1;
    moonmod::ChronicleTwoPhotoTaken(1000, 1, -1, -1, true);
    moonmod::ChronicleTwoInventionCreated(369, 1, 2, 3);
    moonmod::ChronicleTwoSphedaCompleted(1, 2, 1);
    moonmod::SharedRuntime().Pump();
    for (const std::string_view marker : {"SIGNATURE_EVENT:photo", "SIGNATURE_EVENT:invention:native",
                                          "SIGNATURE_EVENT:spheda"}) {
        EXPECT_EQ(std::ranges::count_if(logs, [marker](const std::string &message) {
            return message == marker;
        }), 1) << moonmod::Json(logs).dump();
    }
}



