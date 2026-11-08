#include <gtest/gtest.h>

#include <memory>

#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

TEST(MoonModEconomyApi, UsesNativeWalletClampAndNeverAllowsNegativeBalance) {
    CSaveData *previous_save = PortActiveSaveData;
    auto save = std::make_unique<CSaveData>();
    PortActiveSaveData = save.get();
    save->Initialize();
    CUserDataManager &user_data = save->user_data;
    user_data.money = 100;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *get = moonmod::FindApiOperation("economy.get");
    const moonmod::ApiOperationSpec *add = moonmod::FindApiOperation("economy.add");
    const moonmod::ApiOperationSpec *spend = moonmod::FindApiOperation("economy.spend");
    ASSERT_NE(get, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(spend, nullptr);

    const moonmod::ApiCallResult initial = config.api_handler("test.moonmod.economy", *get, moonmod::Json::array());
    ASSERT_EQ(initial.status, moonmod::ApiCallStatus::Ok) << initial.message;
    EXPECT_EQ(initial.value.value("amount", -1), 100);
    EXPECT_EQ(initial.value.value("max_amount", -1), 999999);

    const moonmod::ApiCallResult credited = config.api_handler(
        "test.moonmod.economy", *add, moonmod::Json::array({{{"amount", 25}}}));
    ASSERT_EQ(credited.status, moonmod::ApiCallStatus::Ok) << credited.message;
    EXPECT_EQ(credited.value.value("added", -1), 25);
    EXPECT_EQ(user_data.money, 125);

    const moonmod::ApiCallResult spent = config.api_handler(
        "test.moonmod.economy", *spend, moonmod::Json::array({{{"amount", 40}}}));
    ASSERT_EQ(spent.status, moonmod::ApiCallStatus::Ok) << spent.message;
    EXPECT_EQ(spent.value.value("spent", -1), 40);
    EXPECT_EQ(user_data.money, 85);

    const moonmod::ApiCallResult insufficient = config.api_handler(
        "test.moonmod.economy", *spend, moonmod::Json::array({{{"amount", 86}}}));
    EXPECT_EQ(insufficient.status, moonmod::ApiCallStatus::Failed);
    EXPECT_EQ(user_data.money, 85);

    user_data.money = 999990;
    const moonmod::ApiCallResult clamped = config.api_handler(
        "test.moonmod.economy", *add, moonmod::Json::array({{{"amount", 20}}}));
    ASSERT_EQ(clamped.status, moonmod::ApiCallStatus::Ok) << clamped.message;
    EXPECT_EQ(clamped.value.value("amount", -1), 999999);
    EXPECT_EQ(clamped.value.value("added", -1), 9);

    const moonmod::ApiCallResult invalid = config.api_handler(
        "test.moonmod.economy", *add, moonmod::Json::array({{{"amount", 0}}}));
    EXPECT_EQ(invalid.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult oversized = config.api_handler(
        "test.moonmod.economy", *spend, moonmod::Json::array({{{"amount", 32768}}}));
    EXPECT_EQ(oversized.status, moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult huge_unsigned = config.api_handler(
        "test.moonmod.economy", *spend,
        moonmod::Json::parse("[{\"amount\":18446744073709551615}]"));
    EXPECT_EQ(huge_unsigned.status, moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(user_data.money, 999999);

    PortActiveSaveData = previous_save;
}
