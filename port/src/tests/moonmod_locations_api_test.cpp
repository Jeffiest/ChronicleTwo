#include <gtest/gtest.h>

#include "moonmod/chronicle_two_adapter.hpp"

TEST(MoonModLocationsApi, ReportsNativeMapCatalogAvailabilityAndValidatesLookups) {
    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("locations.list");
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("locations.get");
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(get_operation, nullptr);
    for (const moonmod::ApiCapability &capability : config.api_capabilities) {
        if (capability.path == "locations.list" || capability.path == "locations.get") {
            EXPECT_TRUE(capability.supported);
            EXPECT_TRUE(capability.reason.empty());
        }
    }

    const moonmod::ApiCallResult not_loaded = config.api_handler(
        "test.moonmod.locations", *list_operation, moonmod::Json::array());
    EXPECT_EQ(not_loaded.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(not_loaded.message.find("catalog is not loaded"), std::string::npos);

    const moonmod::ApiCallResult malformed = config.api_handler(
        "test.moonmod.locations", *get_operation, moonmod::Json::array({{{"name", "probe-town"}}}));
    EXPECT_EQ(malformed.status, moonmod::ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult no_catalog = config.api_handler(
        "test.moonmod.locations", *get_operation, moonmod::Json::array({{{"id", "probe-town"}}}));
    EXPECT_EQ(no_catalog.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(no_catalog.message.find("catalog is not loaded"), std::string::npos);
}
