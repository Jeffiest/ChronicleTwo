#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#include "dng_main.hpp"
#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "monster.hpp"
#include "savedata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModModelRegistration, OwnedCustomAssetsBindToANativeCharacterDefinition) {
    moonmod::ResetChronicleTwoContentForTests();
    moonmod::ResetNativeResourceOverridesForTests();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path package = std::filesystem::temp_directory_path() /
                                          ("moonmod-model-registration-" + std::to_string(unique_suffix));
    std::filesystem::create_directories(package / "resources");
    std::ofstream(package / "resources" / "model.chr", std::ios::binary) << "model-bytes";
    std::ofstream(package / "resources" / "script.stb", std::ios::binary) << "script-bytes";
    struct Cleanup {
        std::filesystem::path package;
        ~Cleanup() {
            moonmod::ResetNativeResourceOverridesForTests();
            moonmod::ResetChronicleTwoContentForTests();
            std::error_code error;
            std::filesystem::remove_all(package, error);
        }
    } cleanup{package};

    constexpr std::string_view owner = "test.moonmod.custom-model";
    ASSERT_EQ(moonmod::RegisterNativeResourceOverride(
                  owner, package,
                  moonmod::Json::array({{{"path", "resources/model.chr"},
                                         {"game_path", "dungeon/monster/moon_sentinel.chr"}}})).status,
              moonmod::ApiCallStatus::Ok);
    ASSERT_EQ(moonmod::RegisterNativeResourceOverride(
                  owner, package,
                  moonmod::Json::array({{{"path", "resources/script.stb"},
                                         {"game_path", "dungeon/monster/moon_brain.stb"}}})).status,
              moonmod::ApiCallStatus::Ok);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const auto *model_register = moonmod::FindApiOperation("models.register");
    const auto *model_unregister = moonmod::FindApiOperation("models.unregister");
    const auto *model_replace = moonmod::FindApiOperation("models.replace");
    const auto *model_remove = moonmod::FindApiOperation("models.remove");
    const auto *model_resolve = moonmod::FindApiOperation("models.resolve");
    const auto *character_register = moonmod::FindApiOperation("characters.register");
    ASSERT_NE(model_register, nullptr);
    ASSERT_NE(model_unregister, nullptr);
    ASSERT_NE(model_replace, nullptr);
    ASSERT_NE(model_remove, nullptr);
    ASSERT_NE(model_resolve, nullptr);
    ASSERT_NE(character_register, nullptr);
    const auto model_capability = std::ranges::find(config.api_capabilities, "models.register",
                                                    &moonmod::ApiCapability::path);
    ASSERT_NE(model_capability, config.api_capabilities.end());
    EXPECT_TRUE(model_capability->supported);
    for (const char *path : {"models.replace", "models.remove"}) {
        const auto capability = std::ranges::find(config.api_capabilities, path, &moonmod::ApiCapability::path);
        ASSERT_NE(capability, config.api_capabilities.end());
        EXPECT_TRUE(capability->supported) << capability->reason;
    }

    const moonmod::Json model_definition = {{"id", "moon-sentinel-model"}, {"template_monster_id", 0},
                                             {"model", "moon_sentinel"}, {"script", "moon_brain"}};
    const moonmod::ApiCallResult model = config.api_handler(owner, *model_register,
                                                            moonmod::Json::array({model_definition}));
    ASSERT_EQ(model.status, moonmod::ApiCallStatus::Ok) << model.message;
    EXPECT_EQ(model.value.value("id", ""), "test.moonmod.custom-model:moon-sentinel-model");
    EXPECT_EQ(model.value.value("model", ""), "moon_sentinel");
    EXPECT_EQ(model.value.value("script", ""), "moon_brain");
    EXPECT_TRUE(model.value.at("native_monster_id").is_null());
    ASSERT_EQ(model.value.at("assets").size(), 2u);
    EXPECT_TRUE(model.value.at("assets")[0].value("overridden", false));
    EXPECT_TRUE(model.value.at("assets")[1].value("overridden", false));

    BASE_MONSTER_TBL *native_template = GetMonsterTable(0);
    ASSERT_NE(native_template, nullptr);
    const std::string original_native_model = native_template->model;
    const moonmod::ApiCallResult replaced_native_model = config.api_handler(
        owner, *model_replace, moonmod::Json::array({{{"monster_id", 0}, {"model", "moon_sentinel"}}}));
    ASSERT_EQ(replaced_native_model.status, moonmod::ApiCallStatus::Ok) << replaced_native_model.message;
    EXPECT_EQ(replaced_native_model.value.value("previous_model", ""), original_native_model);
    EXPECT_EQ(replaced_native_model.value.value("game_path", ""), "dungeon/monster/moon_sentinel.chr");
    EXPECT_STREQ(native_template->model, "moon_sentinel");
    const moonmod::ApiCallResult native_model_resolve = config.api_handler(
        owner, *model_resolve, moonmod::Json::array({{{"monster_id", 0}}}));
    ASSERT_EQ(native_model_resolve.status, moonmod::ApiCallStatus::Ok) << native_model_resolve.message;
    EXPECT_EQ(native_model_resolve.value.value("model", ""), "moon_sentinel");
    ASSERT_TRUE(static_cast<bool>(config.api_resource_override_removal_guard));
    const moonmod::Json model_resource_path = moonmod::Json::array({{{"game_path", "DUNGEON/MONSTER/MOON_SENTINEL.CHR"}}});
    EXPECT_NE(config.api_resource_override_removal_guard(owner, model_resource_path).find("registered model"),
              std::string::npos);
    const moonmod::Json custom_script_path = moonmod::Json::array({{{"game_path", "dungeon/monster/moon_brain.stb"}}});
    EXPECT_NE(config.api_resource_override_removal_guard(owner, custom_script_path).find("registered model"),
              std::string::npos);
    EXPECT_TRUE(config.api_resource_override_removal_guard("test.moonmod.foreign", model_resource_path).empty());
    const moonmod::ApiCallResult foreign_remove = config.api_handler(
        "test.moonmod.foreign", *model_remove, moonmod::Json::array({{{"monster_id", 0}}}));
    EXPECT_EQ(foreign_remove.status, moonmod::ApiCallStatus::PermissionDenied);
    const moonmod::ApiCallResult removed_native_model = config.api_handler(
        owner, *model_remove, moonmod::Json::array({{{"monster_id", 0}}}));
    ASSERT_EQ(removed_native_model.status, moonmod::ApiCallStatus::Ok) << removed_native_model.message;
    EXPECT_TRUE(removed_native_model.value.value("removed", false));
    EXPECT_STREQ(native_template->model, original_native_model.c_str());
    EXPECT_NE(config.api_resource_override_removal_guard(owner, model_resource_path).find("registered model"),
              std::string::npos);
    const moonmod::ApiCallResult replaced_for_unload = config.api_handler(
        owner, *model_replace, moonmod::Json::array({{{"monster_id", 0}, {"model", "moon_sentinel"}}}));
    ASSERT_EQ(replaced_for_unload.status, moonmod::ApiCallStatus::Ok) << replaced_for_unload.message;
    ASSERT_TRUE(static_cast<bool>(config.api_owner_unloaded));
    config.api_owner_unloaded(owner);
    EXPECT_STREQ(native_template->model, original_native_model.c_str());

    const moonmod::ApiCallResult resolved_model = config.api_handler(
        owner, *model_resolve,
        moonmod::Json::array({{{"id", "test.moonmod.custom-model:moon-sentinel-model"}}}));
    ASSERT_EQ(resolved_model.status, moonmod::ApiCallStatus::Ok) << resolved_model.message;
    EXPECT_EQ(resolved_model.value.value("model", ""), "moon_sentinel");
    EXPECT_EQ(resolved_model.value.value("source", ""), "package_override");

    const moonmod::Json character_definition = {{"id", "moon-sentinel"}, {"kind", "enemy"},
                                                 {"name", "Moon Sentinel"},
                                                 {"model", "test.moonmod.custom-model:moon-sentinel-model"},
                                                 {"stats", {{"hp", 455}, {"attack", 91}}}};
    const moonmod::ApiCallResult character = config.api_handler(
        owner, *character_register, moonmod::Json::array({character_definition}));
    ASSERT_EQ(character.status, moonmod::ApiCallStatus::Ok) << character.message;
    EXPECT_EQ(character.value.value("model_id", ""), "test.moonmod.custom-model:moon-sentinel-model");
    const int native_id = character.value.at("native_monster_id").get<int>();
    BASE_MONSTER_TBL *native_definition = GetMonsterTable(native_id);
    ASSERT_NE(native_definition, nullptr);
    EXPECT_STREQ(native_definition->model, "moon_sentinel");
    EXPECT_STREQ(native_definition->script, "moon_brain");
    EXPECT_EQ(native_definition->life, 455);
    EXPECT_EQ(native_definition->attack, 91);

    const moonmod::ApiCallResult native_resolve = config.api_handler(
        owner, *model_resolve, moonmod::Json::array({{{"monster_id", native_id}}}));
    ASSERT_EQ(native_resolve.status, moonmod::ApiCallStatus::Ok) << native_resolve.message;
    EXPECT_EQ(native_resolve.value.value("model_id", ""), "test.moonmod.custom-model:moon-sentinel-model");
    EXPECT_EQ(native_resolve.value.value("source", ""), "package_override");

    const moonmod::ApiCallResult foreign_character = config.api_handler(
        "test.moonmod.foreign", *character_register,
        moonmod::Json::array({{{"id", "foreign-sentinel"}, {"name", "Foreign Sentinel"},
                              {"model", "test.moonmod.custom-model:moon-sentinel-model"}}}));
    EXPECT_EQ(foreign_character.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult referenced_model_removal = config.api_handler(
        owner, *model_unregister,
        moonmod::Json::array({{{"id", "moon-sentinel-model"}}}));
    EXPECT_EQ(referenced_model_removal.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(referenced_model_removal.message.find("registered character uses this model binding"),
              std::string::npos);
}

TEST(MoonModAnimationRegistration, OwnsModelMotionBindingsAndRejectsPlaybackWithoutALiveSpawn) {
    moonmod::ResetChronicleTwoContentForTests();
    moonmod::ResetNativeResourceOverridesForTests();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path package = std::filesystem::temp_directory_path() /
                                          ("moonmod-animation-registration-" + std::to_string(unique_suffix));
    std::filesystem::create_directories(package / "resources");
    std::ofstream(package / "resources" / "motion.chr", std::ios::binary) << "model-with-motion-data";
    struct Cleanup {
        std::filesystem::path package;
        ~Cleanup() {
            moonmod::ResetNativeResourceOverridesForTests();
            moonmod::ResetChronicleTwoContentForTests();
            std::error_code error;
            std::filesystem::remove_all(package, error);
        }
    } cleanup{package};

    constexpr std::string_view owner = "test.moonmod.animation-probe";
    ASSERT_EQ(moonmod::RegisterNativeResourceOverride(
                  owner, package,
                  moonmod::Json::array({{{"path", "resources/motion.chr"},
                                         {"game_path", "dungeon/monster/moon_motion.chr"}}})).status,
              moonmod::ApiCallStatus::Ok);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const auto *model_register = moonmod::FindApiOperation("models.register");
    const auto *animation_register = moonmod::FindApiOperation("animations.register");
    const auto *animation_play = moonmod::FindApiOperation("animations.play");
    const auto *animation_stop = moonmod::FindApiOperation("animations.stop");
    ASSERT_NE(model_register, nullptr);
    ASSERT_NE(animation_register, nullptr);
    ASSERT_NE(animation_play, nullptr);
    ASSERT_NE(animation_stop, nullptr);
    for (const char *path : {"animations.register", "animations.play", "animations.stop"}) {
        const auto capability = std::ranges::find(config.api_capabilities, path, &moonmod::ApiCapability::path);
        ASSERT_NE(capability, config.api_capabilities.end());
        EXPECT_TRUE(capability->supported) << capability->reason;
        EXPECT_TRUE(capability->reason.empty());
    }

    const moonmod::ApiCallResult model = config.api_handler(
        owner, *model_register,
        moonmod::Json::array({{{"id", "motion-model"}, {"template_monster_id", 0}, {"model", "moon_motion"}}}));
    ASSERT_EQ(model.status, moonmod::ApiCallStatus::Ok) << model.message;
    const std::string model_id = model.value.at("id").get<std::string>();
    ASSERT_EQ(model_id, "test.moonmod.animation-probe:motion-model");

    const moonmod::Json animation_definition = {
        {"id", "idle"}, {"model", model_id}, {"motion_index", 0},
    };
    const moonmod::ApiCallResult registered = config.api_handler(
        owner, *animation_register, moonmod::Json::array({animation_definition}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.value("id", ""), "test.moonmod.animation-probe:idle");
    EXPECT_EQ(registered.value.value("model", ""), model_id);
    EXPECT_EQ(registered.value.value("motion_index", -1), 0);
    EXPECT_EQ(registered.value.value("motion_validation", ""), "deferred_until_model_is_loaded");

    EXPECT_EQ(config.api_handler(owner, *animation_register,
                                 moonmod::Json::array({animation_definition})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    moonmod::Json invalid_motion = animation_definition;
    invalid_motion["id"] = "invalid-motion";
    invalid_motion["motion_index"] = 256;
    EXPECT_EQ(config.api_handler(owner, *animation_register,
                                 moonmod::Json::array({invalid_motion})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    moonmod::Json foreign_model = animation_definition;
    foreign_model["id"] = "foreign-model-binding";
    foreign_model["model"] = "test.moonmod.other:some-model";
    EXPECT_EQ(config.api_handler(owner, *animation_register,
                                 moonmod::Json::array({foreign_model})).status,
              moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult no_live_spawn = config.api_handler(
        owner, *animation_play,
        moonmod::Json::array({{{"animation", "idle"}, {"entity", "chronicle-two:monster:0:1"}}}));
    EXPECT_EQ(no_live_spawn.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(no_live_spawn.message.find("characters.spawn"), std::string::npos);
    const moonmod::ApiCallResult no_playback = config.api_handler(
        owner, *animation_stop,
        moonmod::Json::array({{{"entity", "chronicle-two:monster:0:1"}}}));
    EXPECT_EQ(no_playback.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(no_playback.message.find("no active MoonMod animation"), std::string::npos);

    ASSERT_TRUE(static_cast<bool>(config.api_owner_unloaded));
    config.api_owner_unloaded(owner);
    const moonmod::ApiCallResult registered_after_unload = config.api_handler(
        owner, *animation_register, moonmod::Json::array({animation_definition}));
    EXPECT_EQ(registered_after_unload.status, moonmod::ApiCallStatus::Ok) << registered_after_unload.message;
}

TEST(MoonModCharacterRegistration, LuaAdapterAddsADataOnlyEnemyToTheNativeMonsterTable) {
    moonmod::ResetChronicleTwoContentForTests();
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("characters.register");
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("characters.get");
    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("characters.list");
    const moonmod::ApiOperationSpec *spawn_operation = moonmod::FindApiOperation("characters.spawn");
    const moonmod::ApiOperationSpec *despawn_operation = moonmod::FindApiOperation("entities.despawn");
    const moonmod::ApiOperationSpec *model_resolve_operation = moonmod::FindApiOperation("models.resolve");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(get_operation, nullptr);
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(spawn_operation, nullptr);
    ASSERT_NE(despawn_operation, nullptr);
    ASSERT_NE(model_resolve_operation, nullptr);
    const auto spawn_capability = std::ranges::find(config.api_capabilities, "characters.spawn", &moonmod::ApiCapability::path);
    ASSERT_NE(spawn_capability, config.api_capabilities.end());
    EXPECT_TRUE(spawn_capability->supported);
    const auto despawn_capability = std::ranges::find(config.api_capabilities, "entities.despawn", &moonmod::ApiCapability::path);
    ASSERT_NE(despawn_capability, config.api_capabilities.end());
    EXPECT_TRUE(despawn_capability->supported);
    const auto model_resolve_capability = std::ranges::find(config.api_capabilities, "models.resolve", &moonmod::ApiCapability::path);
    ASSERT_NE(model_resolve_capability, config.api_capabilities.end());
    EXPECT_TRUE(model_resolve_capability->supported);

    const moonmod::Json definition = {
        {"id", "bright-sentinel"}, {"kind", "enemy"}, {"name", "Bright Sentinel"},
        {"template_monster_id", 0},
        {"stats", {{"hp", 300}, {"attack", 75}, {"defense", 41}, {"reward_exp", 90}, {"reward_money", 55}}},
        {"assets", moonmod::Json::object()},
    };
    const moonmod::ApiCallResult registration = config.api_handler(
        "test.moonmod.character-probe", *register_operation, moonmod::Json::array({definition}));
    ASSERT_EQ(registration.status, moonmod::ApiCallStatus::Ok) << registration.message;
    EXPECT_EQ(registration.value.value("id", ""), "test.moonmod.character-probe:bright-sentinel");
    EXPECT_EQ(registration.value.value("kind", ""), "enemy");
    EXPECT_EQ(registration.value.value("status", ""), "registered");
    EXPECT_EQ(registration.value.value("model", ""), "e201a");
    EXPECT_EQ(registration.value.value("script", ""), "e201a");
    EXPECT_EQ(registration.value.at("stats").value("hp", 0), 300);
    EXPECT_EQ(registration.value.at("stats").value("attack", 0), 75);
    EXPECT_EQ(registration.value.at("stats").value("defense", 0), 41);

    const int native_id = registration.value.at("native_monster_id").get<int>();
    ASSERT_NE(native_id, 0);
    BASE_MONSTER_TBL *native_definition = GetMonsterTable(native_id);
    ASSERT_NE(native_definition, nullptr);
    EXPECT_STREQ(native_definition->name, "Bright Sentinel");
    EXPECT_STREQ(native_definition->model, "e201a");
    EXPECT_STREQ(native_definition->script, "e201a");
    EXPECT_EQ(native_definition->life, 300);
    EXPECT_EQ(native_definition->attack, 75);
    EXPECT_EQ(native_definition->defense, 41);

    const moonmod::ApiCallResult resolved_model = config.api_handler(
        "test.moonmod.character-probe", *model_resolve_operation,
        moonmod::Json::array({{{"monster_id", native_id}}}));
    ASSERT_EQ(resolved_model.status, moonmod::ApiCallStatus::Ok) << resolved_model.message;
    EXPECT_EQ(resolved_model.value.value("model", ""), "e201a");
    EXPECT_EQ(resolved_model.value.value("source", ""), "native_table");
    EXPECT_EQ(resolved_model.value.value("character_id", ""), "test.moonmod.character-probe:bright-sentinel");
    ASSERT_EQ(resolved_model.value.at("assets").size(), 1u);
    EXPECT_EQ(resolved_model.value.at("assets")[0].value("game_path", ""), "dungeon/monster/e201a.chr");
    EXPECT_FALSE(resolved_model.value.at("assets")[0].value("overridden", true));

    CMonsterMan manager{};
    EXPECT_EQ(manager.GetReferPtr2(native_id), native_definition);

    const moonmod::ApiCallResult found = config.api_handler(
        "test.moonmod.character-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.character-probe:bright-sentinel"}}}));
    ASSERT_EQ(found.status, moonmod::ApiCallStatus::Ok) << found.message;
    EXPECT_EQ(found.value.value("native_monster_id", -1), native_id);
    EXPECT_EQ(found.value.value("template_monster_id", -1), 0);

    const moonmod::ApiCallResult listed = config.api_handler(
        "test.moonmod.character-probe", *list_operation, moonmod::Json::array());
    ASSERT_EQ(listed.status, moonmod::ApiCallStatus::Ok) << listed.message;
    bool found_registered = false;
    for (const moonmod::Json &entry : listed.value) {
        if (entry.value("id", "") == "test.moonmod.character-probe:bright-sentinel") found_registered = true;
    }
    EXPECT_TRUE(found_registered);

    const moonmod::Json spawn_request = {
        {"character", "test.moonmod.character-probe:bright-sentinel"},
        {"position", {{"x", 12.0}, {"y", 0.0}, {"z", -8.0}}},
    };
    const moonmod::ApiCallResult foreign_spawn = config.api_handler(
        "test.moonmod.foreign-mod", *spawn_operation, moonmod::Json::array({spawn_request}));
    EXPECT_EQ(foreign_spawn.status, moonmod::ApiCallStatus::Failed);

    moonmod::Json invalid_spawn_request = spawn_request;
    invalid_spawn_request["position"]["y"] = std::numeric_limits<double>::infinity();
    const moonmod::ApiCallResult invalid_spawn = config.api_handler(
        "test.moonmod.character-probe", *spawn_operation, moonmod::Json::array({invalid_spawn_request}));
    EXPECT_EQ(invalid_spawn.status, moonmod::ApiCallStatus::InvalidArgument);

    CScene *previous_dungeon_scene = DngMainScene;
    CMonsterMan *previous_monster_manager = ActiveMonster;
    DngMainScene = nullptr;
    ActiveMonster = nullptr;
    const moonmod::ApiCallResult no_dungeon_spawn = config.api_handler(
        "test.moonmod.character-probe", *spawn_operation, moonmod::Json::array({spawn_request}));
    EXPECT_EQ(no_dungeon_spawn.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(no_dungeon_spawn.message.find("active Chronicle Two dungeon"), std::string::npos);
    const moonmod::ApiCallResult native_despawn = config.api_handler(
        "test.moonmod.character-probe", *despawn_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:monster:1:1"}}}));
    EXPECT_EQ(native_despawn.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(native_despawn.message.find("created by characters.spawn"), std::string::npos);
    DngMainScene = previous_dungeon_scene;
    ActiveMonster = previous_monster_manager;

    const moonmod::ApiCallResult duplicate = config.api_handler(
        "test.moonmod.character-probe", *register_operation, moonmod::Json::array({definition}));
    EXPECT_EQ(duplicate.status, moonmod::ApiCallStatus::InvalidArgument);
    moonmod::Json invalid_template = definition;
    invalid_template["id"] = "not-a-monster";
    invalid_template["template_monster_id"] = 32767;
    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.character-probe", *register_operation, moonmod::Json::array({invalid_template}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);

    PortActiveSaveData = previous_save;
    moonmod::ResetChronicleTwoContentForTests();
}

TEST(MoonModContentApi, GenericContentRegistrationAndListingUseTheTypedCharacterAdapter) {
    moonmod::ResetChronicleTwoContentForTests();
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const auto *register_operation = moonmod::FindApiOperation("content.register");
    const auto *list_operation = moonmod::FindApiOperation("content.list");
    const auto *unregister_operation = moonmod::FindApiOperation("content.unregister");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(unregister_operation, nullptr);

    const moonmod::Json definition = {
        {"id", "content-sentinel"}, {"kind", "enemy"}, {"name", "Content Sentinel"},
        {"template_monster_id", 0}, {"stats", {{"hp", 240}}},
    };
    const moonmod::ApiCallResult registered = config.api_handler(
        "test.moonmod.content-probe", *register_operation,
        moonmod::Json::array({{{"type", "character"}, {"definition", definition}}}));
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.value("id", ""), "test.moonmod.content-probe:content-sentinel");

    const moonmod::ApiCallResult listed = config.api_handler(
        "test.moonmod.content-probe", *list_operation, moonmod::Json::array());
    ASSERT_EQ(listed.status, moonmod::ApiCallStatus::Ok) << listed.message;
    EXPECT_EQ(listed.value.value("count", 0), 1u);
    ASSERT_EQ(listed.value.at("entries").size(), 1u);
    EXPECT_EQ(listed.value.at("entries")[0].value("content_type", ""), "character");
    EXPECT_EQ(listed.value.at("entries")[0].value("id", ""), "test.moonmod.content-probe:content-sentinel");

    const moonmod::ApiCallResult unregister_character = config.api_handler(
        "test.moonmod.content-probe", *unregister_operation,
        moonmod::Json::array({{{"type", "character"}, {"id", "content-sentinel"}}}));
    EXPECT_EQ(unregister_character.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(unregister_character.message.find("immediately adds a native monster variant"), std::string::npos);

    const moonmod::ApiCallResult unknown_type = config.api_handler(
        "test.moonmod.content-probe", *register_operation,
        moonmod::Json::array({{{"type", "model"}, {"definition", definition}}}));
    EXPECT_EQ(unknown_type.status, moonmod::ApiCallStatus::InvalidArgument);
    const auto unregister_capability = std::ranges::find(
        config.api_capabilities, "content.unregister", &moonmod::ApiCapability::path);
    ASSERT_NE(unregister_capability, config.api_capabilities.end());
    EXPECT_TRUE(unregister_capability->supported);
    EXPECT_TRUE(unregister_capability->reason.empty());
    moonmod::ResetChronicleTwoContentForTests();
}

TEST(MoonModContentApi, UnregistersPendingTypedDefinitionsAndEnforcesOwnership) {
    moonmod::ResetChronicleTwoContentForTests();
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const auto *register_operation = moonmod::FindApiOperation("content.register");
    const auto *unregister_operation = moonmod::FindApiOperation("content.unregister");
    const auto *list_operation = moonmod::FindApiOperation("content.list");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(unregister_operation, nullptr);
    ASSERT_NE(list_operation, nullptr);

    constexpr std::string_view owner = "test.moonmod.pending-content";
    const std::array<std::pair<const char *, moonmod::Json>, 3> definitions{{
        {"weapon", {{"id", "pending-blade"}, {"name", "Pending Blade"}, {"template_weapon", 1}}},
        {"dungeon", {{"id", "pending-vault"}, {"name", "Pending Vault"}, {"dungeon", 0},
                      {"floors", moonmod::Json::array({{{"id", "annex"}, {"source_floor", 0}}})}}},
        {"georama_piece", {{"id", "pending-pillar"}, {"name", "Pending Pillar"}, {"template_part_id", 7}}},
    }};
    for (const auto &[type, definition] : definitions) {
        const moonmod::ApiCallResult registered = config.api_handler(
            owner, *register_operation, moonmod::Json::array({{{"type", type}, {"definition", definition}}}));
        ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
        if (std::string_view(type) == "dungeon") {
            ASSERT_EQ(registered.value.at("floors").size(), 1u);
            EXPECT_EQ(registered.value.at("floors")[0].value("status", ""), "pending");
        } else {
            EXPECT_EQ(registered.value.value("status", ""), "pending");
        }
        const moonmod::ApiCallResult foreign = config.api_handler(
            "test.moonmod.foreign", *unregister_operation,
            moonmod::Json::array({{{"type", type}, {"id", registered.value.at("id")}}}));
        EXPECT_EQ(foreign.status, moonmod::ApiCallStatus::InvalidArgument);
        const moonmod::ApiCallResult removed = config.api_handler(
            owner, *unregister_operation,
            moonmod::Json::array({{{"type", type}, {"id", registered.value.at("id")}}}));
        ASSERT_EQ(removed.status, moonmod::ApiCallStatus::Ok) << removed.message;
        EXPECT_TRUE(removed.value.value("removed", false));
        EXPECT_EQ(removed.value.value("type", ""), type);
    }
    const moonmod::ApiCallResult listed = config.api_handler(owner, *list_operation, moonmod::Json::array());
    ASSERT_EQ(listed.status, moonmod::ApiCallStatus::Ok) << listed.message;
    EXPECT_EQ(listed.value.value("count", 0), 0u);
    moonmod::ResetChronicleTwoContentForTests();
}
