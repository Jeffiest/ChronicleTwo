#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "gamedata.hpp"
#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModWeaponRegistration, AddsNamedWeaponWithNativeAssetsAndOverriddenStats) {
    moonmod::ResetChronicleTwoContentForTests();
    GameItemDataManage.Initialize();

    CDataCommon &vanilla_common = GameItemDataManage.common_data[0];
    std::memset(&vanilla_common, 0, sizeof(vanilla_common));
    vanilla_common.type = ITEM_DATA_MAX_MELEE;
    vanilla_common.item_no = 0x20;
    vanilla_common.list_no = 0;
    vanilla_common.icon_no = 11;
    vanilla_common.message_no = 50;
    vanilla_common.max_num = 1;
    std::strcpy(vanilla_common.file_name, "VANILLA_SWORD");
    vanilla_common.active_set = 1;
    vanilla_common.stack_num = 1;
    vanilla_common.icon_texture_no = 4;
    vanilla_common.attribute = 0x1234;
    vanilla_common.name = const_cast<char *>("Vanilla Sword");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.max_item_no = vanilla_common.item_no;

    CDataWeapon &vanilla_weapon = GameItemDataManage.weapon_data[0];
    vanilla_weapon.durability = 16;
    vanilla_weapon.levelup_exp = 24;
    vanilla_weapon.status[0] = 7;
    vanilla_weapon.status[1] = 90;
    vanilla_weapon.status_max[0] = 80;
    vanilla_weapon.status_max[1] = 400;
    vanilla_weapon.attribute[0] = 2;
    vanilla_weapon.attribute_max[0] = 40;
    vanilla_weapon.special = 0x10;
    vanilla_weapon.attack_type = 3;
    vanilla_weapon.model_no = 2;
    vanilla_weapon.pallet_color = 5;
    vanilla_weapon.buildup_weapon[0] = 0x21;
    GameItemDataManage.weapon_num = 1;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("weapons.register");
    ASSERT_NE(register_operation, nullptr);

    const moonmod::Json invalid_assets = {
        {"id", "moonblade"}, {"name", "Moonblade"}, {"template_weapon", 0x20},
        {"assets", {{"model", "mods/moonblade.chr"}}},
    };
    const moonmod::ApiCallResult asset_result = config.api_handler(
        "test.moonmod.weapon-probe", *register_operation, moonmod::Json::array({invalid_assets}));
    EXPECT_EQ(asset_result.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::Json definition = {
        {"id", "moonblade"},
        {"name", "Moonblade"},
        {"template_weapon", 0x20},
        {"max_count", 1},
        {"stack_size", 1},
        {"durability_capacity", 31},
        {"level_up_exp", 40},
        {"stats", {{"attack", 18}, {"durability", 125}, {"attributes", {3, 4, 5, 6, 7, 8, 9, 10}}}},
        {"stat_caps", {{"attack", 120}, {"durability", 500}, {"attributes", {50, 51, 52, 53, 54, 55, 56, 57}}}},
        {"assets", moonmod::Json::object()},
    };
    const moonmod::ApiCallResult registration =
        config.api_handler("test.moonmod.weapon-probe", *register_operation, moonmod::Json::array({definition}));
    ASSERT_EQ(registration.status, moonmod::ApiCallStatus::Ok) << registration.message;
    EXPECT_EQ(registration.value.value("id", ""), "test.moonmod.weapon-probe:moonblade");
    EXPECT_EQ(registration.value.value("status", ""), "pending");

    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("weapons.get");
    ASSERT_NE(get_operation, nullptr);
    const moonmod::ApiCallResult pending_get = config.api_handler(
        "test.moonmod.weapon-probe", *get_operation,
        moonmod::Json::array({moonmod::Json("test.moonmod.weapon-probe:moonblade")}));
    EXPECT_EQ(pending_get.status, moonmod::ApiCallStatus::PhaseViolation);

    moonmod::ApplyChronicleTwoContent();
    const moonmod::ApiCallResult weapon_result = config.api_handler(
        "test.moonmod.weapon-probe", *get_operation,
        moonmod::Json::array({moonmod::Json("test.moonmod.weapon-probe:moonblade")}));
    ASSERT_EQ(weapon_result.status, moonmod::ApiCallStatus::Ok) << weapon_result.message;
    EXPECT_EQ(weapon_result.value.value("id", ""), "test.moonmod.weapon-probe:moonblade");
    EXPECT_TRUE(weapon_result.value.value("mod_owned", false));
    EXPECT_EQ(weapon_result.value.value("status", ""), "registered");
    ASSERT_TRUE(weapon_result.value.at("native_item_no").is_number_integer());
    const int item_no = weapon_result.value.at("native_item_no").get<int>();
    EXPECT_NE(item_no, vanilla_common.item_no);

    CDataCommon *registered_common = GetCommonItemData(item_no);
    ASSERT_NE(registered_common, nullptr);
    EXPECT_STREQ(registered_common->name, "Moonblade");
    EXPECT_EQ(registered_common->type, vanilla_common.type);
    EXPECT_EQ(registered_common->icon_no, vanilla_common.icon_no);
    EXPECT_EQ(registered_common->icon_texture_no, vanilla_common.icon_texture_no);
    EXPECT_STREQ(registered_common->file_name, vanilla_common.file_name);
    EXPECT_EQ(registered_common->attribute, vanilla_common.attribute);

    CDataWeapon *registered_weapon = GameItemDataManage.GetWeaponData(item_no);
    ASSERT_NE(registered_weapon, nullptr);
    EXPECT_EQ(registered_weapon->status[0], 18);
    EXPECT_EQ(registered_weapon->status[1], 125);
    EXPECT_EQ(registered_weapon->status_max[0], 120);
    EXPECT_EQ(registered_weapon->status_max[1], 500);
    EXPECT_EQ(registered_weapon->attribute[0], 3);
    EXPECT_EQ(registered_weapon->attribute[7], 10);
    EXPECT_EQ(registered_weapon->attribute_max[0], 50);
    EXPECT_EQ(registered_weapon->attribute_max[7], 57);
    EXPECT_EQ(registered_weapon->durability, 31);
    EXPECT_EQ(registered_weapon->levelup_exp, 40);
    EXPECT_EQ(registered_weapon->special, vanilla_weapon.special);
    EXPECT_EQ(registered_weapon->attack_type, vanilla_weapon.attack_type);
    EXPECT_EQ(registered_weapon->model_no, vanilla_weapon.model_no);
    EXPECT_EQ(registered_weapon->pallet_color, vanilla_weapon.pallet_color);
    EXPECT_EQ(registered_weapon->buildup_weapon[0], vanilla_weapon.buildup_weapon[0]);

    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("weapons.list");
    ASSERT_NE(list_operation, nullptr);
    const moonmod::ApiCallResult list_result = config.api_handler(
        "test.moonmod.weapon-probe", *list_operation, moonmod::Json::array());
    ASSERT_EQ(list_result.status, moonmod::ApiCallStatus::Ok) << list_result.message;
    bool found_mod_weapon = false;
    for (const moonmod::Json &listed_weapon : list_result.value) {
        if (listed_weapon.value("id", "") == "test.moonmod.weapon-probe:moonblade") found_mod_weapon = true;
    }
    EXPECT_TRUE(found_mod_weapon);

    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    const moonmod::ApiOperationSpec *give_operation = moonmod::FindApiOperation("items.give");
    ASSERT_NE(give_operation, nullptr);
    const moonmod::ApiCallResult give_result = config.api_handler(
        "test.moonmod.weapon-probe", *give_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.weapon-probe:moonblade"}, {"count", 1}}}));
    ASSERT_EQ(give_result.status, moonmod::ApiCallStatus::Ok) << give_result.message;
    EXPECT_EQ(give_result.value.value("added", 0), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 1);

    const moonmod::ApiCallResult owned_weapon_result = config.api_handler(
        "test.moonmod.weapon-probe", *get_operation,
        moonmod::Json::array({moonmod::Json("test.moonmod.weapon-probe:moonblade")}));
    ASSERT_EQ(owned_weapon_result.status, moonmod::ApiCallStatus::Ok) << owned_weapon_result.message;
    EXPECT_EQ(owned_weapon_result.value.value("owned_count", 0), 1);
    EXPECT_TRUE(owned_weapon_result.value.at("state").is_object());

    const moonmod::ApiOperationSpec *remove_operation = moonmod::FindApiOperation("items.remove");
    ASSERT_NE(remove_operation, nullptr);
    const moonmod::ApiCallResult remove_result = config.api_handler(
        "test.moonmod.weapon-probe", *remove_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.weapon-probe:moonblade"}, {"count", 1}}}));
    ASSERT_EQ(remove_result.status, moonmod::ApiCallStatus::Ok) << remove_result.message;
    EXPECT_EQ(remove_result.value.value("removed", 0), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 0);

    const moonmod::ApiOperationSpec *weapons_give_operation = moonmod::FindApiOperation("weapons.give");
    ASSERT_NE(weapons_give_operation, nullptr);
    const auto weapons_give_capability = std::ranges::find(config.api_capabilities, "weapons.give",
                                                           &moonmod::ApiCapability::path);
    ASSERT_NE(weapons_give_capability, config.api_capabilities.end());
    EXPECT_TRUE(weapons_give_capability->supported);
    const moonmod::ApiCallResult weapons_give_result = config.api_handler(
        "test.moonmod.weapon-probe", *weapons_give_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.weapon-probe:moonblade"}}}));
    ASSERT_EQ(weapons_give_result.status, moonmod::ApiCallStatus::Ok) << weapons_give_result.message;
    EXPECT_EQ(weapons_give_result.value.value("native_item_no", -1), item_no);
    EXPECT_EQ(weapons_give_result.value.value("added", 0), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 1);

    PortActiveSaveData = previous_save;
    moonmod::ResetChronicleTwoContentForTests();
}
