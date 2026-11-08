#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "dng_main.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "scene.hpp"
#include "savedata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModWorldApi, ReadsNativeMapAndDungeonContext) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    save->map_no = 17;
    save->sub_map_no = 5;
    save->area_no = 3;
    save->save_dungeon.floor_id[2] = 8;
    const int previous_dungeon = DngStatus.dungeon_no;
    const int previous_loop = PortLoopNo;
    DngStatus.dungeon_no = 2;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("world.get");
    ASSERT_NE(operation, nullptr);

    const moonmod::ApiCallResult world = config.api_handler("test.moonmod.world-probe", *operation,
                                                            moonmod::Json::array());
    ASSERT_EQ(world.status, moonmod::ApiCallStatus::Ok) << world.message;
    EXPECT_EQ(world.value.value("map_id", -1), 17);
    EXPECT_EQ(world.value.value("area_id", -1), 3);
    EXPECT_EQ(world.value.value("sub_map_id", -1), 5);
    EXPECT_EQ(world.value.at("time").at("native_cycle_hours"), 24);
    EXPECT_NEAR(world.value.at("time").at("fraction").get<double>(), save->now_time / 24.0, 0.00001);
    const std::string context = world.value.value("context", "");
    EXPECT_TRUE(context == "menu" || context == "title" || context == "map" || context == "georama" ||
                context == "dungeon");
    if (context == "dungeon") {
        EXPECT_EQ(world.value.value("dungeon_id", -1), 2);
        EXPECT_EQ(world.value.value("floor_id", -1), 8);
    } else {
        EXPECT_TRUE(world.value.at("dungeon_id").is_null());
        EXPECT_TRUE(world.value.at("floor_id").is_null());
    }

    PortActiveSaveData = previous_save;
    DngStatus.dungeon_no = previous_dungeon;
    PortLoopNo = previous_loop;
}

TEST(MoonModWorldApi, SetsNormalizedTimeAndRejectsInvalidOrMissingSave) {
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    CScene *const previous_dungeon_scene = DngMainScene;
    struct Restore {
        CSaveData *save;
        int loop;
        CScene *dungeon_scene;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
            DngMainScene = dungeon_scene;
        }
    } restore{previous_save, previous_loop, previous_dungeon_scene};

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("world.set_time");
    ASSERT_NE(operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "world.set_time" && capability.supported;
    }));

    PortLoopNo = LOOP_TITLE;
    PortActiveSaveData = nullptr;
    const moonmod::ApiCallResult no_save = config.api_handler(
        "test.moonmod.world-time", *operation,
        moonmod::Json::array({moonmod::Json{{"fraction", 0.5}}}));
    EXPECT_EQ(no_save.status, moonmod::ApiCallStatus::Failed);

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    const moonmod::ApiCallResult set = config.api_handler(
        "test.moonmod.world-time", *operation,
        moonmod::Json::array({moonmod::Json{{"fraction", 0.25}}}));
    ASSERT_EQ(set.status, moonmod::ApiCallStatus::Ok) << set.message;
    EXPECT_NEAR(save->now_time, 6.0, 0.00001);
    EXPECT_NEAR(set.value.at("time").at("fraction").get<double>(), 0.25, 0.00001);

    CScene *const scene = GetMainScene();
    const float previous_scene_time = scene->time;
    CSaveData *const previous_scene_save = scene->save_data;
    struct RestoreScene {
        CScene *scene;
        float time;
        CSaveData *save;
        ~RestoreScene() {
            scene->time = time;
            scene->save_data = save;
        }
    } restore_scene{scene, previous_scene_time, previous_scene_save};
    DngMainScene = scene;
    scene->save_data = save.get();
    PortLoopNo = LOOP_DUNGEON;
    const moonmod::ApiCallResult live_set = config.api_handler(
        "test.moonmod.world-time", *operation,
        moonmod::Json::array({moonmod::Json{{"fraction", 0.75}}}));
    ASSERT_EQ(live_set.status, moonmod::ApiCallStatus::Ok) << live_set.message;
    EXPECT_NEAR(scene->time, 18.0, 0.00001);
    EXPECT_NEAR(save->now_time, 18.0, 0.00001);

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.world-time", *operation,
        moonmod::Json::array({moonmod::Json{{"fraction", -0.01}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_NEAR(save->now_time, 18.0, 0.00001);
}
