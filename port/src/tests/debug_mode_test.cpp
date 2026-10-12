#include <gtest/gtest.h>

#include "debug_mode.hpp"
#include "mainloop.hpp"
#include "platform/input.hpp"
#include "title.hpp"

TEST(PortDebugMode, ConfigEnablesAndDisablesRetailUnlocks) {
    PortApplyDebugMode(true);
    EXPECT_EQ(DebugFlag, 1);
    EXPECT_EQ(MasterDebugCode, MASTER_DEBUG_CODE);
    EXPECT_EQ(MasterDebugModeOn, 1);

    PortApplyDebugMode(false);
    EXPECT_EQ(DebugFlag, 0);
    EXPECT_EQ(MasterDebugCode, 0);
    EXPECT_EQ(MasterDebugModeOn, 0);
}

TEST(PortDebugMode, ToggleChangesTheRetailDebugFlag) {
    PortApplyDebugMode(false);
    PortToggleDebugMode();
    EXPECT_EQ(DebugFlag, 1);
    PortToggleDebugMode();
    EXPECT_EQ(DebugFlag, 0);
}

TEST(PortDebugMode, HostBindingProducesOneDebugMenuPress) {
    InputResetBindings();
    constexpr std::string_view bindings[] = {"F10", "Gamepad:paddle1"};
    ASSERT_TRUE(InputBindKeys("debug_menu", bindings));

    InputKeyboardMouse held;
    held.keys.push_back(InputScancodeFromName("F10"));
    InputSetScriptedDevices(held);
    EXPECT_TRUE(InputHostPressed(InputHostAction::DebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::DebugMenu));

    InputSetScriptedDevices({});
}
