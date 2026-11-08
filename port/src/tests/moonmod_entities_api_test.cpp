#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "dng_main.hpp"
#include "moonmod_monster_slots.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "monster.hpp"
#include "savedata.hpp"

TEST(MoonModEntitiesApi, QueriesLiveDungeonMonstersAndRejectsUnsupportedFilters) {
    moonmod::ResetChronicleTwoContentForTests();
    CMonsterMan *previous_manager = ActiveMonster;
    DNG_BATTLE_AREA *previous_area = BattleAreaScene;
    CScene *previous_scene = DngMainScene;
    CSaveData *previous_save = DngSaveData;
    const DNG_STATUS previous_status = DngStatus;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CMonsterMan *manager;
        DNG_BATTLE_AREA *area;
        CScene *scene;
        CSaveData *save;
        DNG_STATUS status;
        int loop;
        ~Restore() {
            ActiveMonster = manager;
            BattleAreaScene = area;
            DngMainScene = scene;
            DngSaveData = save;
            DngStatus = status;
            PortLoopNo = loop;
        }
    } restore{previous_manager, previous_area, previous_scene, previous_save, previous_status, previous_loop};

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("entities.query");
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("entities.get");
    const moonmod::ApiOperationSpec *spawn_operation = moonmod::FindApiOperation("entities.spawn");
    const moonmod::ApiOperationSpec *physics_query_operation = moonmod::FindApiOperation("physics.query");
    const moonmod::ApiOperationSpec *velocity_operation = moonmod::FindApiOperation("physics.velocity_get");
    const moonmod::ApiOperationSpec *velocity_set_operation = moonmod::FindApiOperation("physics.velocity_set");
    ASSERT_NE(operation, nullptr);
    ASSERT_NE(get_operation, nullptr);
    ASSERT_NE(spawn_operation, nullptr);
    ASSERT_NE(physics_query_operation, nullptr);
    ASSERT_NE(velocity_operation, nullptr);
    ASSERT_NE(velocity_set_operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "entities.query" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return (capability.path == "entities.get" || capability.path == "entities.spawn") && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "physics.velocity_get" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "physics.velocity_set" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "physics.query" && capability.supported;
    }));

    auto manager = std::make_unique<CMonsterMan>();
    CScene scene{};
    moonmod_test::MonsterSlots slots(*manager, scene, {3, 7});
    CActiveMonster &live = slots[3];
    CActiveMonster &dead = slots[7];
    DNG_BATTLE_AREA area{};
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    DngStatus.mode = DNG_STATUS_FIELD;
    PortLoopNo = LOOP_TITLE;
    ActiveMonster = manager.get();
    BattleAreaScene = &area;
    DngSaveData = nullptr;
    const moonmod::ApiCallResult outside_dungeon = config.api_handler(
        "test.moonmod.entities-probe", *operation, moonmod::Json::array());
    EXPECT_EQ(outside_dungeon.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult stale_outside_dungeon = config.api_handler(
        "test.moonmod.entities-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:monster:3:1"}}}));
    ASSERT_EQ(stale_outside_dungeon.status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(stale_outside_dungeon.value.value("status", ""), "expired");
    const moonmod::ApiCallResult velocity_outside_dungeon = config.api_handler(
        "test.moonmod.entities-probe", *velocity_operation,
        moonmod::Json::array({moonmod::Json{{"entity", "chronicle-two:monster:3:1"}}}));
    EXPECT_EQ(velocity_outside_dungeon.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult velocity_set_outside_dungeon = config.api_handler(
        "test.moonmod.entities-probe", *velocity_set_operation,
        moonmod::Json::array({moonmod::Json{{"entity", "chronicle-two:monster:3:1"},
                                           {"velocity", {{"x", 1.0}, {"y", 0.0}, {"z", 0.0}}}}}));
    EXPECT_EQ(velocity_set_outside_dungeon.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult physics_query_outside_dungeon = config.api_handler(
        "test.moonmod.entities-probe", *physics_query_operation,
        moonmod::Json::array({moonmod::Json{{"shape", "sphere"}, {"center", {0.0, 0.0, 0.0}}, {"radius", 5.0}}}));
    EXPECT_EQ(physics_query_outside_dungeon.status, moonmod::ApiCallStatus::Failed);

    live.state = ACTIVE_MONSTER_LIVE;
    live.monster_id = 28;
    live.life = 64;
    live.max_life = 100;
    live.status.attr = MONSTER_STATUS_POISON | MONSTER_STATUS_SLOW;
    live.target_no = 1;
    live.velocity[0] = 1.25f;
    live.velocity[1] = -2.5f;
    live.velocity[2] = 0.75f;
    live.SetPosition(3.0f, 4.0f, 0.0f);
    dead.state = ACTIVE_MONSTER_DEAD;
    dead.monster_id = 42;
    dead.life = 0;
    dead.max_life = 200;
    DngMainScene = &scene;
    DngSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;

    const moonmod::ApiCallResult queried = config.api_handler(
        "test.moonmod.entities-probe", *operation,
        moonmod::Json::array({moonmod::Json{{"type", "monster"}}}));
    ASSERT_EQ(queried.status, moonmod::ApiCallStatus::Ok) << queried.message;
    EXPECT_EQ(queried.value.at("count").get<int>(), 1);
    ASSERT_EQ(queried.value.at("entities").size(), 1u);
    const moonmod::Json &entity = queried.value.at("entities")[0];
    EXPECT_EQ(entity.at("id").get<std::string>().substr(0, std::string("chronicle-two:monster:3:").size()),
              "chronicle-two:monster:3:");
    EXPECT_EQ(entity.at("type"), "monster");
    EXPECT_EQ(entity.at("definition").at("monster_id"), 28);
    EXPECT_EQ(entity.at("health").at("current"), 64);
    EXPECT_EQ(entity.at("health").at("maximum"), 100);
    EXPECT_EQ(entity.at("status").at("flags"), MONSTER_STATUS_POISON | MONSTER_STATUS_SLOW);
    EXPECT_EQ(entity.at("status").at("target_slot"), 1);

    const moonmod::ApiCallResult spatial_query = config.api_handler(
        "test.moonmod.entities-probe", *physics_query_operation,
        moonmod::Json::array({moonmod::Json{{"shape", "sphere"}, {"center", {0.0, 0.0, 0.0}}, {"radius", 5.0}}}));
    ASSERT_EQ(spatial_query.status, moonmod::ApiCallStatus::Ok) << spatial_query.message;
    EXPECT_EQ(spatial_query.value.value("semantics", ""),
              "live monster actor origins inside or on the sphere; no collider or world geometry test");
    EXPECT_EQ(spatial_query.value.at("count").get<int>(), 1);
    ASSERT_EQ(spatial_query.value.at("matches").size(), 1u);
    EXPECT_EQ(spatial_query.value.at("matches")[0].at("entity"), entity.at("id"));
    EXPECT_NEAR(spatial_query.value.at("matches")[0].at("distance").get<double>(), 5.0, 0.00001);
    const moonmod::ApiCallResult malformed_spatial_query = config.api_handler(
        "test.moonmod.entities-probe", *physics_query_operation,
        moonmod::Json::array({moonmod::Json{{"shape", "ray"}, {"center", {0.0, 0.0, 0.0}}, {"radius", 5.0}}}));
    EXPECT_EQ(malformed_spatial_query.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult oversized_spatial_query = config.api_handler(
        "test.moonmod.entities-probe", *physics_query_operation,
        moonmod::Json::array({moonmod::Json{{"shape", "sphere"}, {"center", {0.0, 0.0, 0.0}}, {"radius", 10001.0}}}));
    EXPECT_EQ(oversized_spatial_query.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult resolved = config.api_handler(
        "test.moonmod.entities-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", entity.at("id")}}}));
    ASSERT_EQ(resolved.status, moonmod::ApiCallStatus::Ok) << resolved.message;
    EXPECT_EQ(resolved.value.value("status", ""), "alive");
    EXPECT_EQ(resolved.value.at("health").at("current"), 64);

    moonmod::ChronicleTwoTrackModSpawnedEnemy("test.moonmod.entities-probe",
                                              "test.moonmod.entities-probe:sentinel", 3,
                                              entity.at("generation").get<unsigned int>(), live.monster_id);
    const moonmod::ApiCallResult mod_resolved = config.api_handler(
        "test.moonmod.entities-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", entity.at("id")}}}));
    ASSERT_EQ(mod_resolved.status, moonmod::ApiCallStatus::Ok) << mod_resolved.message;
    EXPECT_EQ(mod_resolved.value.value("status", ""), "alive");
    EXPECT_EQ(mod_resolved.value.at("definition").at("monster_id"), live.monster_id);
    EXPECT_EQ(mod_resolved.value.at("health").at("current"), live.life);
    EXPECT_EQ(mod_resolved.value.value("character_id", ""), "test.moonmod.entities-probe:sentinel");
    EXPECT_EQ(mod_resolved.value.value("owner", ""), "test.moonmod.entities-probe");
    EXPECT_TRUE(mod_resolved.value.value("mod_owned", false));
    const moonmod::ApiCallResult mod_queried = config.api_handler(
        "test.moonmod.entities-probe", *operation,
        moonmod::Json::array({moonmod::Json{{"type", "monster"}}}));
    ASSERT_EQ(mod_queried.status, moonmod::ApiCallStatus::Ok) << mod_queried.message;
    const auto listed_mod_entity = std::ranges::find_if(mod_queried.value.at("entities"), [&](const moonmod::Json &entry) {
        return entry.value("id", "") == entity.at("id").get<std::string>();
    });
    ASSERT_NE(listed_mod_entity, mod_queried.value.at("entities").end());
    EXPECT_EQ(listed_mod_entity->value("character_id", ""), "test.moonmod.entities-probe:sentinel");
    EXPECT_EQ(listed_mod_entity->value("owner", ""), "test.moonmod.entities-probe");

    const moonmod::ApiCallResult velocity = config.api_handler(
        "test.moonmod.entities-probe", *velocity_operation,
        moonmod::Json::array({moonmod::Json{{"entity", entity.at("id")}}}));
    ASSERT_EQ(velocity.status, moonmod::ApiCallStatus::Ok) << velocity.message;
    EXPECT_EQ(velocity.value.value("entity", ""), entity.at("id").get<std::string>());
    EXPECT_EQ(velocity.value.value("units", ""), "game_units_per_frame");
    EXPECT_NEAR(velocity.value.at("velocity").at("x").get<float>(), 1.25f, 0.00001f);
    EXPECT_NEAR(velocity.value.at("velocity").at("y").get<float>(), -2.5f, 0.00001f);
    EXPECT_NEAR(velocity.value.at("velocity").at("z").get<float>(), 0.75f, 0.00001f);

    const moonmod::ApiCallResult velocity_set = config.api_handler(
        "test.moonmod.entities-probe", *velocity_set_operation,
        moonmod::Json::array({moonmod::Json{{"entity", entity.at("id")},
                                           {"velocity", {{"x", 3.5}, {"y", 4.0}, {"z", -5.0}}}}}));
    ASSERT_EQ(velocity_set.status, moonmod::ApiCallStatus::Ok) << velocity_set.message;
    EXPECT_TRUE(velocity_set.value.at("changed").get<bool>());
    EXPECT_NEAR(velocity_set.value.at("previous_velocity").at("x").get<float>(), 1.25f, 0.00001f);
    EXPECT_NEAR(velocity_set.value.at("previous_velocity").at("y").get<float>(), -2.5f, 0.00001f);
    EXPECT_NEAR(velocity_set.value.at("previous_velocity").at("z").get<float>(), 0.75f, 0.00001f);
    EXPECT_NEAR(live.velocity[0], 3.5f, 0.00001f);
    EXPECT_NEAR(live.velocity[1], 4.0f, 0.00001f);
    EXPECT_NEAR(live.velocity[2], -5.0f, 0.00001f);
    EXPECT_FLOAT_EQ(live.velocity[3], 0.0f);

    const moonmod::ApiCallResult invalid_velocity_set = config.api_handler(
        "test.moonmod.entities-probe", *velocity_set_operation,
        moonmod::Json::array({moonmod::Json{{"entity", entity.at("id")},
                                           {"velocity", {{"x", 100001.0}, {"y", 0.0}, {"z", 0.0}}}}}));
    EXPECT_EQ(invalid_velocity_set.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult unsupported_type = config.api_handler(
        "test.moonmod.entities-probe", *operation,
        moonmod::Json::array({moonmod::Json{{"type", "npc"}}}));
    EXPECT_EQ(unsupported_type.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult unsupported_filter = config.api_handler(
        "test.moonmod.entities-probe", *operation,
        moonmod::Json::array({moonmod::Json{{"tag", "quest"}}}));
    EXPECT_EQ(unsupported_filter.status, moonmod::ApiCallStatus::InvalidArgument);

    live.life = 0;
    const moonmod::ApiCallResult empty = config.api_handler(
        "test.moonmod.entities-probe", *operation, moonmod::Json::array());
    ASSERT_EQ(empty.status, moonmod::ApiCallStatus::Ok) << empty.message;
    EXPECT_EQ(empty.value.at("count").get<int>(), 0);
    EXPECT_TRUE(empty.value.at("entities").empty());
    const moonmod::ApiCallResult dead_entity = config.api_handler(
        "test.moonmod.entities-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", entity.at("id")}}}));
    ASSERT_EQ(dead_entity.status, moonmod::ApiCallStatus::Ok) << dead_entity.message;
    EXPECT_EQ(dead_entity.value.value("status", ""), "dead");
    const moonmod::ApiCallResult velocity_set_dead = config.api_handler(
        "test.moonmod.entities-probe", *velocity_set_operation,
        moonmod::Json::array({moonmod::Json{{"entity", entity.at("id")},
                                           {"velocity", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}}}}));
    EXPECT_EQ(velocity_set_dead.status, moonmod::ApiCallStatus::Failed);
    moonmod::ResetChronicleTwoContentForTests();
}

TEST(MoonModEntitiesApi, DespawnsOwnedMonsterWithoutKillSideEffectsAndInvalidatesItsHandle) {
    constexpr int slot = 5;
    constexpr int primitive_slot = 60;
    const int chara_id = slot + DNG_CHARA_SLOT_MONSTER;
    CMonsterMan *previous_manager = ActiveMonster;
    CScene *previous_scene = DngMainScene;
    struct RestoreDungeon {
        CMonsterMan *manager;
        CScene *scene;
        ~RestoreDungeon() {
            ActiveMonster = manager;
            DngMainScene = scene;
        }
    } restore_dungeon{previous_manager, previous_scene};
    CScene scene{};
    CMonsterMan manager{};
    auto player_owner = std::make_shared<CActionChara>();
    CActionChara &player = *player_owner;
    BASE_MONSTER_TBL definition{};
    CColPrim saved_primitive = ColPrimMan.prim[primitive_slot];
    struct RestorePrimitive {
        CColPrim &target;
        CColPrim saved;
        ~RestorePrimitive() { target = saved; }
    } restore{ColPrimMan.prim[primitive_slot], saved_primitive};

    moonmod_test::MonsterSlots slots(manager, scene, {slot, slot + 1});
    CActiveMonster &monster = slots[slot];
    CActiveMonster &targeter = slots[slot + 1];
    ASSERT_EQ(scene.AssignChara(0, dc2::scene::CharacterResource::FromOwned(player_owner)), 0);
    ActiveMonster = &manager;
    DngMainScene = &scene;
    manager.effect_man = nullptr;
    manager.locate.num = 2;
    manager.locate.put_num = 1;
    manager.locate.put_flag = 1u << slot;
    manager.memory[slot].stack_used = 7;
    manager.memory[slot].lock = 1;

    monster.state = ACTIVE_MONSTER_LIVE;
    monster.monster_id = 0x455;
    monster.chara_type = chara_id;
    monster.life = 100;
    monster.max_life = 100;
    monster.tbl = &definition;
    monster.damage[0].use = true;
    monster.damage[0].prim = &ColPrimMan.prim[primitive_slot];
    targeter.target_no = chara_id;
    targeter.state = ACTIVE_MONSTER_LIVE;
    player.target_no = chara_id;
    player.lock_on = true;
    scene.player_chara = 0;

    ColPrimMan.prim[primitive_slot].active = true;
    ColPrimMan.prim[primitive_slot].owner = chara_id;
    manager.locate.SetPutFlag(slot, true);
    EXPECT_EQ(manager.locate.put_num, 1);

    const unsigned int generation = EnsureMoonModMonsterSlotGeneration(slot);
    ASSERT_NE(generation, 0u);
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    moonmod::ChronicleTwoTrackModSpawnedEnemy("test.moonmod.unload-probe", "test.moonmod.unload-probe:sentinel",
                                              slot, generation, monster.monster_id);
    ASSERT_TRUE(static_cast<bool>(config.api_owner_unloaded));
    config.api_owner_unloaded("test.moonmod.unload-probe");

    EXPECT_EQ(monster.state, ACTIVE_MONSTER_NONE);
    EXPECT_EQ(monster.chara_kind, ACTION_KIND_NONE);
    EXPECT_EQ(monster.monster_id, 0);
    EXPECT_EQ(monster.damage[0].prim, nullptr);
    EXPECT_FALSE(ColPrimMan.prim[primitive_slot].active);
    EXPECT_EQ(targeter.target_no, -1);
    EXPECT_EQ(player.target_no, -1);
    EXPECT_FALSE(player.lock_on);
    EXPECT_EQ(manager.locate.put_num, 0);
    EXPECT_EQ(manager.locate.put_flag, 0u);
    EXPECT_EQ(manager.memory[slot].stack_used, 0);
    EXPECT_EQ(manager.memory[slot].lock, 0);
    EXPECT_NE(GetMoonModMonsterSlotGeneration(slot), generation);
    EXPECT_FALSE(manager.DespawnMoonModMonster(slot, generation));

    manager.locate.SetPutFlag(slot, false);
    EXPECT_EQ(manager.locate.put_num, 0);
}

TEST(MoonModEntitiesApi, SetsActiveMonsterLimitAndUpdatesLiveManager) {
    moonmod::ResetChronicleTwoContentForTests();
    CMonsterMan *previous_manager = ActiveMonster;
    struct RestoreManager {
        CMonsterMan *manager;
        ~RestoreManager() {
            ActiveMonster = manager;
            moonmod::ResetChronicleTwoContentForTests();
        }
    } restore_manager{previous_manager};

    CMonsterMan manager{};
    manager.priority_limit = 5;
    ActiveMonster = &manager;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *limit_operation = moonmod::FindApiOperation("entities.set_active_limit");
    ASSERT_NE(limit_operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "entities.set_active_limit" && capability.supported;
    }));

    const moonmod::ApiCallResult updated = config.api_handler(
        "test.moonmod.entities-probe", *limit_operation,
        moonmod::Json::array({moonmod::Json{{"active", 12}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok) << updated.message;
    EXPECT_EQ(updated.value.value("previous", -1), 5);
    EXPECT_EQ(updated.value.value("active", -1), 12);
    EXPECT_EQ(updated.value.value("slots", -1), MONSTER_ACTIVE_MAX);
    EXPECT_EQ(MonsterActiveLimit, 12);
    EXPECT_EQ(manager.priority_limit, 12);

    const moonmod::ApiCallResult second_updated = config.api_handler(
        "test.moonmod.entities-probe", *limit_operation,
        moonmod::Json::array({moonmod::Json{{"active", 8}}}));
    ASSERT_EQ(second_updated.status, moonmod::ApiCallStatus::Ok) << second_updated.message;
    EXPECT_EQ(second_updated.value.value("previous", -1), 12);
    EXPECT_EQ(second_updated.value.value("active", -1), 8);
    EXPECT_EQ(MonsterActiveLimit, 8);
    EXPECT_EQ(manager.priority_limit, 8);

    EXPECT_EQ(config.api_handler("test.moonmod.entities-probe", *limit_operation,
                                 moonmod::Json::array({moonmod::Json{{"active", 0}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.entities-probe", *limit_operation,
                                 moonmod::Json::array({moonmod::Json{{"active", MONSTER_ACTIVE_MAX + 1}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
}
