#include <gtest/gtest.h>

#include <memory>

#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "dng_main.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModPlayerApi, ReadsHealsAndDamagesActiveNativeCharacterWithHealthClamp) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.SetActiveChrNo(USER_CHARA_MAX);
    CHARA_DATA *max = user_data.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(max, nullptr);
    max->hp.max = 100.0f;
    max->hp.now = 40.0f;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("player.get");
    const moonmod::ApiOperationSpec *heal_operation = moonmod::FindApiOperation("player.heal");
    const moonmod::ApiOperationSpec *damage_operation = moonmod::FindApiOperation("player.damage");
    ASSERT_NE(get_operation, nullptr);
    ASSERT_NE(heal_operation, nullptr);
    ASSERT_NE(damage_operation, nullptr);

    const moonmod::ApiCallResult before = config.api_handler("test.moonmod.player-probe", *get_operation,
                                                            moonmod::Json::array());
    ASSERT_EQ(before.status, moonmod::ApiCallStatus::Ok) << before.message;
    EXPECT_EQ(before.value.value("character", ""), "max");
    EXPECT_FLOAT_EQ(before.value.value("hp", 0.0f), 40.0f);
    EXPECT_FLOAT_EQ(before.value.value("max_hp", 0.0f), 100.0f);

    const moonmod::ApiCallResult healed = config.api_handler(
        "test.moonmod.player-probe", *heal_operation, moonmod::Json::array({{{"amount", 25}}}));
    ASSERT_EQ(healed.status, moonmod::ApiCallStatus::Ok) << healed.message;
    EXPECT_EQ(healed.value.value("character", ""), "max");
    EXPECT_FLOAT_EQ(healed.value.value("healed", 0.0f), 25.0f);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 65.0f);

    max->hp.now = 98.0f;
    const moonmod::ApiCallResult clamped = config.api_handler(
        "test.moonmod.player-probe", *heal_operation, moonmod::Json::array({{{"amount", 5}}}));
    ASSERT_EQ(clamped.status, moonmod::ApiCallStatus::Ok) << clamped.message;
    EXPECT_FLOAT_EQ(clamped.value.value("healed", 0.0f), 2.0f);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 100.0f);

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.player-probe", *heal_operation, moonmod::Json::array({{{"amount", 0}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 100.0f);

    const moonmod::ApiCallResult invalid_target = config.api_handler(
        "test.moonmod.player-probe", *heal_operation,
        moonmod::Json::array({{{"amount", 5}, {"character", "townsfolk"}}}));
    EXPECT_EQ(invalid_target.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 100.0f);

    max->hp.now = 65.0f;
    const moonmod::ApiCallResult damaged = config.api_handler(
        "test.moonmod.player-probe", *damage_operation, moonmod::Json::array({{{"amount", 25}}}));
    ASSERT_EQ(damaged.status, moonmod::ApiCallStatus::Ok) << damaged.message;
    EXPECT_FLOAT_EQ(damaged.value.value("damaged", 0.0f), 25.0f);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 40.0f);

    const moonmod::ApiCallResult clamped_damage = config.api_handler(
        "test.moonmod.player-probe", *damage_operation, moonmod::Json::array({{{"amount", 100}}}));
    ASSERT_EQ(clamped_damage.status, moonmod::ApiCallStatus::Ok) << clamped_damage.message;
    EXPECT_FLOAT_EQ(clamped_damage.value.value("damaged", 0.0f), 40.0f);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 0.0f);

    const moonmod::ApiCallResult invalid_damage = config.api_handler(
        "test.moonmod.player-probe", *damage_operation, moonmod::Json::array({{{"amount", 0}}}));
    EXPECT_EQ(invalid_damage.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 0.0f);

    const moonmod::ApiCallResult invalid_damage_target = config.api_handler(
        "test.moonmod.player-probe", *damage_operation,
        moonmod::Json::array({{{"amount", 5}, {"character", "townsfolk"}}}));
    EXPECT_EQ(invalid_damage_target.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(user_data.GetHp(USER_CHARA_MAX), 0.0f);

    user_data.SetActiveChrNo(USER_CHARA_ROBO);
    user_data.robo_data.hp.max = 80.0f;
    user_data.robo_data.hp.now = 20.0f;
    const moonmod::ApiCallResult ridepod = config.api_handler("test.moonmod.player-probe", *get_operation,
                                                              moonmod::Json::array());
    ASSERT_EQ(ridepod.status, moonmod::ApiCallStatus::Ok) << ridepod.message;
    EXPECT_EQ(ridepod.value.value("character", ""), "ridepod");
    EXPECT_FLOAT_EQ(ridepod.value.value("hp", 0.0f), 20.0f);

    PortActiveSaveData = previous_save;
}

TEST(MoonModPlayerApi, IncludesNativePositionInActiveDungeonPlayerSnapshot) {
    struct Restore {
        int loop;
        CActionChara *player;
        CSaveData *save;
        ~Restore() {
            MainChara__2 = player;
            PortLoopNo = loop;
            PortActiveSaveData = save;
        }
    } restore{PortLoopNo, MainChara__2, PortActiveSaveData};
    CActionChara player_character;
    player_character.SetPosition(11.0f, 22.0f, 33.0f);
    MainChara__2 = &player_character;
    PortLoopNo = LOOP_DUNGEON;

    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.SetActiveChrNo(USER_CHARA_MAX);
    CHARA_DATA *max = user_data.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(max, nullptr);
    max->hp.max = 100.0f;
    max->hp.now = 40.0f;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("player.get");
    ASSERT_NE(get, nullptr);
    const moonmod::ApiCallResult snapshot = config.api_handler(
        "test.moonmod.chronicle-two-player-position", *get, moonmod::Json::array());
    ASSERT_EQ(snapshot.status, moonmod::ApiCallStatus::Ok) << snapshot.message;
    ASSERT_TRUE(snapshot.value.contains("position"));
    EXPECT_FLOAT_EQ(snapshot.value.at("position").at("x").get<float>(), 11.0f);
    EXPECT_FLOAT_EQ(snapshot.value.at("position").at("y").get<float>(), 22.0f);
    EXPECT_FLOAT_EQ(snapshot.value.at("position").at("z").get<float>(), 33.0f);

}

TEST(MoonModPlayerApi, HealsAndDamagesNamedNativeHealthGaugesWithoutSwitchingActiveCharacter) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.SetActiveChrNo(USER_CHARA_MAX);

    CHARA_DATA *monica = user_data.GetCharaDataPtr(USER_CHARA_MONICA);
    ASSERT_NE(monica, nullptr);
    monica->hp.max = 150.0f;
    monica->hp.now = 50.0f;
    user_data.robo_data.hp.max = 80.0f;
    user_data.robo_data.hp.now = 20.0f;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *heal = moonmod::FindApiOperation("player.heal");
    const moonmod::ApiOperationSpec *damage = moonmod::FindApiOperation("player.damage");
    ASSERT_NE(heal, nullptr);
    ASSERT_NE(damage, nullptr);

    const moonmod::ApiCallResult monica_heal = config.api_handler(
        "test.moonmod.player-probe", *heal,
        moonmod::Json::array({{{"amount", 20}, {"character", "monica"}}}));
    ASSERT_EQ(monica_heal.status, moonmod::ApiCallStatus::Ok) << monica_heal.message;
    EXPECT_EQ(monica_heal.value.value("character", ""), "monica");
    EXPECT_FLOAT_EQ(monica_heal.value.value("healed", 0.0f), 20.0f);
    EXPECT_FLOAT_EQ(monica->hp.now, 70.0f);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    const moonmod::ApiCallResult ridepod_damage = config.api_handler(
        "test.moonmod.player-probe", *damage,
        moonmod::Json::array({{{"amount", 5}, {"character", "ridepod"}}}));
    ASSERT_EQ(ridepod_damage.status, moonmod::ApiCallStatus::Ok) << ridepod_damage.message;
    EXPECT_EQ(ridepod_damage.value.value("character", ""), "ridepod");
    EXPECT_FLOAT_EQ(ridepod_damage.value.value("damaged", 0.0f), 5.0f);
    EXPECT_FLOAT_EQ(user_data.robo_data.hp.now, 15.0f);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    const moonmod::ApiCallResult invalid_target = config.api_handler(
        "test.moonmod.player-probe", *heal,
        moonmod::Json::array({{{"amount", 10}, {"character", "monster"}}}));
    EXPECT_EQ(invalid_target.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(monica->hp.now, 70.0f);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    PortActiveSaveData = previous_save;
}

TEST(MoonModPlayerApi, SetsExactHpForActiveAndNamedCharactersWithinNativeRange) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.SetActiveChrNo(USER_CHARA_MAX);
    CHARA_DATA *max = user_data.GetCharaDataPtr(USER_CHARA_MAX);
    CHARA_DATA *monica = user_data.GetCharaDataPtr(USER_CHARA_MONICA);
    ASSERT_NE(max, nullptr);
    ASSERT_NE(monica, nullptr);
    max->hp.max = 100.0f;
    max->hp.now = 40.0f;
    monica->hp.max = 150.0f;
    monica->hp.now = 70.0f;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *set = moonmod::FindApiOperation("player.set");
    ASSERT_NE(set, nullptr);

    const moonmod::ApiCallResult active = config.api_handler(
        "test.moonmod.player-set", *set, moonmod::Json::array({{{"hp", 0}}}));
    ASSERT_EQ(active.status, moonmod::ApiCallStatus::Ok) << active.message;
    EXPECT_FLOAT_EQ(active.value.value("previous_hp", -1.0f), 40.0f);
    EXPECT_FLOAT_EQ(active.value.value("hp", -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(max->hp.now, 0.0f);

    const moonmod::ApiCallResult named = config.api_handler(
        "test.moonmod.player-set", *set,
        moonmod::Json::array({{{"hp", 125}, {"character", "monica"}}}));
    ASSERT_EQ(named.status, moonmod::ApiCallStatus::Ok) << named.message;
    EXPECT_EQ(named.value.value("character", ""), "monica");
    EXPECT_FLOAT_EQ(monica->hp.now, 125.0f);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    const moonmod::ApiCallResult too_high = config.api_handler(
        "test.moonmod.player-set", *set,
        moonmod::Json::array({{{"hp", 151}, {"character", "monica"}}}));
    EXPECT_EQ(too_high.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(monica->hp.now, 125.0f);
    const moonmod::ApiCallResult unsupported_field = config.api_handler(
        "test.moonmod.player-set", *set,
        moonmod::Json::array({{{"max_hp", 200}}}));
    EXPECT_EQ(unsupported_field.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult fractional = config.api_handler(
        "test.moonmod.player-set", *set,
        moonmod::Json::array({{{"hp", 10.5}}}));
    EXPECT_EQ(fractional.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(monica->hp.now, 125.0f);

    PortActiveSaveData = previous_save;
}

TEST(MoonModPlayerApi, AppliesNativeStatusRulesAndClearsTimedEffects) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.SetActiveChrNo(USER_CHARA_MAX);
    CHARA_DATA *max = user_data.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(max, nullptr);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *add = moonmod::FindApiOperation("player.status_add");
    const moonmod::ApiOperationSpec *remove = moonmod::FindApiOperation("player.status_remove");
    ASSERT_NE(add, nullptr);
    ASSERT_NE(remove, nullptr);

    const moonmod::ApiCallResult missing_duration = config.api_handler(
        "test.moonmod.status", *add, moonmod::Json::array({{{"status", "slow"}}}));
    EXPECT_EQ(missing_duration.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult power = config.api_handler(
        "test.moonmod.status", *add,
        moonmod::Json::array({{{"status", "power"}, {"duration_frames", 300}}}));
    ASSERT_EQ(power.status, moonmod::ApiCallStatus::Ok) << power.message;
    EXPECT_EQ(power.value.value("character", ""), "max");
    EXPECT_EQ(power.value.value("duration_frames", -1), 300);
    EXPECT_NE(max->status_attr & CHARA_STATUS_POWER, 0);
    EXPECT_EQ(max->status_time[0], 300);

    const moonmod::ApiCallResult resisted_slow = config.api_handler(
        "test.moonmod.status", *add,
        moonmod::Json::array({{{"status", "slow"}, {"duration_frames", 90}}}));
    EXPECT_EQ(resisted_slow.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(max->status_attr & CHARA_STATUS_SLOW, 0);
    EXPECT_EQ(max->status_time[1], 0);

    const moonmod::ApiCallResult resisted = config.api_handler(
        "test.moonmod.status", *add, moonmod::Json::array({{{"status", "poison"}}}));
    EXPECT_EQ(resisted.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(max->status_attr & CHARA_STATUS_POISON, 0);

    const moonmod::ApiCallResult cleared = config.api_handler(
        "test.moonmod.status", *remove, moonmod::Json::array({{{"status", "power"}}}));
    ASSERT_EQ(cleared.status, moonmod::ApiCallStatus::Ok) << cleared.message;
    EXPECT_TRUE(cleared.value.value("removed", false));
    EXPECT_FALSE(cleared.value.value("active", true));
    EXPECT_EQ(max->status_attr & CHARA_STATUS_POWER, 0);
    EXPECT_EQ(max->status_time[0], 0);

    const moonmod::ApiCallResult slow = config.api_handler(
        "test.moonmod.status", *add,
        moonmod::Json::array({{{"status", "slow"}, {"character", "monica"}, {"duration_frames", 120}}}));
    ASSERT_EQ(slow.status, moonmod::ApiCallStatus::Ok) << slow.message;
    CHARA_DATA *monica = user_data.GetCharaDataPtr(USER_CHARA_MONICA);
    ASSERT_NE(monica, nullptr);
    EXPECT_NE(monica->status_attr & CHARA_STATUS_SLOW, 0);
    EXPECT_EQ(monica->status_time[1], 120);

    const moonmod::ApiCallResult slow_removed = config.api_handler(
        "test.moonmod.status", *remove,
        moonmod::Json::array({{{"status", "slow"}, {"character", "monica"}}}));
    ASSERT_EQ(slow_removed.status, moonmod::ApiCallStatus::Ok) << slow_removed.message;
    EXPECT_EQ(monica->status_attr & CHARA_STATUS_SLOW, 0);
    EXPECT_EQ(monica->status_time[1], 0);

    const moonmod::ApiCallResult invalid_target = config.api_handler(
        "test.moonmod.status", *add,
        moonmod::Json::array({{{"status", "curse"}, {"character", "ridepod"}}}));
    EXPECT_EQ(invalid_target.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(max->status_attr & CHARA_STATUS_CURSE, 0);

    PortActiveSaveData = previous_save;
}
