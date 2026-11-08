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
#include "savedata.hpp"
#include "moonmod/manager_ui.hpp"
#include "moonmod/runtime.hpp"
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "scriptinterpreter.hpp"
extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");
#elif defined(MOONMOD_TEST_CHRONICLE)
#include "moonmod/chronicle_adapter.hpp"
extern CSaveData *SaveData;
#endif

namespace {

namespace fs = std::filesystem;

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-example-package-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

struct StopRuntime {
    ~StopRuntime() { moonmod::SharedRuntime().Stop(); }
};

struct ActiveSaveRestore {
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    CSaveData *previous = PortActiveSaveData;
    ~ActiveSaveRestore() { PortActiveSaveData = previous; }
#elif defined(MOONMOD_TEST_CHRONICLE)
    CSaveData *previous = SaveData;
    ~ActiveSaveRestore() { SaveData = previous; }
#endif
};

#if defined(MOONMOD_TEST_CHRONICLE_TWO)
void SeedVanillaItemTemplate() {
    GameItemDataManage.Initialize();
    SPI_STACK count{};
    count.type = SPI_STACK_TYPE_INT;
    count.value.integer = 1;
    ASSERT_EQ(moonmod_test::ItemTableInit(&count, 1), 1);

    SPI_STACK fields[11]{};
    const int values[] = {274, ITEM_DATA_RECOVERY, 0, 1, 10, 30, 3, 17, 100};
    for (std::size_t index = 0; index < std::size(values); ++index) {
        fields[index].type = SPI_STACK_TYPE_INT;
        fields[index].value.integer = values[index];
    }
    fields[9].type = SPI_STACK_TYPE_STRING;
    fields[9].value.string = const_cast<char *>("VANILLA_TONIC");
    fields[10].type = SPI_STACK_TYPE_INT;
    fields[10].value.integer = ITEM_ATTRIBUTE_USABLE;
    ASSERT_EQ(moonmod_test::ItemTableAdd(fields, 11), 1);

    CDataCommon &common = GameItemDataManage.common_data[0];
    std::memset(&common, 0, sizeof(common));
    common.type = ITEM_DATA_RECOVERY;
    common.item_no = 274;
    common.list_no = 0;
    common.icon_no = 17;
    common.message_no = 100;
    common.max_num = 30;
    std::strcpy(common.file_name, "VANILLA_TONIC");
    common.active_set = 1;
    common.stack_num = 10;
    common.icon_texture_no = 3;
    common.attribute = ITEM_ATTRIBUTE_USABLE;
    common.name = const_cast<char *>("Vanilla Tonic");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.max_item_no = common.item_no;

    CDataItem &item = GameItemDataManage.item_data[0];
    std::memset(&item, 0, sizeof(item));
    item.action_flags = ITEM_ACTION_USE;
    item.use_flags = ITEM_USE_FLAG_RESTORE_HP;
    item.target_flags = ITEM_TARGET_MAX | ITEM_TARGET_MONICA;
    item.value[0] = 8;
    item.value[1] = 0;
    item.value[2] = 0;
    GameItemDataManage.item_num = 1;
}

struct ChronicleTwoContentRestore {
    ~ChronicleTwoContentRestore() {
        moonmod::ResetChronicleTwoContentForTests();
        GameItemDataManage.Initialize();
    }
};
#endif

} // namespace

TEST(MoonModExamplePackage, FirstStepsRunsQuestSkillAndManagerActions) {
#if !defined(MOONFACTORY_FIRST_STEPS_PATH)
    GTEST_SKIP() << "The standalone MoonFactory example package is not present beside this game checkout.";
#else
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    moonmod::ResetChronicleTwoContentForTests();
    SeedVanillaItemTemplate();
    ChronicleTwoContentRestore restore_content;
#endif
    auto save = std::make_unique<CSaveData>();
    ActiveSaveRestore restore_save;
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    PortActiveSaveData = save.get();
#elif defined(MOONMOD_TEST_CHRONICLE)
    SaveData = save.get();
#endif
    save->Initialize();
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    save->user_data.money = 100;
#elif defined(MOONMOD_TEST_CHRONICLE)
    CDngStatusData *dungeon_status = save->GetDngStatus();
    ASSERT_NE(dungeon_status, nullptr);
    dungeon_status->money = 100;
#endif
    moonmod::SharedRuntime().Stop();
    StopRuntime stop_runtime;
    TempRoot root;
    const fs::path source = MOONFACTORY_FIRST_STEPS_PATH;
    ASSERT_TRUE(fs::exists(source / "mod.json"));
    ASSERT_TRUE(fs::exists(source / "main.lua"));
    const fs::path package = root.path / "mods" / "example.first-steps";
    fs::create_directories(package.parent_path());
    fs::copy(source, package, fs::copy_options::recursive);
    fs::create_directories(root.path / "save" / "moonmod");
    std::ofstream profile(root.path / "save" / "moonmod" / "profile.json", std::ios::binary | std::ios::trunc);
    profile << R"json({"format_version":1,"enabled":{"example.first-steps":true},"settings":{}})json";
    ASSERT_TRUE(profile.good());
    profile.close();

    moonmod::RuntimeConfig config;
    config.game_id = MOONMOD_TEST_GAME_ID;
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    config.game_name = "Chronicle Two";
    moonmod::ConfigureChronicleTwoApi(config);
#elif defined(MOONMOD_TEST_CHRONICLE)
    config.game_name = "Chronicle";
    moonmod::ConfigureChronicleApi(config);
#endif
    std::vector<std::string> logs;
    config.log = [&logs](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(moonmod::SharedRuntime().Start(std::move(config), error)) << error;
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    // Simulate the native item-table-ready callback after Lua has queued the item.
    moonmod::ApplyChronicleTwoContent();
#endif
    ASSERT_EQ(moonmod::SharedRuntime().Mods().size(), 1u);
    if (moonmod::SharedRuntime().Mods()[0].state != moonmod::ModState::Loaded) {
        for (const std::string &message : logs) ADD_FAILURE() << message;
    }
    ASSERT_EQ(moonmod::SharedRuntime().Mods()[0].state, moonmod::ModState::Loaded);
    EXPECT_TRUE(moonmod::SharedRuntime().Setting("example.first-steps", "show_log").get<bool>());
    ASSERT_EQ(moonmod::ManagerActions("example.first-steps").size(), 3u);
    const moonmod::ApiCallResult registered_quests = moonmod::SharedRuntime().Quests("example.first-steps");
    ASSERT_EQ(registered_quests.status, moonmod::ApiCallStatus::Ok) << registered_quests.message;
    ASSERT_EQ(registered_quests.value.at("quests").size(), 1u);
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("status"), "registered");
    ASSERT_EQ(registered_quests.value.at("quests").at(0).at("rewards").size(), 2u);
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("rewards").at(0).at("kind"), "currency");
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("rewards").at(0).at("amount"), 25);
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("rewards").at(1).at("kind"), "item");
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("rewards").at(1).at("id"),
              "example.first-steps:moon-cookie");
    EXPECT_EQ(registered_quests.value.at("quests").at(0).at("rewards").at(1).at("count"), 1);

    ASSERT_TRUE(moonmod::ActivateManagerAction("example.first-steps", "start-journey"));
    ASSERT_TRUE(moonmod::ActivateManagerAction("example.first-steps", "mark-clue"));
    moonmod::SharedRuntime().Publish("game.scene.entered", {{"scene_id", "town"}});
    moonmod::SharedRuntime().Publish("game.scene.entered", {{"scene_id", "town"}});
    const moonmod::ApiCallResult completed_quests = moonmod::SharedRuntime().Quests("example.first-steps");
    ASSERT_EQ(completed_quests.status, moonmod::ApiCallStatus::Ok) << completed_quests.message;
    EXPECT_EQ(completed_quests.value.at("quests").at(0).at("status"), "completed");
    EXPECT_EQ(completed_quests.value.at("quests").at(0).at("objectives").at(0).at("progress"), 1);
    EXPECT_EQ(completed_quests.value.at("quests").at(0).at("objectives").at(1).at("progress"), 1);
#if defined(MOONMOD_TEST_CHRONICLE_TWO)
    EXPECT_EQ(save->user_data.money, 125);
    int moon_cookie_item_no = -1;
    for (int index = 0; index < GameItemDataManage.common_num; ++index) {
        const CDataCommon &common = GameItemDataManage.common_data[index];
        if (common.name != nullptr && std::string_view(common.name) == "Moon Cookie") {
            moon_cookie_item_no = common.item_no;
        }
    }
    ASSERT_GT(moon_cookie_item_no, 274);
    EXPECT_EQ(save->user_data.GetNumSameItem(moon_cookie_item_no), 1);
#elif defined(MOONMOD_TEST_CHRONICLE)
    ASSERT_NE(save->GetDngStatus(), nullptr);
    EXPECT_EQ(save->GetDngStatus()->money, 125);
#endif
    if (std::count(logs.begin(), logs.end(), "FIRST_STEPS_REWARDS_GRANTED") != 1) {
        for (const std::string &message : logs) ADD_FAILURE() << message;
    }
    EXPECT_EQ(std::count(logs.begin(), logs.end(), "FIRST_STEPS_REWARDS_GRANTED"), 1);

    ASSERT_TRUE(moonmod::ActivateManagerAction("example.first-steps", "unlock-focus"));
    const moonmod::ApiCallResult skills = moonmod::ManagerSkills("example.first-steps");
    ASSERT_EQ(skills.status, moonmod::ApiCallStatus::Ok) << skills.message;
    ASSERT_EQ(skills.value.at("skills").size(), 2u);
    EXPECT_EQ(skills.value.at("skills").at(0).at("rank"), 1);
    EXPECT_EQ(skills.value.at("skills").at(1).at("rank"), 0);
    EXPECT_DOUBLE_EQ(skills.value.at("skills").at(0).at("effects").at("enemy_damage_per_rank").get<double>(), 0.1);
#endif
}
