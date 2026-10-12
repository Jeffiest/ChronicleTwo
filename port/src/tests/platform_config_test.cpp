#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform_fixture.hpp"

namespace fs = std::filesystem;

namespace {

// Each case gets an empty save root: SettingsChange on a window-affecting field would open a window,
// so the cases that need one write the file and load it.
class PlatformConfig : public testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / std::format("dc2_config_{}", dc::test::ProcessId());
        fs::remove_all(root_);
        fs::create_directories(root_);
        PathsSetSaveRoot(root_);
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path Path() const { return root_ / "config.json"; }

    void Write(std::string_view text) const { std::ofstream(Path(), std::ios::binary) << text; }

    std::string Read() const {
        std::ifstream file(Path(), std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

private:
    fs::path root_;
};

int          g_hook_calls = 0;
HostSettings g_hook_before;
HostSettings g_hook_after;

void CaptureChange(const HostSettings &before, const HostSettings &after) {
    ++g_hook_calls;
    g_hook_before = before;
    g_hook_after = after;
}

} // namespace

TEST_F(PlatformConfig, DefaultsAreWrittenWhenAbsent) {
    ASSERT_FALSE(ConfigLoad());
    ASSERT_TRUE(fs::exists(Path()));
    ASSERT_TRUE(ConfigParse(Read()) == Config{});
    ASSERT_TRUE(SettingsGet().tick_rate == kConfigTickRateDefault);
    ASSERT_TRUE(ConfigLoad());
    ASSERT_TRUE(SettingsGet() == static_cast<const HostSettings &>(Config{}));
}

TEST_F(PlatformConfig, FullFileRoundTrips) {
    Config full;
    full.tick_rate = 120.0;
    full.present_mode = ConfigPresentMode::Mailbox;
    full.max_fps = 144.0;
    full.window_width = 1920;
    full.window_height = 1080;
    full.fullscreen = true;
    full.show_fps = false;
    full.fps_detail = ConfigFpsDetail::Ticks;
    full.master_volume = 0.5f;
    full.surround = true;
    full.soundtrack = true;
    full.key_bindings = {ConfigKeyBinding{"cross", {"Space", "Z"}}, ConfigKeyBinding{"start", {"Return"}}};
    full.mouse_sensitivity = 0.25f;
    full.stick_sensitivity = 1.5f;
    full.stick_invert_x = true;
    full.stick_invert_y = true;
    full.gyro = ConfigGyro::FirstPerson;
    full.gyro_sensitivity = 1.25f;
    full.gyro_invert_x = true;
    full.gyro_invert_y = true;
    full.mouse_invert_y = true;
    full.mouse_capture = false;
    full.mouse_zoom = true;
    full.touchpad = false;
    full.touchpad_sensitivity = 2.0f;
    full.lightbar = false;
    full.rumble_strength = 0.5f;
    full.glyphs_new = false;
    full.glyph_device = ConfigGlyphDevice::Ps5Color;
    full.mouse_release_keys = {"F12", "Pause"};

    ASSERT_TRUE(ConfigParse(ConfigSerialize(full)) == full);

    Write(ConfigSerialize(full));
    ASSERT_TRUE(ConfigLoad());
    const HostSettings &expected = full;
    ASSERT_TRUE(SettingsGet() == expected);
}

TEST_F(PlatformConfig, TickRateOutsideItsRangeKeepsTheDefault) {
    ASSERT_TRUE(ConfigParse(R"({"game": {"tick_rate": 25}})").tick_rate == 25.0);
    ASSERT_TRUE(ConfigParse(R"({"game": {"tick_rate": 240}})").tick_rate == 240.0);
    for (const char *value : {"0", "-3", "24.9", "240.1", "1e9", "\"fast\"", "null", "true"}) {
        std::string json = std::string(R"({"game": {"tick_rate": )") + value + "}}";
        ASSERT_TRUE(ConfigParse(json).tick_rate == kConfigTickRateDefault) << value;
    }
    Write(R"({"game": {"tick_rate": 1000}})");
    ASSERT_TRUE(ConfigLoad());
    ASSERT_TRUE(SettingsGet().tick_rate == kConfigTickRateDefault);
}

TEST_F(PlatformConfig, UnknownKeysAndSectionsAreIgnored) {
    Config config = ConfigParse(R"({
        // comment
        "game": {"tick_rate": 60, "debug_mode": true, "unknown": 1},
        "video": {"vsync": false, "width": 1280},
        "audio": {"master_volume": 0.5},
        "extra": {"tick_rate": 30},
        "stray": 1
    })");
    ASSERT_TRUE(config.tick_rate == 60.0);
    ASSERT_TRUE(config.window_width == 1280 && config.window_height == 0);
    ASSERT_TRUE(config.master_volume == 0.5f);
    ASSERT_TRUE(config.present_mode == ConfigPresentMode::Fifo);
    ASSERT_TRUE(ConfigParse(R"({"game": {"debug_mode": true}, "stray": 1})") == Config{});
}

TEST_F(PlatformConfig, WrongTypesKeepTheDefaults) {
    ASSERT_TRUE(ConfigParse(R"({
        "game": {"tick_rate": "50"},
        "video": {"width": 1.5, "height": "tall", "fullscreen": "yes", "present_mode": 3,
                  "fps_detail": true, "max_fps": -1},
        "audio": {"master_volume": 1.5, "sound": "mono", "soundtrack": 1},
        "input": {"mouse_sensitivity": 0, "gyro": true, "glyphs": 0, "glyph_device": "xbox360",
                  "rumble_strength": 2, "mouse_release": 4, "bindings": "cross"}
    })") == Config{});
}

TEST_F(PlatformConfig, MalformedFileKeepsTheDefaults) {
    ASSERT_TRUE(ConfigParse("[game]\ntick_rate = 50\n") == Config{});
    ASSERT_TRUE(ConfigParse(R"({"game": {"tick_rate": 50})") == Config{});
    ASSERT_TRUE(ConfigParse("[1, 2]") == Config{});
    ASSERT_TRUE(ConfigParse(" \n") == Config{});
}

TEST_F(PlatformConfig, SettingsChangeStoresAndNormalises) {
    ASSERT_FALSE(ConfigLoad());
    HostSettings changed = SettingsGet();
    changed.tick_rate = 120.0;
    changed.rumble_strength = 2.0f;
    ASSERT_TRUE(SettingsChange(changed));
    ASSERT_TRUE(SettingsGet().tick_rate == 120.0);
    ASSERT_TRUE(SettingsGet().rumble_strength == 1.0f);
    ASSERT_TRUE(ConfigParse(Read()).tick_rate == 120.0);
}

TEST_F(PlatformConfig, ExistingUnreadableFileIsNeverOverwritten) {
    fs::create_directory(Path());
    ASSERT_FALSE(ConfigLoad());
    ASSERT_TRUE(SettingsGet() == static_cast<const HostSettings &>(Config{}));
    HostSettings changed = SettingsGet();
    changed.tick_rate = 120.0;
    ASSERT_FALSE(SettingsChange(changed));
    ASSERT_TRUE(SettingsGet().tick_rate == 120.0);
    ASSERT_FALSE(ConfigSave());
    ASSERT_TRUE(fs::is_directory(Path()));
    ASSERT_FALSE(fs::is_regular_file(Path()));
}

TEST_F(PlatformConfig, ChangeHookSeesBeforeAndAfter) {
    ASSERT_FALSE(ConfigLoad());
    g_hook_calls = 0;
    ConfigSetChangeHook(CaptureChange);
    HostSettings changed = SettingsGet();
    changed.tick_rate = 120.0;
    ASSERT_TRUE(SettingsChange(changed));
    ASSERT_TRUE(g_hook_calls == 1);
    ASSERT_TRUE(g_hook_before.tick_rate == kConfigTickRateDefault);
    ASSERT_TRUE(g_hook_after.tick_rate == 120.0);
    ASSERT_TRUE(SettingsChange(SettingsGet()));
    ASSERT_TRUE(g_hook_calls == 1);
    ConfigSetChangeHook(nullptr);
}
