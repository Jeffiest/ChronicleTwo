#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "dng_main.hpp"
#include "mainloop.hpp"
#include "moonmod_monster_slots.hpp"
#include "platform/game_loop_state.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/combat_formula.hpp"
#include "monster.hpp"
#include "savedata.hpp"

TEST(MoonModCombatApi, AppliesBoundedRankedSkillDamageBonuses) {
    const moonmod::Json bonuses = moonmod::Json::array({{{"rank", 2}, {"per_rank", 0.1}}});
    EXPECT_FLOAT_EQ(moonmod::ApplyCombatDamageBonuses(10.0f, bonuses), 12.0f);
    EXPECT_FLOAT_EQ(moonmod::ApplyCombatDamageBonuses(0.0f, bonuses), 0.0f);
    EXPECT_FLOAT_EQ(moonmod::ApplyCombatDamageBonuses(10.0f, moonmod::Json::object()), 10.0f);
    EXPECT_FLOAT_EQ(moonmod::ApplyCombatDamageBonuses(
                        10.0f, moonmod::Json::array({{{"rank", 100}, {"per_rank", 0.5}}})), 10.0f);
}

TEST(MoonModCombatApi, RegistersAndOrdersOwnerScopedDamageFormulas) {
    moonmod::ResetChronicleTwoCombatFormulasForTests();
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("combat.register_formula");
    ASSERT_NE(operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "combat.register_formula" && capability.supported;
    }));
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 10.0f);

    const std::string owner = "test.moonmod.formula-probe";
    const moonmod::Json definition = {{{"id", "double"}, {"operation", "multiply"}, {"value", 2.0}, {"priority", 10}}};
    const moonmod::ApiCallResult registered = config.api_handler(owner, *operation, definition);
    ASSERT_EQ(registered.status, moonmod::ApiCallStatus::Ok) << registered.message;
    EXPECT_FALSE(registered.value.at("idempotent").get<bool>());
    EXPECT_EQ(registered.value.at("id"), owner + ":double");
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 20.0f);

    const moonmod::ApiCallResult repeated = config.api_handler(owner, *operation, definition);
    ASSERT_EQ(repeated.status, moonmod::ApiCallStatus::Ok) << repeated.message;
    EXPECT_TRUE(repeated.value.at("idempotent").get<bool>());
    const auto updated = config.api_handler(owner, *operation,
                                 moonmod::Json::array({{{"id", "double"}, {"operation", "add"}, {"value", 1}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok);
    EXPECT_FALSE(updated.value.at("idempotent").get<bool>());
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 11.0f);
    ASSERT_EQ(config.api_handler(owner, *operation, definition).status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(config.api_handler(owner, *operation,
                                 moonmod::Json::array({{{"id", "bad"}, {"operation", "multiply"}, {"value", 101}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);

    const std::string earlier_owner = "test.moonmod.formula-earlier";
    const moonmod::Json earlier_formula = {{{"id", "bonus"}, {"operation", "add"}, {"value", 3.0}, {"priority", 5}}};
    ASSERT_EQ(config.api_handler(earlier_owner, *operation, earlier_formula).status, moonmod::ApiCallStatus::Ok);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 26.0f);

    config.api_owner_unloaded(owner);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 13.0f);
    config.api_owner_unloaded(earlier_owner);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 10.0f);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(0.0f), 0.0f);

    const std::string alpha_owner = "test.moonmod.formula-alpha";
    const std::string zeta_owner = "test.moonmod.formula-zeta";
    const moonmod::Json zeta_add = {{{"id", "add"}, {"operation", "add"}, {"value", 1.0}, {"priority", 3}}};
    const moonmod::Json alpha_multiply = {{{"id", "multiply"}, {"operation", "multiply"}, {"value", 2.0}, {"priority", 3}}};
    ASSERT_EQ(config.api_handler(zeta_owner, *operation, zeta_add).status, moonmod::ApiCallStatus::Ok);
    ASSERT_EQ(config.api_handler(alpha_owner, *operation, alpha_multiply).status, moonmod::ApiCallStatus::Ok);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 21.0f);
    config.api_owner_unloaded(alpha_owner);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 11.0f);
    config.api_owner_unloaded(zeta_owner);
    EXPECT_FLOAT_EQ(moonmod::ApplyChronicleTwoCombatDamageFormula(10.0f), 10.0f);
    moonmod::ResetChronicleTwoCombatFormulasForTests();
}

TEST(MoonModCombatApi, ReadsLiveDungeonMonsterHealthStatusAndEmptyCombatState) {
    CMonsterMan *previous_manager = ActiveMonster;
    DNG_BATTLE_AREA *previous_area = BattleAreaScene;
    CSaveData *previous_save = DngSaveData;
    const DNG_STATUS previous_status = DngStatus;
    const int previous_loop = PortLoopNo;
    struct Restore {
        CMonsterMan *manager;
        DNG_BATTLE_AREA *area;
        CSaveData *save;
        DNG_STATUS status;
        int loop;
        ~Restore() {
            ActiveMonster = manager;
            BattleAreaScene = area;
            DngSaveData = save;
            DngStatus = status;
            PortLoopNo = loop;
        }
    } restore{previous_manager, previous_area, previous_save, previous_status, previous_loop};

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation("combat.get");
    const moonmod::ApiOperationSpec *damage_operation = moonmod::FindApiOperation("combat.damage");
    const moonmod::ApiOperationSpec *heal_operation = moonmod::FindApiOperation("combat.heal");
    ASSERT_NE(operation, nullptr);
    ASSERT_NE(damage_operation, nullptr);
    ASSERT_NE(heal_operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "combat.get" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "combat.heal" && capability.supported;
    }));
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "combat.damage" && capability.supported;
    }));

    auto manager = std::make_unique<CMonsterMan>();
    CScene scene{};
    moonmod_test::MonsterSlots slots(*manager, scene, {3, 7});
    CActiveMonster &first = slots[3];
    CActiveMonster &dead = slots[7];
    DNG_BATTLE_AREA area{};
    auto save = std::make_unique<CSaveData>();
    save->Initialize();
    DngStatus.mode = DNG_STATUS_FIELD;
    PortLoopNo = LOOP_TITLE;
    ActiveMonster = manager.get();
    BattleAreaScene = &area;
    DngSaveData = nullptr;
    const moonmod::ApiCallResult no_dungeon = config.api_handler("test.moonmod.combat-probe", *operation,
                                                                 moonmod::Json::array());
    EXPECT_EQ(no_dungeon.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult no_dungeon_heal = config.api_handler(
        "test.moonmod.combat-probe", *heal_operation,
        moonmod::Json::array({{{"entity", "chronicle-two:monster:3:1"}, {"amount", 12}}}));
    EXPECT_EQ(no_dungeon_heal.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult no_dungeon_damage = config.api_handler(
        "test.moonmod.combat-probe", *damage_operation,
        moonmod::Json::array({{{"entity", "chronicle-two:monster:3:1"}, {"amount", 12}}}));
    EXPECT_EQ(no_dungeon_damage.status, moonmod::ApiCallStatus::Failed);

    first.state = ACTIVE_MONSTER_LIVE;
    first.monster_id = 28;
    first.life = 64;
    first.max_life = 100;
    first.attrib = 0;
    first.muteki_time = 0;
    first.catch_state = ACTION_CATCH_NONE;
    first.status.attr = MONSTER_STATUS_POISON | MONSTER_STATUS_SLOW;
    first.target_no = 1;
    dead.state = ACTIVE_MONSTER_DEAD;
    dead.monster_id = 42;
    dead.life = 0;
    dead.max_life = 200;
    ActiveMonster = manager.get();
    BattleAreaScene = &area;
    DngSaveData = save.get();
    PortLoopNo = LOOP_DUNGEON;

    const moonmod::ApiCallResult current = config.api_handler("test.moonmod.combat-probe", *operation,
                                                              moonmod::Json::array());
    ASSERT_EQ(current.status, moonmod::ApiCallStatus::Ok) << current.message;
    EXPECT_TRUE(current.value.at("in_combat").get<bool>());
    ASSERT_EQ(current.value.at("enemy_count").get<int>(), 1);
    ASSERT_EQ(current.value.at("enemies").size(), 1u);
    EXPECT_EQ(current.value["enemies"][0].at("id"), "chronicle-two:monster-slot:3");
    EXPECT_EQ(current.value["enemies"][0].at("monster_id"), 28);
    EXPECT_EQ(current.value["enemies"][0].at("hp"), 64);
    EXPECT_EQ(current.value["enemies"][0].at("max_hp"), 100);
    EXPECT_EQ(current.value["enemies"][0].at("status_flags"),
              MONSTER_STATUS_POISON | MONSTER_STATUS_SLOW);
    EXPECT_EQ(current.value["enemies"][0].at("target_slot"), 1);
    const std::string entity_handle = current.value["enemies"][0].at("entity_id").get<std::string>();
    EXPECT_EQ(entity_handle.substr(0, std::string("chronicle-two:monster:3:").size()), "chronicle-two:monster:3:");

    const moonmod::ApiCallResult healed = config.api_handler(
        "test.moonmod.combat-probe", *heal_operation,
        moonmod::Json::array({{{"entity", entity_handle}, {"amount", 80}}}));
    ASSERT_EQ(healed.status, moonmod::ApiCallStatus::Ok) << healed.message;
    EXPECT_EQ(healed.value.at("entity"), entity_handle);
    EXPECT_EQ(healed.value.at("monster_id"), 28);
    EXPECT_EQ(healed.value.at("hp_before"), 64);
    EXPECT_EQ(healed.value.at("hp_after"), 100);
    EXPECT_EQ(healed.value.at("restored"), 36);
    EXPECT_TRUE(healed.value.at("changed").get<bool>());
    EXPECT_EQ(first.life, 100);

    const moonmod::ApiCallResult at_cap = config.api_handler(
        "test.moonmod.combat-probe", *heal_operation,
        moonmod::Json::array({{{"entity", entity_handle}, {"amount", 12}}}));
    ASSERT_EQ(at_cap.status, moonmod::ApiCallStatus::Ok) << at_cap.message;
    EXPECT_EQ(at_cap.value.at("hp_after"), 100);
    EXPECT_EQ(at_cap.value.at("restored"), 0);
    EXPECT_FALSE(at_cap.value.at("changed").get<bool>());
    EXPECT_EQ(config.api_handler("test.moonmod.combat-probe", *heal_operation,
                                 moonmod::Json::array({{{"entity", "chronicle-two:monster:7:1"}, {"amount", 12}}})).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.combat-probe", *heal_operation,
                                 moonmod::Json::array({{{"entity", "chronicle-two:monster:" + std::to_string(MONSTER_ACTIVE_MAX) + ":1"}, {"amount", 12}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.combat-probe", *heal_operation,
                                 moonmod::Json::array({{{"entity", entity_handle}, {"amount", 0}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.combat-probe", *heal_operation,
                                 moonmod::Json::array({{{"entity", "chronicle-two:monster:3:99999999"},
                                                       {"amount", 12}}})).status,
              moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(config.api_handler("test.moonmod.combat-probe", *heal_operation,
                                 moonmod::Json::array({{{"entity", entity_handle}, {"slot", 3}, {"amount", 12}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);

    // The former slot form remains usable for existing mods; new mods should retain the entity handle.
    first.life = 50;
    const moonmod::ApiCallResult legacy_slot_heal = config.api_handler(
        "test.moonmod.combat-probe", *heal_operation, moonmod::Json::array({{{"slot", 3}, {"amount", 5}}}));
    ASSERT_EQ(legacy_slot_heal.status, moonmod::ApiCallStatus::Ok) << legacy_slot_heal.message;
    EXPECT_EQ(legacy_slot_heal.value.at("hp_after"), 55);
    first.life = 100;

    const moonmod::ApiCallResult damaged = config.api_handler(
        "test.moonmod.combat-probe", *damage_operation,
        moonmod::Json::array({{{"entity", entity_handle}, {"amount", 30}}}));
    ASSERT_EQ(damaged.status, moonmod::ApiCallStatus::Ok) << damaged.message;
    EXPECT_EQ(damaged.value.at("hp_before"), 100);
    EXPECT_EQ(damaged.value.at("hp_after"), 70);
    EXPECT_EQ(damaged.value.at("damage"), 30);
    EXPECT_TRUE(damaged.value.at("changed").get<bool>());
    EXPECT_FALSE(damaged.value.at("blocked").get<bool>());
    EXPECT_FALSE(damaged.value.at("death_queued").get<bool>());
    EXPECT_EQ(first.req_prog, MONSTER_PROG_DAMAGE);
    EXPECT_EQ(first.life, 70);

    first.muteki_time = 4;
    const moonmod::ApiCallResult blocked = config.api_handler(
        "test.moonmod.combat-probe", *damage_operation,
        moonmod::Json::array({{{"entity", entity_handle}, {"amount", 20}}}));
    ASSERT_EQ(blocked.status, moonmod::ApiCallStatus::Ok) << blocked.message;
    EXPECT_TRUE(blocked.value.at("blocked").get<bool>());
    EXPECT_FALSE(blocked.value.at("changed").get<bool>());
    EXPECT_EQ(first.life, 70);
    first.muteki_time = 0;

    const moonmod::ApiCallResult stale_damage = config.api_handler(
        "test.moonmod.combat-probe", *damage_operation,
        moonmod::Json::array({{{"entity", "chronicle-two:monster:3:99999999"}, {"amount", 20}}}));
    EXPECT_EQ(stale_damage.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult lethal = config.api_handler(
        "test.moonmod.combat-probe", *damage_operation,
        moonmod::Json::array({{{"entity", entity_handle}, {"amount", 200}}}));
    ASSERT_EQ(lethal.status, moonmod::ApiCallStatus::Ok) << lethal.message;
    EXPECT_EQ(lethal.value.at("hp_after"), 0);
    EXPECT_TRUE(lethal.value.at("death_queued").get<bool>());
    EXPECT_EQ(first.damage_req, ACTION_DAMAGE_REQ_DEAD);
    EXPECT_EQ(first.last_hit_chara, -1);
    EXPECT_EQ(first.last_hit_source, DAMAGE_SOURCE_NONE);
    EXPECT_EQ(first.last_hit_attr, 0u);

    first.life = 0;
    const moonmod::ApiCallResult cleared = config.api_handler("test.moonmod.combat-probe", *operation,
                                                             moonmod::Json::array());
    ASSERT_EQ(cleared.status, moonmod::ApiCallStatus::Ok) << cleared.message;
    EXPECT_FALSE(cleared.value.at("in_combat").get<bool>());
    EXPECT_EQ(cleared.value.at("enemy_count").get<int>(), 0);
    EXPECT_TRUE(cleared.value.at("enemies").empty());

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.combat-probe", *operation, moonmod::Json::array({moonmod::Json::object()}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
}
