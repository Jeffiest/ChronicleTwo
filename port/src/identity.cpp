#include "identity.hpp"

#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC2");
    PathsConfigure({.app_dir = "chronicletwo", .env_data = "DC2_DATA", .env_save = "DC2_SAVE"});
    WindowSetIdentity({.title = "Dark Chronicle", .app_id = "chronicletwo"});
}
