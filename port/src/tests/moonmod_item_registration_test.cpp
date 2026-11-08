#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "moonmod_item_table_fixture.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "menucls1.hpp"
#include "scene.hpp"
#include "savedata.hpp"
#include "scriptinterpreter.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModItemRegistration, DataOnlyConsumableEntersNativeTablesAndInventory) {
    moonmod::ResetChronicleTwoContentForTests();
    GameItemDataManage.Initialize();

    SPI_STACK common_count{};
    common_count.type = SPI_STACK_TYPE_INT;
    common_count.value.integer = 1;
    ASSERT_EQ(moonmod_test::ItemTableInit(&common_count, 1), 1);
    SPI_STACK common_fields[11]{};
    const int common_values[] = {0x112, ITEM_DATA_RECOVERY, 0, 1, 10, 30, 3, 17, 100};
    for (std::size_t index = 0; index < sizeof(common_values) / sizeof(common_values[0]); ++index) {
        common_fields[index].type = SPI_STACK_TYPE_INT;
        common_fields[index].value.integer = common_values[index];
    }
    common_fields[9].type = SPI_STACK_TYPE_STRING;
    common_fields[9].value.string = const_cast<char *>("VANILLA_TONIC");
    common_fields[10].type = SPI_STACK_TYPE_INT;
    common_fields[10].value.integer = ITEM_ATTRIBUTE_USABLE;
    ASSERT_EQ(moonmod_test::ItemTableAdd(common_fields, 11), 1);

    CDataCommon &vanilla_common = GameItemDataManage.common_data[0];
    std::memset(&vanilla_common, 0, sizeof(vanilla_common));
    vanilla_common.type = ITEM_DATA_RECOVERY;
    vanilla_common.item_no = 0x112;
    vanilla_common.list_no = 0;
    vanilla_common.icon_no = 17;
    vanilla_common.message_no = 100;
    vanilla_common.max_num = 30;
    std::strcpy(vanilla_common.file_name, "VANILLA_TONIC");
    vanilla_common.active_set = 1;
    vanilla_common.stack_num = 10;
    vanilla_common.icon_texture_no = 3;
    vanilla_common.attribute = ITEM_ATTRIBUTE_USABLE;
    vanilla_common.name = const_cast<char *>("Vanilla Tonic");
    GameItemDataManage.common_num = 1;
    GameItemDataManage.max_item_no = vanilla_common.item_no;

    CDataItem &vanilla_item = GameItemDataManage.item_data[0];
    vanilla_item.action_flags = ITEM_ACTION_USE;
    vanilla_item.use_flags = ITEM_USE_FLAG_RESTORE_HP;
    vanilla_item.target_flags = ITEM_TARGET_MAX | ITEM_TARGET_MONICA;
    vanilla_item.value[0] = 8;
    vanilla_item.value[1] = 0;
    vanilla_item.value[2] = 0;
    GameItemDataManage.item_num = 1;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("items.register");
    const moonmod::ApiOperationSpec *unregister_operation = moonmod::FindApiOperation("content.unregister");
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(unregister_operation, nullptr);
    const moonmod::Json definition = {
        {"id", "starlight-tonic"},
        {"name", "Starlight Tonic"},
        {"kind", "consumable"},
        {"template_item", 0x112},
        {"max_count", 5},
        {"effect", {{"kind", "heal"}, {"amount", 25}}},
        {"assets", moonmod::Json::object()},
    };
    const moonmod::ApiCallResult registration =
        config.api_handler("test.moonmod.item-probe", *register_operation, moonmod::Json::array({definition}));
    ASSERT_EQ(registration.status, moonmod::ApiCallStatus::Ok) << registration.message;
    EXPECT_EQ(registration.value.value("id", ""), "test.moonmod.item-probe:starlight-tonic");
    EXPECT_EQ(registration.value.value("status", ""), "pending");

    moonmod::ApplyChronicleTwoContent();
    const moonmod::ApiCallResult unregister_materialized = config.api_handler(
        "test.moonmod.item-probe", *unregister_operation,
        moonmod::Json::array({{{"type", "item"}, {"id", "starlight-tonic"}}}));
    EXPECT_EQ(unregister_materialized.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(unregister_materialized.message.find("already materialized"), std::string::npos);
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("items.get");
    ASSERT_NE(get_operation, nullptr);
    const moonmod::ApiCallResult item_result = config.api_handler(
        "test.moonmod.item-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}}}));
    ASSERT_EQ(item_result.status, moonmod::ApiCallStatus::Ok) << item_result.message;
    ASSERT_TRUE(item_result.value.at("native_item_no").is_number_integer()) << item_result.value.dump();
    const int item_no = item_result.value.at("native_item_no").get<int>();
    ASSERT_NE(item_no, vanilla_common.item_no);
    EXPECT_EQ(item_result.value.value("status", ""), "registered");

    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("items.list");
    ASSERT_NE(list_operation, nullptr);
    const moonmod::ApiCallResult list_result = config.api_handler(
        "test.moonmod.item-probe", *list_operation, moonmod::Json::array());
    ASSERT_EQ(list_result.status, moonmod::ApiCallStatus::Ok) << list_result.message;
    bool listed_builtin = false;
    bool listed_mod_item = false;
    for (const moonmod::Json &listed_item : list_result.value) {
        if (listed_item.value("id", "") == "chronicle-two:item:274") listed_builtin = true;
        if (listed_item.value("id", "") == "test.moonmod.item-probe:starlight-tonic") listed_mod_item = true;
    }
    EXPECT_TRUE(listed_builtin);
    EXPECT_TRUE(listed_mod_item);

    const moonmod::ApiCallResult builtin_result = config.api_handler(
        "test.moonmod.item-probe", *get_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:item:274"}}}));
    ASSERT_EQ(builtin_result.status, moonmod::ApiCallStatus::Ok) << builtin_result.message;
    EXPECT_EQ(builtin_result.value.value("native_item_no", 0), vanilla_common.item_no);
    EXPECT_FALSE(builtin_result.value.value("mod_owned", true));

    CDataCommon *registered_common = GetCommonItemData(item_no);
    ASSERT_NE(registered_common, nullptr);
    EXPECT_STREQ(registered_common->name, "Starlight Tonic");
    EXPECT_EQ(registered_common->type, vanilla_common.type);
    EXPECT_EQ(registered_common->icon_no, vanilla_common.icon_no);
    EXPECT_EQ(registered_common->icon_texture_no, vanilla_common.icon_texture_no);
    EXPECT_STREQ(registered_common->file_name, vanilla_common.file_name);
    EXPECT_EQ(registered_common->max_num, 5);
    EXPECT_EQ(registered_common->stack_num, 5);

    CDataItem *registered_item = GetItemInfoData(item_no);
    ASSERT_NE(registered_item, nullptr);
    EXPECT_EQ(registered_item->action_flags, vanilla_item.action_flags);
    EXPECT_EQ(registered_item->use_flags & ITEM_USE_FLAG_RESTORE_HP, ITEM_USE_FLAG_RESTORE_HP);
    EXPECT_EQ(registered_item->value[0], 25);
    EXPECT_EQ(vanilla_item.value[0], 8);

    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    const moonmod::ApiOperationSpec *give_operation = moonmod::FindApiOperation("items.give");
    ASSERT_NE(give_operation, nullptr);
    const moonmod::ApiCallResult give_result = config.api_handler(
        "test.moonmod.item-probe", *give_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}, {"count", 2}}}));
    ASSERT_EQ(give_result.status, moonmod::ApiCallStatus::Ok) << give_result.message;
    EXPECT_EQ(give_result.value.value("added", 0), 2);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 2);

    const moonmod::ApiCallResult builtin_give_result = config.api_handler(
        "test.moonmod.item-probe", *give_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:item:274"}, {"count", 3}}}));
    ASSERT_EQ(builtin_give_result.status, moonmod::ApiCallStatus::Ok) << builtin_give_result.message;
    EXPECT_EQ(builtin_give_result.value.value("added", 0), 3);
    EXPECT_EQ(save->user_data.GetNumSameItem(vanilla_common.item_no), 3);

    CGameDataUsed *owned_item = save->user_data.GetUsedDataPtr(0);
    ASSERT_NE(owned_item, nullptr);
    ASSERT_EQ(owned_item->item_no, item_no);
    CHARA_DATA *target = save->user_data.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(target, nullptr);
    target->hp.max = 100.0f;
    target->hp.now = 40.0f;
    GetMainScene()->battle_area.floor_status = 0;
    const moonmod::ApiOperationSpec *use_operation = moonmod::FindApiOperation("items.use");
    ASSERT_NE(use_operation, nullptr);
    const moonmod::ApiCallResult invalid_use = config.api_handler(
        "test.moonmod.item-probe", *use_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}, {"target", "ridepod"}}}));
    EXPECT_EQ(invalid_use.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_FLOAT_EQ(target->hp.now, 40.0f);
    const moonmod::ApiCallResult use_result = config.api_handler(
        "test.moonmod.item-probe", *use_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}, {"target", "max"}}}));
    ASSERT_EQ(use_result.status, moonmod::ApiCallStatus::Ok) << use_result.message;
    EXPECT_EQ(use_result.value.value("effects_applied", 0), 1);
    EXPECT_EQ(use_result.value.value("consumed", 0), 1);
    EXPECT_EQ(use_result.value.value("remaining", -1), 1);
    EXPECT_FLOAT_EQ(use_result.value.value("hp_before", 0.0f), 40.0f);
    EXPECT_FLOAT_EQ(use_result.value.value("hp_after", 0.0f), 65.0f);
    EXPECT_FLOAT_EQ(use_result.value.value("healed", 0.0f), 25.0f);
    EXPECT_FLOAT_EQ(target->hp.now, 65.0f);
    EXPECT_EQ(owned_item->GetNum(), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 1);

    const moonmod::ApiOperationSpec *remove_operation = moonmod::FindApiOperation("items.remove");
    ASSERT_NE(remove_operation, nullptr);
    const moonmod::ApiCallResult remove_result = config.api_handler(
        "test.moonmod.item-probe", *remove_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}, {"count", 1}}}));
    ASSERT_EQ(remove_result.status, moonmod::ApiCallStatus::Ok) << remove_result.message;
    EXPECT_EQ(remove_result.value.value("removed", 0), 1);
    EXPECT_EQ(remove_result.value.value("remaining", -1), 0);
    EXPECT_EQ(save->user_data.GetNumSameItem(item_no), 0);

    const moonmod::ApiCallResult builtin_remove_result = config.api_handler(
        "test.moonmod.item-probe", *remove_operation,
        moonmod::Json::array({moonmod::Json{{"id", "chronicle-two:item:274"}, {"count", 2}}}));
    ASSERT_EQ(builtin_remove_result.status, moonmod::ApiCallStatus::Ok) << builtin_remove_result.message;
    EXPECT_EQ(builtin_remove_result.value.value("removed", 0), 2);
    EXPECT_EQ(builtin_remove_result.value.value("remaining", -1), 1);
    EXPECT_EQ(save->user_data.GetNumSameItem(vanilla_common.item_no), 1);

    const moonmod::ApiCallResult invalid_remove = config.api_handler(
        "test.moonmod.item-probe", *remove_operation,
        moonmod::Json::array({moonmod::Json{{"id", "test.moonmod.item-probe:starlight-tonic"}, {"count", 0}}}));
    EXPECT_EQ(invalid_remove.status, moonmod::ApiCallStatus::InvalidArgument);

    PortActiveSaveData = previous_save;
    moonmod::ResetChronicleTwoContentForTests();
}
