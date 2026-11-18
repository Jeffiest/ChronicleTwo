#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/coop.hpp"
#include "moonmod/dungeon_scale.hpp"
#include "moonmod/dungeon_scale_api.hpp"

#include "automap.hpp"
#include "colprim.hpp"
#include "dng_main.hpp"
#include "gamedata.hpp"

namespace {

struct ScaleScope {
    ScaleScope() { moonmod::scale::ResetForTests(); }
    ~ScaleScope() { moonmod::scale::ResetForTests(); }
};

moonmod::ApiCallResult Call(const moonmod::RuntimeConfig &config, const char *mod, const char *path, const moonmod::Json &request) {
    const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation(path);
    EXPECT_NE(operation, nullptr) << path;
    if (operation == nullptr) return {};
    return config.api_handler(mod, *operation, moonmod::Json::array({request}));
}

} // namespace

TEST(MoonModDungeonScale, ModifiersAddUpPerOwnerAndGrowTheFloor) {
    ScaleScope scope;
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    for (const char *path : {"dungeons.set_modifiers", "dungeons.set_enemy_scale"}) {
        EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [path](const moonmod::ApiCapability &capability) {
            return capability.path == path && capability.supported;
        })) << path;
    }

    // none asks: the game's own floor
    EXPECT_EQ(moonmod::scale::RoomGoal(5), 5);
    EXPECT_EQ(moonmod::scale::BuildArea(14), 14);
    EXPECT_EQ(moonmod::scale::ExtraMonsters(8), 0);

    moonmod::ApiCallResult set = Call(config, "test.one", "dungeons.set_modifiers", {{"rooms", 50}, {"monsters", 50}});
    ASSERT_EQ(set.status, moonmod::ApiCallStatus::Ok) << set.message;
    EXPECT_EQ(set.value.value("rooms", -1), 50);
    EXPECT_EQ(set.value.value("monsters", -1), 50);
    set = Call(config, "test.two", "dungeons.set_modifiers", {{"rooms", 50}});
    ASSERT_EQ(set.status, moonmod::ApiCallStatus::Ok) << set.message;
    EXPECT_EQ(set.value.value("rooms", -1), 100);
    EXPECT_EQ(moonmod::scale::RoomGoal(5), 10);
    EXPECT_EQ(moonmod::scale::BuildArea(14), 28); // twice the rooms, on a grid twice as wide
    EXPECT_EQ(moonmod::scale::ExtraMonsters(8), 4);
    EXPECT_LE(moonmod::scale::BuildArea(DNG_GRID), DNG_GRID);

    // an owner's {} takes back its own only; unloading a mod takes back its own
    ASSERT_EQ(Call(config, "test.two", "dungeons.set_modifiers", moonmod::Json::object()).status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::scale::RoomBonus(), 50);
    config.api_owner_unloaded("test.one");
    EXPECT_EQ(moonmod::scale::RoomBonus(), 0);
    EXPECT_EQ(moonmod::scale::MonsterBonus(), 0);

    EXPECT_EQ(Call(config, "test.one", "dungeons.set_modifiers", {{"rooms", 301}}).status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(Call(config, "test.one", "dungeons.set_modifiers", {{"rooms", -1}}).status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(Call(config, "test.one", "dungeons.set_modifiers", {{"chests", 20}}).status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(Call(config, "test.one", "dungeons.set_modifiers", {{"halls", 20}}).status, moonmod::ApiCallStatus::InvalidArgument);
}

TEST(MoonModDungeonScale, EnemyScaleMultipliesOwnersAndScalesLifeAndBlows) {
    ScaleScope scope;
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);

    BASE_MONSTER_TBL rat{};
    rat.life = 100;
    BASE_MONSTER_TBL boss{};
    boss.life = 1000;
    boss.boss = 1;
    EXPECT_EQ(moonmod::scale::SpawnLife(rat), 100);

    moonmod::ApiCallResult set =
        Call(config, "test.one", "dungeons.set_enemy_scale", {{"life", 160}, {"blows", 125}, {"boss_life", 175}, {"boss_blows", 150}});
    ASSERT_EQ(set.status, moonmod::ApiCallStatus::Ok) << set.message;
    EXPECT_EQ(set.value.value("life", -1), 160);
    EXPECT_EQ(moonmod::scale::SpawnLife(rat), 160);
    EXPECT_EQ(moonmod::scale::SpawnLife(boss), 1750);

    // a second mod's scale multiplies the first's
    ASSERT_EQ(Call(config, "test.two", "dungeons.set_enemy_scale", {{"life", 50}}).status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::scale::Enemies().life, 80);
    EXPECT_EQ(moonmod::scale::SpawnLife(rat), 80);
    ASSERT_EQ(Call(config, "test.two", "dungeons.set_enemy_scale", moonmod::Json::object()).status, moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::scale::Enemies().life, 160);

    // a blow aimed at the players, from no monster and off a boss's floor, lands as an ordinary monster's; the hero's
    // own (owner 0) and one aimed at the monsters are as they were
    CColPrim prim{};
    prim.owner = -1;
    prim.target = DAMAGE_TARGET_PLAYER;
    EXPECT_FLOAT_EQ(moonmod::scale::Blow(40.0f, prim), 50.0f);
    prim.owner = 0;
    EXPECT_FLOAT_EQ(moonmod::scale::Blow(40.0f, prim), 40.0f);
    prim.owner = -1;
    prim.target = DAMAGE_TARGET_MONSTER;
    EXPECT_FLOAT_EQ(moonmod::scale::Blow(40.0f, prim), 40.0f);

    EXPECT_EQ(Call(config, "test.one", "dungeons.set_enemy_scale", {{"life", 9}}).status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(Call(config, "test.one", "dungeons.set_enemy_scale", {{"life", 1001}}).status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(Call(config, "test.one", "dungeons.set_enemy_scale", {{"speed", 120}}).status, moonmod::ApiCallStatus::InvalidArgument);

    // {} (or all hundreds) takes it back, as does the mod unloading
    ASSERT_EQ(Call(config, "test.one", "dungeons.set_enemy_scale", {{"life", 100}, {"blows", 100}, {"boss_life", 100}, {"boss_blows", 100}}).status,
              moonmod::ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::scale::SpawnLife(boss), 1000);
    ASSERT_EQ(Call(config, "test.one", "dungeons.set_enemy_scale", {{"blows", 200}}).status, moonmod::ApiCallStatus::Ok);
    prim.target = DAMAGE_TARGET_PLAYER;
    EXPECT_FLOAT_EQ(moonmod::scale::Blow(40.0f, prim), 80.0f);
    config.api_owner_unloaded("test.one");
    EXPECT_FLOAT_EQ(moonmod::scale::Blow(40.0f, prim), 40.0f);
}

TEST(MoonModDungeonScale, BossRoomsAreKnownByTheirMapsName) {
    EXPECT_TRUE(moonmod::coop::BossRoomMap("d01b01"));
    EXPECT_TRUE(moonmod::coop::BossRoomMap("d06b03"));
    EXPECT_FALSE(moonmod::coop::BossRoomMap("d01f01"));  // a built floor's map
    EXPECT_FALSE(moonmod::coop::BossRoomMap("d06e01"));  // the way out
    EXPECT_FALSE(moonmod::coop::BossRoomMap("s06"));
    EXPECT_FALSE(moonmod::coop::BossRoomMap(""));
    EXPECT_FALSE(moonmod::coop::BossRoomMap("d01b011"));
}

TEST(MoonModDungeonScale, AnUnloadLeavesTheRescaleToTheFloorsNextStep) {
    ScaleScope scope;
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    ASSERT_EQ(Call(config, "mod.a", "dungeons.set_enemy_scale", {{"life", 160}}).status, moonmod::ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::scale::RescaleDue());
    config.api_owner_unloaded("mod.b");
    EXPECT_FALSE(moonmod::scale::RescaleDue()) << "a mod that scaled nothing leaves nothing to do";
    config.api_owner_unloaded("mod.a");
    EXPECT_TRUE(moonmod::scale::RescaleDue()) << "unloading touches no monster then and there (the game may be shutting down)";
    EXPECT_EQ(moonmod::scale::Enemies().life, 100);
    moonmod::scale::StepFloor(); // (off a floor it finds no monsters)
    EXPECT_FALSE(moonmod::scale::RescaleDue());
}

