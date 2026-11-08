#include <gtest/gtest.h>

#include <memory>

#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModPartyApi, ReadsNativePartyMaskAndActiveCharacter) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.party_member = (1 << USER_CHARA_MAX) | (1 << USER_CHARA_MONICA) | (1 << USER_CHARA_ROBO);
    user_data.SetActiveChrNo(USER_CHARA_MONICA);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("party.list");
    const moonmod::ApiOperationSpec *active_operation = moonmod::FindApiOperation("party.active");
    const moonmod::ApiOperationSpec *characters_list = moonmod::FindApiOperation("characters.list");
    const moonmod::ApiOperationSpec *characters_get = moonmod::FindApiOperation("characters.get");
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(active_operation, nullptr);
    ASSERT_NE(characters_list, nullptr);
    ASSERT_NE(characters_get, nullptr);

    const moonmod::ApiCallResult party = config.api_handler("test.moonmod.party-probe", *list_operation,
                                                            moonmod::Json::array());
    ASSERT_EQ(party.status, moonmod::ApiCallStatus::Ok) << party.message;
    ASSERT_EQ(party.value.size(), 3u);
    EXPECT_EQ(party.value[0].value("id", ""), "max");
    EXPECT_FALSE(party.value[0].value("active", true));
    EXPECT_EQ(party.value[1].value("id", ""), "monica");
    EXPECT_TRUE(party.value[1].value("active", false));
    EXPECT_EQ(party.value[2].value("id", ""), "ridepod");
    EXPECT_FALSE(party.value[2].value("active", true));

    const moonmod::ApiCallResult active = config.api_handler("test.moonmod.party-probe", *active_operation,
                                                             moonmod::Json::array());
    ASSERT_EQ(active.status, moonmod::ApiCallStatus::Ok) << active.message;
    EXPECT_EQ(active.value.value("id", ""), "monica");
    EXPECT_EQ(active.value.value("character_id", -1), USER_CHARA_MONICA);
    EXPECT_TRUE(active.value.value("active", false));

    user_data.GetCharaHpGage(USER_CHARA_MAX)->now = 75.0f;
    user_data.GetCharaHpGage(USER_CHARA_MAX)->max = 100.0f;
    user_data.GetCharaHpGage(USER_CHARA_MONICA)->now = 55.0f;
    user_data.GetCharaHpGage(USER_CHARA_MONICA)->max = 90.0f;
    user_data.GetCharaHpGage(USER_CHARA_ROBO)->now = 30.0f;
    user_data.GetCharaHpGage(USER_CHARA_ROBO)->max = 80.0f;

    const moonmod::ApiCallResult characters = config.api_handler(
        "test.moonmod.party-probe", *characters_list, moonmod::Json::array());
    ASSERT_EQ(characters.status, moonmod::ApiCallStatus::Ok) << characters.message;
    ASSERT_EQ(characters.value.size(), 4u);
    EXPECT_EQ(characters.value[0].value("id", ""), "max");
    EXPECT_TRUE(characters.value[0].value("in_party", false));
    EXPECT_EQ(characters.value[0].value("hp", -1.0f), 75.0f);
    EXPECT_EQ(characters.value[1].value("id", ""), "monica");
    EXPECT_TRUE(characters.value[1].value("active", false));
    EXPECT_EQ(characters.value[2].value("id", ""), "ridepod");
    EXPECT_EQ(characters.value[2].value("kind", ""), "vehicle");
    EXPECT_EQ(characters.value[2].value("max_hp", -1.0f), 80.0f);
    EXPECT_EQ(characters.value[3].value("id", ""), "monster");
    EXPECT_EQ(characters.value[3].value("kind", ""), "transformation");

    const moonmod::ApiCallResult character = config.api_handler(
        "test.moonmod.party-probe", *characters_get, moonmod::Json::array({{{"id", "ridepod"}}}));
    ASSERT_EQ(character.status, moonmod::ApiCallStatus::Ok) << character.message;
    EXPECT_EQ(character.value.value("character_id", -1), USER_CHARA_ROBO);
    EXPECT_EQ(character.value.value("hp", -1.0f), 30.0f);
    const moonmod::ApiCallResult invalid_character = config.api_handler(
        "test.moonmod.party-probe", *characters_get, moonmod::Json::array({"npc:unknown"}));
    EXPECT_EQ(invalid_character.status, moonmod::ApiCallStatus::InvalidArgument);

    PortActiveSaveData = previous_save;
}

TEST(MoonModPartyApi, ChangesActiveCharacterThroughNativeQuickChangeRules) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.party_member = (1 << USER_CHARA_MAX) | (1 << USER_CHARA_MONICA) | (1 << USER_CHARA_ROBO);
    user_data.chara_change = user_data.party_member;
    user_data.chara_change_mask = 0xFF;
    user_data.chara_data[USER_CHARA_MAX].hp.now = 80.0f;
    user_data.chara_data[USER_CHARA_MAX].hp.max = 100.0f;
    user_data.chara_data[USER_CHARA_MONICA].hp.now = 70.0f;
    user_data.chara_data[USER_CHARA_MONICA].hp.max = 100.0f;
    user_data.robo_data.hp.now = 60.0f;
    user_data.robo_data.hp.max = 100.0f;
    user_data.SetActiveChrNo(USER_CHARA_MONICA);
    GetMainScene()->battle_area.floor_status = 0;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *set_active = moonmod::FindApiOperation("party.set_active");
    ASSERT_NE(set_active, nullptr);

    const moonmod::ApiCallResult switched = config.api_handler(
        "test.moonmod.party-switch", *set_active, moonmod::Json::array({{{"id", "max"}}}));
    ASSERT_EQ(switched.status, moonmod::ApiCallStatus::Ok) << switched.message;
    EXPECT_EQ(switched.value.value("previous", ""), "monica");
    EXPECT_EQ(switched.value.value("active", ""), "max");
    EXPECT_TRUE(switched.value.value("changed", false));
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    user_data.SetCharaStatusAttirbute(USER_CHARA_MAX, CHARA_STATUS_STOP, CHARA_STATUS_ATTR_MODE_ADD);
    const moonmod::ApiCallResult blocked = config.api_handler(
        "test.moonmod.party-switch", *set_active, moonmod::Json::array({{{"id", "monica"}}}));
    EXPECT_EQ(blocked.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);
    user_data.SetCharaStatusAttirbute(USER_CHARA_MAX, CHARA_STATUS_STOP, CHARA_STATUS_ATTR_MODE_CLEAR);

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.party-switch", *set_active, moonmod::Json::array({{{"id", "townsfolk"}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MAX);

    const moonmod::ApiCallResult switched_back = config.api_handler(
        "test.moonmod.party-switch", *set_active, moonmod::Json::array({{{"id", "monica"}}}));
    ASSERT_EQ(switched_back.status, moonmod::ApiCallStatus::Ok) << switched_back.message;
    EXPECT_EQ(user_data.GetActiveChrNo(), USER_CHARA_MONICA);

    PortActiveSaveData = previous_save;
}

TEST(MoonModPartyApi, RecruitsTownspersonAndReportsNativeCompanionReplacement) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.party_member = (1 << USER_CHARA_MAX) | (1 << USER_CHARA_MONICA) | (1 << USER_CHARA_ROBO);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *recruit = moonmod::FindApiOperation("party.recruit");
    const moonmod::ApiOperationSpec *list = moonmod::FindApiOperation("party.list");
    ASSERT_NE(recruit, nullptr);
    ASSERT_NE(list, nullptr);

    const moonmod::ApiCallResult first = config.api_handler(
        "test.moonmod.party-recruit", *recruit, moonmod::Json::array({{{"npc_id", 7}}}));
    ASSERT_EQ(first.status, moonmod::ApiCallStatus::Ok) << first.message;
    EXPECT_EQ(first.value.value("id", ""), "chronicle-two:npc:7");
    EXPECT_TRUE(first.value.value("recruited", false));
    EXPECT_TRUE(first.value.value("changed", false));
    EXPECT_EQ(user_data.GetPartyCharaStatus(7) & PARTY_CHARA_STATUS_IN_PARTY, PARTY_CHARA_STATUS_IN_PARTY);
    EXPECT_EQ(user_data.NowPartyCharaID(), 7);

    PARTY_CHARA_INFO *npc7 = user_data.GetPartyCharaInfo(7);
    ASSERT_NE(npc7, nullptr);
    npc7->point = 5;
    const moonmod::ApiCallResult repeated = config.api_handler(
        "test.moonmod.party-recruit", *recruit, moonmod::Json::array({{{"npc_id", 7}}}));
    ASSERT_EQ(repeated.status, moonmod::ApiCallStatus::Ok) << repeated.message;
    EXPECT_FALSE(repeated.value.value("recruited", true));
    EXPECT_FALSE(repeated.value.value("changed", true));
    EXPECT_TRUE(repeated.value.value("already_in_party", false));
    EXPECT_EQ(npc7->point, 5);

    const moonmod::ApiCallResult second = config.api_handler(
        "test.moonmod.party-recruit", *recruit, moonmod::Json::array({{{"npc_id", 8}}}));
    ASSERT_EQ(second.status, moonmod::ApiCallStatus::Ok) << second.message;
    EXPECT_EQ(second.value.value("replaced", ""), "chronicle-two:npc:7");
    EXPECT_EQ(user_data.GetPartyCharaStatus(7) & PARTY_CHARA_STATUS_JOINED, PARTY_CHARA_STATUS_JOINED);
    EXPECT_EQ(user_data.GetPartyCharaStatus(8) & PARTY_CHARA_STATUS_IN_PARTY, PARTY_CHARA_STATUS_IN_PARTY);
    EXPECT_EQ(user_data.NowPartyCharaID(), 8);

    const moonmod::ApiCallResult party = config.api_handler(
        "test.moonmod.party-recruit", *list, moonmod::Json::array());
    ASSERT_EQ(party.status, moonmod::ApiCallStatus::Ok) << party.message;
    ASSERT_EQ(party.value.size(), 5u);
    EXPECT_EQ(party.value[3].value("id", ""), "chronicle-two:npc:7");
    EXPECT_EQ(party.value[3].value("status", ""), "joined");
    EXPECT_EQ(party.value[4].value("id", ""), "chronicle-two:npc:8");
    EXPECT_EQ(party.value[4].value("status", ""), "in_party");

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.party-recruit", *recruit, moonmod::Json::array({{{"npc_id", 33}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(user_data.NowPartyCharaID(), 8);

    PortActiveSaveData = previous_save;
}
