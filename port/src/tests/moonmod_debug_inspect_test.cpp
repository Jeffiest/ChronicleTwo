#include <gtest/gtest.h>

#include <ranges>

#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"

TEST(MoonModDebugInspect, ReturnsBoundedNativeSceneSnapshot) {
    const int previous_loop = PortLoopNo;
    const INIT_LOOP_ARG previous_init = InitArg;
    struct RestoreState {
        int loop;
        INIT_LOOP_ARG init;
        ~RestoreState() {
            PortLoopNo = loop;
            InitArg = init;
        }
    } restore{previous_loop, previous_init};

    PortLoopNo = LOOP_DUNGEON;
    InitArg.map_no = 4;
    InitArg.floor_no = 7;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("debug.inspect");
    ASSERT_NE(operation, nullptr);
    const auto capability = std::ranges::find(config.api_capabilities, "debug.inspect",
                                               &moonmod::ApiCapability::path);
    ASSERT_NE(capability, config.api_capabilities.end());
    EXPECT_TRUE(capability->supported);
    EXPECT_TRUE(capability->reason.empty());

    const moonmod::ApiCallResult snapshot = config.api_handler("test.moonmod.debug", *operation,
                                                                moonmod::Json::array());
    ASSERT_EQ(snapshot.status, moonmod::ApiCallStatus::Ok) << snapshot.message;
    EXPECT_EQ(snapshot.value.at("scene_id"), "dungeon");
    EXPECT_EQ(snapshot.value.at("native_mode"), LOOP_DUNGEON);
    EXPECT_EQ(snapshot.value.at("native_map_id"), 4);
    EXPECT_EQ(snapshot.value.at("native_floor_id"), 7);

    PortLoopNo = LOOP_TITLE;
    InitArg.map_no = -1;
    const moonmod::ApiCallResult title = config.api_handler("test.moonmod.debug", *operation,
                                                             moonmod::Json::array());
    ASSERT_EQ(title.status, moonmod::ApiCallStatus::Ok) << title.message;
    EXPECT_EQ(title.value.at("scene_id"), "title");
    EXPECT_TRUE(title.value.at("native_map_id").is_null());
    EXPECT_TRUE(title.value.at("native_floor_id").is_null());

    const moonmod::ApiCallResult malformed = config.api_handler(
        "test.moonmod.debug", *operation, moonmod::Json::array({{{"target", "memory"}}}));
    EXPECT_EQ(malformed.status, moonmod::ApiCallStatus::InvalidArgument);
}

TEST(MoonModDebugInspect, RegistersAndUnloadsOwnedOverlay) {
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("debug.overlay_register");
    ASSERT_NE(operation, nullptr);
    const auto capability = std::ranges::find(config.api_capabilities, "debug.overlay_register",
                                               &moonmod::ApiCapability::path);
    ASSERT_NE(capability, config.api_capabilities.end());
    EXPECT_TRUE(capability->supported);
    EXPECT_TRUE(capability->reason.empty());

    constexpr std::string_view owner = "test.moonmod.debug-overlay";
    struct Cleanup {
        moonmod::RuntimeConfig &config;
        ~Cleanup() { if (config.api_owner_unloaded) config.api_owner_unloaded("test.moonmod.debug-overlay"); }
    } cleanup{config};

    const moonmod::ApiCallResult registered = config.api_handler(
        owner, *operation, moonmod::Json::array({{{"id", "status"}, {"text", "PROBE READY"}}}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("id"), "test.moonmod.debug-overlay:status");
    EXPECT_EQ(moonmod::ChronicleTwoDebugOverlayText(), "PROBE READY");

    const moonmod::ApiCallResult updated = config.api_handler(
        owner, *operation, moonmod::Json::array({{{"id", "status"}, {"text", "UPDATED"}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok) << updated.message;
    EXPECT_EQ(moonmod::ChronicleTwoDebugOverlayText(), "UPDATED");

    const moonmod::ApiCallResult malformed = config.api_handler(
        owner, *operation, moonmod::Json::array({{{"id", "status"}, {"text", "BAD\nTEXT"}}}));
    EXPECT_EQ(malformed.status, moonmod::ApiCallStatus::InvalidArgument);
}

TEST(MoonModRenderApi, RegistersUpdatesAndRemovesOwnedHudTextLayer) {
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("render.layer_register");
    const moonmod::ApiOperationSpec *remove_operation = moonmod::FindApiOperation("render.layer_remove");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(remove_operation, nullptr);
    for (const std::string_view path : {"render.layer_register", "render.layer_remove"}) {
        const auto capability = std::ranges::find(config.api_capabilities, path, &moonmod::ApiCapability::path);
        ASSERT_NE(capability, config.api_capabilities.end());
        EXPECT_TRUE(capability->supported) << path;
        EXPECT_TRUE(capability->reason.empty()) << path;
    }

    constexpr std::string_view owner = "test.moonmod.hud-layer";
    struct Cleanup {
        moonmod::RuntimeConfig &config;
        ~Cleanup() { if (config.api_owner_unloaded) config.api_owner_unloaded("test.moonmod.hud-layer"); }
    } cleanup{config};

    const moonmod::ApiCallResult registered = config.api_handler(
        owner, *register_operation, moonmod::Json::array({{{"id", "health"}, {"text", "HP 24/30"}}}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("id"), "test.moonmod.hud-layer:health");
    EXPECT_EQ(moonmod::ChronicleTwoDebugOverlayText(), "HP 24/30");

    const moonmod::ApiCallResult updated = config.api_handler(
        owner, *register_operation, moonmod::Json::array({{{"id", "health"}, {"text", "HP 25/30"}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok) << updated.message;
    EXPECT_EQ(moonmod::ChronicleTwoDebugOverlayText(), "HP 25/30");

    const moonmod::ApiCallResult wrong_owner = config.api_handler(
        "test.moonmod.other", *remove_operation, moonmod::Json::array({{{"id", "health"}}}));
    EXPECT_EQ(wrong_owner.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(moonmod::ChronicleTwoDebugOverlayText(), "HP 25/30");

    const moonmod::ApiCallResult malformed = config.api_handler(
        owner, *register_operation, moonmod::Json::array({{{"id", "health"}, {"text", "BAD\nTEXT"}}}));
    EXPECT_EQ(malformed.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult removed = config.api_handler(
        owner, *remove_operation, moonmod::Json::array({{{"id", "health"}}}));
    ASSERT_EQ(removed.status, moonmod::ApiCallStatus::Ok) << removed.message;
    EXPECT_TRUE(removed.value.at("removed").get<bool>());
    EXPECT_TRUE(moonmod::ChronicleTwoDebugOverlayText().empty());
}
