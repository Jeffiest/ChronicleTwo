#include <gtest/gtest.h>

#include "platform/settings.hpp"
#include "platform/window.hpp"

// A main of the tests' own rather than gtest_main's: the game's main is a weak definition in the
// link, and an archive member is not pulled in to replace a symbol that is already defined.
int main(int argc, char **argv) {
    // The shared host tests run as this game: its environment variables and window identity.
    SettingsSetEnvPrefix("DC");
    WindowSetIdentity({.title = "Dark Chronicle tests", .app_id = "dcdecomp.darkchronicle.tests"});
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
