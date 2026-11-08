#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "gamedata.hpp"
#include "moonmod_item_table_fixture.hpp"
#include "scriptinterpreter.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModWeaponsApi, ListsNativeDefinitionsAndReadsOwnedWeaponState) {
    GameItemDataManage.Initialize();

    constexpr int item_no = 0x50;
    SPI_STACK common_count{};
    common_count.type = SPI_STACK_TYPE_INT;
    common_count.value.integer = 1;
    ASSERT_EQ(moonmod_test::ItemTableInit(&common_count, 1), 1);
    SPI_STACK common_fields[11]{};
    const int common_values[] = {item_no, ITEM_DATA_MAX_MELEE, 0, 1, 1, 144, 4, 23, 300};
    for (std::size_t index = 0; index < sizeof(common_values) / sizeof(common_values[0]); ++index) {
        common_fields[index].type = SPI_STACK_TYPE_INT;
        common_fields[index].value.integer = common_values[index];
    }
    common_fields[9].type = SPI_STACK_TYPE_STRING;
    common_fields[9].value.string = const_cast<char *>("PROBE_SWORD");
    common_fields[10].type = SPI_STACK_TYPE_INT;
    common_fields[10].value.integer = ITEM_ATTRIBUTE_UNIQUE;
    ASSERT_EQ(moonmod_test::ItemTableAdd(common_fields, 11), 1);

    CDataCommon &common = GameItemDataManage.common_data[0];
    std::memset(&common, 0, sizeof(common));
    common.type = ITEM_DATA_MAX_MELEE;
    common.item_no = item_no;
    common.list_no = 0;
    common.icon_no = 23;
    common.message_no = 300;
    common.max_num = 144;
    std::strcpy(common.file_name, "PROBE_SWORD");
    common.stack_num = 1;
    common.attribute = ITEM_ATTRIBUTE_UNIQUE;
    common.name = const_cast<char *>("Probe Sword");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.weapon_num = 1;
    GameItemDataManage.max_item_no = item_no;

    CDataWeapon &definition = GameItemDataManage.weapon_data[0];
    definition.durability = 88;
    definition.levelup_exp = 27;
    definition.status[0] = 12;
    definition.status[1] = 34;
    definition.status_max[0] = 99;
    definition.status_max[1] = 120;
    definition.attribute[0] = 5;
    definition.attribute_max[0] = 50;
    definition.special = WEAPON_SPECIAL_POISON;
    definition.initial_fusion_point = 3;
    definition.fusion_point = 4;
    definition.buildup_weapon[0] = 0x51;
    definition.buildup_weapon[1] = -1;
    definition.buildup_weapon[2] = 0;
    definition.buildup_monster[0] = 7;
    definition.buildup_monster[1] = -1;
    definition.buildup_monster[2] = 0;
    definition.attack_type = WEAPON_ATTACK_GUN;
    definition.model_no = 6;
    definition.pallet_color = 2;

    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CGameDataUsed *owned = save->user_data.GetUsedDataPtr(0);
    ASSERT_NE(owned, nullptr);
    owned->used_type = USED_ITEM_TYPE_WEAPON;
    owned->item_no = item_no;
    owned->item_type = ITEM_DATA_MAX_MELEE;
    owned->data.weapon.whp.max = 88.0f;
    owned->data.weapon.whp.now = 61.0f;
    owned->data.weapon.abs.max = 100.0f;
    owned->data.weapon.abs.now = 42.0f;
    owned->data.weapon.level = 6;
    owned->data.weapon.status[0] = 19;
    owned->data.weapon.status[1] = 39;
    owned->data.weapon.attribute[0] = 8;
    owned->data.weapon.special = WEAPON_SPECIAL_POISON | WEAPON_SPECIAL_CRITICAL;
    owned->data.weapon.fusion_point = 11;
    std::strcpy(owned->data.weapon.name, "Storm Edge");

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const auto *list_operation = moonmod::FindApiOperation("weapons.list");
    const auto *get_operation = moonmod::FindApiOperation("weapons.get");
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(get_operation, nullptr);

    const moonmod::ApiCallResult listed = config.api_handler("test.weapon-probe", *list_operation,
                                                              moonmod::Json::array());
    ASSERT_EQ(listed.status, moonmod::ApiCallStatus::Ok) << listed.message;
    ASSERT_EQ(listed.value.size(), 1u);
    const moonmod::Json &listed_weapon = listed.value[0];
    EXPECT_EQ(listed_weapon.value("id", ""), "chronicle-two:weapon:80");
    EXPECT_EQ(listed_weapon.value("name", ""), "Probe Sword");
    EXPECT_EQ(listed_weapon.value("model_key", ""), "PROBE_SWORD");
    EXPECT_EQ(listed_weapon.value("icon_no", -1), 23);
    EXPECT_EQ(listed_weapon.value("durability_capacity", -1), 88);
    EXPECT_EQ(listed_weapon.at("stats").at("attack"), 12);
    EXPECT_EQ(listed_weapon.at("stats").at("durability"), 34);
    EXPECT_EQ(listed_weapon.at("stats").at("attributes")[0], 5);
    EXPECT_EQ(listed_weapon.at("stat_caps").at("attributes")[0], 50);
    EXPECT_EQ(listed_weapon.value("special_mask", 0u), WEAPON_SPECIAL_POISON);
    ASSERT_EQ(listed_weapon.at("builds_into").size(), 1u);
    EXPECT_EQ(listed_weapon.at("builds_into")[0], 0x51);
    ASSERT_EQ(listed_weapon.at("required_monster_defeats").size(), 2u);

    const moonmod::ApiCallResult read = config.api_handler(
        "test.weapon-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}, {"item_no", item_no}}}));
    ASSERT_EQ(read.status, moonmod::ApiCallStatus::Ok) << read.message;
    EXPECT_EQ(read.value.value("owned_count", 0), 1);
    const moonmod::Json &state = read.value.at("state");
    EXPECT_EQ(state.value("level", 0), 6);
    EXPECT_FLOAT_EQ(state.at("durability").at("current").get<float>(), 61.0f);
    EXPECT_FLOAT_EQ(state.at("durability").at("maximum").get<float>(), 88.0f);
    EXPECT_FLOAT_EQ(state.at("absorption").at("current").get<float>(), 42.0f);
    EXPECT_EQ(state.at("stats")[0], 19);
    EXPECT_EQ(state.at("attributes")[0], 8);
    EXPECT_EQ(state.value("fusion_points", 0), 11);
    EXPECT_EQ(state.value("custom_name", ""), "Storm Edge");

    const moonmod::ApiOperationSpec *equip_operation = moonmod::FindApiOperation("weapons.equip");
    const moonmod::ApiOperationSpec *upgrade_operation = moonmod::FindApiOperation("weapons.upgrade");
    ASSERT_NE(equip_operation, nullptr);
    ASSERT_NE(upgrade_operation, nullptr);
    const auto upgrade_capability = std::ranges::find(config.api_capabilities, "weapons.upgrade",
                                                       &moonmod::ApiCapability::path);
    ASSERT_NE(upgrade_capability, config.api_capabilities.end());
    EXPECT_TRUE(upgrade_capability->supported);
    EXPECT_TRUE(upgrade_capability->reason.empty());

    const moonmod::ApiCallResult not_ready = config.api_handler(
        "test.weapon-probe", *upgrade_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}}}));
    ASSERT_EQ(not_ready.status, moonmod::ApiCallStatus::Ok) << not_ready.message;
    EXPECT_FALSE(not_ready.value.value("ready", true));
    EXPECT_FALSE(not_ready.value.value("upgraded", true));
    EXPECT_FALSE(not_ready.value.value("changed", true));
    EXPECT_EQ(not_ready.value.value("level", 0), 6);

    const moonmod::ApiCallResult equip_result = config.api_handler(
        "test.weapon-probe", *equip_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}, {"character", "max"}}}));
    ASSERT_EQ(equip_result.status, moonmod::ApiCallStatus::Ok) << equip_result.message;
    EXPECT_EQ(equip_result.value.value("id", ""), "chronicle-two:weapon:80");
    EXPECT_EQ(equip_result.value.value("native_item_no", -1), item_no);
    EXPECT_EQ(equip_result.value.value("character", ""), "max");
    EXPECT_TRUE(equip_result.value.value("equipped", false));
    EXPECT_TRUE(equip_result.value.value("changed", false));
    EXPECT_EQ(save->user_data.chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_MELEE].item_no, item_no);

    const moonmod::ApiCallResult already_equipped = config.api_handler(
        "test.weapon-probe", *equip_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}, {"character", "max"}}}));
    EXPECT_EQ(already_equipped.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult unsupported_character = config.api_handler(
        "test.weapon-probe", *equip_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}, {"character", "ridepod"}}}));
    EXPECT_EQ(unsupported_character.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult equipped_read = config.api_handler(
        "test.weapon-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"item_no", item_no}}}));
    ASSERT_EQ(equipped_read.status, moonmod::ApiCallStatus::Ok) << equipped_read.message;
    EXPECT_EQ(equipped_read.value.value("owned_count", 0), 1);
    EXPECT_EQ(equipped_read.value.at("state").value("custom_name", ""), "Storm Edge");

    CGameDataUsed &equipped_weapon = save->user_data.chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_MELEE];
    equipped_weapon.data.weapon.abs.now = equipped_weapon.data.weapon.abs.max;
    const moonmod::ApiCallResult upgraded = config.api_handler(
        "test.weapon-probe", *upgrade_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}}}));
    ASSERT_EQ(upgraded.status, moonmod::ApiCallStatus::Ok) << upgraded.message;
    EXPECT_TRUE(upgraded.value.value("ready", false));
    EXPECT_TRUE(upgraded.value.value("upgraded", false));
    EXPECT_TRUE(upgraded.value.value("changed", false));
    EXPECT_EQ(upgraded.value.value("previous_level", 0), 6);
    EXPECT_EQ(upgraded.value.value("level", 0), 7);
    EXPECT_EQ(upgraded.value.at("stats").value("attack", 0), 20);
    EXPECT_EQ(upgraded.value.at("stats").value("durability", 0), 40);
    EXPECT_EQ(upgraded.value.value("fusion_points", 0), 15);
    EXPECT_FLOAT_EQ(equipped_weapon.data.weapon.abs.now, 0.0f);
    EXPECT_EQ(equipped_weapon.data.weapon.abs.max, 105.0f);
    EXPECT_GE(equipped_weapon.data.weapon.whp.max, 89.0f);
    EXPECT_LE(equipped_weapon.data.weapon.whp.max, 91.0f);

    const moonmod::ApiCallResult repeated_upgrade = config.api_handler(
        "test.weapon-probe", *upgrade_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:80"}}}));
    ASSERT_EQ(repeated_upgrade.status, moonmod::ApiCallStatus::Ok) << repeated_upgrade.message;
    EXPECT_FALSE(repeated_upgrade.value.value("upgraded", true));
    EXPECT_FALSE(repeated_upgrade.value.value("changed", true));

    const moonmod::ApiCallResult missing_upgrade = config.api_handler(
        "test.weapon-probe", *upgrade_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:weapon:96"}}}));
    EXPECT_EQ(missing_upgrade.status, moonmod::ApiCallStatus::Failed);
    const moonmod::ApiCallResult invalid_upgrade = config.api_handler(
        "test.weapon-probe", *upgrade_operation, moonmod::Json::array({moonmod::Json::object()}));
    EXPECT_EQ(invalid_upgrade.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult bad_list = config.api_handler(
        "test.weapon-probe", *list_operation, moonmod::Json::array({moonmod::Json::object()}));
    EXPECT_EQ(bad_list.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult bad_selector = config.api_handler(
        "test.weapon-probe", *get_operation, moonmod::Json::array({moonmod::Json{{"item_no", 0}}}));
    EXPECT_EQ(bad_selector.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult missing = config.api_handler(
        "test.weapon-probe", *get_operation, moonmod::Json::array({moonmod::Json{{"item_no", 0x60}}}));
    EXPECT_EQ(missing.status, moonmod::ApiCallStatus::Failed);

    PortActiveSaveData = previous_save;
    GameItemDataManage.Initialize();
}
