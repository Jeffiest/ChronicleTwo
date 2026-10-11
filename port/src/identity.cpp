#include "identity.hpp"

#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC");
    PathsConfigure({.app_dir = "chronicle-two", .env_data = "DC_DATA", .env_save = "DC_SAVE"});
    WindowSetIdentity({.title = "Dark Chronicle", .app_id = "dcdecomp.darkchronicle"});
}
