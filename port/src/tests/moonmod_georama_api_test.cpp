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

#include "save_json.hpp"
#include "moonmod_item_table_fixture.hpp"
#include "editdata.hpp"
#include "editmap.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "mg_memory.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/content_georama.hpp"
#include "scriptinterpreter.hpp"
#include "scenesnd.hpp"
#include "savedata.hpp"
#include "userdata.hpp"


namespace {

namespace fs = std::filesystem;

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-georama-event-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST(MoonModGeoramaApi, ReadsNativeTownPlacementsAndAnalysisState) {
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CSaveData *save;
        int loop;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
        }
    } restore{previous_save, previous_loop};

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("georama.get");
    const moonmod::ApiOperationSpec *condition_set = moonmod::FindApiOperation("georama.condition_set");
    const moonmod::ApiOperationSpec *request_known_set = moonmod::FindApiOperation("georama.request_known_set");
    const moonmod::ApiOperationSpec *request_set = moonmod::FindApiOperation("georama.request_set");
    ASSERT_NE(get, nullptr);
    ASSERT_NE(condition_set, nullptr);
    ASSERT_NE(request_known_set, nullptr);
    ASSERT_NE(request_set, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.get" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.request_set" && capability.supported && capability.reason.empty();
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.condition_set" && capability.supported && capability.reason.empty();
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.request_known_set" && capability.supported && capability.reason.empty();
    }));

    PortActiveSaveData = nullptr;
    EXPECT_EQ(config.api_handler("test.moonmod.georama", *get, moonmod::Json::array()).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.georama", *condition_set,
                                 moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"condition_id", 5}, {"met", true}}})).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.georama", *request_known_set,
                                 moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 2}, {"known", true}}})).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.georama", *request_set,
                                 moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 2}, {"completed", true}}})).status,
              moonmod::ApiCallStatus::Failed);

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_EDIT;
    save->area_no = 3;
    CEditData *town = save->GetEditData(3);
    ASSERT_NE(town, nullptr);
    town->save_count = 7;
    town->culture_point = 125;
    // The town's layout is kept beside its save record (save_json.hpp).
    TownLayout &layout = PortTownLayout(*town);
    layout.parts.resize(3);
    layout.houses.resize(1);
    layout.parts[2].id = 0x42;
    layout.parts[2].state = 1;
    layout.parts[2].angle = 6;
    layout.parts[2].pos = {120, -24, 360};
    layout.parts[2].color[0] = {10, 20, 30};
    layout.parts[2].house_no = 1;
    layout.houses[0].npc_no = 8;
    town->analyze.data_open[2] = 1;
    town->analyze.condition[5] = 1;
    town->analyze.condition_open[7] = 1;

    const moonmod::ApiCallResult active =
        config.api_handler("test.moonmod.georama", *get, moonmod::Json::array());
    ASSERT_EQ(active.status, moonmod::ApiCallStatus::Ok) << active.message;
    EXPECT_EQ(active.value.at("town_id"), 3);
    EXPECT_EQ(active.value.at("id"), "chronicle-two:town:3");
    EXPECT_EQ(active.value.at("save_count"), 7);
    EXPECT_EQ(active.value.at("culture_points"), 125);
    ASSERT_EQ(active.value.at("placed_parts").size(), 1u);
    const auto &part = active.value.at("placed_parts").at(0);
    EXPECT_EQ(part.at("index"), 2);
    EXPECT_EQ(part.at("id"), "chronicle-two:georama-placement:3:2");
    EXPECT_EQ(part.at("part_id"), 0x42);
    EXPECT_EQ(part.at("rotation_steps"), 6);
    EXPECT_EQ(part.at("position"), moonmod::Json({{"x", 120}, {"y", -24}, {"z", 360}}));
    EXPECT_EQ(part.at("colors").at(0), moonmod::Json::array({10, 20, 30}));
    EXPECT_EQ(part.at("house_id"), 0);
    ASSERT_EQ(active.value.at("houses").size(), 1u);
    EXPECT_EQ(active.value.at("houses").at(0).at("npc_id"), 8);
    EXPECT_EQ(active.value.at("known_requests"), moonmod::Json::array({2}));
    EXPECT_EQ(active.value.at("met_conditions"), moonmod::Json::array({5}));
    EXPECT_EQ(active.value.at("known_conditions"), moonmod::Json::array({7}));

    const moonmod::ApiCallResult condition_cleared = config.api_handler(
        "test.moonmod.georama", *condition_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"condition_id", 5}, {"met", false}}}));
    ASSERT_EQ(condition_cleared.status, moonmod::ApiCallStatus::Ok) << condition_cleared.message;
    EXPECT_TRUE(condition_cleared.value.at("previous"));
    EXPECT_FALSE(condition_cleared.value.at("met"));
    EXPECT_TRUE(condition_cleared.value.at("changed"));
    const moonmod::ApiCallResult cleared_town = config.api_handler(
        "test.moonmod.georama", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(cleared_town.status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(cleared_town.value.at("met_conditions"), moonmod::Json::array());

    const moonmod::ApiCallResult condition_restored = config.api_handler(
        "test.moonmod.georama", *condition_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"condition_id", 5}, {"met", true}}}));
    ASSERT_EQ(condition_restored.status, moonmod::ApiCallStatus::Ok);
    EXPECT_FALSE(condition_restored.value.at("previous"));
    EXPECT_TRUE(condition_restored.value.at("met"));
    EXPECT_TRUE(condition_restored.value.at("changed"));
    EXPECT_EQ(town->analyze.condition[5], 1);

    const moonmod::ApiCallResult condition_unchanged = config.api_handler(
        "test.moonmod.georama", *condition_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"condition_id", 5}, {"met", true}}}));
    ASSERT_EQ(condition_unchanged.status, moonmod::ApiCallStatus::Ok);
    EXPECT_TRUE(condition_unchanged.value.at("previous"));
    EXPECT_FALSE(condition_unchanged.value.at("changed"));

    const moonmod::ApiCallResult request_hidden = config.api_handler(
        "test.moonmod.georama", *request_known_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 2}, {"known", false}}}));
    ASSERT_EQ(request_hidden.status, moonmod::ApiCallStatus::Ok) << request_hidden.message;
    EXPECT_TRUE(request_hidden.value.at("previous"));
    EXPECT_FALSE(request_hidden.value.at("known"));
    EXPECT_TRUE(request_hidden.value.at("changed"));
    const moonmod::ApiCallResult hidden_town = config.api_handler(
        "test.moonmod.georama", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(hidden_town.status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(hidden_town.value.at("known_requests"), moonmod::Json::array());
    const moonmod::ApiCallResult request_known = config.api_handler(
        "test.moonmod.georama", *request_known_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 9}, {"known", true}}}));
    ASSERT_EQ(request_known.status, moonmod::ApiCallStatus::Ok) << request_known.message;
    EXPECT_FALSE(request_known.value.at("previous"));
    EXPECT_TRUE(request_known.value.at("known"));
    EXPECT_TRUE(request_known.value.at("changed"));
    const moonmod::ApiCallResult request_known_unchanged = config.api_handler(
        "test.moonmod.georama", *request_known_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 9}, {"known", true}}}));
    ASSERT_EQ(request_known_unchanged.status, moonmod::ApiCallStatus::Ok);
    EXPECT_TRUE(request_known_unchanged.value.at("previous"));
    EXPECT_FALSE(request_known_unchanged.value.at("changed"));
    const moonmod::ApiCallResult known_town = config.api_handler(
        "test.moonmod.georama", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(known_town.status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(known_town.value.at("known_requests"), moonmod::Json::array({9}));

    for (const moonmod::Json &invalid : {
             moonmod::Json::array(),
             moonmod::Json::array({moonmod::Json{{"town_id", 5}, {"request_id", 0}, {"known", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 16}, {"known", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 0}, {"known", 1}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 0}, {"known", true}, {"extra", 1}}}),
         }) {
        EXPECT_EQ(config.api_handler("test.moonmod.georama", *request_known_set, invalid).status,
                  moonmod::ApiCallStatus::InvalidArgument);
    }

    for (const moonmod::Json &invalid : {
             moonmod::Json::array(),
             moonmod::Json::array({moonmod::Json{{"town_id", 5}, {"condition_id", 0}, {"met", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"condition_id", 64}, {"met", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"condition_id", 0}, {"met", 1}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"condition_id", 0}, {"met", true}, {"extra", 1}}}),
         }) {
        EXPECT_EQ(config.api_handler("test.moonmod.georama", *condition_set, invalid).status,
                  moonmod::ApiCallStatus::InvalidArgument);
    }

    PortLoopNo = LOOP_DUNGEON;
    EXPECT_EQ(config.api_handler("test.moonmod.georama", *get, moonmod::Json::array()).status,
              moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult selected = config.api_handler(
        "test.moonmod.georama", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(selected.status, moonmod::ApiCallStatus::Ok) << selected.message;
    EXPECT_EQ(selected.value.at("town_id"), 3);

    for (const moonmod::Json &invalid : {
             moonmod::Json::array({1}),
             moonmod::Json::array({moonmod::Json{{"town_id", 5}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"unexpected", true}}}),
             moonmod::Json::array({moonmod::Json{{"unexpected", true}}}),
         }) {
        EXPECT_EQ(config.api_handler("test.moonmod.georama", *get, invalid).status,
                  moonmod::ApiCallStatus::InvalidArgument);
    }

    layout.parts[2].angle = 24;
    const moonmod::ApiCallResult malformed_native = config.api_handler(
        "test.moonmod.georama", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    EXPECT_EQ(malformed_native.status, moonmod::ApiCallStatus::Failed);
    PortClearTownLayout(*town);
}

TEST(MoonModGeoramaApi, SetsCompletionThroughNativeRequestConditions) {
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CSaveData *save;
        int loop;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
        }
    } restore{previous_save, previous_loop};

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_TITLE;
    CEditData *town = save->GetEditData(3);
    ASSERT_NE(town, nullptr);
    EditAnalyzeSrc *analysis = town->GetAnalyzeSrc(3);
    ASSERT_NE(analysis, nullptr);
    struct RestoreAnalysis {
        EditAnalyzeSrc *target;
        EditAnalyzeSrc saved;
        explicit RestoreAnalysis(EditAnalyzeSrc *source) : target(source), saved(*source) {}
        ~RestoreAnalysis() { *target = saved; }
    } restore_analysis(analysis);

    analysis->Init();
    analysis->data[0].message = const_cast<char *>("MoonFactory request probe");
    analysis->data[0].percent = 35;
    analysis->data[0].con_no[0] = 5;
    analysis->data[0].con_no[1] = 6;
    analysis->data[0].con_no[2] = -1;
    town->analyze.condition[5] = 0;
    town->analyze.condition[6] = 0;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *request_set = moonmod::FindApiOperation("georama.request_set");
    ASSERT_NE(request_set, nullptr);
    const moonmod::Json args = moonmod::Json::array(
        {moonmod::Json{{"town_id", 3}, {"request_id", 0}, {"completed", true}}});
    const moonmod::ApiCallResult completed =
        config.api_handler("test.moonmod.georama", *request_set, args);
    ASSERT_EQ(completed.status, moonmod::ApiCallStatus::Ok) << completed.message;
    EXPECT_FALSE(completed.value.at("previous"));
    EXPECT_TRUE(completed.value.at("completed"));
    EXPECT_TRUE(completed.value.at("changed"));
    EXPECT_EQ(completed.value.at("progress_percent"), 35);
    EXPECT_EQ(town->analyze.condition[5], 1);
    EXPECT_EQ(town->analyze.condition[6], 1);
    EXPECT_EQ(town->GetAnalyzeFlag(3, 0), 1);

    const moonmod::ApiCallResult repeated =
        config.api_handler("test.moonmod.georama", *request_set, args);
    ASSERT_EQ(repeated.status, moonmod::ApiCallStatus::Ok) << repeated.message;
    EXPECT_TRUE(repeated.value.at("previous"));
    EXPECT_TRUE(repeated.value.at("completed"));
    EXPECT_FALSE(repeated.value.at("changed"));

    const moonmod::ApiCallResult cleared = config.api_handler(
        "test.moonmod.georama", *request_set,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 0}, {"completed", false}}}));
    ASSERT_EQ(cleared.status, moonmod::ApiCallStatus::Ok) << cleared.message;
    EXPECT_TRUE(cleared.value.at("previous"));
    EXPECT_FALSE(cleared.value.at("completed"));
    EXPECT_TRUE(cleared.value.at("changed"));
    EXPECT_EQ(cleared.value.at("progress_percent"), 0);
    EXPECT_EQ(town->analyze.condition[5], 0);
    EXPECT_EQ(town->analyze.condition[6], 0);

    EXPECT_EQ(config.api_handler(
                  "test.moonmod.georama", *request_set,
                  moonmod::Json::array({moonmod::Json{{"town_id", 3}, {"request_id", 1}, {"completed", true}}})).status,
              moonmod::ApiCallStatus::Failed);
    for (const moonmod::Json &invalid : {
             moonmod::Json::array(),
             moonmod::Json::array({moonmod::Json{{"town_id", 5}, {"request_id", 0}, {"completed", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 16}, {"completed", true}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 0}, {"completed", 1}}}),
             moonmod::Json::array({moonmod::Json{{"town_id", 0}, {"request_id", 0}, {"completed", true}, {"extra", 1}}}),
         }) {
        EXPECT_EQ(config.api_handler("test.moonmod.georama", *request_set, invalid).status,
                  moonmod::ApiCallStatus::InvalidArgument);
    }
}

TEST(MoonModGeoramaApi, ReadsBoundedSavedRiverGridCells) {
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CSaveData *save;
        int loop;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
        }
    } restore{previous_save, previous_loop};

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    PortLoopNo = LOOP_TITLE;
    CEditData *town = save->GetEditData(2);
    ASSERT_NE(town, nullptr);

    // The town's river grids are kept in its layout beside the save record (save_json.hpp).
    TownLayout &layout = PortTownLayout(*town);
    layout.grids = {
        TownGrid{.num_x = 2, .num_z = 2, .pos = {100, -5, 200}, .cells = {1, 0, 0x81, 0}},
        TownGrid{.num_x = 1, .num_z = 2, .pos = {-30, 0, 40}, .cells = {0, 1}},
    };

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("georama.get");
    ASSERT_NE(get, nullptr);
    const moonmod::ApiCallResult result = config.api_handler(
        "test.moonmod.georama-river", *get, moonmod::Json::array({moonmod::Json{{"town_id", 2}}}));
    ASSERT_EQ(result.status, moonmod::ApiCallStatus::Ok) << result.message;
    ASSERT_EQ(result.value.at("river_grids").size(), 2u);
    EXPECT_EQ(result.value.at("river_grids").at(0).at("index"), 0);
    EXPECT_EQ(result.value.at("river_grids").at(0).at("size"), moonmod::Json({{"x", 2}, {"z", 2}}));
    EXPECT_EQ(result.value.at("river_grids").at(0).at("position"),
              moonmod::Json({{"x", 100}, {"y", -5}, {"z", 200}}));
    EXPECT_EQ(result.value.at("river_grids").at(0).at("cells"),
              moonmod::Json::array({moonmod::Json::array({true, false}), moonmod::Json::array({true, false})}));
    EXPECT_EQ(result.value.at("river_grids").at(1).at("cells"),
              moonmod::Json::array({moonmod::Json::array({false, true})}));

    layout.grids[0].num_x = 255;
    layout.grids[0].num_z = 255;
    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.georama-river", *get, moonmod::Json::array({moonmod::Json{{"town_id", 2}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(invalid.message.find("dimensions are invalid"), std::string::npos);
    PortClearTownLayout(*town);
}

TEST(MoonModGeoramaApi, SetsNativeRiverGridCellsPersistsReadbackAndIsIdempotent) {
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    CScene *scene = GetMainScene();
    ASSERT_NE(scene, nullptr);
    const int previous_map_count = scene->map_num;
    const int previous_active_map = scene->active_map;
    CSceneMap previous_map_slot = std::move(scene->map[0]);
    struct Restore {
        CSaveData *save;
        int loop;
        CScene *scene;
        int map_count;
        int active_map;
        CSceneMap map_slot;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
            scene->map[0] = std::move(map_slot);
            scene->map_num = map_count;
            scene->active_map = active_map;
        }
    } restore{previous_save, previous_loop, scene, previous_map_count, previous_active_map, std::move(previous_map_slot)};

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    save->area_no = 3;
    PortActiveSaveData = save.get();

    alignas(64) u_long128 storage[9000]{};
    mgCMemory scene_memory;
    scene_memory.stSetBuffer(storage, static_cast<int>(std::size(storage)));
    CEditGrid river_grid;
    river_grid.Initialize();
    river_grid.Create(2, 2, &scene_memory);
    river_grid.step_x = 80.0f;
    river_grid.step_z = 80.0f;
    river_grid.origin[0] = 100.0f;
    river_grid.origin[1] = 0.0f;
    river_grid.origin[2] = 200.0f;
    river_grid.origin[3] = 1.0f;

    CEditMap map;
    map.area_no = 3;
    map.CreateTable(&scene_memory, 8, 4096);
    map.grid[0] = &river_grid;
    map.grid_max = EDIT_MAP_GRID_MAX;
    scene->map_num = std::max(scene->map_num, 1);
    ASSERT_EQ(scene->AssignMap(0, &map, const_cast<char *>("MoonFactoryRiverTown")), 0);
    scene->active_map = 0;
    PortLoopNo = LOOP_EDIT;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *set_river = moonmod::FindApiOperation("georama.river_set");
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("georama.get");
    ASSERT_NE(set_river, nullptr);
    ASSERT_NE(get, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.river_set" && capability.supported && capability.reason.empty();
    }));

    CEditData *town = save->GetEditData(3);
    ASSERT_NE(town, nullptr);
    const int initial_save_count = town->save_count;
    const moonmod::Json add_request = {
        {"grid_index", 0}, {"x", 1}, {"z", 0}, {"has_river", true}};
    const moonmod::ApiCallResult added = config.api_handler(
        "test.moonmod.georama-river-edit", *set_river, moonmod::Json::array({add_request}));
    ASSERT_EQ(added.status, moonmod::ApiCallStatus::Ok) << added.message;
    EXPECT_FALSE(added.value.at("previous"));
    EXPECT_TRUE(added.value.at("has_river"));
    EXPECT_TRUE(added.value.at("changed"));
    EXPECT_TRUE(river_grid.River(1, 0));
    EXPECT_EQ(town->save_count, initial_save_count + 1);

    const moonmod::ApiCallResult saved = config.api_handler(
        "test.moonmod.georama-river-edit", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(saved.status, moonmod::ApiCallStatus::Ok) << saved.message;
    ASSERT_EQ(saved.value.at("river_grids").size(), 1u);
    EXPECT_EQ(saved.value.at("river_grids").at(0).at("cells"),
              moonmod::Json::array({moonmod::Json::array({false, false}), moonmod::Json::array({true, false})}));

    const moonmod::ApiCallResult unchanged = config.api_handler(
        "test.moonmod.georama-river-edit", *set_river, moonmod::Json::array({add_request}));
    ASSERT_EQ(unchanged.status, moonmod::ApiCallStatus::Ok) << unchanged.message;
    EXPECT_FALSE(unchanged.value.at("changed"));
    EXPECT_EQ(town->save_count, initial_save_count + 1);

    moonmod::Json remove_request = add_request;
    remove_request["has_river"] = false;
    const moonmod::ApiCallResult removed = config.api_handler(
        "test.moonmod.georama-river-edit", *set_river, moonmod::Json::array({remove_request}));
    ASSERT_EQ(removed.status, moonmod::ApiCallStatus::Ok) << removed.message;
    EXPECT_TRUE(removed.value.at("previous"));
    EXPECT_FALSE(removed.value.at("has_river"));
    EXPECT_TRUE(removed.value.at("changed"));
    EXPECT_FALSE(river_grid.River(1, 0));
    EXPECT_EQ(town->save_count, initial_save_count + 2);

    moonmod::Json out_of_bounds = add_request;
    out_of_bounds["x"] = 2;
    EXPECT_EQ(config.api_handler("test.moonmod.georama-river-edit", *set_river,
                                 moonmod::Json::array({out_of_bounds})).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.georama-river-edit", *set_river,
                                 moonmod::Json::array({moonmod::Json{{"grid_index", 0}, {"x", 0}, {"z", 0},
                                                                      {"has_river", 1}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
}

TEST(MoonModGeoramaApi, RegistersDataOnlyPieceVariantIntoNativeBuildDefinitions) {
    moonmod::ResetChronicleTwoContentForTests();
    CSaveData *previous_save = PortActiveSaveData;
    struct Restore {
        CSaveData *save;
        ~Restore() {
            PortActiveSaveData = save;
            moonmod::ResetChronicleTwoContentForTests();
        }
    } restore{previous_save};

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_piece = moonmod::FindApiOperation("georama.register");
    const moonmod::ApiOperationSpec *content_register = moonmod::FindApiOperation("content.register");
    const moonmod::ApiOperationSpec *content_unregister = moonmod::FindApiOperation("content.unregister");
    const moonmod::ApiOperationSpec *content_list = moonmod::FindApiOperation("content.list");
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("georama.get");
    ASSERT_NE(register_piece, nullptr);
    ASSERT_NE(content_register, nullptr);
    ASSERT_NE(content_unregister, nullptr);
    ASSERT_NE(content_list, nullptr);
    ASSERT_NE(get, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.register" && capability.supported && capability.reason.empty();
    }));

    const moonmod::Json piece_definition = {
        {"id", "moonstone-pillar"}, {"name", "Moonstone Pillar"}, {"template_part_id", 7}};
    const moonmod::ApiCallResult registered = config.api_handler(
        "test.moonmod.georama-pieces", *content_register,
        moonmod::Json::array({{{"type", "georama_piece"}, {"definition", piece_definition}}}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.value("id", ""), "test.moonmod.georama-pieces:moonstone-pillar");
    EXPECT_EQ(registered.value.value("status", ""), "pending");
    EXPECT_EQ(registered.value.value("template_part_id", -1), 7);
    const moonmod::ApiCallResult content_entries = config.api_handler(
        "test.moonmod.georama-pieces", *content_list, moonmod::Json::array());
    ASSERT_EQ(content_entries.status, moonmod::ApiCallStatus::Ok) << content_entries.message;
    ASSERT_EQ(content_entries.value.value("count", 0), 1);
    EXPECT_EQ(content_entries.value.at("entries")[0].value("content_type", ""), "georama_piece");
    EXPECT_EQ(config.api_handler("test.moonmod.georama-pieces", *register_piece,
                                 moonmod::Json::array({piece_definition})).status,
              moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::Json unavailable_piece = {
        {"id", "missing-template"}, {"name", "Missing Template"}, {"template_part_id", 99}};
    const moonmod::ApiCallResult pending_missing = config.api_handler(
        "test.moonmod.georama-pieces", *register_piece, moonmod::Json::array({unavailable_piece}));
    ASSERT_EQ(pending_missing.status, moonmod::ApiCallStatus::Ok) << pending_missing.message;

    alignas(64) u_long128 scene_storage[2048]{};
    mgCMemory scene_memory;
    scene_memory.stSetBuffer(scene_storage, static_cast<int>(sizeof(scene_storage) / sizeof(scene_storage[0])));
    const int source_quads = static_cast<int>((sizeof(CEditPartsInfo) + 15u) / 16u + 2u);
    u_long128 *source_memory = scene_memory.Alloc(source_quads);
    ASSERT_NE(source_memory, nullptr);
    CEditPartsInfo *source_table = new (source_memory) CEditPartsInfo[1];
    ASSERT_NE(source_table, nullptr);
    source_table[0].id = 7;
    source_table[0].attr = EDIT_PARTS_ATR_QUARTER_TURN;
    source_table[0].max_num = 9;
    source_table[0].polyn[0] = 3;
    source_table[0].parts_name = mgCopyString(const_cast<char *>("NativePillarModel"), &scene_memory);
    source_table[0].edit_name = mgCopyString(const_cast<char *>("Native Pillar"), &scene_memory);
    source_table[0].parts = reinterpret_cast<CMapParts *>(static_cast<std::uintptr_t>(1));
    ASSERT_NE(source_table[0].parts_name, nullptr);
    ASSERT_NE(source_table[0].edit_name, nullptr);

    CEditMap map;
    map.area_no = 3;
    map.info_mngr.SetePartsInfoTable(source_table, 1);
    moonmod::ApplyChronicleTwoGeorama(&map, &scene_memory);

    const moonmod::ApiCallResult unregister_materialized = config.api_handler(
        "test.moonmod.georama-pieces", *content_unregister,
        moonmod::Json::array({{{"type", "georama_piece"}, {"id", "moonstone-pillar"}}}));
    EXPECT_EQ(unregister_materialized.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(unregister_materialized.message.find("already materialized"), std::string::npos);

    ASSERT_EQ(map.info_mngr.parts_info_num, 2);
    // The variant takes its persistent definition id (content_georama.hpp), handed out from 0x7F downward.
    const int pillar_id = moonmod::ChronicleTwoGeoramaNativePartId("test.moonmod.georama-pieces:moonstone-pillar", 3);
    ASSERT_GE(pillar_id, moonmod::kModGeoramaPartIdBottom);
    ASSERT_LE(pillar_id, moonmod::kModGeoramaPartIdTop);
    CEditPartsInfo *custom = map.info_mngr.GetePartsInfoAtID(pillar_id);
    ASSERT_NE(custom, nullptr);
    EXPECT_STREQ(custom->edit_name, "Moonstone Pillar");
    EXPECT_EQ(custom->parts, source_table[0].parts);
    EXPECT_STREQ(custom->parts_name, "NativePillarModel");
    EXPECT_EQ(custom->max_num, 9);
    EXPECT_EQ(custom->polyn[0], 3);
    EXPECT_EQ(custom->attr & EDIT_PARTS_ATR_NO_REFUND, 0u);
    EXPECT_EQ(map.info_mngr.GetePartsInfoAtID(7), &map.info_mngr.parts_info[0]);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId("test.moonmod.georama-pieces:moonstone-pillar", 3), pillar_id);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId("test.moonmod.georama-pieces:moonstone-pillar", 2), -1);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId("test.moonmod.georama-pieces:missing-template", 3), -1);

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    const moonmod::ApiCallResult town = config.api_handler(
        "test.moonmod.georama-pieces", *get,
        moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(town.status, moonmod::ApiCallStatus::Ok) << town.message;
    ASSERT_TRUE(town.value.at("placement_anchors").is_array());
    ASSERT_EQ(town.value.at("native_piece_templates").size(), 1u);
    EXPECT_EQ(town.value.at("native_piece_templates").at(0).at("template_part_id"), 7);
    EXPECT_EQ(town.value.at("native_piece_templates").at(0).at("name"), "Native Pillar");
    ASSERT_EQ(town.value.at("mod_pieces").size(), 2u);
    const auto applied = std::ranges::find(town.value.at("mod_pieces"),
                                           "test.moonmod.georama-pieces:moonstone-pillar",
                                           [](const moonmod::Json &entry) { return entry.at("id").get<std::string>(); });
    ASSERT_NE(applied, town.value.at("mod_pieces").end());
    EXPECT_EQ(applied->value("status", ""), "registered");
    EXPECT_EQ(applied->value("native_part_id", -1), pillar_id);
    const auto missing = std::ranges::find(town.value.at("mod_pieces"),
                                           "test.moonmod.georama-pieces:missing-template",
                                           [](const moonmod::Json &entry) { return entry.at("id").get<std::string>(); });
    ASSERT_NE(missing, town.value.at("mod_pieces").end());
    EXPECT_EQ(missing->value("status", ""), "failed");
    EXPECT_NE(missing->value("reason", "").find("template piece is unavailable"), std::string::npos);
}

TEST(MoonModGeoramaApi, PlacesRegisteredPieceWithNativeRulesAndPersistsSavedLayout) {
    moonmod::ResetChronicleTwoContentForTests();
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    CScene *scene = GetMainScene();
    ASSERT_NE(scene, nullptr);
    const int previous_map_count = scene->map_num;
    const int previous_active_map = scene->active_map;
    CSceneMap previous_map_slot = std::move(scene->map[0]);
    struct Restore {
        CSaveData *save;
        int loop;
        CScene *scene;
        int map_count;
        int active_map;
        CSceneMap map_slot;
        ~Restore() {
            PortActiveSaveData = save;
            PortLoopNo = loop;
            scene->map[0] = std::move(map_slot);
            scene->map_num = map_count;
            scene->active_map = active_map;
            moonmod::ResetChronicleTwoContentForTests();
        }
    } restore{previous_save, previous_loop, scene, previous_map_count, previous_active_map, std::move(previous_map_slot)};

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    save->area_no = 3;
    PortActiveSaveData = save.get();

    alignas(64) u_long128 storage[9000]{};
    mgCMemory scene_memory;
    scene_memory.stSetBuffer(storage, static_cast<int>(std::size(storage)));
    CEditMap map;
    map.area_no = 3;
    map.CreateTable(&scene_memory, 8, 4096);
    for (int index = 0; index < map.place_log_max; ++index) {
        map.place_log[index].parts_no = -1;
        map.place_log[index].base_no = -1;
    }

    const int info_quads = static_cast<int>((sizeof(CEditPartsInfo) + 15u) / 16u + 2u);
    u_long128 *info_memory = scene_memory.Alloc(info_quads);
    ASSERT_NE(info_memory, nullptr);
    auto *source_table = new (info_memory) CEditPartsInfo[1];
    ASSERT_NE(source_table, nullptr);
    source_table[0].id = 7;
    source_table[0].attr = EDIT_PARTS_ATR_QUARTER_TURN;
    source_table[0].max_num = 9;
    source_table[0].parts_name = mgCopyString(const_cast<char *>("MoonFactoryTestModel"), &scene_memory);
    source_table[0].edit_name = mgCopyString(const_cast<char *>("Native Pillar"), &scene_memory);
    ASSERT_NE(source_table[0].parts_name, nullptr);
    ASSERT_NE(source_table[0].edit_name, nullptr);

    CList<CMapParts> model_node;
    model_node.data.SetName(const_cast<char *>("MoonFactoryTestModel"));
    map.AddParts(&model_node);
    source_table[0].parts = &model_node.data;
    map.info_mngr.SetePartsInfoTable(source_table, 1);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_piece = moonmod::FindApiOperation("georama.register");
    const moonmod::ApiOperationSpec *place_piece = moonmod::FindApiOperation("georama.place");
    const moonmod::ApiOperationSpec *remove_piece = moonmod::FindApiOperation("georama.remove");
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("georama.get");
    ASSERT_NE(register_piece, nullptr);
    ASSERT_NE(place_piece, nullptr);
    ASSERT_NE(remove_piece, nullptr);
    ASSERT_NE(get, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.place" && capability.supported && capability.reason.empty();
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "georama.remove" && capability.supported && capability.reason.empty();
    }));

    const moonmod::Json definition = {
        {"id", "moon-pillar"}, {"name", "Moon Pillar"}, {"template_part_id", 7}};
    const moonmod::ApiCallResult registered = config.api_handler(
        "test.moonmod.georama-place", *register_piece, moonmod::Json::array({definition}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    moonmod::ApplyChronicleTwoGeorama(&map, &scene_memory);
    const int native_id = moonmod::ChronicleTwoGeoramaNativePartId(registered.value.at("id").get<std::string>(), 3);
    ASSERT_GE(native_id, moonmod::kModGeoramaPartIdBottom) << "a persistent definition id (content_georama.hpp)";
    ASSERT_NE(map.GetePartsInfoAtID(native_id), nullptr);
    save->SetBuildPartsNum(native_id, 1);

    scene->map_num = std::max(scene->map_num, 1);
    ASSERT_EQ(scene->AssignMap(0, &map, const_cast<char *>("MoonFactoryTestTown")), 0);
    scene->active_map = 0;
    PortLoopNo = LOOP_EDIT;

    const moonmod::Json request = {
        {"id", registered.value.at("id")},
        {"position", {{"x", 20}, {"y", 0}, {"z", 30}}},
        {"rotation_steps", 6}};
    const moonmod::ApiCallResult placed = config.api_handler(
        "test.moonmod.georama-place", *place_piece, moonmod::Json::array({request}));
    ASSERT_EQ(placed.status, moonmod::ApiCallStatus::Ok) << placed.message;
    EXPECT_EQ(placed.value.at("id"), registered.value.at("id"));
    EXPECT_EQ(placed.value.at("town_id"), 3);
    EXPECT_EQ(placed.value.at("part_id"), native_id);
    EXPECT_EQ(placed.value.at("rotation_steps"), 6);
    EXPECT_EQ(placed.value.at("remaining_built"), 0);
    EXPECT_TRUE(placed.value.at("changed"));
    EXPECT_EQ(save->GetBuildPartsNum(native_id), 0);
    const TownLayout *placed_layout = PortFindTownLayout(*save->GetEditData(3));
    ASSERT_NE(placed_layout, nullptr);
    ASSERT_EQ(std::ranges::count_if(placed_layout->parts,
                                    [native_id](const TownPart &part) { return part.id == native_id && part.state != 0; }),
              1);
    ASSERT_EQ(map.GetePlacePartsAtInfoID(native_id, nullptr, 0), 1);

    const moonmod::ApiCallResult town = config.api_handler(
        "test.moonmod.georama-place", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(town.status, moonmod::ApiCallStatus::Ok) << town.message;
    const auto saved_placement = std::ranges::find(
        town.value.at("placed_parts"), placed.value.at("placement_id").get<std::string>(),
        [](const moonmod::Json &entry) { return entry.at("id").get<std::string>(); });
    ASSERT_NE(saved_placement, town.value.at("placed_parts").end());
    EXPECT_EQ(saved_placement->at("part_id"), native_id);
    EXPECT_EQ(saved_placement->at("rotation_steps"), 6);
    EXPECT_EQ(saved_placement->at("position"), placed.value.at("position"));

    const std::string placement_id = placed.value.at("placement_id").get<std::string>();
    const moonmod::ApiCallResult removed = config.api_handler(
        "test.moonmod.georama-place", *remove_piece,
        moonmod::Json::array({moonmod::Json{{"placement_id", placement_id}}}));
    ASSERT_EQ(removed.status, moonmod::ApiCallStatus::Ok) << removed.message;
    EXPECT_TRUE(removed.value.at("removed"));
    EXPECT_TRUE(removed.value.at("changed"));
    EXPECT_EQ(removed.value.at("town_id"), 3);
    EXPECT_EQ(removed.value.at("part_id"), native_id);
    ASSERT_EQ(removed.value.at("removed_parts").size(), 1u);
    EXPECT_EQ(removed.value.at("removed_parts").at(0).at("part_id"), native_id);
    EXPECT_EQ(removed.value.at("removed_parts").at(0).at("count"), 1);
    ASSERT_EQ(removed.value.at("returned_stock").size(), 1u);
    EXPECT_EQ(removed.value.at("returned_stock").at(0).at("part_id"), native_id);
    EXPECT_EQ(removed.value.at("returned_stock").at(0).at("count"), 1);
    EXPECT_EQ(save->GetBuildPartsNum(native_id), 1);
    EXPECT_EQ(map.GetePlacePartsAtInfoID(native_id, nullptr, 0), 0);

    const moonmod::ApiCallResult removed_town = config.api_handler(
        "test.moonmod.georama-place", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(removed_town.status, moonmod::ApiCallStatus::Ok) << removed_town.message;
    EXPECT_TRUE(removed_town.value.at("placed_parts").empty());
    const moonmod::ApiCallResult stale_remove = config.api_handler(
        "test.moonmod.georama-place", *remove_piece,
        moonmod::Json::array({moonmod::Json{{"placement_id", placement_id}}}));
    EXPECT_EQ(stale_remove.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(save->GetBuildPartsNum(native_id), 1);

    // Model a placed piece resting on another piece, then verify that removal
    // follows the native dependency log and refunds the entire cascade.
    save->SetBuildPartsNum(native_id, 2);
    const moonmod::ApiCallResult base_placed = config.api_handler(
        "test.moonmod.georama-place", *place_piece,
        moonmod::Json::array({moonmod::Json{{"id", registered.value.at("id")},
                                           {"rotation_steps", 0},
                                           {"position", {{"x", 20}, {"y", 0}, {"z", 30}}}}}));
    ASSERT_EQ(base_placed.status, moonmod::ApiCallStatus::Ok) << base_placed.message;
    const moonmod::ApiCallResult dependent_placed = config.api_handler(
        "test.moonmod.georama-place", *place_piece,
        moonmod::Json::array({moonmod::Json{{"id", registered.value.at("id")},
                                           {"rotation_steps", 0},
                                           {"position", {{"x", 60}, {"y", 0}, {"z", 30}}}}}));
    ASSERT_EQ(dependent_placed.status, moonmod::ApiCallStatus::Ok) << dependent_placed.message;
    int placed_slots[2]{};
    ASSERT_EQ(map.GetePlacePartsAtInfoID(native_id, placed_slots, 2), 2);
    EP_PLACE_INFO dependency{};
    dependency.num = 1;
    dependency.base[0] = placed_slots[0];
    ASSERT_TRUE(map.CreatePlaceLog(placed_slots[1], &dependency));

    const moonmod::ApiCallResult cascade_removed = config.api_handler(
        "test.moonmod.georama-place", *remove_piece,
        moonmod::Json::array({moonmod::Json{{"placement_id", base_placed.value.at("placement_id")}}}));
    ASSERT_EQ(cascade_removed.status, moonmod::ApiCallStatus::Ok) << cascade_removed.message;
    ASSERT_EQ(cascade_removed.value.at("removed_parts").size(), 1u);
    EXPECT_EQ(cascade_removed.value.at("removed_parts").at(0).at("part_id"), native_id);
    EXPECT_EQ(cascade_removed.value.at("removed_parts").at(0).at("count"), 2);
    ASSERT_EQ(cascade_removed.value.at("returned_stock").size(), 1u);
    EXPECT_EQ(cascade_removed.value.at("returned_stock").at(0).at("part_id"), native_id);
    EXPECT_EQ(cascade_removed.value.at("returned_stock").at(0).at("count"), 2);
    EXPECT_EQ(save->GetBuildPartsNum(native_id), 2);
    EXPECT_EQ(map.GetePlacePartsAtInfoID(native_id, nullptr, 0), 0);
    const moonmod::ApiCallResult cascade_town = config.api_handler(
        "test.moonmod.georama-place", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(cascade_town.status, moonmod::ApiCallStatus::Ok) << cascade_town.message;
    EXPECT_TRUE(cascade_town.value.at("placed_parts").empty());
    const moonmod::ApiCallResult stale_dependent_remove = config.api_handler(
        "test.moonmod.georama-place", *remove_piece,
        moonmod::Json::array({moonmod::Json{{"placement_id", dependent_placed.value.at("placement_id")}}}));
    EXPECT_EQ(stale_dependent_remove.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(save->GetBuildPartsNum(native_id), 2);

    moonmod::ResetChronicleTwoContentForTests();
    moonmod::RuntimeConfig reloaded_config;
    moonmod::ConfigureChronicleTwoApi(reloaded_config);
    const moonmod::ApiCallResult reloaded_town = reloaded_config.api_handler(
        "test.moonmod.georama-place", *get, moonmod::Json::array({moonmod::Json{{"town_id", 3}}}));
    ASSERT_EQ(reloaded_town.status, moonmod::ApiCallStatus::Ok) << reloaded_town.message;
    EXPECT_EQ(reloaded_town.value.at("placed_parts"), cascade_town.value.at("placed_parts"));
}

TEST(MoonModGeoramaApi, PublishesChangedWorldWritesOnceForQuestObjectives) {
    moonmod::SharedRuntime().Stop();
    CSaveData *previous_save = PortActiveSaveData;
    const int previous_loop = PortLoopNo;
    CScene *scene = GetMainScene();
    ASSERT_NE(scene, nullptr);
    const int previous_map_count = scene->map_num;
    const int previous_active_map = scene->active_map;
    CSceneMap previous_map_slot = std::move(scene->map[0]);
    struct Restore {
        CSaveData *save;
        int loop;
        CScene *scene;
        int map_count;
        int active_map;
        CSceneMap map_slot;
        ~Restore() {
            moonmod::SharedRuntime().Stop();
            PortActiveSaveData = save;
            PortLoopNo = loop;
            scene->map[0] = std::move(map_slot);
            scene->map_num = map_count;
            scene->active_map = active_map;
            GameItemDataManage.Initialize();
        }
    } restore{previous_save, previous_loop, scene, previous_map_count, previous_active_map, std::move(previous_map_slot)};

    GameItemDataManage.Initialize();
    constexpr int weapon_item_no = 0x50;
    SPI_STACK common_count{};
    common_count.type = SPI_STACK_TYPE_INT;
    common_count.value.integer = 1;
    ASSERT_EQ(moonmod_test::ItemTableInit(&common_count, 1), 1);
    SPI_STACK common_fields[11]{};
    const int common_values[] = {weapon_item_no, ITEM_DATA_MAX_MELEE, 0, 1, 1, 144, 4, 23, 300};
    for (std::size_t index = 0; index < std::size(common_values) / sizeof(common_values[0]); ++index) {
        common_fields[index].type = SPI_STACK_TYPE_INT;
        common_fields[index].value.integer = common_values[index];
    }
    common_fields[9].type = SPI_STACK_TYPE_STRING;
    common_fields[9].value.string = const_cast<char *>("EVENT_PROBE_BLADE");
    common_fields[10].type = SPI_STACK_TYPE_INT;
    common_fields[10].value.integer = ITEM_ATTRIBUTE_UNIQUE;
    ASSERT_EQ(moonmod_test::ItemTableAdd(common_fields, 11), 1);
    CDataCommon &weapon_common = GameItemDataManage.common_data[0];
    std::memset(&weapon_common, 0, sizeof(weapon_common));
    weapon_common.type = ITEM_DATA_MAX_MELEE;
    weapon_common.item_no = weapon_item_no;
    weapon_common.list_no = 0;
    weapon_common.max_num = 1;
    weapon_common.stack_num = 1;
    weapon_common.name = const_cast<char *>("Event Probe Blade");
    std::strcpy(weapon_common.file_name, "EVENT_PROBE_BLADE");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.weapon_num = 1;
    GameItemDataManage.max_item_no = weapon_item_no;
    CDataWeapon &weapon_definition = GameItemDataManage.weapon_data[0];
    weapon_definition.durability = 100;
    weapon_definition.levelup_exp = 10;
    weapon_definition.status_max[WEAPON_STAT_ATTACK] = 100;
    weapon_definition.status_max[WEAPON_STAT_DURABILITY] = 100;
    weapon_definition.fusion_point = 3;

    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    PortActiveSaveData = save.get();
    CGameDataUsed *event_weapon = save->user_data.GetUsedDataPtr(0);
    ASSERT_NE(event_weapon, nullptr);
    event_weapon->used_type = USED_ITEM_TYPE_WEAPON;
    event_weapon->item_no = weapon_item_no;
    event_weapon->item_type = ITEM_DATA_MAX_MELEE;
    event_weapon->data.weapon.level = 1;
    event_weapon->data.weapon.whp.now = 100.0f;
    event_weapon->data.weapon.whp.max = 100.0f;
    event_weapon->data.weapon.abs.now = 100.0f;
    event_weapon->data.weapon.abs.max = 100.0f;
    alignas(64) u_long128 storage[9000]{};
    mgCMemory scene_memory;
    scene_memory.stSetBuffer(storage, static_cast<int>(std::size(storage)));
    CEditGrid river_grid;
    river_grid.Initialize();
    river_grid.Create(2, 2, &scene_memory);
    river_grid.step_x = 80.0f;
    river_grid.step_z = 80.0f;
    river_grid.origin[0] = 100.0f;
    river_grid.origin[1] = 0.0f;
    river_grid.origin[2] = 200.0f;
    river_grid.origin[3] = 1.0f;
    CEditMap map;
    map.area_no = 3;
    map.CreateTable(&scene_memory, 8, 4096);
    map.grid[0] = &river_grid;
    map.grid_max = EDIT_MAP_GRID_MAX;
    scene->map_num = std::max(scene->map_num, 1);
    ASSERT_EQ(scene->AssignMap(0, &map, const_cast<char *>("MoonFactoryGeoramaEvents")), 0);
    scene->active_map = 0;
    PortLoopNo = LOOP_EDIT;
    CEditData *town = save->GetEditData(3);
    ASSERT_NE(town, nullptr);
    town->analyze.condition[5] = 0;
    EditAnalyzeSrc *analysis = town->GetAnalyzeSrc(3);
    ASSERT_NE(analysis, nullptr);
    struct RestoreAnalysis {
        EditAnalyzeSrc *target;
        EditAnalyzeSrc saved;
        explicit RestoreAnalysis(EditAnalyzeSrc *source) : target(source), saved(*source) {}
        ~RestoreAnalysis() { *target = saved; }
    } restore_analysis(analysis);
    analysis->Init();
    analysis->data[0].message = const_cast<char *>("MoonFactory event request");
    analysis->data[0].percent = 20;
    analysis->data[0].con_no[0] = 6;
    analysis->data[0].con_no[1] = -1;
    town->analyze.condition[6] = 0;

    TempRoot root;
    const fs::path package = root.path / "mods" / "test.moonmod.georama-event";
    Write(package / "mod.json", R"json({
      "schema_version":1,"id":"test.moonmod.georama-event","name":"Georama Event Probe","version":"1.0.0",
      "framework_api":1,"games":["chronicle-two"],"entry":"main.lua",
      "permissions":["content.register","game.read","game.write"]
    })json");
    Write(package / "main.lua", R"lua(
assert(game.quests.register({
  id = "condition", title = "Meet a town condition",
  objectives = {{id = "condition", title = "Meet condition", target = 1,
    event = "game.georama.condition_changed", match = {town_id = 3, condition_id = 5, met = true}}}
}))
assert(game.quests.register({
  id = "request-known", title = "Discover a town request",
  objectives = {{id = "request", title = "Discover request", target = 1,
    event = "game.georama.request_known_changed", match = {town_id = 3, request_id = 8, known = true}}}
}))
assert(game.quests.register({
  id = "request-completion", title = "Complete a town request",
  objectives = {{id = "request", title = "Complete request", target = 1,
    event = "game.georama.request_changed", match = {town_id = 3, request_id = 0, completed = true}}}
}))
assert(game.quests.register({
  id = "story-flag", title = "Set a story flag",
  objectives = {{id = "flag", title = "Set flag", target = 1,
    event = "game.story_flag_changed", match = {id = 17, value = true}}}
}))
assert(game.quests.register({
  id = "story-value", title = "Set a story value",
  objectives = {{id = "value", title = "Set value", target = 1,
    event = "game.story_value_changed", match = {id = 17, value = 900}}}
}))
assert(game.quests.register({
  id = "map-flag", title = "Set a map flag",
  objectives = {{id = "flag", title = "Set map flag", target = 1,
    event = "game.map_flag_changed", match = {map_id = 2, flag_id = 7, value = true}}}
}))
assert(game.quests.register({
  id = "river-cell", title = "Change a river cell",
  objectives = {{id = "river", title = "Add river", target = 1,
    event = "game.georama.river_changed", match = {town_id = 3, grid_index = 0, x = 1, z = 0, has_river = true}}}
}))
assert(game.quests.register({
  id = "weapon-upgrade", title = "Upgrade a weapon",
  objectives = {{id = "upgrade", title = "Raise a weapon level", target = 1,
    event = "game.weapon.upgraded", match = {native_item_no = 80, previous_level = 1, level = 2}}}
}))
local applied = false
moonmod.on("game.georama.condition_changed", function(event)
  assert(event.town_id == 3 and event.condition_id == 5 and event.met == true)
  assert(event.changed == true and event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "GEORAMA_EVENT:" .. tostring(event.condition_id))
end)
moonmod.on("game.georama.request_known_changed", function(event)
  assert(event.town_id == 3 and event.request_id == 8 and event.known == true)
  assert(event.changed == true and event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "GEORAMA_REQUEST_KNOWN_EVENT:" .. tostring(event.request_id))
end)
moonmod.on("game.georama.request_changed", function(event)
  assert(event.town_id == 3 and event.request_id == 0 and event.completed == true)
  assert(event.changed == true and event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "GEORAMA_REQUEST_EVENT:" .. tostring(event.request_id))
end)
moonmod.on("game.story_flag_changed", function(event)
  assert(event.id == 17 and event.value == true and event.changed == true)
  assert(event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "WORLD_EVENT:story-flag")
end)
moonmod.on("game.story_value_changed", function(event)
  assert(event.id == 17 and event.value == 900 and event.changed == true)
  assert(event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "WORLD_EVENT:story-value")
end)
moonmod.on("game.map_flag_changed", function(event)
  assert(event.map_id == 2 and event.flag_id == 7 and event.value == true and event.changed == true)
  assert(event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "WORLD_EVENT:map-flag")
end)
moonmod.on("game.georama.river_changed", function(event)
  assert(event.town_id == 3 and event.grid_index == 0 and event.x == 1 and event.z == 0)
  assert(event.has_river == true and event.changed == true)
  assert(event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "GEORAMA_RIVER_EVENT:" .. tostring(event.x) .. ":" .. tostring(event.z))
end)
moonmod.on("game.weapon.upgraded", function(event)
  assert(event.native_item_no == 80 and event.previous_level == 1 and event.level == 2)
  assert(event.upgraded == true and event.changed == true)
  assert(event.source_mod == "test.moonmod.georama-event")
  moonmod.log("info", "WEAPON_UPGRADE_EVENT:" .. tostring(event.level))
end)
moonmod.on("game.tick", function()
  if applied then return end
  assert(game.quests.start({id = "condition"}))
  assert(game.quests.start({id = "request-known"}))
  assert(game.quests.start({id = "request-completion"}))
  assert(game.quests.start({id = "story-flag"}))
  assert(game.quests.start({id = "story-value"}))
  assert(game.quests.start({id = "map-flag"}))
  assert(game.quests.start({id = "river-cell"}))
  assert(game.quests.start({id = "weapon-upgrade"}))
  assert(assert(game.georama.condition_set({town_id = 3, condition_id = 5, met = true})).changed)
  assert(not assert(game.georama.condition_set({town_id = 3, condition_id = 5, met = true})).changed)
  assert(assert(game.georama.request_known_set({town_id = 3, request_id = 8, known = true})).changed)
  assert(not assert(game.georama.request_known_set({town_id = 3, request_id = 8, known = true})).changed)
  assert(assert(game.georama.request_set({town_id = 3, request_id = 0, completed = true})).changed)
  assert(not assert(game.georama.request_set({town_id = 3, request_id = 0, completed = true})).changed)
  assert(assert(game.world.story_flag_set({id = 17, value = true})).changed)
  assert(not assert(game.world.story_flag_set({id = 17, value = true})).changed)
  assert(assert(game.world.story_value_set({id = 17, value = 900})).changed)
  assert(not assert(game.world.story_value_set({id = 17, value = 900})).changed)
  assert(assert(game.world.map_flag_set({map_id = 2, flag_id = 7, value = true})).changed)
  assert(not assert(game.world.map_flag_set({map_id = 2, flag_id = 7, value = true})).changed)
  assert(assert(game.georama.river_set({grid_index = 0, x = 1, z = 0, has_river = true})).changed)
  assert(not assert(game.georama.river_set({grid_index = 0, x = 1, z = 0, has_river = true})).changed)
  assert(game.georama.get({town_id = 3}).river_grids[1].cells[2][1] == true)
  local upgrade, upgrade_error = game.weapons.upgrade({id = "chronicle-two:weapon:80"})
  if not upgrade then
    moonmod.log("error", "WEAPON_UPGRADE_FAILURE:" .. tostring(upgrade_error and upgrade_error.message))
    error("weapon upgrade API returned no result")
  end
  assert(upgrade.upgraded and upgrade.level == 2 and upgrade.changed)
  assert(not assert(game.weapons.upgrade({id = "chronicle-two:weapon:80"})).changed)
  applied = true
end)
)lua");
    Write(root.path / "save" / "moonmod" / "profile.json",
          R"json({"format_version":1,"enabled":{"test.moonmod.georama-event":true},"settings":{}})json");

    std::vector<std::string> logs;
    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    moonmod::ConfigureChronicleTwoApi(config);
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    config.world_location_reader = [] {
        return moonmod::ApiCallResult{moonmod::ApiCallStatus::Failed, nullptr, "no active world in event probe"};
    };
    config.log = [&](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(moonmod::SharedRuntime().Start(std::move(config), error)) << error;
    ASSERT_EQ(moonmod::SharedRuntime().Mods().size(), 1u);
    ASSERT_EQ(moonmod::SharedRuntime().Mods()[0].state, moonmod::ModState::Loaded);

    moonmod::SharedRuntime().Pump();
    EXPECT_TRUE(std::ranges::none_of(logs, [](const std::string &message) {
        return message.find("GEORAMA_EVENT:") != std::string::npos ||
               message.find("GEORAMA_RIVER_EVENT:") != std::string::npos ||
               message.find("GEORAMA_REQUEST_EVENT:") != std::string::npos ||
               message.find("WEAPON_UPGRADE_EVENT:") != std::string::npos ||
               message.find("WORLD_EVENT:") != std::string::npos;
    }));
    moonmod::SharedRuntime().Pump();
    const auto event_count = std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("GEORAMA_EVENT:5") != std::string::npos;
    });
    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("GEORAMA_REQUEST_KNOWN_EVENT:8") != std::string::npos;
    }), 1);
    EXPECT_EQ(std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("GEORAMA_REQUEST_EVENT:0") != std::string::npos;
    }), 1);
    EXPECT_EQ(std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("GEORAMA_RIVER_EVENT:1:0") != std::string::npos;
    }), 1);
    std::string probe_logs;
    for (const std::string &message : logs) probe_logs += message + "\n";
    EXPECT_EQ(std::ranges::count_if(logs, [](const std::string &message) {
        return message.find("WEAPON_UPGRADE_EVENT:2") != std::string::npos;
    }), 1) << probe_logs;
    for (const std::string_view event_name : {"WORLD_EVENT:story-flag", "WORLD_EVENT:story-value", "WORLD_EVENT:map-flag"}) {
        EXPECT_EQ(std::ranges::count_if(logs, [event_name](const std::string &message) {
            return message.find(event_name) != std::string::npos;
        }), 1);
    }
    const moonmod::ApiCallResult quests = moonmod::SharedRuntime().Quests("test.moonmod.georama-event");
    ASSERT_EQ(quests.status, moonmod::ApiCallStatus::Ok) << quests.message;
    ASSERT_EQ(quests.value.at("quests").size(), 8u);
    EXPECT_TRUE(std::ranges::all_of(quests.value.at("quests"), [](const moonmod::Json &quest) {
        return quest.at("status") == "completed" && quest.at("objectives").at(0).at("progress") == 1;
    }));
}
