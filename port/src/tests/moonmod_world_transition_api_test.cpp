#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "dataread.hpp"
#include "assets/data_registry.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "mapselect.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "platform/paths.hpp"
#include "scene.hpp"

namespace fs = std::filesystem;

TEST(MoonModWorldTransitionApi, QueuesNativeTownsAndRejectsUnregisteredInteriorMaps) {
    const fs::path root = fs::temp_directory_path() /
                          ("moonmod_world_transition_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path map_directory = root / "map";
    ASSERT_TRUE(fs::create_directories(map_directory));
    {
        std::ofstream map_catalog(map_directory / "map2.cfg.json", std::ios::binary);
        ASSERT_TRUE(map_catalog.good());
        map_catalog << R"({"schema_version":2,"kind":"map_names","count":2,"maps":[)"
                    << R"({"name":"probe-town","title":"Probe Town","add_path":"","type":1,"sel_type":1,"snd_data_id":-1,"area_no":2},)"
                    << R"({"name":"probe-field","title":"Probe Field","add_path":"","type":4,"sel_type":3,"snd_data_id":-1,"area_no":4}]})";
    }

    DataReadIndex(root);
    alignas(64) u_long128 load_buffer[256]{};
    LoadMapName(LANG_FRENCH, load_buffer);
    ASSERT_EQ(GetMapNameCount(), 2);

    struct SavedLoopState {
        int loop = PortLoopNo;
        int next_loop = PortNextLoopNo;
        INIT_LOOP_ARG next_arg = NextInitArg;
        CScene *scene = GetMainScene();
        int exit_flag = scene == nullptr ? 0 : scene->exit_flag;
        ~SavedLoopState() {
            PortLoopNo = loop;
            PortNextLoopNo = next_loop;
            NextInitArg = next_arg;
            if (scene != nullptr) scene->exit_flag = exit_flag;
        }
    } saved_state;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("world.transition");
    ASSERT_NE(operation, nullptr);
    const auto capability = std::ranges::find(config.api_capabilities, "world.transition",
                                               &moonmod::ApiCapability::path);
    ASSERT_NE(capability, config.api_capabilities.end());
    EXPECT_TRUE(capability->supported);
    EXPECT_TRUE(capability->reason.empty());

    const moonmod::Json town = moonmod::Json::array({{{"id", "chronicle-two:location:probe-town"}}});
    PortLoopNo = LOOP_TITLE;
    const int original_next_loop = PortNextLoopNo;
    const moonmod::ApiCallResult title_transition = config.api_handler("test.moonmod.transition", *operation, town);
    EXPECT_EQ(title_transition.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(title_transition.message.find("active town or dungeon"), std::string::npos);
    EXPECT_EQ(PortNextLoopNo, original_next_loop);

    PortLoopNo = LOOP_DUNGEON;
    const moonmod::ApiCallResult queued = config.api_handler("test.moonmod.transition", *operation, town);
    ASSERT_EQ(queued.status, moonmod::ApiCallStatus::Ok) << queued.message;
    EXPECT_EQ(queued.value.value("queued", false), true);
    EXPECT_EQ(queued.value.value("changed", false), true);
    EXPECT_EQ(queued.value.value("native_map_id", -1), 0);
    EXPECT_EQ(PortNextLoopNo, LOOP_EDIT);
    EXPECT_EQ(NextInitArg.map_no, 0);
    EXPECT_EQ(NextInitArg.floor_no, -1);
    EXPECT_EQ(NextInitArg.event_no, 100);
    ASSERT_NE(saved_state.scene, nullptr);
    EXPECT_TRUE(saved_state.scene->exit_flag);

    PortLoopNo = LOOP_EDIT;
    const moonmod::ApiCallResult current_town = config.api_handler("test.moonmod.transition", *operation, town);
    EXPECT_EQ(current_town.status, moonmod::ApiCallStatus::Ok);
    EXPECT_FALSE(current_town.value.value("queued", true));
    EXPECT_FALSE(current_town.value.value("changed", true));

    const moonmod::ApiCallResult interior = config.api_handler(
        "test.moonmod.transition", *operation,
        moonmod::Json::array({{{"id", "chronicle-two:location:probe-field"}}}));
    EXPECT_EQ(interior.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(interior.message.find("registered MoonMod interior maps"), std::string::npos);

    const fs::path empty_root = root / "empty";
    ASSERT_TRUE(fs::create_directories(empty_root));
    DataReadIndex(empty_root);
    LoadMapName(LANG_FRENCH, load_buffer);
    EXPECT_EQ(GetMapNameCount(), 0);
    PathsReset();
    std::error_code error;
    fs::remove_all(root, error);
}
