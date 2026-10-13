#include "platform/config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/display.hpp"
#include "platform/paths.hpp"

namespace {

using Json = nlohmann::ordered_json;

Config g_config;
// Whether config.json holds g_config, so a failed save is retried with the next change.
bool g_saved = false;
// An existing config.json that could not be read: replacing it would discard the user's settings.
bool g_keep_file = false;
ConfigChangeHook g_change_hook = nullptr;

constexpr auto kPositive = [](auto value) { return value > 0; };
constexpr auto kNonNegative = [](auto value) { return value >= 0; };
constexpr auto kUnit = [](auto value) { return value >= 0 && value <= 1; };

constexpr const char *kPresentModeNames[] = {"fifo", "mailbox", "immediate"};
constexpr const char *kAspectNames[] = {"auto", "4:3"};
constexpr const char *kFpsDetailNames[] = {"fps", "ticks", "all"};
constexpr const char *kGyroNames[] = {"off", "always", "first_person", "held"};
constexpr const char *kGlyphDeviceNames[] = {
    "auto", "ps3", "ps4", "ps5", "ps5color", "xbox", "switch", "steamdeck", "steamcontroller", "keyboard",
};

std::string_view Trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r\n";
    std::size_t                first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

// The double that prints as the float does: 0.1f as 0.1, not 0.10000000149011612.
double Shortest(float value) {
    return std::stod(std::format("{}", value));
}

void Ignore(std::string_view name, const Json &value) {
    std::fprintf(stderr, "config.json: ignoring %.*s = %s\n", static_cast<int>(name.size()), name.data(),
                 value.dump().c_str());
}

template <class T, class Valid>
bool ReadNumber(const Json &value, T &out, Valid valid) {
    if (!value.is_number()) {
        return false;
    }
    T number = value.get<T>();
    if (!std::isfinite(number) || !valid(number)) {
        return false;
    }
    out = number;
    return true;
}

bool ReadInt(const Json &value, int &out) {
    if (!value.is_number_integer()) {
        return false;
    }
    std::int64_t number = value.get<std::int64_t>();
    if (number < 0 || number > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

bool ReadBool(const Json &value, bool &out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

// A choice between two names, the second setting true. Matched in any case.
bool ReadChoice(const Json &value, std::string_view first, std::string_view second, bool &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string choice = Lower(value.get<std::string>());
    if (choice != first && choice != second) {
        return false;
    }
    out = choice == second;
    return true;
}

// An enum's values are the indices of its names, in order.
template <class E, std::size_t N>
bool ReadEnum(const Json &value, const char *const (&names)[N], E &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string name = Lower(value.get<std::string>());
    for (std::size_t i = 0; i < N; ++i) {
        if (name == names[i]) {
            out = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

bool ReadList(const Json &value, std::vector<std::string> &out) {
    std::vector<std::string> items;
    if (value.is_string()) {
        items.push_back(value.get<std::string>());
    } else if (value.is_array()) {
        for (const Json &item : value) {
            if (!item.is_string()) {
                return false;
            }
            items.push_back(item.get<std::string>());
        }
    } else {
        return false;
    }
    out = std::move(items);
    return true;
}

bool ApplyBindings(Config &config, const Json &bindings) {
    if (!bindings.is_object()) {
        return false;
    }
    for (const auto &[action, keys] : bindings.items()) {
        ConfigKeyBinding binding = {Lower(action), {}};
        if (ReadList(keys, binding.keys)) {
            config.key_bindings.push_back(std::move(binding));
        } else {
            Ignore("input.bindings." + action, keys);
        }
    }
    return true;
}

bool Apply(Config &config, std::string_view name, const Json &value) {
    HostSettings &s = config;
    if (name == "game.tick_rate") {
        return ReadNumber(value, s.tick_rate, [](double rate) {
            return rate >= kConfigTickRateMin && rate <= kConfigTickRateMax;
        });
    }
    if (name == "game.debug_mode") {
        return ReadBool(value, config.debug_mode);
    }
    if (name == "video.present_mode") {
        return ReadEnum(value, kPresentModeNames, s.present_mode);
    }
    if (name == "video.interpolation") {
        return ReadBool(value, config.interpolation);
    }
    if (name == "video.max_fps") {
        return ReadNumber(value, s.max_fps, kNonNegative);
    }
    if (name == "video.width") {
        return ReadInt(value, s.window_width);
    }
    if (name == "video.height") {
        return ReadInt(value, s.window_height);
    }
    if (name == "video.fullscreen") {
        return ReadBool(value, s.fullscreen);
    }
    if (name == "video.aspect") {
        return ReadEnum(value, kAspectNames, config.aspect);
    }
    if (name == "video.ui_scale") {
        return ReadNumber(value, config.ui_scale, [](float scale) { return scale >= 0.25f && scale <= 4.0f; });
    }
    if (name == "video.show_fps") {
        return ReadBool(value, s.show_fps);
    }
    if (name == "video.fps_detail") {
        return ReadEnum(value, kFpsDetailNames, s.fps_detail);
    }
    if (name == "audio.master_volume") {
        return ReadNumber(value, s.master_volume, kUnit);
    }
    if (name == "audio.sound") {
        return ReadChoice(value, "stereo", "surround", s.surround);
    }
    if (name == "audio.soundtrack") {
        return ReadChoice(value, "ps2", "custom", s.soundtrack);
    }
    if (name == "input.mouse_sensitivity") {
        return ReadNumber(value, s.mouse_sensitivity, kPositive);
    }
    if (name == "input.stick_sensitivity") {
        return ReadNumber(value, s.stick_sensitivity, kPositive);
    }
    if (name == "input.stick_invert_x") {
        return ReadBool(value, s.stick_invert_x);
    }
    if (name == "input.stick_invert_y") {
        return ReadBool(value, s.stick_invert_y);
    }
    if (name == "input.gyro") {
        return ReadEnum(value, kGyroNames, s.gyro);
    }
    if (name == "input.gyro_sensitivity") {
        return ReadNumber(value, s.gyro_sensitivity, kPositive);
    }
    if (name == "input.gyro_invert_x") {
        return ReadBool(value, s.gyro_invert_x);
    }
    if (name == "input.gyro_invert_y") {
        return ReadBool(value, s.gyro_invert_y);
    }
    if (name == "input.mouse_invert_y") {
        return ReadBool(value, s.mouse_invert_y);
    }
    if (name == "input.mouse_capture") {
        return ReadBool(value, s.mouse_capture);
    }
    if (name == "input.mouse_zoom") {
        return ReadBool(value, s.mouse_zoom);
    }
    if (name == "input.touchpad") {
        return ReadBool(value, s.touchpad);
    }
    if (name == "input.touchpad_sensitivity") {
        return ReadNumber(value, s.touchpad_sensitivity, kPositive);
    }
    if (name == "input.lightbar") {
        return ReadBool(value, s.lightbar);
    }
    if (name == "input.rumble_strength") {
        return ReadNumber(value, s.rumble_strength, kUnit);
    }
    if (name == "input.glyphs") {
        return ReadChoice(value, "original", "new", s.glyphs_new);
    }
    if (name == "input.glyph_device") {
        return ReadEnum(value, kGlyphDeviceNames, s.glyph_device);
    }
    if (name == "input.mouse_release") {
        return ReadList(value, s.mouse_release_keys);
    }
    if (name == "input.bindings") {
        return ApplyBindings(config, value);
    }
    return false;
}

bool WriteReplacing(const std::filesystem::path &path, std::string_view text) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    std::error_code error;
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        if (!file) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

} // namespace

Config ConfigParse(std::string_view text) {
    Config config;
    if (Trim(text).empty()) {
        return config;
    }
    Json root = Json::parse(text, nullptr, false, true);
    if (!root.is_object()) {
        std::fprintf(stderr, "config.json: %s; using the defaults\n",
                     root.is_discarded() ? "not valid JSON" : "expected an object");
        return config;
    }
    for (const auto &[section, keys] : root.items()) {
        if (!keys.is_object()) {
            Ignore(section, keys);
            continue;
        }
        for (const auto &[key, value] : keys.items()) {
            std::string name = section + "." + key;
            if (!Apply(config, name, value)) {
                Ignore(name, value);
            }
        }
    }
    return config;
}

std::string ConfigSerialize(const Config &config) {
    const HostSettings &s = config;
    Json                bindings = Json::object();
    for (const ConfigKeyBinding &binding : s.key_bindings) {
        bindings[binding.action] = binding.keys;
    }
    Json root;
    root["game"]["tick_rate"] = s.tick_rate;
    root["game"]["debug_mode"] = config.debug_mode;
    root["video"]["present_mode"] = kPresentModeNames[static_cast<std::size_t>(s.present_mode)];
    root["video"]["interpolation"] = config.interpolation;
    root["video"]["max_fps"] = s.max_fps;
    root["video"]["width"] = s.window_width;
    root["video"]["height"] = s.window_height;
    root["video"]["fullscreen"] = s.fullscreen;
    root["video"]["aspect"] = kAspectNames[static_cast<std::size_t>(config.aspect)];
    root["video"]["ui_scale"] = Shortest(config.ui_scale);
    root["video"]["show_fps"] = s.show_fps;
    root["video"]["fps_detail"] = kFpsDetailNames[static_cast<std::size_t>(s.fps_detail)];
    root["audio"]["master_volume"] = Shortest(s.master_volume);
    root["audio"]["sound"] = s.surround ? "surround" : "stereo";
    root["audio"]["soundtrack"] = s.soundtrack ? "custom" : "ps2";
    root["input"]["mouse_sensitivity"] = Shortest(s.mouse_sensitivity);
    root["input"]["stick_sensitivity"] = Shortest(s.stick_sensitivity);
    root["input"]["stick_invert_x"] = s.stick_invert_x;
    root["input"]["stick_invert_y"] = s.stick_invert_y;
    root["input"]["gyro"] = kGyroNames[static_cast<std::size_t>(s.gyro)];
    root["input"]["gyro_sensitivity"] = Shortest(s.gyro_sensitivity);
    root["input"]["gyro_invert_x"] = s.gyro_invert_x;
    root["input"]["gyro_invert_y"] = s.gyro_invert_y;
    root["input"]["mouse_invert_y"] = s.mouse_invert_y;
    root["input"]["mouse_capture"] = s.mouse_capture;
    root["input"]["mouse_zoom"] = s.mouse_zoom;
    root["input"]["touchpad"] = s.touchpad;
    root["input"]["touchpad_sensitivity"] = Shortest(s.touchpad_sensitivity);
    root["input"]["lightbar"] = s.lightbar;
    root["input"]["rumble_strength"] = Shortest(s.rumble_strength);
    root["input"]["glyphs"] = s.glyphs_new ? "new" : "original";
    root["input"]["glyph_device"] = kGlyphDeviceNames[static_cast<std::size_t>(s.glyph_device)];
    root["input"]["mouse_release"] = s.mouse_release_keys;
    root["input"]["bindings"] = std::move(bindings);
    return root.dump(4) + "\n";
}

bool ConfigSave() {
    if (g_keep_file) {
        return false;
    }
    const std::filesystem::path path = PathsSaveRoot() / "config.json";
    std::error_code             error;
    std::filesystem::create_directories(path.parent_path(), error);
    g_saved = WriteReplacing(path, ConfigSerialize(g_config));
    if (!g_saved) {
        std::fprintf(stderr, "config: could not save %s\n", PathsDisplay(path).c_str());
    }
    return g_saved;
}

bool ConfigLoad() {
    const std::filesystem::path path = PathsSaveRoot() / "config.json";
    std::error_code             error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    g_keep_file = false;
    if (status.type() == std::filesystem::file_type::not_found) {
        g_config = Config{};
        ConfigSave();
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (error || !std::filesystem::is_regular_file(status) || !file) {
        std::fprintf(stderr, "config: could not read %s; using defaults and leaving it untouched\n",
                     PathsDisplay(path).c_str());
        g_config = Config{};
        g_saved = false;
        g_keep_file = true;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    g_config = ConfigParse(text.str());
    g_saved = true;
    return true;
}

const HostSettings &SettingsGet() {
    return g_config;
}

const Config &ConfigGet() {
    return g_config;
}

// Stored as ConfigParse reads ConfigSerialize's output, so a value out of range is normalised the
// way a hand-edited file's would be.
bool SettingsChange(const HostSettings &settings) {
    Config candidate = g_config;
    static_cast<HostSettings &>(candidate) = settings;
    Config after = ConfigParse(ConfigSerialize(candidate));
    if (after == g_config) {
        return g_saved || ConfigSave();
    }
    Config before = std::exchange(g_config, std::move(after));
    ConfigSave();
    DisplayChanged(before, g_config);
    if (g_change_hook != nullptr) {
        g_change_hook(before, g_config);
    }
    return g_saved;
}

void ConfigSetChangeHook(ConfigChangeHook hook) {
    g_change_hook = hook;
}
