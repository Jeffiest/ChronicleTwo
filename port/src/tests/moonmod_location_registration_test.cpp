#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dataread.hpp"
#include "assets/data_registry.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "mapselect.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "platform/paths.hpp"
#include "scene.hpp"
#include "texture_source.hpp"

namespace fs = std::filesystem;

TEST(MoonModLocationRegistration, RegistersOwnedMapAssetsAndQueuesNativeEntryTransition) {
    const fs::path root = fs::temp_directory_path() /
                          ("moonmod_location_registration_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path package = root / "mods" / "test.location";
    ASSERT_TRUE(fs::create_directories(root / "map"));
    ASSERT_TRUE(fs::create_directories(package / "resources"));
    {
        std::ofstream catalog(root / "map" / "map2.cfg.json", std::ios::binary);
        ASSERT_TRUE(catalog.good());
        catalog << R"({"schema_version":2,"kind":"map_names","count":1,"maps":[)"
                << R"({"name":"native_probe","title":"Native Probe","add_path":"","type":4,"sel_type":7,"snd_data_id":-1,"area_no":-1}]})";
    }
    const std::vector<unsigned char> source_mpk = {'n', 'a', 't', 'i', 'v', 'e', '-', 'm', 'p', 'k'};
    const std::vector<unsigned char> source_ipk = {'n', 'a', 't', 'i', 'v', 'e', '-', 'i', 'p', 'k'};
    fs::create_directories(root / "map" / "n" / "nat" / "ive");
    {
        std::ofstream mpk(root / "map" / "n" / "nat" / "ive" / "native_probe.mpk", std::ios::binary);
        std::ofstream ipk(root / "map" / "n" / "nat" / "ive" / "native_probe.ipk", std::ios::binary);
        ASSERT_TRUE(mpk.good());
        ASSERT_TRUE(ipk.good());
        mpk.write(reinterpret_cast<const char *>(source_mpk.data()), source_mpk.size());
        ipk.write(reinterpret_cast<const char *>(source_ipk.data()), source_ipk.size());
    }

    moonmod::ResetChronicleTwoContentForTests();
    moonmod::ResetNativeResourceOverridesForTests();
    const auto cleanup = [&] {
        moonmod::ResetChronicleTwoContentForTests();
        moonmod::ResetNativeResourceOverridesForTests();
        PathsReset();
        std::error_code error;
        fs::remove_all(root, error);
    };
    struct Cleanup {
        decltype(cleanup) &fn;
        ~Cleanup() { fn(); }
    } cleanup_guard{cleanup};

    DataReadIndex(root);
    alignas(64) u_long128 load_buffer[256]{};
    LoadMapName(LANG_FRENCH, load_buffer);
    ASSERT_EQ(GetMapNameCount(), 1);
    EXPECT_STREQ(GetMapName(0, nullptr), "native_probe");

    const std::string map_base = dc2::world::MapPath("moonprobe");
    const std::vector<std::string> extensions = {".map", ".cfg"};
    const std::vector<unsigned char> content = {'m', 'o', 'o', 'n'};
    for (const std::string &extension : extensions) {
        const std::string resource_name = "resources/map" + extension;
        {
            std::ofstream resource(package / resource_name, std::ios::binary);
            ASSERT_TRUE(resource.good());
            resource.write(reinterpret_cast<const char *>(content.data()), content.size());
        }
        const auto registered = moonmod::RegisterNativeResourceOverride(
            "test.location", package,
            moonmod::Json::array({{{"path", resource_name}, {"game_path", std::string(map_base) + extension}}}));
        ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    }

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("locations.register");
    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("locations.list");
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("locations.get");
    const moonmod::ApiOperationSpec *transition_operation = moonmod::FindApiOperation("world.transition");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(get_operation, nullptr);
    ASSERT_NE(transition_operation, nullptr);
    const auto capability = std::ranges::find(config.api_capabilities, "locations.register",
                                               &moonmod::ApiCapability::path);
    ASSERT_NE(capability, config.api_capabilities.end());
    EXPECT_TRUE(capability->supported);
    EXPECT_TRUE(capability->reason.empty());

    const moonmod::Json registration = moonmod::Json::array({{{"id", "moon-cave"},
                                                               {"map_name", "moonprobe"},
                                                               {"name", "Moon Cave"},
                                                               {"texture_source_map", "native_probe"}}});
    const moonmod::ApiCallResult created = config.api_handler("test.location", *register_operation, registration);
    ASSERT_EQ(created.status, moonmod::ApiCallStatus::Ok) << created.message;
    EXPECT_EQ(created.value.value("id", ""), "chronicle-two:location:test.location:moon-cave");
    EXPECT_EQ(created.value.value("native_map_id", -1), 1);
    EXPECT_EQ(created.value.value("native_type", -1), MAP_TYPE_INTERIOR);
    EXPECT_EQ(created.value.value("status", ""), "registered");
    EXPECT_EQ(created.value.value("texture_source_map", ""), "native_probe");
    EXPECT_EQ(created.value.at("resources").size(), 2);
    for (const auto &[extension, source_bytes] :
         std::array<std::pair<std::string_view, const std::vector<unsigned char> *>, 2>{
             std::pair<std::string_view, const std::vector<unsigned char> *>{".mpk", &source_mpk},
             {".ipk", &source_ipk}}) {
        std::string game_path = std::string(map_base) + std::string(extension);
        std::array<u_long128, 256> loaded_pack{};
        int loaded_size = 0;
        ASSERT_TRUE(LoadFile2(game_path.data(), std::span(reinterpret_cast<u8 *>(loaded_pack.data()), sizeof(loaded_pack)),
                              &loaded_size, LOAD_FILE_READ))
            << game_path;
        ASSERT_EQ(loaded_size, static_cast<int>(source_bytes->size())) << game_path;
        EXPECT_EQ(std::memcmp(loaded_pack.data(), source_bytes->data(), source_bytes->size()), 0) << game_path;
        EXPECT_EQ(TextureSourceLookup(loaded_pack.data()), std::string("map/n/nat/ive/native_probe") + extension);
    }

    ASSERT_EQ(GetMapNameCount(), 2);
    EXPECT_STREQ(GetMapName(0, nullptr), "native_probe");
    EXPECT_STREQ(GetMapName(1, nullptr), "moonprobe");
    EXPECT_EQ(GetMapType(1), MAP_TYPE_INTERIOR);
    char mutable_map_name[16] = "moonprobe";
    EXPECT_EQ(SearchMapNo(mutable_map_name), 1);

    const moonmod::ApiCallResult locations = config.api_handler(
        "test.location", *list_operation, moonmod::Json::array());
    ASSERT_EQ(locations.status, moonmod::ApiCallStatus::Ok) << locations.message;
    ASSERT_EQ(locations.value.size(), 2);
    EXPECT_EQ(locations.value[1].value("id", ""), "chronicle-two:location:test.location:moon-cave");
    EXPECT_EQ(locations.value[1].value("source", ""), "mod_package");
    EXPECT_EQ(locations.value[1].value("owner", ""), "test.location");
    EXPECT_EQ(locations.value[1].value("texture_source_map", ""), "native_probe");

    const moonmod::ApiCallResult queried = config.api_handler(
        "test.location", *get_operation,
        moonmod::Json::array({{{"id", "chronicle-two:location:test.location:moon-cave"}}}));
    ASSERT_EQ(queried.status, moonmod::ApiCallStatus::Ok) << queried.message;
    EXPECT_EQ(queried.value.value("native_map_id", -1), 1);
    EXPECT_EQ(queried.value.value("map_name", ""), "moonprobe");

    struct SavedLoopState {
        int loop = PortLoopNo;
        int next_loop = PortNextLoopNo;
        INIT_LOOP_ARG next_arg = NextInitArg;
        INIT_LOOP_ARG current_arg = InitArg;
        CScene *scene = GetMainScene();
        int exit_flag = scene == nullptr ? 0 : scene->exit_flag;
        ~SavedLoopState() {
            PortLoopNo = loop;
            PortNextLoopNo = next_loop;
            NextInitArg = next_arg;
            InitArg = current_arg;
            if (scene != nullptr) scene->exit_flag = exit_flag;
        }
    } saved_state;

    PortLoopNo = LOOP_TITLE;
    const moonmod::ApiCallResult inactive = config.api_handler(
        "test.location", *transition_operation,
        moonmod::Json::array({{{"id", "chronicle-two:location:test.location:moon-cave"}}}));
    EXPECT_EQ(inactive.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(inactive.message.find("active town or dungeon"), std::string::npos);

    PortLoopNo = LOOP_DUNGEON;
    const moonmod::ApiCallResult queued = config.api_handler(
        "test.location", *transition_operation,
        moonmod::Json::array({{{"id", "chronicle-two:location:test.location:moon-cave"}}}));
    ASSERT_EQ(queued.status, moonmod::ApiCallStatus::Ok) << queued.message;
    EXPECT_EQ(queued.value.value("context", ""), "mod_location");
    EXPECT_TRUE(queued.value.value("queued", false));
    EXPECT_EQ(queued.value.value("native_map_id", -1), 1);
    EXPECT_EQ(PortNextLoopNo, LOOP_EDIT);
    EXPECT_EQ(NextInitArg.map_no, 1);
    EXPECT_EQ(NextInitArg.floor_no, -1);
    EXPECT_EQ(NextInitArg.event_no, 100);
    ASSERT_NE(saved_state.scene, nullptr);
    EXPECT_TRUE(saved_state.scene->exit_flag);

    PortLoopNo = LOOP_EDIT;
    InitArg.map_no = 1;
    const moonmod::ApiCallResult current = config.api_handler(
        "test.location", *transition_operation,
        moonmod::Json::array({{{"id", "chronicle-two:location:test.location:moon-cave"}}}));
    ASSERT_EQ(current.status, moonmod::ApiCallStatus::Ok) << current.message;
    EXPECT_FALSE(current.value.value("queued", true));
    EXPECT_FALSE(current.value.value("changed", true));

    moonmod::RemoveNativeResourceOverridesByOwner("test.location");
    for (const std::string &extension : extensions) {
        const std::string game_path = std::string(map_base) + extension;
        EXPECT_TRUE(moonmod::NativeResourceOverrideOwnedBy("test.location", game_path));
    }
}
