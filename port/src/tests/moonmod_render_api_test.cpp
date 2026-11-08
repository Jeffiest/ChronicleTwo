#include <gtest/gtest.h>

#include <cmath>
#include <ranges>

#include "cameracontrol.hpp"
#include "dng_main.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "scene.hpp"

TEST(MoonModRenderApi, ReadsActiveDungeonCameraPoseAndRejectsUnavailableContext) {
    CScene *const previous_scene = DngMainScene;
    const int previous_loop = PortLoopNo;
    CScene scene;
    CCameraControl camera;
    struct Restore {
        CScene *scene;
        int loop;
        ~Restore() {
            DngMainScene = scene;
            PortLoopNo = loop;
        }
    } restore{previous_scene, previous_loop};

    camera.pos[0] = 11.0f;
    camera.pos[1] = 22.0f;
    camera.pos[2] = 33.0f;
    camera.ref[0] = -4.0f;
    camera.ref[1] = 5.0f;
    camera.ref[2] = 6.0f;
    camera.angle_h = 0.25f;
    camera.angle_v = -0.5f;
    camera.roll = 0.125f;
    char camera_name[] = "moonfactory-test-camera";
    ASSERT_EQ(scene.camera[0].AssignData(&camera, camera_name), 1);
    scene.camera_num = 1;
    scene.active_camera = 0;
    DngMainScene = &scene;
    PortLoopNo = LOOP_DUNGEON;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("render.camera_get");
    ASSERT_NE(operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "render.camera_get" && capability.supported;
    }));

    const moonmod::ApiCallResult result = config.api_handler(
        "test.moonmod.render-probe", *operation, moonmod::Json::array());
    ASSERT_EQ(result.status, moonmod::ApiCallStatus::Ok) << result.message;
    EXPECT_EQ(result.value.value("scene", ""), "dungeon");
    EXPECT_NEAR(result.value.at("eye").at("x").get<float>(), 11.0f, 0.00001f);
    EXPECT_NEAR(result.value.at("eye").at("y").get<float>(), 22.0f, 0.00001f);
    EXPECT_NEAR(result.value.at("eye").at("z").get<float>(), 33.0f, 0.00001f);
    EXPECT_NEAR(result.value.at("target").at("x").get<float>(), -4.0f, 0.00001f);
    EXPECT_NEAR(result.value.at("heading").get<float>(), 0.25f, 0.00001f);
    EXPECT_NEAR(result.value.at("pitch").get<float>(), -0.5f, 0.00001f);
    EXPECT_NEAR(result.value.at("roll").get<float>(), 0.125f, 0.00001f);

    const moonmod::ApiOperationSpec *set_operation = moonmod::FindApiOperation("render.camera_set");
    ASSERT_NE(set_operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "render.camera_set" && capability.supported;
    }));
    const moonmod::ApiCallResult updated = config.api_handler(
        "test.moonmod.render-probe", *set_operation,
        moonmod::Json::array({moonmod::Json{{"distance", 85.0}, {"height", 25.0}, {"angle", 2.25}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok) << updated.message;
    EXPECT_NEAR(camera.distance, 85.0f, 0.00001f);
    EXPECT_NEAR(camera.height, 25.0f, 0.00001f);
    EXPECT_NEAR(camera.next_angle, 2.25f, 0.00001f);
    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.render-probe", *set_operation,
        moonmod::Json::array({moonmod::Json{{"distance", 500.0}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_NEAR(camera.distance, 85.0f, 0.00001f);

    camera.next_ref[0] = 1.0f;
    camera.next_ref[1] = 2.0f;
    camera.next_ref[2] = 3.0f;
    camera.next_pos[0] = 1.0f;
    camera.next_pos[1] = 32.0f;
    camera.next_pos[2] = 43.0f;
    camera.control_on = 1;
    camera.follow_on = 0;
    const moonmod::ApiCallResult controlled_update = config.api_handler(
        "test.moonmod.render-probe", *set_operation,
        moonmod::Json::array({moonmod::Json{{"distance", 125.0}, {"height", 25.0}, {"angle", 0.5}}}));
    ASSERT_EQ(controlled_update.status, moonmod::ApiCallStatus::Ok) << controlled_update.message;
    EXPECT_NEAR(std::hypot(camera.next_pos[0] - camera.next_ref[0],
                           camera.next_pos[2] - camera.next_ref[2]), 125.0f, 0.0001f);
    EXPECT_NEAR(camera.next_pos[1] - camera.next_ref[1], 25.0f, 0.00001f);
    EXPECT_NEAR(camera.GetActiveParam()->height, 25.0f, 0.00001f);
    EXPECT_NEAR(controlled_update.value.at("angle").get<float>(), 0.5f, 0.00001f);
    camera.control_on = 0;

    PortLoopNo = LOOP_TITLE;
    const moonmod::ApiCallResult unavailable = config.api_handler(
        "test.moonmod.render-probe", *operation, moonmod::Json::array());
    EXPECT_EQ(unavailable.status, moonmod::ApiCallStatus::Failed);
}
